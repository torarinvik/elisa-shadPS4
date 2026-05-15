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
