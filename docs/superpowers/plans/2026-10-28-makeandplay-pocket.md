# Make and Play Pocket Implementation Plan

> **For agentic workers:** Use the subagent-driven-development workflow to implement the independent tasks, then review and verify the integrated edition.

**Goal:** Add the official Make and Play player edition to the existing VVVVVV Pocket core, selected through Pocket's native launch-file picker.

**Architecture:** Keep core `zenibako.vvvvvv`, platform `vvvvvv` and the existing catalog entry. Select the full game or upstream's `MAKEANDPLAY` build using separate instance files, binaries, build directories, data paths and save names. Reuse the preparation tool for the official public distribution; bundle only its verified free assets. Purchased full-game data remains excluded.

**Revision:** The user requested one core and catalog Required Files integration after reviewing the separate-core implementation. The native picker below supersedes that layout. Keep two package boundaries within the same core: the main catalog ZIP carries both launch binaries/instances without game assets; the self-contained MP ZIP carries only its MAKEANDPLAY binary/instance and verified free assets. This keeps original levels in the full-game ELF outside the asset-bearing MP package. Pocket Sync derives required assets from instances; catalog YAML cannot attach arbitrary download URLs.

**Tech stack:** Existing C++98 engine, C Pocket adapter, Make/bash packaging, Python standard-library release tooling, Go preparation tool, pinned openfpgaOS runtime.

**Spec:** [Issue #20](https://github.com/zenibako/VVVVVV-pocket/issues/20), approved by the user on 2026-10-28. Reconciles the [existing preparation architecture](../specs/2026-10-26-vvvvvv-plugin-design.md), [local release guide](../../../openfpga/release/README.md), and [shared release contract](https://github.com/zenibako/pocket-dev-tools/blob/b5eafef17f9ebc3e61fef0bf4b825311dede32c0/docs/RELEASING.md).

## Global constraints

- Preserve SDK/API/runtime/loader/bitstream pins and the standard core's behavior.
- Keep core ID `zenibako.vvvvvv` and platform ID `vvvvvv`. Launch instances `vvvvvv.json` and `vvvvvv-mp.json` select their own application/data/save. Make and Play application: `vvvvvv-mp`; physical save: `vvvvvv-mp_0.sav`. Remove the fixed slot-0 filename and filter for JSON to expose Pocket's picker.
- Build with `MAKEANDPLAY=1` under `openfpga/build/makeandplay/`; do not reuse standard edition objects. Reject combining this edition with campaign benchmark modes.
- Keep purchased full-game data at `Assets/vvvvvv/common/data.zip` and its converted music beside it. Put verified public Make and Play data/music/report/notices in `Assets/vvvvvv/common/mp/`, renaming data/music to unique `vvvvvv-mp-data.zip` and `vvvvvv-mp-music.bin` basenames to avoid archive-provider collisions. Share runtime and fonts/languages, allowing only byte-identical duplicate package files.
- Use only the official 2.4.4 Linux distribution: `https://thelettervsixtim.es/makeandplay/vvvvvv-mp-linux-04212026.zip`, 65,564,426 bytes, SHA-256 `a6b704a8e5e5a71a04632eeadbd3c6d4e24b97f3872bb7b0366230d9d06b8ca1`.
- Its root `data.zip` is 61,404,595 bytes, SHA-256 `6fae3cdec06062d05827d4181c438153f3ea3900437a44db73bcd29799fe57e0`; it contains 17 player levels and embedded primary music. No purchased installation is used for this edition.
- Preserve the game's license, Make and Play exception, source attribution, third-party notices and source availability. Compile out original campaign content using the upstream define.
- Retain the existing disabled editor on Pocket. Playing bundled levels needs no new editor or arbitrary custom-asset mounting feature.
- Source/cache/generated assets stay ignored. The checked-in lock and sanitized provenance identify the public source and deterministic converted bytes.
- Release output: `vvvvvv-openfpga.zip` with both launch instances/binaries but no game assets, and `vvvvvv-makeandplay-openfpga.zip` with only MP launch files and verified free assets under the same core/platform. Preserve complete plugin/native/source/license artifacts and the existing catalog filter. Instance data slots expose the bundled public data/music in Pocket Sync Required Files. External archive-provider download support is separate from the catalog and is not claimed.
- No stable publication, tag, catalog submission or SD write is authorized by implementation. Hardware boot/play/save remains an explicit acceptance step after host/CI verification.

## Task 1: Isolated engine build and SD packaging

**Files:** `openfpga/Makefile`, `openfpga/scripts/package.sh`, `openfpga/sdl3/savefs.c`, new `openfpga/tests/makeandplay_package_test.py`.

**Consumes:** Prepared public assets directory containing `data.zip`, `vvvvvv-music.bin`, and `makeandplay-report.json`, validated through `python3 openfpga/release/makeandplay.py --verify DIRECTORY`.

**Produces:** `make -C openfpga MAKEANDPLAY=1` builds the existing engine edition; `make -C openfpga MAKEANDPLAY=1 package MAKEANDPLAY_ASSETS=DIRECTORY` emits its isolated SD tree. Export edition-specific JSON and a copied original platform banner without altering the standard templates.

- [ ] Add meaningful packaging/build contract tests before implementation: edition flag and object isolation, metadata/slot/save paths, coexistence with standard core, rejected benchmark combinations and missing/unverified public assets.
- [ ] Run the tests and verify failure on the current implementation.
- [ ] Select the upstream define in device and host builds; isolate object directories and save name.
- [ ] Extend packaging to the shared core/platform and separate instances and public assets, retaining fonts/languages from the existing source package.
- [ ] Verify standard packaging still excludes purchased inputs and Make and Play files.

## Task 2: Locked public assets and deterministic preparation

**Files:** New `openfpga/release/makeandplay.py`, `makeandplay.lock.json`, and `test_makeandplay.py`.

**Interfaces:** `load_lock(path=None) -> dict`; `validate_prepared(directory, lock=None) -> dict` verifies files plus sanitized `makeandplay-report.json`; CLI `--converter PATH --output DIRECTORY [--archive PATH]` downloads or accepts the exact pinned public ZIP and calls existing `prepare-data SOURCE OUTPUT`. `--verify DIRECTORY` only validates existing prepared outputs.

- [ ] Use synthetic fixtures to test wrong archive/data hashes, unexpected levels, changed converted music, output collision and sanitized provenance before implementing.
- [ ] Fetch only the pinned public input, bound download/extraction sizes, and extract only the verified root data ZIP. Retain the distribution's relevant notices.
- [ ] Reuse the native converter; record and pin the deterministic converted-music digest, exact 17-level inventory, public URL and input/output hashes. Do not retain host paths in the distribution report.
- [ ] Prepare the real public files and verify the lock and output bytes. Keep downloaded assets outside tracked source.

## Task 3: Combined release inventory and CI

**Files:** `openfpga/release/assemble.py`, `test_assemble.py`, `.github/workflows/openfpga-release.yml`, `.github/workflows/openfpga-tools.yml`, release docs.

**Consumes:** Task 1 Make and Play SD tree plus Task 2 verified public report. Shared helper `validate_prepared` establishes the explicit exception for public Make and Play data/music.

**Produces:** Optional assembler CLI `--makeandplay-sd DIRECTORY` that validates both trees for two package ZIPs under one core, rejecting differing shared-file collisions and full-game content in the free package, with provenance/checksum entries and complete distribution CI.

- [ ] Add failing synthetic tests for independent asset paths, standard-data exclusion, mismatched public inputs/report/music, wrong edition binary, absent campaign-exclusion marker, and checksums for both ZIPs.
- [ ] Add an exact Make and Play allowlist with the existing matched runtime checks; require the compiled `MAKE AND PLAY EDITION` marker and validated public assets before admitting data/music.
- [ ] Include Make and Play provenance and license exception in its package; preserve the complete standard release inventory.
- [ ] Extend CI to build/prepare/package both editions and assemble the complete candidate. Keep protected publication, exact-source checks and latest-release completeness.
- [ ] Run release/asset tests and retain the normal standard-only assembler behavior for compatibility.

## Task 4: Real engine playback and integration acceptance

**Files:** New `openfpga/tests/makeandplay_smoke.cpp`; root integration owns Makefile hook and Pocket/user documentation.

- [ ] Run the actual host Make and Play engine on the pinned public assets, with an isolated basedir and controlled clock/event driver.
- [ ] Verify title/edition label, player-level menu, all 17 discovered levels, controller selection and active custom-level gameplay; verify campaign/editor entry are unavailable on Pocket.
- [ ] Check custom-level progress/settings persistence in an isolated namespace without touching the standard save. Capture the actual menu/gameplay output.
- [ ] Build the device ELF and check edition marker; inspect the actual candidate ZIPs and provenance against the full source commit.
- [ ] Run relevant host renderer/save/audio and preparation/release tests. Record host versus physical device evidence separately.
- [ ] Complete a whole-change review, signed commits and review PR. Keep issue #20 open until the clean-card physical boot/play/save criterion is verified.
