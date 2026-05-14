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

The app runs the `baseline-safe` profile with an 8 second timeout and prints a render summary. The test target uses fixture logs and does not launch the emulator.

The native reader caps analysis input at the latest 2 MiB of a trace log, so runaway traces cannot make the Elisa analyzer chew through unbounded output. Normal short safe runs are currently small enough to analyze whole.

The harness intentionally keeps the effect grants local. For example, `main.elisa` grants process/env/filesystem authority only around `ufc_trace_run_baseline_safe`, and grants `Console.Write` only around summary printing. Pure Elisa parsing code does not need those permissions.

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

The parser also classifies metadata trace lines in Elisa. Current summaries include FMASK/CMASK/
HTILE read counts, null-vs-sample metadata actions, whether metadata reads hit the last render
metadata addresses, and whether render targets reported aliased FMASK/CMASK addresses.

Safety rule: do not use this harness for long black-screen repro loops yet. It is intentionally an observability and process-control slice, not a renderer/FMASK behavior change.
