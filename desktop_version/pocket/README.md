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
| `scripts/fetch-deps.sh` | Fetches the pinned openfpgaOS SDK, runtime and SDL3 headers into `.deps/` |
| `dist/`, `scripts/package.sh` | The Pocket core definition and the SD card packaging |
| `compat/cxx/` | The small part of the C++ standard library the game uses (`string`, `vector`, `map`, `ostringstream`). The SDK has no C++ library. |
| `sdl3/` | The SDL3 calls the game makes, implemented on openfpgaOS (input, files, the save archive, surfaces and the software renderer) |
| `host/` | Shows the port's renderer in a desktop window for `make host-render` |
| `src/` | Pocket-only code: the sound player that stands in for FAudio |
| `fonts/` | The game's button fonts with Pocket glyphs added (generated) |
| `tests/` | Host checks for the compat headers, the save archive, the renderer and the sound player |

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

`make package` assembles the SD card tree in `build/sd`: the core
(`dist/`), the pinned openfpgaOS runtime, the app, and the game's fonts and
lang folders zipped as `vvvvvv-repo.zip`, laid out like the Android build's
`repo.zip` because openfpgaOS has no directories. Copy it to the root of
the card and add your `data.zip` as `Assets/vvvvvv/common/data.zip`. For
now the Chinese, Japanese and Korean fonts are left out to save memory.

`make test` runs the compat-header, save-archive and sound checks on the
host with AddressSanitizer and UBSan.

## Sound

The Pocket can't decode the game's Ogg Vorbis music while it runs, so the
music is converted once, on a computer, with
[pocket-music](https://github.com/zenibako/pocket-music):

```sh
pocket-music vvvvvv vvvvvvmusic.vvv vvvvvv-music.bin
```

`vvvvvvmusic.vvv` is next to `data.zip` in the game's install folder; add
`-mmmmmm mmmmmm.vvv` for the MMMMMM soundtrack too. Copy
`vvvvvv-music.bin` to `Assets/vvvvvv/common/`. Without it the game runs
without music.

`src/pocket_audio.c` streams the converted tracks (mono IMA ADPCM at
48 kHz, see `src/music_pack.h`) from the card, loops them at the points the
game reads from the Ogg comments, and mixes them with the sound effects,
which load from the WAV files in `data.zip`. It replaces only the two
FAudio classes in `../src/Music.cpp`; the game's own music logic (fades,
pausing, track changes, volume settings) is unchanged.

## Controls and saves

The Pocket's controller is reported to the game as one SDL gamepad, with
the face buttons mapped by position (A right, B bottom, X top, Y left).

Button prompts use Pocket glyphs in the style of Butterscotch Pocket's: the
face-button diamond with the pressed button marked, the row of three small
buttons below the screen, and the shoulder pair. They live in Pocket copies
of the game's button fonts in `fonts/`, at U+EB37 to U+EB3E, which the
packaged core ships in place of the originals. `make button-fonts`
regenerates them from `../fonts` with `scripts/make-button-fonts.py`
(Python 3 and Pillow).

openfpgaOS has no writable directory, so everything the game writes under
`saves/`, `levels/` and `screenshots/` is kept as a small archive in one
save file, `vvvvvv_0.sav` by default (`POCKET_SAVE_FILE`). The archive is
rewritten each time the game closes a file it wrote.

`make host SDL3_PREFIX=<prefix>` builds the game for your computer with the
same compat headers, on desktop SDL3 and FAudio. Use it to debug game logic
off the device: run `build/host/vvvvvv -assets <path to data.zip>`. Saves
go to your desktop VVVVVV folder unless you also pass `-basedir <folder>/`.

## Drawing

openfpgaOS has no GPU path for the game, so `sdl3/render.c` is a small
software renderer for the SDL3 calls the game makes: textures and render
targets, colour and alpha mods, the SDL blend modes, flips, nearest
scaling, rectangles, lines and points. Everything is drawn in ARGB8888 and
each presented frame is converted to the framebuffer's RGB565
(`sdl3/video.c`). `sdl3/surface.c` covers the surfaces the game uses for
image loading, collision masks and the bad signal filter.

The game redraws the whole screen several times a frame, so the renderer
holds clears back until it needs them, copies opaque layers as plain rows
and skips the transparent runs of layers that are drawn again unchanged.
Build with `make SHOW_FPS=1` (after `rm -rf build/obj`) to draw, down the
top right corner, the frame rate and the milliseconds per frame spent in
the renderer, presenting, and everywhere else.

Two host builds check it against desktop SDL3 (`SDL3_PREFIX`):

- `make render-test` draws the same steps with SDL3's own software
  renderer and with the port's, and compares the pixels. Set
  `RENDER_TEST_OUT=<folder>` to also write each step as a PPM, SDL on the
  left and the port on the right.
- `make host-render` is the host build with the game drawn by the port's
  renderer, shown in a window by `host/present.c`. Run it like `make host`
  to compare whole frames.
