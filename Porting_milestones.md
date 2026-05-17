# shadPS4 Elisa Porting Milestones

This file tracks which shadPS4 C++ responsibilities have been ported, shadowed,
or prepared for incremental Elisa ownership. The goal is not a big-bang rewrite;
it is to replace small, inspectable pieces of C++ with safer Elisa code while
keeping the emulator runnable after every slice.

## Status Legend

- `ported`: Elisa owns the behavior in normal Elisa-enabled builds.
- `shadowed`: Elisa computes/checks the behavior, while C++ remains authoritative.
- `wrapped`: Elisa controls or calls into the C++ behavior through a narrow ABI.
- `planned`: Good next candidate, not implemented yet.

## Ported

| Area | C++ Source | Elisa Source | Status | Notes |
| --- | --- | --- | --- | --- |
| Launch intent parsing | `src/launch_cli.cpp` | `elisa/src/launch_intent.elisa` | ported | Elisa is the default parser when `SHADPS4_ENABLE_ELISA_PORTS` is enabled; `SHADPS4_DISABLE_ELISA_LAUNCH_INTENT=1` keeps the C++ escape hatch. |
| Launch directory validation | `src/launch_cli.cpp` | `elisa/src/launch_intent.elisa` | ported | Elisa validates `--override-root`, `--add-game-folder`, and `--set-addon-folder` through a small filesystem C ABI. |
| Root main dispatch | `src/main.cpp` | `elisa/src/shadps4_main.elisa` | ported | C++ `main.cpp` is now a platform trampoline in Elisa-enabled builds. Elisa decides no-args, help, parse-error, and normal-run flow. |
| macOS debugger wait helper | `core/debugger.*` path usage | `elisa/src/debugger.elisa` | ported | Elisa-backed wait path is used by the launch pipeline in Elisa-enabled builds, with platform-specific native helpers. |

## Wrapped Or Shadowed

| Area | C++ Source | Elisa/C ABI Surface | Status | Notes |
| --- | --- | --- | --- | --- |
| Launch execution pipeline | `src/launch_pipeline.cpp` | `elisa/src/launch_pipeline.elisa`, `src/launch_elisa_main_bridge.cpp` | wrapped | Elisa now owns the `LaunchPipeline::run_parsed_launch` module boundary and calls a narrow host bridge; C++ still owns runtime settings, utility commands, game resolution, and emulator start. |
| C ABI packaging | `CMakeLists.txt`, `cmake/Elisa.cmake` | `-emit c-archive` targets | ported | Elisa modules build as static archives with checked-in ABI headers and sidecar manifests. |
| Launch intent smoke/shadow tests | `elisa/native/launch_intent_smoke.cpp` | `elisa/src/launch_intent.elisa` | shadowed | Smoke compares normalized launch intent behavior without changing non-Elisa runtime behavior. |

## Compiler Porting Support

| Feature | Status | Notes |
| --- | --- | --- |
| Static C ABI archives | ported | `project.json` targets can emit `.a` archives for C/C++ linking. |
| Checked-in ABI headers | ported | Generated headers are audit artifacts; checked-in headers remain the build contract. |
| Unsafe permission fencing | ported | Trusted low-level regions are visible and auditable. |
| Bounds/provenance/alias safety slices | ported | These safety checks are compiler-enforced enough to dogfood low-level ports. |
| Progress safety | ported | Budgeted loops/recursion/blocking checks give us pressure tests against runaway code. |
| C++-style module paths | ported | `module Foo::Bar:` and `Foo::Bar::baz()` parse to existing dotted qualified names internally. |

## Planned Next Ports

| Area | C++ Source | Proposed Elisa Module | Status | Why It Is A Good Candidate |
| --- | --- | --- | --- | --- |
| Launch pipeline orchestration | `src/launch_pipeline.cpp` | `elisa/src/launch_pipeline.elisa` | planned | Mostly decision flow around already-parsed launch intent; good fit for module syntax and permissions. |
| Runtime settings launch flag policy | `src/launch_pipeline.cpp` | `LaunchPipeline::apply_launch_flags` | planned | Small branchy logic; effectful setters can remain native externs at first. |
| Utility command routing | `src/launch_pipeline.cpp` | `LaunchPipeline::handle_utility_command` | planned | Simple command selection; native calls can stay wrapped. |
| Game path normalization | `src/launch_pipeline.cpp` | `LaunchPipeline::normalize_game_path_and_args` | planned | Mostly pure argument policy with clear error cases. |
| Game checklist/update harness | manual testing workflow | `elisa/src/game_checklist.elisa` or fixtures | planned | Keeps compatibility discoveries structured as we dogfood. |

## Current Principle

Prefer porting policy, validation, orchestration, and trace/checklist code before
renderer/GPU execution. Effectful functions are welcome, but they should enter
Elisa through narrow permissions and small native ABI surfaces first.
