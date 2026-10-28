# VVVVVV for the Analogue Pocket (openfpgaOS)

This directory builds VVVVVV as an [openfpgaOS](https://github.com/zenibako/openfpgaCore)
app for the Analogue Pocket. It is an altered version of the game: see the
[licence](../LICENSE.md). VVVVVV is copyright Terry Cavanagh, and this
port is based on the VVVVVV source code. The `openfpga/` directory names
the port; its hardware target remains the Analogue Pocket.

The port is in progress. The plan and its phases are tracked in
[zenibako/VVVVVV-pocket#1](https://github.com/zenibako/VVVVVV-pocket/issues/1).

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
| `assets/branding/` | Artwork provenance and the platform banner preview |
| `tests/` | Host checks for the compat headers, the save archive, the renderer and the sound player |

The game sources in `../desktop_version/src` build with the same C++98 flags as the desktop
build. The few changes they need are guarded by `VVV_POCKET`.

## Build

```sh
git submodule update --init       # physfs, tinyxml2, lodepng, c-hashmap, SheenBidi, FAudio
make -C openfpga deps
make -C openfpga    # build/app.elf
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

The package includes the VVVVVV logo as `Platforms/_images/vvvvvv.bin`, using
the Pocket's monochrome platform-banner format. Pocket Sync uses the original
Captain Viridian app artwork, exported at 40×40 pixels, as the plugin's
`plugin_icon.png` release asset so its compact plugin row stays small.
Regenerate both from the checked-in game artwork with
`python3 openfpga/scripts/make-branding.py` (Python 3 and Pillow, from the
repository root). See [artwork provenance](assets/branding/README.md).

`make test` runs the compat-header, save-archive and sound checks on the
host with AddressSanitizer and UBSan.

`make BENCH=1` builds the scripted benchmark; `make BENCH=1 package`
packages it as the separate `zenibako.vvvvvvbench` core. Use fresh object
and build directories when switching between the game and benchmark.
The benchmark always selects English before loading fonts and skips the
first-run language prompt, including with an empty or unreadable benchmark
save. Its save slot is separate from the game's, and benchmark writes stay
in memory. The startup check is included in `make test`.

The timed route covers Teleporter Divot (with and without the time-trial
HUD), Tower, map, climbing, the stabilizer-off final stretch, and the story Gravitron. Gravitron uses its
normal introduction and complete 60-second countdown, including the denser
late waves, with a fixed random seed and invincibility to prevent resets.
The introduction is excluded from timing. Results include average frame,
logic, drawing and text costs; press A to switch between results pages and
take a screenshot of each page.

The `final` segment runs Party Time! (42,52), immediately after the stabilizer
terminal, for 600 logic steps (20 seconds). It invokes the real terminal
transition in DIY V Repair, excludes its flash/shake prelude, then keeps
normal colour cycling and the changing obstacle shapes enabled. A fixed seed
and invincibility make the run reproducible; reduced-flashing and colourblind
settings are disabled for this segment so saved preferences cannot remove
the workload. The host smoke captures two room frames and verifies that
obstacle pixels change at the same map colour, plus multiple colours and all
four animation phases. It leaves Gravitron last, with its existing profiling.

Final-room foreground rebuilds use a shared renderer helper that writes a
complete grid of binary-alpha tiles in rows. Empty and transparent pixels
remain clear. Partial-alpha or modulated sheets and recoloured one-way tiles
retain individual drawing. Tile variants are selected once in their original
order, including when the batch falls back, so animation and RNG consumption
stay unchanged. Host checks compare foreground pixels and the next RNG value
across three rooms, six colours, four phases and reduced-flashing settings.

`final map: rebuild ... copy ... ms/frame` separates the foreground rebuild
from its per-frame copy; `final rebuilds` counts rebuilds during the segment.
`final redraw us: old ... new ...` forces 60 redraws on each path before
Gravitron starts, outside the timed route. This comparison uses a regular
animation colour so it cannot consume random variants and change later rooms.
The final foreground also enables its existing opaque-span cache on its first
copy after each rebuild. `final copy us: old ... new ...` compares the two
copy policies over 60 rebuild-and-retain pairs. Its per-copy average includes
building the spans, excludes foreground rebuilding, and uses the same batched
grid renderer for both policies. Host checks compare composed pixels on both
the first and retained copies as well as the foreground texture and RNG.

Keyed tile uploads reuse each sheet's 8-pixel transparency patterns to build
opaque spans directly. The texture grid also copies the sheet's cached RGB565
pixels, avoiding a second foreground scan/conversion; tower layers avoid a
separate mask scan. Both paths share span assembly and merge opaque runs across
tile boundaries. Source changes invalidate the sheet cache, and texture writes
invalidate the assembled runs. Unsupported sheets retain their existing drawing
fallbacks. Host renderer checks exercise every 8-bit transparency pattern,
source mutations, row-ring scrolling, snapshot detachment and composed pixels.
The legacy copy-policy comparison disables grid span assembly for both modes,
so it still measures first-copy versus second-copy run construction.

For shared platform profiles, evidence collection and comparison, see the
[benchmark integration guide](BENCHMARKING.md). It preserves these workloads and
keeps host timing, screenshot-backed Pocket results and hardware validation separate.

The benchmark forces 30 Hz rendering, independent of saved settings. This
keeps one presentation per logic timestep; faster render-only iterations
would otherwise be excluded from its per-frame averages.

For quick final-level device runs, build `make BENCH=1 FINAL_SWEEP=1` and
`make BENCH=1 FINAL_SWEEP=1 package`. This focused preset keeps six rooms with
the stabilizer off: The Villi People, Timeslip, Please enjoy these repeats,
The Last Straw, Try Jiggling the Antenna and Party Time! It also keeps Panic
Room and The Final Challenge before and after shutdown, continuously scrolling
from both entrances. There are 14 cases and 4,320 measured frames, after 12
warmup frames per case. Allow roughly 3–5 minutes on Pocket. Its independent
build/object directories are under `build/final-sweep`; packaging replaces
only the benchmark core.

For complete final-level regression coverage, add `FINAL_SWEEP_ALL=1` to both
build and package commands. This uses `build/final-sweep-all` and retains all
34 main final-level rooms before and after stabilizer shutdown. Each ordinary
room runs six palettes at the native 40-step cadence, with normal obstacle
animation. Both mini-towers run from both entrances at cameras 0, 192, 384 and
568, plus continuous native camera scrolling over their full height in both
directions. There are 104 cases and 25,920 measured frames. Allow roughly
15–25 minutes on Pocket; slow frames may make it longer.

The `Sweep v7` counter shows the current case, stabilizer state and frame
progress while running. Its overlay drawing cost is excluded from frame/draw
timings. Results are drawn directly onto the presented window; A advances
through the pages (one focused page, three exhaustive pages). The focused
report also splits the top three ranked rooms' worst frames into logic and
draw time, so average improvements can be distinguished from remaining spikes. Host checks
compare these presented pixels against the expected progress/report images
and exercise gamepad A paging.

The sweep pins only the player at a clear, trigger-free position; enemies,
platforms and gravity lines keep updating through the real main loop. It
checks that rooms, camera positions and game state stay valid, all animated
palettes/phases occur, and continuous cases actually scroll the background.
This is a controlled room workload, not an automated playthrough. Intermission
rooms sharing Finalclass.cpp and the main Tower's entrance corridors are not
part of the 34-room final level; the existing route still covers Gravitron.

Results rank each selected room/state by its slowest viewport's average frame
time, alongside its worst single frame across all views. Logic/draw timings
are included for the top three cases. The tower detail rows split each tower's
worst frame (across both states/entrances) into background-update (`B`) and
foreground (`F`) time. `C` marks a colour change, `R` a full background rebuild;
`P` marks a retained palette update, `S` cached background strips; `-` means that event did not occur. These costs are already inside the frame's
logic/draw totals: the fixed update renders the background, so its work appears
under logic. `+` means the stabilizer is off; `-` means on. Capture each result page.
Host coverage uses `make host-render HOST_BENCH_SMOKE=1 FINAL_SWEEP=1
SDL3_PREFIX=/opt/homebrew` (also add `FINAL_SWEEP_ALL=1` for the exhaustive
regression), then the usual private assets/basedir launch. Host sweep timers
use the real performance counter; the simulation clock is accelerated.
`SWEEP_VIEW` and `SWEEP_RESULT` CSV lines report host microseconds, not Pocket
timings. Run `python3 tests/final_sweep_roster_test.py` to check both presets.

Tower grid rebuilds resolve vertical wrapping once per source row, shared by
main-tower, mini-tower and background row access. The row lookup preserves the
original truncation of negative pixel coordinates before positive wrapping.
Opaque grids write directly to contiguous destination row spans, removing the
scratch-row copy; horizontally wrapped spans retain their existing fallback.
Renderer checks cover clipped/ring/padded destinations, and host gameplay
checks compare batched row values with the original per-tile accessors across
signed offsets and wrap boundaries.

Tower foreground colour changes upload only the new RGB565 colour data for
retained tile IDs, leaving transparency masks and opaque spans intact. Each
sheet revision is validated once: corresponding tiles must keep identical
binary alpha across all palettes. Camera scrolling still shifts the layer and
uploads exposed rows; geometry/room changes rebuild normally. Individual tile
writes, nonaligned layer shifts, changed/destroyed source textures, unsupported
palette families, modulation and allocation failures retain the full-upload
fallback. The 320x248 tower layer retains about 5 KiB of tile IDs. Host checks
compare all 900 shipped palette pairs and 120 colour/scroll/camera-jump frames
with redraws, including arbitrary RGB565 colour mappings.

Opaque grids using a small palette (up to six consecutive tile IDs) expand
repeated tile rows into cached RGB565 strips, merging adjacent identical tiles
into one copy. The tower background uses six tile types, so this reduces the
many short tile-row copies during colour rebuilds. The cache uses about 33 KiB
and expands only tiles that actually repeat; source identity, revision and
palette range invalidate it. Incoming map IDs are read every draw, so map edits
remain visible. Larger tile ranges and allocation failures retain individual
tile copies. Clipped edges, ring destinations, source changes and wider-palette
fallbacks are compared against SDL; background checks include changing colours.

Device checks during development used the same focused 14-case preset (240
frames per ordinary room, 360 per scrolling tower case, after warmup). These
are measured Pocket results; v3 already included earlier foreground work, so
this comparison covers the later incremental optimizations.

| Stabilizer off unless marked | v3 mean / peak (ms) | v7 mean / peak (ms) |
| --- | --- | --- |
| The Villi People | 35.1 / 51.3 | 28.4 / 38.4 |
| Party Time! | 32.7 / 49.0 | 25.8 / 36.4 |
| The Final Challenge | 31.8 / 56.0 | 29.5 / 38.1 |
| The Final Challenge, stabilizer on | 31.8 / 58.7 | 29.5 / 38.0 |
| Panic Room | 31.6 / 58.5 | 29.0 / 38.8 |
| Panic Room, stabilizer on | 32.0 / 58.5 | 29.5 / 46.0 |

All selected room/state averages in v7 are below the 34 ms frame budget.
Peak frames still exceed it, including a 46.0 ms Panic Room result. Background
cost on The Final Challenge's peak frame fell from 10.1 ms in v6 to 6.8 ms in
v7; the strip marker confirms that the new path ran on that frame. Timing
variation and this controlled fixture limit conclusions about a full playthrough.

The Gravitron split reports background update/drawing and enemy spawning,
movement (including physics and wall collisions), pair collisions,
animation/colour updates, and drawing (including warning indicators).
Each phase shows milliseconds and calls per presented frame; these times
are already included in the main frame totals. Animation counts both its
physics probes and colour pass. Wave-spawn calls include delay checks.
Invincibility keeps the run reproducible and skips harmful pixel collisions.

The detailed Gravitron draw breakdown partitions clear/setup, background,
map/foreground, all entities, HUD/overlays, gameplay copying, final window
copy/effects, presentation, and the post-present clear. Its sum plus
`other` reconciles with the existing draw total; `overlap` should be zero.
Existing text, enemy and background counters are nested diagnostics.
Framebuffer flip and audio servicing are shown separately inside presentation,
so their times must not be added to the flat draw sum. The flip queues a
frame; this is not a measurement of physical scanout latency. Deferred clears
are charged to whichever later drawing call actually writes their pixels.
The report now spans three pages; press A and capture every page.

The final window copy batches tightly packed, unscaled RGB565 rows into
one transfer. Flips, scrolling row storage, padded pitch and horizontal
clipping retain the existing row path. `window copy us: old ... new ...`
compares both paths on the device, including the deferred window clear.

The Pocket's story countdown reuses digit textures instead of caching each
changing time string. It preserves the font's spacing, glow and border order;
localized numerals and RTL text use the existing renderer. Results include
an old/new comparison in microseconds for the same changing clock values,
plus pixel comparisons for the optimized countdown.
For the default font's regular border, each digit uses a cached black
border layer followed by its foreground layer, reducing five copies to
two. All borders still precede all foregrounds. The font's clock masks are
checked for binary alpha when loaded; replacement fonts with partial alpha,
other fonts, full borders and dimmed styles keep the existing path.
`clock border us: old ... new ...` isolates this change from glyph caching.
All cached text also checks the actual glyph's alpha mask, including fallback
and replacement glyphs. Partial-alpha glyphs retain direct drawing so alpha
is not applied twice; binary glyphs in the same font can still be cached.
Text requiring bidi shaping also retains direct drawing, since shaping can
substitute a glyph with a different alpha mask.

Warp-background scrolling uses one shared renderer helper for both axes:
vertical scrolling changes the first stored row, and horizontal RGB565
scrolling changes the first stored column. Both retain the full scrolling
texture and fill only exposed pixels black. Packed copies and repeated-tile
repainting split spans at the physical row ends; readback preserves the
column origin. Other drawing operations normalize columns when needed,
and ARGB scrolling retains the existing move. Unsupported blend or colour mods
keep the original temporary-texture path. Results include `warp bg us:
old ... new ...`, which compares complete background updates on device.
Both warp directions also share a repeated-tile helper. It batches their
four-tile pattern into cached RGB565 row spans, preserving transparent
pixels and the complete original repaint band. Pixel changes and texture
destruction invalidate the cache. Partial alpha, colour modulation and
unsupported targets or blending keep the original tile draws.
`warp tiles us: old ... new ...` isolates batching with shared scrolling
enabled in both measurements.
`warp scroll us: old ... new ...` compares horizontal movement with column
offsets, with tile batching enabled in both measurements.

Tower full-background redraws batch the indexed 8x8 tile grid from the
existing opaque tile sheet into contiguous row copies. The scrolling
update path and colour/camera logic retain their existing behaviour.
`tower updates: full ... scroll ...` reports how often each path runs;
invincible camera corrections can request full redraws on consecutive
updates, as well as the normal colour transitions. `tower redraw us:
old ... new ...` compares both full-redraw implementations on device.

An opt-in host check runs the real scripted route with an accelerated test
clock and AddressSanitizer/UBSan. It checks the Gravitron room, active enemy
waves, countdown completion and report pages, and captures room/results BMPs:

```sh
make host HOST_BENCH_SMOKE=1 HOST_DIR="$PWD/build/host-bench-smoke" SDL3_PREFIX=/opt/homebrew
mkdir -p build/host-bench-smoke/captures build/host-bench-smoke/saves
cd build/host-bench-smoke/captures
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software \
  ASAN_OPTIONS=detect_leaks=0 ../vvvvvv -assets /absolute/path/to/data.zip \
  -basedir "$PWD/../saves/"
```

These host timings are synthetic and must not be used as Pocket performance
results. The host check uses desktop SDL rendering with the Pocket text cache;
physical Pocket timing remains separate.
Linux smoke builds require the shared SDL3 library so the harness's timing
and presentation overrides do not conflict with static SDL definitions.

To exercise partial-alpha replacement fonts, generate private test assets
with `python3 tests/font_alpha_assets.py /path/to/data.zip /tmp/font-alpha.zip`.
Run either smoke executable with `POCKET_TEST_FONT_ALPHA=1` and
`-assets /tmp/font-alpha.zip`. This checks repeated strings, countdown digits,
fallback and missing-glyph replacements against uncached pixels, plus binary
glyph caching in the same fonts, then exits before the scripted route.

For the same route and 3,764 countdown pixel comparisons using the Pocket's
actual software renderer, build `host-render` with the same
`HOST_BENCH_SMOKE=1`, `HOST_DIR` and `SDL3_PREFIX` arguments, then run
`../render/vvvvvv` from the captures directory. Both checks exercise all
seven Gravitron phases. They also compare host wall time for old/new clock
drawing using the real performance counter; that comparison is independent
of the accelerated game clock and is still not Pocket performance.

The host check verifies draw accounting, continued presentation at the end
of the countdown, all report pages, and unchanged counters on results screens.
Native flip/audio paths are covered by C timer checks and the device build;
the host presentation adapter does not emulate their hardware timing.

The Pocket-renderer host check also compares 960 full warp-background
textures against the original path (both axes, seven colours, 40 updates,
plus sustained scrolling past a complete column wrap, colour changes,
resets and direction switches),
then reports host wall time for background updates. Renderer tests cover
shared snapshots, pending clears, both pixel formats and extreme shifts.
Window-copy checks compare the packed transfer with the original rows.
Run `RENDER_TEST_PADDED=1 make render-test` as well to check framebuffer
padding and its sentinel pixels.
The Pocket-renderer check also compares 2,560 complete Tower backgrounds
across all 30 colours, both directions, every pixel offset, wrap boundaries,
camera jumps and transitions between full redraws and scrolling. Its Tower
and Gravitron captures allow whole-frame comparisons with earlier builds.

## Sound

The preparation tools and Pocket Sync plugin in [`tools/`](tools/README.md)
verify your owned `data.zip` and convert the soundtrack to
`vvvvvv-music.bin`. Music blobs can be inside `data.zip` or supplied beside
it; the optional `mmmmmm.vvv` soundtrack is supported too. See the tools
instructions for native preparation and the SD-card plugin workflow.

The device cannot decode Ogg Vorbis during gameplay. Conversion uses the
pinned [pocket-music](https://github.com/zenibako/pocket-music) implementation
and happens once on the computer. Install `data.zip` and
`vvvvvv-music.bin` in `Assets/vvvvvv/common/`. Without the converted pack,
the game runs without music.

`src/pocket_audio.c` streams the converted tracks (mono IMA ADPCM at
48 kHz, see `src/music_pack.h`) from the card, loops them at the points the
game reads from the Ogg comments, and mixes them with the sound effects,
which load from the WAV files in `data.zip`. It replaces only the two
FAudio classes in `../desktop_version/src/Music.cpp`; the game's own music logic (fades,
pausing, track changes, volume settings) is unchanged.

## Controls and saves

The Pocket's controller is reported to the game as one SDL gamepad. A
(right) is the gamepad's south button and B (bottom) its east; both flip
in gameplay, while menus use A to confirm and B to go back. X and Y map by position. The
defaults, which the core's controls page in the Pocket's menu also lists
(`dist/core/input.json`):

| Button | In game | In menus |
|---|---|---|
| D-pad | Move | Choose |
| A | Flip | Confirm |
| B | Flip | Back |
| X | Map; also talk, unless the interact button is set apart | |
| Y | Talk, when the interact button is set apart in the options | |
| R | Restart from the checkpoint | |
| Start | Pause menu | Back |
| Select | Map; also talk, unless the interact button is set apart | |

The game's own controller options can rebind A, B, X, Y, L, R, Start and
Select. Start and Select are given their defaults whenever no action has
them, which includes saves made before they had any. Saved A-to-flip,
B-to-pause defaults migrate to the new layout; custom gameplay bindings are retained.
Menu navigation always uses A to confirm and B or Start to go back.
The title uses a white `Press any button to continue` prompt instead of a tinted
button glyph. Face buttons, shoulders, Start, Select and the D-pad all open
the menu regardless of custom gameplay bindings.
The host smoke's `POCKET_TEST_CONTROLS=1` mode checks both face buttons,
held-button releases, saved/default/custom layouts and all 12 title buttons,
then saves `title-controls.bmp` with the title colour set to black.

Button prompts use Pocket glyphs in the style of Butterscotch Pocket's: the
face-button diamond with the pressed button marked, the row of three small
buttons below the screen, and the shoulder pair. They live in Pocket copies
of the game's button fonts in `fonts/`, at U+EB37 to U+EB3E, which the
packaged core ships in place of the originals. `make button-fonts`
regenerates them from `../desktop_version/fonts` with `scripts/make-button-fonts.py`
(Python 3 and Pillow).

openfpgaOS has no writable directory, so everything the game writes under
`saves/`, `levels/` and `screenshots/` is kept as a small archive in one
save file, `vvvvvv_0.sav` by default (`POCKET_SAVE_FILE`). The archive is
rewritten each time the game closes a file it wrote. The core's data.json
gives the save slot the same filename, so the Pocket keeps it at
`Saves/vvvvvv/common/vvvvvv_0.sav` even though the core starts without a
game being picked; without it the save landed loose in `Saves/` under a
broken name.

`make host SDL3_PREFIX=<prefix>` builds the game for your computer with the
same compat headers, on desktop SDL3 and FAudio. Use it to debug game logic
off the device: run `build/host/vvvvvv -assets <path to data.zip>`. Saves
go to your desktop VVVVVV folder unless you also pass `-basedir <folder>/`.
The host targets link the shared fonts and language directories beside
their executables, so resource discovery works from root `openfpga/`.
Pass `-translator` to enable the upstream developer translation tools;
executable-adjacent resources do not enable that menu automatically.

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
