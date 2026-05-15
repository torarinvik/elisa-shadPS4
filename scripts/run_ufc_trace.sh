#!/usr/bin/env zsh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

SESSION_STAMP="$(date '+%Y%m%d_%H%M%S')"
SESSION_DIR="$ROOT_DIR/screenshot_logs/session_$SESSION_STAMP"
EMU_SCREENSHOT_DIR="$SESSION_DIR/emulator_screens"
DESKTOP_SCREENSHOT_DIR="$SESSION_DIR/desktop_screens"

mkdir -p "$EMU_SCREENSHOT_DIR" "$DESKTOP_SCREENSHOT_DIR"

: "${SHADPS4_TRACE_SCREENSHOT_INTERVAL_MS:=5000}"
: "${SHADPS4_TRACE_DESKTOP_SCREENSHOTS:=0}"
: "${SHADPS4_TRACE_SCREENSHOT_GAME_ONLY:=1}"
: "${SHADPS4_TRACE_SCREENSHOT_STATS_ONLY:=0}"
: "${SHADPS4_TRACE_SCREENSHOTS:=0}"
: "${SHADPS4_TRACE_RENDER:=0}"
: "${SHADPS4_TRACE_IMAGE_VIEW_INVARIANTS:=0}"
: "${SHADPS4_TRACE_VIDEO_OUT_EVERY:=120}"
: "${SHADPS4_TRACE_LABEL:=ufc_trace}"
: "${SHADPS4_NULL_FMASK_TEXTURE_READS:=0}"
: "${SHADPS4_FMASK_DECOMPRESS_IN_PLACE:=0}"
: "${SHADPS4_VIDEOOUT_UNORM:=1}"
: "${SHADPS4_DUMP_SHADER_HASH:=0xc455a5aa2c447041}"
: "${SHADPS4_COMPOSITOR_NULL_LAYER:=0}"
: "${SHADPS4_COMPOSITOR_ZERO_LAYER:=0}"
: "${SHADPS4_STRICT_BLACK_SCREEN_WATCHDOG:=0}"
: "${SHADPS4_BLACK_WATCHDOG_ARMED:=0}"

export SHADPS4_TRACE_INPUT=1
export SHADPS4_TRACE_RENDER
export SHADPS4_TRACE_IMAGE_VIEW_INVARIANTS
export SHADPS4_TRACE_VIDEO_OUT_EVERY
export SHADPS4_NULL_FMASK_TEXTURE_READS
export SHADPS4_FMASK_DECOMPRESS_IN_PLACE
export SHADPS4_VIDEOOUT_UNORM
export SHADPS4_DUMP_SHADER_HASH
export SHADPS4_COMPOSITOR_NULL_LAYER
export SHADPS4_COMPOSITOR_ZERO_LAYER
export SHADPS4_STRICT_BLACK_SCREEN_WATCHDOG
export SHADPS4_BLACK_WATCHDOG_ARMED

if [[ "$SHADPS4_TRACE_SCREENSHOTS" != "0" ]]; then
    export SHADPS4_TRACE_SCREENSHOT_INTERVAL_MS
    export SHADPS4_TRACE_SCREENSHOT_GAME_ONLY
    export SHADPS4_TRACE_SCREENSHOT_STATS_ONLY
    export SHADPS4_TRACE_SCREENSHOT_DIR="$EMU_SCREENSHOT_DIR"
fi

{
    echo "session=$SESSION_STAMP"
    echo "started_at=$(date '+%Y-%m-%d %H:%M:%S %z')"
    echo "label=$SHADPS4_TRACE_LABEL"
    echo "root=$ROOT_DIR"
    echo "emulator_screenshot_interval_ms=$SHADPS4_TRACE_SCREENSHOT_INTERVAL_MS"
    echo "emulator_screenshots=$SHADPS4_TRACE_SCREENSHOTS"
    echo "desktop_screenshots=$SHADPS4_TRACE_DESKTOP_SCREENSHOTS"
    echo "emulator_screenshot_game_only=$SHADPS4_TRACE_SCREENSHOT_GAME_ONLY"
    echo "emulator_screenshot_stats_only=$SHADPS4_TRACE_SCREENSHOT_STATS_ONLY"
    echo "render_trace=$SHADPS4_TRACE_RENDER"
    echo "image_view_invariants=$SHADPS4_TRACE_IMAGE_VIEW_INVARIANTS"
    echo "null_fmask_texture_reads=$SHADPS4_NULL_FMASK_TEXTURE_READS"
    echo "fmask_decompress_in_place=$SHADPS4_FMASK_DECOMPRESS_IN_PLACE"
    echo "videoout_unorm=$SHADPS4_VIDEOOUT_UNORM"
    echo "dump_shader_hash=$SHADPS4_DUMP_SHADER_HASH"
    echo "compositor_null_layer=$SHADPS4_COMPOSITOR_NULL_LAYER"
    echo "compositor_zero_layer=$SHADPS4_COMPOSITOR_ZERO_LAYER"
    echo "strict_black_screen_watchdog=$SHADPS4_STRICT_BLACK_SCREEN_WATCHDOG"
    echo "black_watchdog_armed=$SHADPS4_BLACK_WATCHDOG_ARMED"
    echo "sigkill_exit_key=F8"
    echo "aggressive_logging_enable_key=F10"
    echo "aggressive_logging_disable_key=F9"
    echo "video_out_trace_every=$SHADPS4_TRACE_VIDEO_OUT_EVERY"
    echo "command=./build/shadps4 -g Games/CUSA00264/eboot.bin"
} > "$SESSION_DIR/session_info.txt"

GAME_CONFIG="$HOME/Library/Application Support/shadPS4/custom_configs/CUSA00264.json"
if [[ -f "$GAME_CONFIG" ]]; then
    cp "$GAME_CONFIG" "$SESSION_DIR/CUSA00264_config.json"
fi

desktop_pid=""
if [[ "$SHADPS4_TRACE_DESKTOP_SCREENSHOTS" != "0" ]]; then
    (
        i=0
        interval_seconds="$(( SHADPS4_TRACE_SCREENSHOT_INTERVAL_MS / 1000 ))"
        if [[ "$interval_seconds" -lt 1 ]]; then
            interval_seconds=1
        fi
        while true; do
            stamp="$(date '+%Y%m%d_%H%M%S')"
            screencapture -x "$DESKTOP_SCREENSHOT_DIR/desktop_${stamp}_$(printf '%04d' "$i").png" \
                >/dev/null 2>&1 || true
            i=$((i + 1))
            sleep "$interval_seconds"
        done
    ) &
    desktop_pid="$!"
fi

cleanup() {
    if [[ -n "$desktop_pid" ]]; then
        kill "$desktop_pid" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT INT TERM

echo "Trace session: $SESSION_DIR"
echo "Press Ctrl-C here after the black-screen point, unless the machine freezes."
echo "Emulator output is being written to $SESSION_DIR/emulator.log"

./build/shadps4 -g Games/CUSA00264/eboot.bin > "$SESSION_DIR/emulator.log" 2>&1
