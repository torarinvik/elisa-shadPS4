# Elisa shadPS4 interop and UFC trace harness

This directory dogfoods Elisa against shadPS4 through a deliberately small C ABI.

The first slice proved the native bridge. The current slice adds a safe UFC 1 trace harness:

- link a C translation unit from the shadPS4 tree
- call exported C ABI functions
- annotate imported C entrypoints with granular permissions
- keep grant blocks narrow around the actual process, filesystem, FFI, and console operations
- launch `build/shadps4` through a timeout-enforced wrapper
- run `CUSA00264` with short, named trace profiles
- keep experiment profile policy in Elisa, then pass explicit toggle flags to the native bridge
- parse render/FMASK trace logs in Elisa
- summarize the last known render checkpoints before risky black-screen investigation

Run from the Elisa-core `compiler` directory:

```sh
go run ./src test tests --project ../shadPS4/elisa
go run ./src run app --project ../shadPS4/elisa
```

The app runs the `baseline-safe` profile, asks Elisa to choose the next safest targeted profile from
that baseline summary, then runs the chosen profile with an 8 second timeout and prints a compact
comparison. The test target uses fixture logs and does not launch the emulator.

Live runs use two safety layers. The C bridge owns process primitives and hard termination, while
Elisa owns the orchestration loop through `start`, `poll`, and `kill` native calls. A cooperative
`TraceProgressBudget` ticks through launch, each bounded poll, native error collection, log readback,
and parser analysis. The native poll sleeps for a short interval and enforces the wall-clock deadline,
so Elisa can supervise the run without busy-spinning or entering one long opaque wait.

For already-captured logs, call `ufc_trace_analyze_existing_log(path)` from Elisa. That path only grants
`FS.Read`, `Sys.FFI`, and `SysMemory.Foreign`; it does not spawn or kill shadPS4. This is the preferred
way to iterate on parser and FMASK/compositor heuristics before any live emulator run.

The native reader filters large trace logs down to render evidence, so runaway traces cannot make the
Elisa analyzer chew through unbounded output. Filtered logs preserve launch sanity markers,
render-target/depth-target/metadata/FM‌ASK `TRACE_RENDER` evidence, `TRACE_VIDEO_OUT`, shader/pipeline
compile lines, and GPU command diagnostics, while dropping repeated image-binding and runtime chatter.

The harness intentionally keeps the effect grants local. For example, `main.elisa` grants process/env/filesystem authority only around `ufc_trace_run_baseline_safe`, and grants `Console.Write` only around summary printing. Pure Elisa parsing code does not need those permissions.

The raw C calls are also fenced as implementation details. Public Elisa wrappers still expose the concrete permissions they need, such as `SysProcess.Spawn`, `FS.Read`, `FS.Write`, `SysEnv.Write`, `SysMemory.Foreign`, `Clock.Read`, and `Console.Write`, but the `Unsafe.RawExtern` grants stay in tiny trusted blocks around the imported symbols. You can audit that boundary with:

```sh
go run ./src -emit unsafe ../shadPS4/elisa/src/ufc_trace.elisa
```

The expected report should list only the imported C ABI symbols, not the higher-level Elisa harness functions.

Available profiles are represented in Elisa:

- `baseline-safe`
- `fmask-null-read`
- `fmask-in-place`
- `compositor-null-layer`
- `videoout-unorm`

## Root-first porting map

Porting from `src/main.cpp` is doable if we treat it as a dependency tree and only replace safe
root-adjacent policy first. The current root does, in order:

1. Parse CLI options and extras.
2. Handle no-args/help and simple utility commands.
3. Initialize logging, IPC, emulator state, user settings, and config.
4. Migrate trophy keys.
5. Apply launch flags such as patch file, fullscreen, FPS, and config mode.
6. Resolve game path or game ID through configured install directories.
7. Hand control to `Core::Emulator::Run`.

The safe Elisa candidates are the first six layers: CLI schema, launch intent construction, config
mode decisions, utility-command policy, path/ID resolution policy, and preflight diagnostics. The
unsafe/high-risk leaf remains `Core::Emulator::Run` and the renderer stack below it. A good next
slice is to define an Elisa `LaunchIntent` mirroring the parsed CLI state, test it with fixture args,
then pass that intent through a narrow C++ bridge while keeping actual emulator execution in C++.

The first root-port slice now lives in `src/launch_intent.elisa`. It mirrors the safe, root-adjacent
policy from `src/main.cpp`: no-args behavior, `--game`/`--patch`/`--fullscreen`, utility commands,
config-mode selection, debugger wait flags, log append, and guest args after `--`. It deliberately
does not touch filesystem resolution or `Core::Emulator::Run`; those remain C++ until the pure
`ShadLaunchIntent` layer has enough fixture coverage to become the handoff contract.

The launch-intent module also exposes a small C ABI, but shadPS4 does not need to build against a
generated Elisa header. The intended integration shape is now a static archive:

```sh
go run ./src -emit c-archive -o ../shadPS4/elisa/build/libshadps4_elisa_launch_intent.a ../shadPS4/elisa/src/launch_intent.elisa
go run ./src build launch_intent_abi --project ../shadPS4/elisa
```

Cross-architecture CMake builds pass an explicit LLVM target triple into the archive builder. You can
also do that by hand from the Elisa-core `compiler` directory:

```sh
go run ./src build launch_intent_abi --project ../shadPS4/elisa -target-triple arm64-apple-macosx15.4
go run ./src build launch_intent_abi --project ../shadPS4/elisa -target-triple x86_64-apple-macosx15.4
```

That archive contains the Elisa object plus the default Elisa runtime object when needed. C++ includes
the reviewed stable declaration in `native/shadps4_elisa_launch_intent.h`; generated headers are not
part of the build contract. Header emission remains useful as an ABI audit tool:

```sh
go run ./src -emit header -o /tmp/launch_intent.h ../shadPS4/elisa/src/launch_intent.elisa
```

but generated headers should be compared against the checked-in C ABI, not treated as an implicit
build dependency. Larger Elisa-owned records cross the boundary through caller-provided output
storage, so the compiler does not need platform-specific large-struct return lowering.

The semantic header drift check compares the checked-in C ABI header against the generated audit
header while ignoring harmless parameter-name and whitespace differences:

```sh
python3 ../shadPS4/elisa/native/check_c_abi_header.py \
  ../shadPS4/elisa/native/shadps4_elisa_launch_intent.h \
  ../shadPS4/elisa/build/libshadps4_elisa_launch_intent.h
```

Stable C ABI rules for incremental ports:

- Do not return large structs by value; pass caller-owned output storage.
- Use fixed-width integers or pointer-compatible fields at the boundary.
- Do not return Elisa-owned heap pointers unless a matching destroy/free function is exported.
- Keep checked-in C/C++ headers as the integration contract; generated headers are drift/audit tools.

For shadPS4 CMake dogfooding, configure with:

```sh
cmake -S . -B build-elisa -DSHADPS4_ENABLE_ELISA_PORTS=ON
cmake --build build-elisa --target shadps4_elisa_launch_intent_abi_check
cmake --build build-elisa --target shadps4_elisa_launch_intent_smoke
./build-elisa/shadps4_elisa_launch_intent_smoke
```

When `SHADPS4_ENABLE_ELISA_PORTS=ON`, the main shadPS4 executable links the launch-intent archive
but still uses the original C++ launch policy by default. To dogfood safely, enable shadow mode:

```sh
SHADPS4_ELISA_SHADOW_LAUNCH_INTENT=1 ./build-elisa/shadps4 --game CUSA00264 --fullscreen true
```

Shadow mode parses the same `argv` through Elisa, compares the normalized launch intent against the
C++ path, and logs mismatches to stderr without changing runtime behavior. Keep this as the default
porting pattern until fixture coverage is strong enough to flip a specific decision from C++ to Elisa.

For active dogfooding, the executable also has an opt-in replacement path:

```sh
SHADPS4_ELISA_LAUNCH_INTENT=1 ./build-elisa/shadps4 --game CUSA00264 --fullscreen true
```

This asks Elisa to produce the real `CliState` for the supported small-argv slice. The C++ CLI11 parser
remains the default and is still used for no-argument help/message-box behavior, argv lists beyond the
tiny v1 C ABI limit, and invocations with more than one guest argument. That fallback is intentional:
the current ABI carries only the first guest argument, so we do not pretend it can safely replace full
argv-vector behavior yet.

A quick error-path smoke that should exit before runtime setup:

```sh
SHADPS4_ELISA_LAUNCH_INTENT=1 ./build-elisa/shadps4 --game CUSA00264 --fullscreen maybe
```

On macOS, `cmake/Elisa.cmake` infers `arm64-apple-macosx${CMAKE_OSX_DEPLOYMENT_TARGET}` or
`x86_64-apple-macosx${CMAKE_OSX_DEPLOYMENT_TARGET}` from `CMAKE_OSX_ARCHITECTURES`, so opt-in Elisa
archives match the C++ build architecture instead of accidentally reusing the compiler host arch.

The native C bridge deliberately does not decide profile policy for new Elisa callers. Elisa
constructs a `TraceProfile` with explicit booleans such as `null_fmask_reads`,
`fmask_decompress_in_place`, `compositor_null_layer`, and `videoout_unorm`; C only applies those
flags while spawning the child process. This keeps the unsafe boundary boring and inspectable.

Safe trace launches also set:

- `SHADPS4_GPU_WAIT_TIMEOUT_MS=2000`
- `SHADPS4_SKIP_IMGUI_TEXTURE_UPLOADS=1`
- `SHADPS4_MOLTENVK_SAFE_MODE=1`
- `SHADPS4_FORCE_FIFO_PRESENT=1`
- `SHADPS4_TRACE_GPU_COMMANDS=1`

The wait timeout keeps Vulkan waits waking up to emit diagnostics instead of sleeping forever. The
ImGui flag avoids the known runtime texture-upload path that can submit and wait for the queue while
scheduler submission is locked. The MoltenVK safe mode applies conservative macOS defaults before
Vulkan instance creation: asynchronous MoltenVK queue-submit processing, finite Metal compile
timeout, FIFO present mode, and a last-64 GPU command ring that is dumped on wait/present failures.

The parser also classifies metadata trace lines in Elisa. Current summaries include FMASK/CMASK/
HTILE read counts, null-vs-sample metadata actions, whether metadata reads hit the last render
metadata addresses, whether render targets reported aliased FMASK/CMASK addresses, image-binding
texture/storage/videoout-storage counts, last bound image metadata, last pipeline hashes, and a small
black-screen-adjacent suspicious score.

The strict black-screen watchdog is also interpreted in Elisa. C++ emits compact facts such as
`TRACE_BLACK_WATCHDOG`, luma stats, raw VideoOut backing-memory stats, and last-writer breadcrumbs;
Elisa turns those facts into a source split:

- `upstream-guest-black`: `GameOnly` and raw VideoOut memory are both black, pointing before the host
  compositor.
- `vulkan-source-black`: `GameOnly` is black while raw guest memory is nonblack, pointing at texture
  cache, detiling, upload, or layout handling.
- `host-compositor-black`: final frame stages are black without matching `GameOnly` evidence, pointing
  at FSR, post-processing, ImGui texture state, or swapchain composition.

This keeps black-screen triage policy in Elisa while the C++ renderer remains responsible only for
emitting facts and failing fast in strict diagnostic mode.

The FMASK classifier is intentionally Elisa-owned rather than C++ renderer-owned. The C++ trace reports
active metadata addresses, while still including raw CMASK/FM‌ASK register bases for audit. Elisa
currently marks missing FMASK metadata, distinct FMASK/CMASK metadata, aliased FMASK/CMASK metadata,
and the especially suspicious case where a single-sample render target still reports an active aliased
FMASK/CMASK address. That keeps this diagnosis inspectable and testable before we port any renderer
behavior.

Profile comparisons also print an explicit conclusion. If either run lacks render-target coverage in
the analyzed excerpt, the comparison is marked inconclusive instead of treating missing FMASK evidence
as proof that a profile fixed the issue.

`ufc_trace_fmask_risk(summary)` turns those parsed facts into an Elisa-owned investigation decision:
low/medium/high risk, a human-readable reason, whether long automated runs should be avoided, and
the next safest experiment profile to try. This keeps FMASK triage policy outside the live C++
renderer path while still making it testable against fixture logs.

Safety rule: do not use this harness for long black-screen repro loops yet. It is intentionally an observability and process-control slice, not a renderer/FMASK behavior change.
