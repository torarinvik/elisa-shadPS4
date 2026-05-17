// SPDX-FileCopyrightText: Copyright 2025-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef SHADPS4_ENABLE_ELISA_PORTS
#include "elisa/native/shadps4_elisa_main.h"
#else
#include "launch_cli.h"
#include "launch_pipeline.h"
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

#ifdef SHADPS4_ENABLE_ELISA_PORTS
    return static_cast<int>(shadps4_elisa_main(argc, reinterpret_cast<uint8_t**>(argv)));
#else
    const auto parsed = LaunchCli::Parse(argc, argv);
    if (parsed.should_exit) {
        return parsed.exit_code;
    }

    return LaunchPipeline::RunParsedLaunch(argv[0], parsed.state);
#endif
}
