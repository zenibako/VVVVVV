# VVVVVV for the Analogue Pocket (openfpgaOS)

This directory builds VVVVVV as an [openfpgaOS](https://github.com/zenibako/openfpgaCore)
app for the Analogue Pocket. It is an altered version of the game: see the
[licence](../../LICENSE.md). VVVVVV is copyright Terry Cavanagh, and this
port is based on the VVVVVV source code.

The port is in progress. The plan and its phases are tracked in
[zenibako/VVVVVV#1](https://github.com/zenibako/VVVVVV/issues/1).

No game data is included. You need your own `data.zip`.

## Layout

| Path | What |
|---|---|
| `Makefile` | Device build (`build/app.elf`), the host build and tests |
| `scripts/fetch-deps.sh` | Fetches the pinned openfpgaOS SDK and SDL3 headers into `.deps/` |
| `compat/cxx/` | The small part of the C++ standard library the game uses (`string`, `vector`, `map`, `ostringstream`). The SDK has no C++ library. |
| `sdl3/` | The SDL3 calls the game makes, implemented on openfpgaOS (input, files, the save archive) |
| `src/` | Pocket-only code (for now, an FAudio placeholder) |
| `tests/` | Host checks for the compat headers and the save archive |

The game sources in `../src` build with the same C++98 flags as the desktop
build. The few changes they need are guarded by `VVV_POCKET`.

## Build

```sh
git submodule update --init       # physfs, tinyxml2, lodepng, c-hashmap, SheenBidi, FAudio
make -C desktop_version/pocket deps
make -C desktop_version/pocket    # build/app.elf
```

By default `make` compiles in the SDK's `openfpgaos-firmware` container, as
the other openfpgaOS ports do. `USE_SDK_CONTAINER=0` uses a host
`riscv64-unknown-elf-gcc` instead. On Ubuntu 24.04 the container's packages
are `gcc-riscv64-unknown-elf` and `picolibc-riscv64-unknown-elf`.

`make test` runs the compat-header and save-archive checks on the host with
AddressSanitizer and UBSan.

## Controls and saves

The Pocket's controller is reported to the game as one SDL gamepad with
Nintendo button positions (A right, B bottom, X top, Y left), so the game
shows matching button prompts.

openfpgaOS has no writable directory, so everything the game writes under
`saves/`, `levels/` and `screenshots/` is kept as a small archive in one
save file, `vvvvvv.sav` by default (`POCKET_SAVE_FILE`). The archive is
rewritten each time the game closes a file it wrote.

`make host SDL3_PREFIX=<prefix>` builds the game for your computer with the
same compat headers, on desktop SDL3 and FAudio. Use it to debug game logic
off the device: run `build/host/vvvvvv -assets <path to data.zip>`. Saves
go to your desktop VVVVVV folder unless you also pass `-basedir <folder>/`.
