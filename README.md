<!--
SPDX-FileCopyrightText: 2026 shadPS4 Emulator Project
SPDX-FileCopyrightText: 2026 Elisa-core port maintainers
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Elisa-core PS4 Emulator Port

> [!IMPORTANT]
> This repository is an independent, incremental port and rework of portions of
> the [shadPS4](https://github.com/shadps4-emu/shadPS4) codebase toward
> Elisa-core. It is not the official shadPS4 repository, is not maintained by the
> shadPS4 team, and is not endorsed by the shadPS4 project.

This project currently contains transitional C++ code derived from shadPS4 while
selected emulator systems are moved into Elisa-core. During that work, file names,
build targets, executable names, configuration paths, and documentation may still
refer to `shadPS4` or `shadps4`. Those references describe the inherited or
not-yet-renamed code, not an official upstream release.

Please do not report issues from this repository to the upstream shadPS4 project
unless the same issue is reproducible on an unmodified upstream checkout.

## Project Status

This repository is a work in progress. The goal is an incremental port of the
emulator core into Elisa-core, so the codebase may temporarily contain upstream
C++ structure, compatibility wrappers, incomplete integrations, and behavior that
differs from both Elisa-core and upstream shadPS4.

If you want the official shadPS4 project, use:

- Upstream repository: [shadps4-emu/shadPS4](https://github.com/shadps4-emu/shadPS4)
- Upstream website: [shadps4.net](https://shadps4.net/)

## Upstream Attribution

This work is derived from and incrementally ports code from
[shadPS4](https://github.com/shadps4-emu/shadPS4), which is licensed under
GPL-2.0-or-later. Original shadPS4 copyright notices, license terms, and source
attribution are preserved.

The original shadPS4 authors are not responsible for changes made in this
repository. Elisa-core port maintainers are responsible for modifications,
integration choices, support, and release artifacts produced from this repository.

See [NOTICE.md](NOTICE.md) for the fuller attribution and non-affiliation notice.

## Building

Build instructions are inherited from the upstream shadPS4 layout and are being
updated as the Elisa-core port evolves:

- [Docker build instructions](documents/building-docker.md)
- [Windows build instructions](documents/building-windows.md)
- [Linux build instructions](documents/building-linux.md)
- [macOS build instructions](documents/building-macos.md)

Some commands still produce an executable named `shadps4` or `shadPS4.exe`.
That name is transitional and does not mean the artifact is an official shadPS4
build.

## Usage Examples

The inherited command-line interface can be inspected with `--help`.

Common command patterns may still use the transitional `shadPS4` executable name:

```sh
shadPS4 CUSA00001
shadPS4 --fullscreen true --config-clean CUSA00001
shadPS4 -g CUSA00001 --fullscreen true --config-clean
shadPS4 /path/to/game.elf
shadPS4 CUSA00001 -- -flag1 -flag2
```

## Debugging

For local development and troubleshooting, see
[Debugging and reporting issues](documents/Debugging/Debugging.md).

Issue reports for this port should go to this repository's maintainers. Upstream
shadPS4 support channels should only be used for issues reproduced on unmodified
upstream shadPS4.

## Firmware Files

The inherited emulator core can load some PlayStation 4 firmware files. Supported
modules must be placed in the `sys_modules` folder expected by the current build.

| Modules                  | Modules                  | Modules                  | Modules                  |
|--------------------------|--------------------------|--------------------------|--------------------------|
| libSceAudiodec.sprx      | libSceCesCs.sprx         | libSceFont.sprx          | libSceFontFt.sprx        |
| libSceFreeTypeOt.sprx    | libSceJpegDec.sprx       | libSceJpegEnc.sprx       | libSceJson.sprx          |
| libSceJson2.sprx         | libSceLibcInternal.sprx  | libSceNgs2.sprx          | libScePngEnc.sprx        |
| libSceRtc.sprx           | libSceSystemGesture.sprx | libSceUlt.sprx           |                          |

> [!CAUTION]
> Firmware files must be dumped from your legally owned PlayStation 4 console.
> They are not provided by this repository.

## Contributing

This repository is focused on the Elisa-core port. Please read
[CONTRIBUTING.md](CONTRIBUTING.md) before sending patches, and make sure changes
are clearly scoped to this port rather than upstream shadPS4 unless intentionally
syncing upstream code.

When contributing code derived from upstream shadPS4, preserve license headers,
copyright notices, and attribution.

## License

This repository is distributed under the
[GPL-2.0-or-later license](LICENSE), consistent with upstream shadPS4.
