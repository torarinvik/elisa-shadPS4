# Elisa shadPS4 interop smoke

This is the smallest possible Elisa-to-C bridge for dogfooding shadPS4 interop.

The first slice intentionally does not call emulator internals. It proves that an Elisa native target can:

- link a C translation unit from the shadPS4 tree
- call exported C ABI functions
- run a native Elisa executable
- run an Elisa test against the same C ABI

Run from the Elisa-core `compiler` directory:

```sh
go run ./src test tests --project ../shadPS4/elisa
go run ./src run app --project ../shadPS4/elisa
```

Expected app output:

```text
shadPS4 C API probe reached from Elisa
```

Next useful slices:

1. Add a tiny `extern "C"` C++ shim that calls a side-effect-free shadPS4 version/config function.
2. Add a C API function that invokes the existing shadPS4 CLI help path and returns its exit code.
3. Wrap the C API in Elisa with `can Sys.FFI:` once granular system effects exist.
4. Start exposing stable handles instead of raw pointers before touching emulator state.
