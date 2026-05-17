// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging/log.h"
#include "core/libraries/error_codes.h"
#include "core/libraries/libs.h"
#include "core/libraries/ngs2/ngs2.h"
#include "core/libraries/ngs2/ngs2_custom.h"
#include "core/libraries/ngs2/ngs2_error.h"
#include "core/libraries/ngs2/ngs2_geom.h"
#include "core/libraries/ngs2/ngs2_impl.h"
#include "core/libraries/ngs2/ngs2_pan.h"
#include "core/libraries/ngs2/ngs2_report.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Libraries::Ngs2 {

namespace {

constexpr OrbisNgs2Handle FirstSyntheticHandle = 0x100000;
constexpr u32 DefaultMaxVoices = 64;
constexpr u32 VoiceStateActive = 1u << 0;
constexpr u32 MaxTrackedPorts = 16;
constexpr u32 MaxTrackedMatrices = 16;

enum class OrbisNgs2VoiceParamId : u32 {
    MatrixLevels = 1,
    PortVolume = 2,
    Event = 3,
    Callback = 5,
    Patch = 6,
};

enum class OrbisNgs2CustomSamplerParamId : u32 {
    Setup = 0x10000000,
    WaveformBlocks = 0x10000001,
    WaveformAddress = 0x10000002,
    WaveformFrameOffset = 0x10000003,
    ExitLoop = 0x10000004,
    Pitch = 0x10000005,
};

struct VoiceRuntimeState {
    OrbisNgs2Handle handle = 0;
    OrbisNgs2Handle rackHandle = 0;
    u32 voiceIndex = 0;
    u32 stateFlags = 0;
    u64 controlCount = 0;
    std::array<OrbisNgs2VoicePortInfo, MaxTrackedPorts> ports{};
    std::array<OrbisNgs2VoiceMatrixInfo, MaxTrackedMatrices> matrices{};
    OrbisNgs2VoiceCallbackHandler callbackHandler = nullptr;
    uintptr_t callbackData = 0;
    OrbisNgs2WaveformFormat waveformFormat{};
    const void* waveformData = nullptr;
    const void* waveformEnd = nullptr;
    std::vector<OrbisNgs2WaveformBlock> waveformBlocks;
    u32 waveformFrameOffset = 0;
    float pitchRatio = 1.0f;
    u64 decodedDataSize = 0;
    u64 decodedSamples = 0;
};

struct RackRuntimeState {
    OrbisNgs2Handle handle = 0;
    OrbisNgs2Handle systemHandle = 0;
    OrbisNgs2ContextBufferInfo bufferInfo{};
    uintptr_t userData = 0;
    u32 rackId = 0;
    u32 maxVoices = 0;
    u64 renderCount = 0;
    std::vector<OrbisNgs2Handle> voices;
};

struct SystemRuntimeState {
    OrbisNgs2Handle handle = 0;
    OrbisNgs2ContextBufferInfo bufferInfo{};
    uintptr_t userData = 0;
    u32 sampleRate = 48000;
    u32 maxGrainSamples = 512;
    u32 numGrainSamples = 256;
    u64 renderCount = 0;
    std::vector<OrbisNgs2Handle> racks;
};

std::mutex g_state_mutex;
std::atomic<OrbisNgs2Handle> g_next_handle{FirstSyntheticHandle};
std::unordered_map<OrbisNgs2Handle, SystemRuntimeState> g_systems;
std::unordered_map<OrbisNgs2Handle, RackRuntimeState> g_racks;
std::unordered_map<OrbisNgs2Handle, VoiceRuntimeState> g_voices;

OrbisNgs2Handle AllocateHandle() {
    return g_next_handle.fetch_add(1, std::memory_order_relaxed);
}

bool IsNgs2TraceEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("SHADPS4_NGS2_TRACE");
        return value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}

void CopyBufferInfo(const OrbisNgs2ContextBufferInfo* src, OrbisNgs2ContextBufferInfo* dst) {
    if (!dst) {
        return;
    }
    if (src) {
        *dst = *src;
    } else {
        std::memset(dst, 0, sizeof(*dst));
    }
}

OrbisNgs2Handle RegisterSystem(const OrbisNgs2SystemOption* option,
                               const OrbisNgs2ContextBufferInfo* bufferInfo) {
    SystemRuntimeState system{};
    system.handle = AllocateHandle();
    CopyBufferInfo(bufferInfo, &system.bufferInfo);
    if (option) {
        system.sampleRate = option->sampleRate ? option->sampleRate : system.sampleRate;
        system.maxGrainSamples =
            option->maxGrainSamples ? option->maxGrainSamples : system.maxGrainSamples;
        system.numGrainSamples =
            option->numGrainSamples ? option->numGrainSamples : system.numGrainSamples;
    }

    const auto handle = system.handle;
    std::scoped_lock lock{g_state_mutex};
    g_systems.emplace(handle, system);
    return handle;
}

OrbisNgs2Handle RegisterRackLocked(SystemRuntimeState& system, u32 rackId,
                                   const OrbisNgs2RackOption* option,
                                   const OrbisNgs2ContextBufferInfo* bufferInfo) {
    RackRuntimeState rack{};
    rack.handle = AllocateHandle();
    rack.systemHandle = system.handle;
    rack.rackId = rackId;
    rack.maxVoices = option && option->maxVoices ? option->maxVoices : DefaultMaxVoices;
    CopyBufferInfo(bufferInfo, &rack.bufferInfo);
    rack.voices.reserve(rack.maxVoices);

    const auto rackHandle = rack.handle;
    for (u32 index = 0; index < rack.maxVoices; ++index) {
        VoiceRuntimeState voice{};
        voice.handle = AllocateHandle();
        voice.rackHandle = rackHandle;
        voice.voiceIndex = index;
        for (auto& port : voice.ports) {
            port.matrixId = -1;
            port.volume = 1.0f;
        }
        for (auto& matrix : voice.matrices) {
            matrix.numLevels = 0;
        }
        rack.voices.push_back(voice.handle);
        g_voices.emplace(voice.handle, voice);
    }

    system.racks.push_back(rackHandle);
    g_racks.emplace(rackHandle, std::move(rack));
    return rackHandle;
}

void RemoveRackLocked(OrbisNgs2Handle rackHandle) {
    const auto rack_it = g_racks.find(rackHandle);
    if (rack_it == g_racks.end()) {
        return;
    }

    for (const auto voice : rack_it->second.voices) {
        g_voices.erase(voice);
    }

    if (auto system_it = g_systems.find(rack_it->second.systemHandle); system_it != g_systems.end()) {
        auto& racks = system_it->second.racks;
        std::erase(racks, rackHandle);
    }
    g_racks.erase(rack_it);
}

void FillSystemInfo(const SystemRuntimeState& system, OrbisNgs2SystemInfo* outInfo,
                    size_t infoSize) {
    const auto bytes = std::min(infoSize, sizeof(OrbisNgs2SystemInfo));
    std::memset(outInfo, 0, infoSize);
    OrbisNgs2SystemInfo info{};
    info.systemHandle = system.handle;
    info.bufferInfo = system.bufferInfo;
    info.uid = static_cast<u32>(system.handle);
    info.minGrainSamples = system.numGrainSamples;
    info.maxGrainSamples = system.maxGrainSamples;
    info.rackCount = static_cast<u32>(system.racks.size());
    info.renderCount = static_cast<s64>(system.renderCount);
    info.sampleRate = system.sampleRate;
    info.numGrainSamples = system.numGrainSamples;
    std::memcpy(outInfo, &info, bytes);
}

void FillRackInfo(const RackRuntimeState& rack, OrbisNgs2RackInfo* outInfo, size_t infoSize) {
    const auto bytes = std::min(infoSize, sizeof(OrbisNgs2RackInfo));
    std::memset(outInfo, 0, infoSize);
    OrbisNgs2RackInfo info{};
    info.rackHandle = rack.handle;
    info.bufferInfo = rack.bufferInfo;
    info.ownerSystemHandle = rack.systemHandle;
    info.rackId = rack.rackId;
    info.uid = static_cast<u32>(rack.handle);
    info.maxVoices = rack.maxVoices;
    info.renderCount = rack.renderCount;
    for (const auto voiceHandle : rack.voices) {
        const auto voice_it = g_voices.find(voiceHandle);
        if (voice_it != g_voices.end() && voice_it->second.stateFlags != 0) {
            ++info.activeVoiceCount;
        }
    }
    std::memcpy(outInfo, &info, bytes);
}

void ZeroRenderBuffers(const OrbisNgs2RenderBufferInfo* aBufferInfo, u32 numBufferInfo) {
    if (!aBufferInfo) {
        return;
    }
    for (u32 i = 0; i < numBufferInfo; ++i) {
        const auto& buffer = aBufferInfo[i];
        if (buffer.buffer && buffer.bufferSize > 0) {
            std::memset(buffer.buffer, 0, buffer.bufferSize);
        }
    }
}

void TraceVoiceParams(OrbisNgs2Handle voiceHandle, const OrbisNgs2VoiceParamHeader* paramList) {
    if (!IsNgs2TraceEnabled() || !paramList) {
        return;
    }

    const auto* param = paramList;
    for (u32 index = 0; index < 64 && param; ++index) {
        if (param->size >= sizeof(OrbisNgs2VoiceEventParam) &&
            param->id == static_cast<u32>(OrbisNgs2VoiceParamId::Event)) {
            const auto* event = reinterpret_cast<const OrbisNgs2VoiceEventParam*>(param);
            LOG_INFO(Lib_Ngs2, "voice {} param[{}]: id={:#x} size={} next={} event={:#x}",
                     voiceHandle, index, param->id, param->size, param->next, event->eventId);
        } else if (param->size >= sizeof(OrbisNgs2VoicePortVolumeParam) &&
                   param->id == static_cast<u32>(OrbisNgs2VoiceParamId::PortVolume)) {
            const auto* volume = reinterpret_cast<const OrbisNgs2VoicePortVolumeParam*>(param);
            LOG_INFO(Lib_Ngs2, "voice {} param[{}]: id={:#x} size={} next={} port={} volume={}",
                     voiceHandle, index, param->id, param->size, param->next, volume->port,
                     volume->level);
        } else if (param->size >= sizeof(OrbisNgs2CustomSamplerVoiceWaveformBlocksParam) &&
                   param->id ==
                       static_cast<u32>(OrbisNgs2CustomSamplerParamId::WaveformBlocks)) {
            const auto* blocks =
                reinterpret_cast<const OrbisNgs2CustomSamplerVoiceWaveformBlocksParam*>(param);
            LOG_INFO(Lib_Ngs2,
                     "voice {} param[{}]: id={:#x} size={} next={} data={} numBlocks={}",
                     voiceHandle, index, param->id, param->size, param->next, blocks->data,
                     blocks->numBlocks);
        } else if (param->size >= sizeof(OrbisNgs2CustomSamplerVoicePitchParam) &&
                   param->id == static_cast<u32>(OrbisNgs2CustomSamplerParamId::Pitch)) {
            const auto* pitch = reinterpret_cast<const OrbisNgs2CustomSamplerVoicePitchParam*>(param);
            LOG_INFO(Lib_Ngs2, "voice {} param[{}]: id={:#x} size={} next={} pitch={}",
                     voiceHandle, index, param->id, param->size, param->next, pitch->ratio);
        } else {
            LOG_INFO(Lib_Ngs2, "voice {} param[{}]: id={:#x} size={} next={}", voiceHandle, index,
                     param->id, param->size, param->next);
        }
        if (param->size < sizeof(OrbisNgs2VoiceParamHeader) || param->next <= 0) {
            break;
        }
        param = reinterpret_cast<const OrbisNgs2VoiceParamHeader*>(
            reinterpret_cast<const u8*>(param) + param->next);
    }
}

void ApplyVoiceParam(VoiceRuntimeState& voice, const OrbisNgs2VoiceParamHeader* param) {
    switch (static_cast<OrbisNgs2VoiceParamId>(param->id)) {
    case OrbisNgs2VoiceParamId::MatrixLevels: {
        if (param->size < sizeof(OrbisNgs2VoiceMatrixLevelsParam)) {
            return;
        }
        const auto* levels = reinterpret_cast<const OrbisNgs2VoiceMatrixLevelsParam*>(param);
        if (levels->matrixId >= voice.matrices.size() || !levels->aLevel) {
            return;
        }
        auto& matrix = voice.matrices[levels->matrixId];
        matrix.numLevels = std::min<u32>(levels->numLevels, ORBIS_NGS2_MAX_MATRIX_LEVELS);
        std::copy_n(levels->aLevel, matrix.numLevels, matrix.aLevel);
        break;
    }
    case OrbisNgs2VoiceParamId::PortVolume: {
        if (param->size < sizeof(OrbisNgs2VoicePortVolumeParam)) {
            return;
        }
        const auto* volume = reinterpret_cast<const OrbisNgs2VoicePortVolumeParam*>(param);
        if (volume->port < voice.ports.size()) {
            voice.ports[volume->port].volume = volume->level;
        }
        break;
    }
    case OrbisNgs2VoiceParamId::Event: {
        if (param->size < sizeof(OrbisNgs2VoiceEventParam)) {
            return;
        }
        const auto* event = reinterpret_cast<const OrbisNgs2VoiceEventParam*>(param);
        if (event->eventId == 0) {
            voice.stateFlags |= VoiceStateActive;
        } else if (event->eventId == 1) {
            voice.stateFlags &= ~VoiceStateActive;
        }
        break;
    }
    case OrbisNgs2VoiceParamId::Callback: {
        if (param->size < sizeof(OrbisNgs2VoiceCallbackParam)) {
            return;
        }
        const auto* callback = reinterpret_cast<const OrbisNgs2VoiceCallbackParam*>(param);
        voice.callbackHandler = callback->callbackHandler;
        voice.callbackData = callback->callbackData;
        break;
    }
    case OrbisNgs2VoiceParamId::Patch: {
        if (param->size < sizeof(OrbisNgs2VoicePatchParam)) {
            return;
        }
        const auto* patch = reinterpret_cast<const OrbisNgs2VoicePatchParam*>(param);
        if (patch->port < voice.ports.size()) {
            voice.ports[patch->port].destInputId = patch->destInputId;
            voice.ports[patch->port].destHandle = patch->destHandle;
        }
        break;
    }
    default:
        switch (static_cast<OrbisNgs2CustomSamplerParamId>(param->id)) {
        case OrbisNgs2CustomSamplerParamId::Setup: {
            if (param->size < sizeof(OrbisNgs2CustomSamplerVoiceSetupParam)) {
                return;
            }
            const auto* setup = reinterpret_cast<const OrbisNgs2CustomSamplerVoiceSetupParam*>(param);
            voice.waveformFormat = setup->format;
            break;
        }
        case OrbisNgs2CustomSamplerParamId::WaveformBlocks: {
            if (param->size < sizeof(OrbisNgs2CustomSamplerVoiceWaveformBlocksParam)) {
                return;
            }
            const auto* blocks =
                reinterpret_cast<const OrbisNgs2CustomSamplerVoiceWaveformBlocksParam*>(param);
            voice.waveformData = blocks->data;
            voice.waveformBlocks.clear();
            if (blocks->aBlock && blocks->numBlocks != 0) {
                const auto count =
                    std::min<u32>(blocks->numBlocks, ORBIS_NGS2_WAVEFORM_INFO_MAX_BLOCKS);
                voice.waveformBlocks.assign(blocks->aBlock, blocks->aBlock + count);
                voice.decodedDataSize = 0;
                voice.decodedSamples = 0;
                for (const auto& block : voice.waveformBlocks) {
                    voice.decodedDataSize += block.dataSize;
                    voice.decodedSamples += block.numSamples;
                }
            }
            break;
        }
        case OrbisNgs2CustomSamplerParamId::WaveformAddress: {
            if (param->size < sizeof(OrbisNgs2CustomSamplerVoiceWaveformAddressParam)) {
                return;
            }
            const auto* address =
                reinterpret_cast<const OrbisNgs2CustomSamplerVoiceWaveformAddressParam*>(param);
            voice.waveformData = address->from;
            voice.waveformEnd = address->to;
            break;
        }
        case OrbisNgs2CustomSamplerParamId::WaveformFrameOffset: {
            if (param->size < sizeof(OrbisNgs2CustomSamplerVoiceWaveformFrameOffsetParam)) {
                return;
            }
            const auto* frame =
                reinterpret_cast<const OrbisNgs2CustomSamplerVoiceWaveformFrameOffsetParam*>(param);
            voice.waveformFrameOffset = frame->frameOffset;
            break;
        }
        case OrbisNgs2CustomSamplerParamId::ExitLoop:
            break;
        case OrbisNgs2CustomSamplerParamId::Pitch: {
            if (param->size < sizeof(OrbisNgs2CustomSamplerVoicePitchParam)) {
                return;
            }
            const auto* pitch = reinterpret_cast<const OrbisNgs2CustomSamplerVoicePitchParam*>(param);
            voice.pitchRatio = pitch->ratio;
            break;
        }
        default:
            break;
        }
        break;
    }
}

void ApplyVoiceParams(VoiceRuntimeState& voice, const OrbisNgs2VoiceParamHeader* paramList) {
    const auto* param = paramList;
    for (u32 index = 0; index < 64 && param; ++index) {
        if (param->size < sizeof(OrbisNgs2VoiceParamHeader)) {
            break;
        }
        ApplyVoiceParam(voice, param);
        if (param->next <= 0) {
            break;
        }
        param = reinterpret_cast<const OrbisNgs2VoiceParamHeader*>(
            reinterpret_cast<const u8*>(param) + param->next);
    }
}

} // namespace

static void FillFallbackWaveformInfo(const void* data, size_t dataSize,
                                     OrbisNgs2WaveformInfo* outInfo) {
    std::memset(outInfo, 0, sizeof(*outInfo));
    outInfo->format.numChannels = 1;
    outInfo->format.sampleRate = 48000;
    outInfo->dataSize = static_cast<u32>(std::min<size_t>(dataSize, UINT32_MAX));
    outInfo->audioUnitSize = 1;
    outInfo->numAudioUnitSamples = 1;
    outInfo->numAudioUnitPerFrame = 1;
    outInfo->audioFrameSize = 1;
    outInfo->numAudioFrameSamples = 1;
    if (data != nullptr && dataSize != 0) {
        outInfo->numBlocks = 1;
        outInfo->aBlock[0].dataSize = outInfo->dataSize;
        outInfo->aBlock[0].numRepeats = 1;
        outInfo->aBlock[0].userData = reinterpret_cast<uintptr_t>(data);
    }
}

// Ngs2

s32 PS4_SYSV_ABI sceNgs2CalcWaveformBlock(const OrbisNgs2WaveformFormat* format, u32 samplePos,
                                          u32 numSamples, OrbisNgs2WaveformBlock* outBlock) {
    LOG_ERROR(Lib_Ngs2, "samplePos = {}, numSamples = {}", samplePos, numSamples);
    if (!format) {
        return ORBIS_NGS2_ERROR_INVALID_WAVEFORM_FORMAT;
    }
    if (!outBlock) {
        return ORBIS_NGS2_ERROR_INVALID_WAVEFORM_BLOCK_ADDRESS;
    }
    std::memset(outBlock, 0, sizeof(*outBlock));
    outBlock->numRepeats = 1;
    outBlock->numSamples = numSamples;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNgs2GetWaveformFrameInfo(const OrbisNgs2WaveformFormat* format,
                                             u32* outFrameSize, u32* outNumFrameSamples,
                                             u32* outUnitsPerFrame, u32* outNumDelaySamples) {
    LOG_ERROR(Lib_Ngs2, "called");
    if (!format) {
        return ORBIS_NGS2_ERROR_INVALID_WAVEFORM_FORMAT;
    }
    if (!outFrameSize || !outNumFrameSamples || !outUnitsPerFrame || !outNumDelaySamples) {
        return ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS;
    }
    *outFrameSize = 1;
    *outNumFrameSamples = 1;
    *outUnitsPerFrame = 1;
    *outNumDelaySamples = 0;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNgs2ParseWaveformData(const void* data, size_t dataSize,
                                          OrbisNgs2WaveformInfo* outInfo) {
    LOG_DEBUG(Lib_Ngs2, "(STUBBED) dataSize = {}", dataSize);
    if (!data || dataSize == 0) {
        return ORBIS_NGS2_ERROR_INVALID_WAVEFORM_DATA;
    }
    if (!outInfo) {
        return ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS;
    }
    FillFallbackWaveformInfo(data, dataSize, outInfo);
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNgs2ParseWaveformFile(const char* path, u64 offset,
                                          OrbisNgs2WaveformInfo* outInfo) {
    LOG_ERROR(Lib_Ngs2, "path = {}, offset = {}", path, offset);
    if (!path) {
        return ORBIS_NGS2_ERROR_INVALID_WAVEFORM_ADDRESS;
    }
    if (!outInfo) {
        return ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS;
    }
    FillFallbackWaveformInfo(nullptr, 0, outInfo);
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNgs2ParseWaveformUser(OrbisNgs2ParseReadHandler handler, uintptr_t userData,
                                          OrbisNgs2WaveformInfo* outInfo) {
    LOG_ERROR(Lib_Ngs2, "userData = {}", userData);
    if (!handler) {
        return ORBIS_NGS2_ERROR_INVALID_WAVEFORM_ADDRESS;
    }
    if (!outInfo) {
        return ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS;
    }
    FillFallbackWaveformInfo(nullptr, 0, outInfo);
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNgs2RackCreate(OrbisNgs2Handle systemHandle, u32 rackId,
                                   const OrbisNgs2RackOption* option,
                                   const OrbisNgs2ContextBufferInfo* bufferInfo,
                                   OrbisNgs2Handle* outHandle) {
    LOG_DEBUG(Lib_Ngs2, "rackId = {}", rackId);
    if (!systemHandle) {
        LOG_ERROR(Lib_Ngs2, "systemHandle is nullptr");
        return ORBIS_NGS2_ERROR_INVALID_SYSTEM_HANDLE;
    }
    if (!outHandle) {
        return ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS;
    }

    std::scoped_lock lock{g_state_mutex};
    auto system = g_systems.find(systemHandle);
    if (system == g_systems.end()) {
        return ORBIS_NGS2_ERROR_INVALID_SYSTEM_HANDLE;
    }
    *outHandle = RegisterRackLocked(system->second, rackId, option, bufferInfo);
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNgs2RackCreateWithAllocator(OrbisNgs2Handle systemHandle, u32 rackId,
                                                const OrbisNgs2RackOption* option,
                                                const OrbisNgs2BufferAllocator* allocator,
                                                OrbisNgs2Handle* outHandle) {
    LOG_DEBUG(Lib_Ngs2, "rackId = {}", rackId);
    if (!systemHandle) {
        LOG_ERROR(Lib_Ngs2, "systemHandle is nullptr");
        return ORBIS_NGS2_ERROR_INVALID_SYSTEM_HANDLE;
    }
    if (!allocator || !allocator->allocHandler) {
        return ORBIS_NGS2_ERROR_INVALID_BUFFER_ALLOCATOR;
    }
    if (!outHandle) {
        return ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS;
    }

    OrbisNgs2ContextBufferInfo bufferInfo{};
    bufferInfo.userData = allocator->userData;
    const auto maxVoices = option && option->maxVoices ? option->maxVoices : DefaultMaxVoices;
    bufferInfo.hostBufferSize = sizeof(RackRuntimeState) + maxVoices * sizeof(VoiceRuntimeState);
    if (const auto alloc_result = allocator->allocHandler(&bufferInfo); alloc_result < 0) {
        return alloc_result;
    }

    std::scoped_lock lock{g_state_mutex};
    auto system = g_systems.find(systemHandle);
    if (system == g_systems.end()) {
        if (allocator->freeHandler) {
            allocator->freeHandler(&bufferInfo);
        }
        return ORBIS_NGS2_ERROR_INVALID_SYSTEM_HANDLE;
    }
    *outHandle = RegisterRackLocked(system->second, rackId, option, &bufferInfo);
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNgs2RackDestroy(OrbisNgs2Handle rackHandle,
                                    OrbisNgs2ContextBufferInfo* outBufferInfo) {
    LOG_DEBUG(Lib_Ngs2, "called");
    if (!rackHandle) {
        return ORBIS_NGS2_ERROR_INVALID_RACK_HANDLE;
    }
    std::scoped_lock lock{g_state_mutex};
    const auto rack = g_racks.find(rackHandle);
    if (rack == g_racks.end()) {
        return ORBIS_NGS2_ERROR_INVALID_RACK_HANDLE;
    }
    CopyBufferInfo(&rack->second.bufferInfo, outBufferInfo);
    RemoveRackLocked(rackHandle);
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNgs2RackGetInfo(OrbisNgs2Handle rackHandle, OrbisNgs2RackInfo* outInfo,
                                    size_t infoSize) {
    LOG_DEBUG(Lib_Ngs2, "infoSize = {}", infoSize);
    if (!rackHandle) {
        return ORBIS_NGS2_ERROR_INVALID_RACK_HANDLE;
    }
    if (!outInfo) {
        return ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS;
    }
    if (infoSize < sizeof(OrbisNgs2RackInfo)) {
        return ORBIS_NGS2_ERROR_INVALID_OUT_SIZE;
    }
    std::scoped_lock lock{g_state_mutex};
    const auto rack = g_racks.find(rackHandle);
    if (rack == g_racks.end()) {
        return ORBIS_NGS2_ERROR_INVALID_RACK_HANDLE;
    }
    FillRackInfo(rack->second, outInfo, infoSize);
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNgs2RackGetUserData(OrbisNgs2Handle rackHandle, uintptr_t* outUserData) {
    LOG_DEBUG(Lib_Ngs2, "called");
    if (!rackHandle) {
        return ORBIS_NGS2_ERROR_INVALID_RACK_HANDLE;
    }
    if (!outUserData) {
        return ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS;
    }
    std::scoped_lock lock{g_state_mutex};
    const auto rack = g_racks.find(rackHandle);
    if (rack == g_racks.end()) {
        return ORBIS_NGS2_ERROR_INVALID_RACK_HANDLE;
    }
    *outUserData = rack->second.userData;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNgs2RackGetVoiceHandle(OrbisNgs2Handle rackHandle, u32 voiceIndex,
                                           OrbisNgs2Handle* outHandle) {
    LOG_DEBUG(Lib_Ngs2, "voiceIndex = {}", voiceIndex);
    if (!rackHandle) {
        return ORBIS_NGS2_ERROR_INVALID_RACK_HANDLE;
    }
    if (!outHandle) {
        return ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS;
    }
    std::scoped_lock lock{g_state_mutex};
    const auto rack = g_racks.find(rackHandle);
    if (rack == g_racks.end()) {
        return ORBIS_NGS2_ERROR_INVALID_RACK_HANDLE;
    }
    if (voiceIndex >= rack->second.voices.size()) {
        return ORBIS_NGS2_ERROR_INVALID_VOICE_INDEX;
    }
    *outHandle = rack->second.voices[voiceIndex];
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNgs2RackLock(OrbisNgs2Handle rackHandle) {
    LOG_ERROR(Lib_Ngs2, "called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNgs2RackQueryBufferSize(u32 rackId, const OrbisNgs2RackOption* option,
                                            OrbisNgs2ContextBufferInfo* outBufferInfo) {
    LOG_DEBUG(Lib_Ngs2, "rackId = {}", rackId);
    if (!outBufferInfo) {
        return ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS;
    }
    std::memset(outBufferInfo, 0, sizeof(*outBufferInfo));
    const auto maxVoices = option && option->maxVoices ? option->maxVoices : DefaultMaxVoices;
    outBufferInfo->hostBufferSize = sizeof(RackRuntimeState) + maxVoices * sizeof(VoiceRuntimeState);
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNgs2RackSetUserData(OrbisNgs2Handle rackHandle, uintptr_t userData) {
    LOG_DEBUG(Lib_Ngs2, "userData = {}", userData);
    if (!rackHandle) {
        return ORBIS_NGS2_ERROR_INVALID_RACK_HANDLE;
    }
    std::scoped_lock lock{g_state_mutex};
    auto rack = g_racks.find(rackHandle);
    if (rack == g_racks.end()) {
        return ORBIS_NGS2_ERROR_INVALID_RACK_HANDLE;
    }
    rack->second.userData = userData;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNgs2RackUnlock(OrbisNgs2Handle rackHandle) {
    LOG_ERROR(Lib_Ngs2, "called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNgs2SystemCreate(const OrbisNgs2SystemOption* option,
                                     const OrbisNgs2ContextBufferInfo* bufferInfo,
                                     OrbisNgs2Handle* outHandle) {
    s32 result;
    OrbisNgs2ContextBufferInfo localInfo;
    if (!bufferInfo || !outHandle) {
        if (!bufferInfo) {
            result = ORBIS_NGS2_ERROR_INVALID_BUFFER_INFO;
            LOG_ERROR(Lib_Ngs2, "Invalid system buffer info {}", (void*)bufferInfo);
        } else {
            result = ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS;
            LOG_ERROR(Lib_Ngs2, "Invalid system handle address {}", (void*)outHandle);
        }

        // TODO: Report errors?
    } else {
        // Make bufferInfo copy
        localInfo.hostBuffer = bufferInfo->hostBuffer;
        localInfo.hostBufferSize = bufferInfo->hostBufferSize;
        for (int i = 0; i < 5; i++) {
            localInfo.reserved[i] = bufferInfo->reserved[i];
        }
        localInfo.userData = bufferInfo->userData;

        result = SystemSetup(option, &localInfo, 0, outHandle);
        if (result >= 0) {
            *outHandle = RegisterSystem(option, &localInfo);
        }
    }

    // TODO: API reporting?

    LOG_INFO(Lib_Ngs2, "called");
    return result;
}

s32 PS4_SYSV_ABI sceNgs2SystemCreateWithAllocator(const OrbisNgs2SystemOption* option,
                                                  const OrbisNgs2BufferAllocator* allocator,
                                                  OrbisNgs2Handle* outHandle) {
    s32 result;
    if (allocator && allocator->allocHandler != 0) {
        OrbisNgs2BufferAllocHandler hostAlloc = allocator->allocHandler;
        if (outHandle) {
            OrbisNgs2BufferFreeHandler hostFree = allocator->freeHandler;
            OrbisNgs2ContextBufferInfo bufferInfo;
            std::memset(&bufferInfo, 0, sizeof(bufferInfo));
            bufferInfo.userData = allocator->userData;
            result = SystemSetup(option, &bufferInfo, 0, 0);
            if (result >= 0) {
                result = hostAlloc(&bufferInfo);
                if (result >= 0) {
                    OrbisNgs2Handle* handleCopy = outHandle;
                    result = SystemSetup(option, &bufferInfo, hostFree, handleCopy);
                    if (result >= 0) {
                        *handleCopy = RegisterSystem(option, &bufferInfo);
                    }
                    if (result < 0) {
                        if (hostFree) {
                            hostFree(&bufferInfo);
                        }
                    }
                }
            }
        } else {
            result = ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS;
            LOG_ERROR(Lib_Ngs2, "Invalid system handle address {}", (void*)outHandle);
        }
    } else {
        result = ORBIS_NGS2_ERROR_INVALID_BUFFER_ALLOCATOR;
        LOG_ERROR(Lib_Ngs2, "Invalid system buffer allocator {}", (void*)allocator);
    }
    LOG_INFO(Lib_Ngs2, "called");
    return result;
}

s32 PS4_SYSV_ABI sceNgs2SystemDestroy(OrbisNgs2Handle systemHandle,
                                      OrbisNgs2ContextBufferInfo* outBufferInfo) {
    if (!systemHandle) {
        LOG_ERROR(Lib_Ngs2, "systemHandle is nullptr");
        return ORBIS_NGS2_ERROR_INVALID_SYSTEM_HANDLE;
    }
    std::scoped_lock lock{g_state_mutex};
    auto system = g_systems.find(systemHandle);
    if (system == g_systems.end()) {
        return ORBIS_NGS2_ERROR_INVALID_SYSTEM_HANDLE;
    }
    CopyBufferInfo(&system->second.bufferInfo, outBufferInfo);
    for (const auto rackHandle : system->second.racks) {
        if (auto rack = g_racks.find(rackHandle); rack != g_racks.end()) {
            for (const auto voiceHandle : rack->second.voices) {
                g_voices.erase(voiceHandle);
            }
            g_racks.erase(rack);
        }
    }
    g_systems.erase(system);
    LOG_INFO(Lib_Ngs2, "called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNgs2SystemEnumHandles(OrbisNgs2Handle* aOutHandle, u32 maxHandles) {
    LOG_DEBUG(Lib_Ngs2, "maxHandles = {}", maxHandles);
    if (!aOutHandle && maxHandles != 0) {
        return ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS;
    }
    std::scoped_lock lock{g_state_mutex};
    u32 count = 0;
    for (const auto& [handle, _] : g_systems) {
        if (count >= maxHandles) {
            break;
        }
        aOutHandle[count++] = handle;
    }
    return static_cast<s32>(count);
}

s32 PS4_SYSV_ABI sceNgs2SystemEnumRackHandles(OrbisNgs2Handle systemHandle,
                                              OrbisNgs2Handle* aOutHandle, u32 maxHandles) {
    LOG_DEBUG(Lib_Ngs2, "maxHandles = {}", maxHandles);
    if (!systemHandle) {
        LOG_ERROR(Lib_Ngs2, "systemHandle is nullptr");
        return ORBIS_NGS2_ERROR_INVALID_SYSTEM_HANDLE;
    }
    if (!aOutHandle && maxHandles != 0) {
        return ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS;
    }
    std::scoped_lock lock{g_state_mutex};
    const auto system = g_systems.find(systemHandle);
    if (system == g_systems.end()) {
        return ORBIS_NGS2_ERROR_INVALID_SYSTEM_HANDLE;
    }
    const auto count = std::min<u32>(maxHandles, static_cast<u32>(system->second.racks.size()));
    std::copy_n(system->second.racks.begin(), count, aOutHandle);
    return static_cast<s32>(count);
}

s32 PS4_SYSV_ABI sceNgs2SystemGetInfo(OrbisNgs2Handle rackHandle, OrbisNgs2SystemInfo* outInfo,
                                      size_t infoSize) {
    LOG_DEBUG(Lib_Ngs2, "infoSize = {}", infoSize);
    if (!rackHandle) {
        return ORBIS_NGS2_ERROR_INVALID_SYSTEM_HANDLE;
    }
    if (!outInfo) {
        return ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS;
    }
    if (infoSize < sizeof(OrbisNgs2SystemInfo)) {
        return ORBIS_NGS2_ERROR_INVALID_OUT_SIZE;
    }
    std::scoped_lock lock{g_state_mutex};
    const auto system = g_systems.find(rackHandle);
    if (system == g_systems.end()) {
        return ORBIS_NGS2_ERROR_INVALID_SYSTEM_HANDLE;
    }
    FillSystemInfo(system->second, outInfo, infoSize);
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNgs2SystemGetUserData(OrbisNgs2Handle systemHandle, uintptr_t* outUserData) {
    if (!systemHandle) {
        LOG_ERROR(Lib_Ngs2, "systemHandle is nullptr");
        return ORBIS_NGS2_ERROR_INVALID_SYSTEM_HANDLE;
    }
    if (!outUserData) {
        return ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS;
    }
    std::scoped_lock lock{g_state_mutex};
    const auto system = g_systems.find(systemHandle);
    if (system == g_systems.end()) {
        return ORBIS_NGS2_ERROR_INVALID_SYSTEM_HANDLE;
    }
    *outUserData = system->second.userData;
    LOG_DEBUG(Lib_Ngs2, "called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNgs2SystemLock(OrbisNgs2Handle systemHandle) {
    if (!systemHandle) {
        LOG_ERROR(Lib_Ngs2, "systemHandle is nullptr");
        return ORBIS_NGS2_ERROR_INVALID_SYSTEM_HANDLE;
    }
    LOG_ERROR(Lib_Ngs2, "called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNgs2SystemQueryBufferSize(const OrbisNgs2SystemOption* option,
                                              OrbisNgs2ContextBufferInfo* outBufferInfo) {
    s32 result;
    if (outBufferInfo) {
        result = SystemSetup(option, outBufferInfo, 0, 0);
        LOG_INFO(Lib_Ngs2, "called");
    } else {
        result = ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS;
        LOG_ERROR(Lib_Ngs2, "Invalid system buffer info {}", (void*)outBufferInfo);
    }

    return result;
}

s32 PS4_SYSV_ABI sceNgs2SystemRender(OrbisNgs2Handle systemHandle,
                                     const OrbisNgs2RenderBufferInfo* aBufferInfo,
                                     u32 numBufferInfo) {
    LOG_DEBUG(Lib_Ngs2, "numBufferInfo = {}", numBufferInfo);
    if (!systemHandle) {
        LOG_ERROR(Lib_Ngs2, "systemHandle is nullptr");
        return ORBIS_NGS2_ERROR_INVALID_SYSTEM_HANDLE;
    }
    {
        std::scoped_lock lock{g_state_mutex};
        auto system = g_systems.find(systemHandle);
        if (system == g_systems.end()) {
            return ORBIS_NGS2_ERROR_INVALID_SYSTEM_HANDLE;
        }
        ++system->second.renderCount;
        for (const auto rackHandle : system->second.racks) {
            if (auto rack = g_racks.find(rackHandle); rack != g_racks.end()) {
                ++rack->second.renderCount;
            }
        }
    }
    ZeroRenderBuffers(aBufferInfo, numBufferInfo);
    return ORBIS_OK;
}

static s32 PS4_SYSV_ABI sceNgs2SystemResetOption(OrbisNgs2SystemOption* outOption) {
    static const OrbisNgs2SystemOption option = {
        sizeof(OrbisNgs2SystemOption), "", 0, 512, 256, 48000, {0}};

    if (!outOption) {
        LOG_ERROR(Lib_Ngs2, "Invalid system option address {}", (void*)outOption);
        return ORBIS_NGS2_ERROR_INVALID_OPTION_ADDRESS;
    }
    *outOption = option;

    LOG_INFO(Lib_Ngs2, "called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNgs2SystemSetGrainSamples(OrbisNgs2Handle systemHandle, u32 numSamples) {
    LOG_DEBUG(Lib_Ngs2, "numSamples = {}", numSamples);
    if (!systemHandle) {
        LOG_ERROR(Lib_Ngs2, "systemHandle is nullptr");
        return ORBIS_NGS2_ERROR_INVALID_SYSTEM_HANDLE;
    }
    std::scoped_lock lock{g_state_mutex};
    auto system = g_systems.find(systemHandle);
    if (system == g_systems.end()) {
        return ORBIS_NGS2_ERROR_INVALID_SYSTEM_HANDLE;
    }
    system->second.numGrainSamples = numSamples;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNgs2SystemSetSampleRate(OrbisNgs2Handle systemHandle, u32 sampleRate) {
    LOG_DEBUG(Lib_Ngs2, "sampleRate = {}", sampleRate);
    if (!systemHandle) {
        LOG_ERROR(Lib_Ngs2, "systemHandle is nullptr");
        return ORBIS_NGS2_ERROR_INVALID_SYSTEM_HANDLE;
    }
    std::scoped_lock lock{g_state_mutex};
    auto system = g_systems.find(systemHandle);
    if (system == g_systems.end()) {
        return ORBIS_NGS2_ERROR_INVALID_SYSTEM_HANDLE;
    }
    system->second.sampleRate = sampleRate;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNgs2SystemSetUserData(OrbisNgs2Handle systemHandle, uintptr_t userData) {
    LOG_DEBUG(Lib_Ngs2, "userData = {}", userData);
    if (!systemHandle) {
        LOG_ERROR(Lib_Ngs2, "systemHandle is nullptr");
        return ORBIS_NGS2_ERROR_INVALID_SYSTEM_HANDLE;
    }
    std::scoped_lock lock{g_state_mutex};
    auto system = g_systems.find(systemHandle);
    if (system == g_systems.end()) {
        return ORBIS_NGS2_ERROR_INVALID_SYSTEM_HANDLE;
    }
    system->second.userData = userData;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNgs2SystemUnlock(OrbisNgs2Handle systemHandle) {
    if (!systemHandle) {
        LOG_ERROR(Lib_Ngs2, "systemHandle is nullptr");
        return ORBIS_NGS2_ERROR_INVALID_SYSTEM_HANDLE;
    }
    LOG_ERROR(Lib_Ngs2, "called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNgs2VoiceControl(OrbisNgs2Handle voiceHandle,
                                     const OrbisNgs2VoiceParamHeader* paramList) {
    LOG_DEBUG(Lib_Ngs2, "called");
    if (!voiceHandle) {
        return ORBIS_NGS2_ERROR_INVALID_VOICE_HANDLE;
    }
    if (!paramList) {
        return ORBIS_NGS2_ERROR_INVALID_VOICE_CONTROL_ADDRESS;
    }
    TraceVoiceParams(voiceHandle, paramList);
    std::scoped_lock lock{g_state_mutex};
    auto voice = g_voices.find(voiceHandle);
    if (voice == g_voices.end()) {
        return ORBIS_NGS2_ERROR_INVALID_VOICE_HANDLE;
    }
    ++voice->second.controlCount;
    ApplyVoiceParams(voice->second, paramList);
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNgs2VoiceGetMatrixInfo(OrbisNgs2Handle voiceHandle, u32 matrixId,
                                           OrbisNgs2VoiceMatrixInfo* outInfo, size_t outInfoSize) {
    LOG_DEBUG(Lib_Ngs2, "matrixId = {}, outInfoSize = {}", matrixId, outInfoSize);
    if (!voiceHandle) {
        return ORBIS_NGS2_ERROR_INVALID_VOICE_HANDLE;
    }
    if (!outInfo) {
        return ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS;
    }
    if (outInfoSize < sizeof(OrbisNgs2VoiceMatrixInfo)) {
        return ORBIS_NGS2_ERROR_INVALID_OUT_SIZE;
    }
    std::scoped_lock lock{g_state_mutex};
    const auto voice = g_voices.find(voiceHandle);
    if (voice == g_voices.end()) {
        return ORBIS_NGS2_ERROR_INVALID_VOICE_HANDLE;
    }
    std::memset(outInfo, 0, outInfoSize);
    if (matrixId < voice->second.matrices.size()) {
        *outInfo = voice->second.matrices[matrixId];
    }
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNgs2VoiceGetOwner(OrbisNgs2Handle voiceHandle, OrbisNgs2Handle* outRackHandle,
                                      u32* outVoiceId) {
    LOG_DEBUG(Lib_Ngs2, "called");
    if (!voiceHandle) {
        return ORBIS_NGS2_ERROR_INVALID_VOICE_HANDLE;
    }
    if (!outRackHandle || !outVoiceId) {
        return ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS;
    }
    std::scoped_lock lock{g_state_mutex};
    const auto voice = g_voices.find(voiceHandle);
    if (voice == g_voices.end()) {
        return ORBIS_NGS2_ERROR_INVALID_VOICE_HANDLE;
    }
    *outRackHandle = voice->second.rackHandle;
    *outVoiceId = voice->second.voiceIndex;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNgs2VoiceGetPortInfo(OrbisNgs2Handle voiceHandle, u32 port,
                                         OrbisNgs2VoicePortInfo* outInfo, size_t outInfoSize) {
    LOG_DEBUG(Lib_Ngs2, "port = {}, outInfoSize = {}", port, outInfoSize);
    if (!voiceHandle) {
        return ORBIS_NGS2_ERROR_INVALID_VOICE_HANDLE;
    }
    if (!outInfo) {
        return ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS;
    }
    if (outInfoSize < sizeof(OrbisNgs2VoicePortInfo)) {
        return ORBIS_NGS2_ERROR_INVALID_OUT_SIZE;
    }
    std::scoped_lock lock{g_state_mutex};
    const auto voice = g_voices.find(voiceHandle);
    if (voice == g_voices.end()) {
        return ORBIS_NGS2_ERROR_INVALID_VOICE_HANDLE;
    }
    std::memset(outInfo, 0, outInfoSize);
    if (port < voice->second.ports.size()) {
        *outInfo = voice->second.ports[port];
    } else {
        outInfo->volume = 1.0f;
        outInfo->matrixId = -1;
    }
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNgs2VoiceGetState(OrbisNgs2Handle voiceHandle, OrbisNgs2VoiceState* outState,
                                      size_t stateSize) {
    LOG_DEBUG(Lib_Ngs2, "stateSize = {}", stateSize);
    if (!voiceHandle) {
        return ORBIS_NGS2_ERROR_INVALID_VOICE_HANDLE;
    }
    if (!outState) {
        return ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS;
    }
    if (stateSize < sizeof(OrbisNgs2VoiceState)) {
        return ORBIS_NGS2_ERROR_INVALID_OUT_SIZE;
    }
    std::memset(outState, 0, stateSize);
    std::scoped_lock lock{g_state_mutex};
    const auto voice = g_voices.find(voiceHandle);
    if (voice == g_voices.end()) {
        return ORBIS_NGS2_ERROR_INVALID_VOICE_HANDLE;
    }
    outState->stateFlags = voice->second.stateFlags;
    if (stateSize >= sizeof(OrbisNgs2CustomSamplerVoiceState)) {
        auto* custom_state = reinterpret_cast<OrbisNgs2CustomSamplerVoiceState*>(outState);
        custom_state->waveformData = voice->second.waveformData;
        custom_state->numDecodedSamples = voice->second.decodedSamples;
        custom_state->decodedDataSize = voice->second.decodedDataSize;
        custom_state->userData = voice->second.callbackData;
    }
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNgs2VoiceGetStateFlags(OrbisNgs2Handle voiceHandle, u32* outStateFlags) {
    LOG_DEBUG(Lib_Ngs2, "called");
    if (!voiceHandle) {
        return ORBIS_NGS2_ERROR_INVALID_VOICE_HANDLE;
    }
    if (!outStateFlags) {
        return ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS;
    }
    std::scoped_lock lock{g_state_mutex};
    const auto voice = g_voices.find(voiceHandle);
    if (voice == g_voices.end()) {
        return ORBIS_NGS2_ERROR_INVALID_VOICE_HANDLE;
    }
    *outStateFlags = voice->second.stateFlags;
    return ORBIS_OK;
}

// Ngs2Custom

s32 PS4_SYSV_ABI sceNgs2CustomRackGetModuleInfo(OrbisNgs2Handle rackHandle, u32 moduleIndex,
                                                OrbisNgs2CustomModuleInfo* outInfo,
                                                size_t infoSize) {
    LOG_ERROR(Lib_Ngs2, "moduleIndex = {}, infoSize = {}", moduleIndex, infoSize);
    return ORBIS_OK;
}

// Ngs2Geom

s32 PS4_SYSV_ABI sceNgs2GeomResetListenerParam(OrbisNgs2GeomListenerParam* outListenerParam) {
    LOG_ERROR(Lib_Ngs2, "called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNgs2GeomResetSourceParam(OrbisNgs2GeomSourceParam* outSourceParam) {
    LOG_ERROR(Lib_Ngs2, "called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNgs2GeomCalcListener(const OrbisNgs2GeomListenerParam* param,
                                         OrbisNgs2GeomListenerWork* outWork, u32 flags) {
    LOG_ERROR(Lib_Ngs2, "flags = {}", flags);
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNgs2GeomApply(const OrbisNgs2GeomListenerWork* listener,
                                  const OrbisNgs2GeomSourceParam* source,
                                  OrbisNgs2GeomAttribute* outAttrib, u32 flags) {
    LOG_ERROR(Lib_Ngs2, "flags = {}", flags);
    return ORBIS_OK;
}

// Ngs2Pan

s32 PS4_SYSV_ABI sceNgs2PanInit(OrbisNgs2PanWork* work, const float* aSpeakerAngle, float unitAngle,
                                u32 numSpeakers) {
    LOG_ERROR(Lib_Ngs2, "unitAngle = {}, numSpeakers = {}", unitAngle, numSpeakers);
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNgs2PanGetVolumeMatrix(OrbisNgs2PanWork* work, const OrbisNgs2PanParam* aParam,
                                           u32 numParams, u32 matrixFormat,
                                           float* outVolumeMatrix) {
    LOG_ERROR(Lib_Ngs2, "numParams = {}, matrixFormat = {}", numParams, matrixFormat);
    return ORBIS_OK;
}

// Ngs2Report

s32 PS4_SYSV_ABI sceNgs2ReportRegisterHandler(u32 reportType, OrbisNgs2ReportHandler handler,
                                              uintptr_t userData, OrbisNgs2Handle* outHandle) {
    LOG_INFO(Lib_Ngs2, "reportType = {}, userData = {}", reportType, userData);
    if (!handler) {
        LOG_ERROR(Lib_Ngs2, "handler is nullptr");
        return ORBIS_NGS2_ERROR_INVALID_REPORT_HANDLE;
    }
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNgs2ReportUnregisterHandler(OrbisNgs2Handle reportHandle) {
    if (!reportHandle) {
        LOG_ERROR(Lib_Ngs2, "reportHandle is nullptr");
        return ORBIS_NGS2_ERROR_INVALID_REPORT_HANDLE;
    }
    LOG_INFO(Lib_Ngs2, "called");
    return ORBIS_OK;
}

// Unknown

int PS4_SYSV_ABI sceNgs2FftInit() {
    LOG_ERROR(Lib_Ngs2, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceNgs2FftProcess() {
    LOG_ERROR(Lib_Ngs2, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceNgs2FftQuerySize() {
    LOG_ERROR(Lib_Ngs2, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceNgs2JobSchedulerResetOption() {
    LOG_ERROR(Lib_Ngs2, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceNgs2ModuleArrayEnumItems() {
    LOG_ERROR(Lib_Ngs2, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceNgs2ModuleEnumConfigs() {
    LOG_ERROR(Lib_Ngs2, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceNgs2ModuleQueueEnumItems() {
    LOG_ERROR(Lib_Ngs2, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceNgs2RackQueryInfo() {
    LOG_ERROR(Lib_Ngs2, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceNgs2RackRunCommands() {
    LOG_ERROR(Lib_Ngs2, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceNgs2SystemQueryInfo() {
    LOG_ERROR(Lib_Ngs2, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceNgs2SystemRunCommands() {
    LOG_ERROR(Lib_Ngs2, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceNgs2SystemSetLoudThreshold() {
    LOG_ERROR(Lib_Ngs2, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceNgs2StreamCreate() {
    LOG_ERROR(Lib_Ngs2, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceNgs2StreamCreateWithAllocator() {
    LOG_ERROR(Lib_Ngs2, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceNgs2StreamDestroy() {
    LOG_ERROR(Lib_Ngs2, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceNgs2StreamQueryBufferSize() {
    LOG_ERROR(Lib_Ngs2, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceNgs2StreamQueryInfo() {
    LOG_ERROR(Lib_Ngs2, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceNgs2StreamResetOption() {
    LOG_ERROR(Lib_Ngs2, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceNgs2StreamRunCommands() {
    LOG_ERROR(Lib_Ngs2, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceNgs2VoiceQueryInfo() {
    LOG_ERROR(Lib_Ngs2, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceNgs2VoiceRunCommands() {
    LOG_ERROR(Lib_Ngs2, "(STUBBED) called");
    return ORBIS_OK;
}

void RegisterLib(Core::Loader::SymbolsResolver* sym) {
    LIB_FUNCTION("3pCNbVM11UA", "libSceNgs2", 1, "libSceNgs2", sceNgs2CalcWaveformBlock);
    LIB_FUNCTION("6qN1zaEZuN0", "libSceNgs2", 1, "libSceNgs2", sceNgs2CustomRackGetModuleInfo);
    LIB_FUNCTION("Kg1MA5j7KFk", "libSceNgs2", 1, "libSceNgs2", sceNgs2FftInit);
    LIB_FUNCTION("D8eCqBxSojA", "libSceNgs2", 1, "libSceNgs2", sceNgs2FftProcess);
    LIB_FUNCTION("-YNfTO6KOMY", "libSceNgs2", 1, "libSceNgs2", sceNgs2FftQuerySize);
    LIB_FUNCTION("eF8yRCC6W64", "libSceNgs2", 1, "libSceNgs2", sceNgs2GeomApply);
    LIB_FUNCTION("1WsleK-MTkE", "libSceNgs2", 1, "libSceNgs2", sceNgs2GeomCalcListener);
    LIB_FUNCTION("7Lcfo8SmpsU", "libSceNgs2", 1, "libSceNgs2", sceNgs2GeomResetListenerParam);
    LIB_FUNCTION("0lbbayqDNoE", "libSceNgs2", 1, "libSceNgs2", sceNgs2GeomResetSourceParam);
    LIB_FUNCTION("ekGJmmoc8j4", "libSceNgs2", 1, "libSceNgs2", sceNgs2GetWaveformFrameInfo);
    LIB_FUNCTION("BcoPfWfpvVI", "libSceNgs2", 1, "libSceNgs2", sceNgs2JobSchedulerResetOption);
    LIB_FUNCTION("EEemGEQCjO8", "libSceNgs2", 1, "libSceNgs2", sceNgs2ModuleArrayEnumItems);
    LIB_FUNCTION("TaoNtmMKkXQ", "libSceNgs2", 1, "libSceNgs2", sceNgs2ModuleEnumConfigs);
    LIB_FUNCTION("ve6bZi+1sYQ", "libSceNgs2", 1, "libSceNgs2", sceNgs2ModuleQueueEnumItems);
    LIB_FUNCTION("gbMKV+8Enuo", "libSceNgs2", 1, "libSceNgs2", sceNgs2PanGetVolumeMatrix);
    LIB_FUNCTION("xa8oL9dmXkM", "libSceNgs2", 1, "libSceNgs2", sceNgs2PanInit);
    LIB_FUNCTION("hyVLT2VlOYk", "libSceNgs2", 1, "libSceNgs2", sceNgs2ParseWaveformData);
    LIB_FUNCTION("iprCTXPVWMI", "libSceNgs2", 1, "libSceNgs2", sceNgs2ParseWaveformFile);
    LIB_FUNCTION("t9T0QM17Kvo", "libSceNgs2", 1, "libSceNgs2", sceNgs2ParseWaveformUser);
    LIB_FUNCTION("cLV4aiT9JpA", "libSceNgs2", 1, "libSceNgs2", sceNgs2RackCreate);
    LIB_FUNCTION("U546k6orxQo", "libSceNgs2", 1, "libSceNgs2", sceNgs2RackCreateWithAllocator);
    LIB_FUNCTION("lCqD7oycmIM", "libSceNgs2", 1, "libSceNgs2", sceNgs2RackDestroy);
    LIB_FUNCTION("M4LYATRhRUE", "libSceNgs2", 1, "libSceNgs2", sceNgs2RackGetInfo);
    LIB_FUNCTION("Mn4XNDg03XY", "libSceNgs2", 1, "libSceNgs2", sceNgs2RackGetUserData);
    LIB_FUNCTION("MwmHz8pAdAo", "libSceNgs2", 1, "libSceNgs2", sceNgs2RackGetVoiceHandle);
    LIB_FUNCTION("MzTa7VLjogY", "libSceNgs2", 1, "libSceNgs2", sceNgs2RackLock);
    LIB_FUNCTION("0eFLVCfWVds", "libSceNgs2", 1, "libSceNgs2", sceNgs2RackQueryBufferSize);
    LIB_FUNCTION("TZqb8E-j3dY", "libSceNgs2", 1, "libSceNgs2", sceNgs2RackQueryInfo);
    LIB_FUNCTION("MI2VmBx2RbM", "libSceNgs2", 1, "libSceNgs2", sceNgs2RackRunCommands);
    LIB_FUNCTION("JNTMIaBIbV4", "libSceNgs2", 1, "libSceNgs2", sceNgs2RackSetUserData);
    LIB_FUNCTION("++YZ7P9e87U", "libSceNgs2", 1, "libSceNgs2", sceNgs2RackUnlock);
    LIB_FUNCTION("uBIN24Tv2MI", "libSceNgs2", 1, "libSceNgs2", sceNgs2ReportRegisterHandler);
    LIB_FUNCTION("nPzb7Ly-VjE", "libSceNgs2", 1, "libSceNgs2", sceNgs2ReportUnregisterHandler);
    LIB_FUNCTION("koBbCMvOKWw", "libSceNgs2", 1, "libSceNgs2", sceNgs2SystemCreate);
    LIB_FUNCTION("mPYgU4oYpuY", "libSceNgs2", 1, "libSceNgs2", sceNgs2SystemCreateWithAllocator);
    LIB_FUNCTION("u-WrYDaJA3k", "libSceNgs2", 1, "libSceNgs2", sceNgs2SystemDestroy);
    LIB_FUNCTION("vubFP0T6MP0", "libSceNgs2", 1, "libSceNgs2", sceNgs2SystemEnumHandles);
    LIB_FUNCTION("U-+7HsswcIs", "libSceNgs2", 1, "libSceNgs2", sceNgs2SystemEnumRackHandles);
    LIB_FUNCTION("vU7TQ62pItw", "libSceNgs2", 1, "libSceNgs2", sceNgs2SystemGetInfo);
    LIB_FUNCTION("4lFaRxd-aLs", "libSceNgs2", 1, "libSceNgs2", sceNgs2SystemGetUserData);
    LIB_FUNCTION("gThZqM5PYlQ", "libSceNgs2", 1, "libSceNgs2", sceNgs2SystemLock);
    LIB_FUNCTION("pgFAiLR5qT4", "libSceNgs2", 1, "libSceNgs2", sceNgs2SystemQueryBufferSize);
    LIB_FUNCTION("3oIK7y7O4k0", "libSceNgs2", 1, "libSceNgs2", sceNgs2SystemQueryInfo)
    LIB_FUNCTION("i0VnXM-C9fc", "libSceNgs2", 1, "libSceNgs2", sceNgs2SystemRender);
    LIB_FUNCTION("AQkj7C0f3PY", "libSceNgs2", 1, "libSceNgs2", sceNgs2SystemResetOption);
    LIB_FUNCTION("gXiormHoZZ4", "libSceNgs2", 1, "libSceNgs2", sceNgs2SystemRunCommands);
    LIB_FUNCTION("l4Q2dWEH6UM", "libSceNgs2", 1, "libSceNgs2", sceNgs2SystemSetGrainSamples);
    LIB_FUNCTION("Wdlx0ZFTV9s", "libSceNgs2", 1, "libSceNgs2", sceNgs2SystemSetLoudThreshold);
    LIB_FUNCTION("-tbc2SxQD60", "libSceNgs2", 1, "libSceNgs2", sceNgs2SystemSetSampleRate);
    LIB_FUNCTION("GZB2v0XnG0k", "libSceNgs2", 1, "libSceNgs2", sceNgs2SystemSetUserData);
    LIB_FUNCTION("JXRC5n0RQls", "libSceNgs2", 1, "libSceNgs2", sceNgs2SystemUnlock);
    LIB_FUNCTION("sU2St3agdjg", "libSceNgs2", 1, "libSceNgs2", sceNgs2StreamCreate);
    LIB_FUNCTION("I+RLwaauggA", "libSceNgs2", 1, "libSceNgs2", sceNgs2StreamCreateWithAllocator);
    LIB_FUNCTION("bfoMXnTRtwE", "libSceNgs2", 1, "libSceNgs2", sceNgs2StreamDestroy);
    LIB_FUNCTION("dxulc33msHM", "libSceNgs2", 1, "libSceNgs2", sceNgs2StreamQueryBufferSize);
    LIB_FUNCTION("rfw6ufRsmow", "libSceNgs2", 1, "libSceNgs2", sceNgs2StreamQueryInfo);
    LIB_FUNCTION("q+2W8YdK0F8", "libSceNgs2", 1, "libSceNgs2", sceNgs2StreamResetOption);
    LIB_FUNCTION("qQHCi9pjDps", "libSceNgs2", 1, "libSceNgs2", sceNgs2StreamRunCommands);
    LIB_FUNCTION("uu94irFOGpA", "libSceNgs2", 1, "libSceNgs2", sceNgs2VoiceControl);
    LIB_FUNCTION("jjBVvPN9964", "libSceNgs2", 1, "libSceNgs2", sceNgs2VoiceGetMatrixInfo);
    LIB_FUNCTION("W-Z8wWMBnhk", "libSceNgs2", 1, "libSceNgs2", sceNgs2VoiceGetOwner);
    LIB_FUNCTION("WCayTgob7-o", "libSceNgs2", 1, "libSceNgs2", sceNgs2VoiceGetPortInfo);
    LIB_FUNCTION("-TOuuAQ-buE", "libSceNgs2", 1, "libSceNgs2", sceNgs2VoiceGetState);
    LIB_FUNCTION("rEh728kXk3w", "libSceNgs2", 1, "libSceNgs2", sceNgs2VoiceGetStateFlags);
    LIB_FUNCTION("9eic4AmjGVI", "libSceNgs2", 1, "libSceNgs2", sceNgs2VoiceQueryInfo);
    LIB_FUNCTION("AbYvTOZ8Pts", "libSceNgs2", 1, "libSceNgs2", sceNgs2VoiceRunCommands);
};

} // namespace Libraries::Ngs2
