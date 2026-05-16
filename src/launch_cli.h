// SPDX-FileCopyrightText: Copyright 2025-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

#include "launch_intent_shadow.h"

namespace LaunchCli {

struct ParseResult {
    LaunchIntent::CliState state;
    bool should_exit = false;
    int exit_code = 0;
};

ParseResult Parse(int argc, char* argv[]);

} // namespace LaunchCli
