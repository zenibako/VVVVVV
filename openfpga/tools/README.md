# VVVVVV game-file preparation

These tools prepare your own VVVVVV installation for the Analogue Pocket.
They are part of [zenibako/VVVVVV-pocket](https://github.com/zenibako/VVVVVV-pocket).
No purchased game data or converted music is included in releases.

## Files to supply

Use the original files from your purchased Steam/itch installation:

- `data.zip`: graphics, sounds and other game assets; it stays a ZIP.
- `vvvvvvmusic.vvv`: the original PPPPPP soundtrack, if supplied separately.
- `mmmmmm.vvv`: optional MMMMMM soundtrack, if supplied separately.

Some releases, including the current Steam macOS installation, store music
blobs inside `data.zip`. The tool reads those directly; no manual extraction
is needed. Separately supplied soundtrack files take precedence.

For Steam, use **Properties → Installed Files → Browse**. Windows and Linux
normally put the files in the opened VVVVVV folder. On macOS, right-click
`VVVVVV.app`, choose **Show Package Contents**, then open `Contents/Resources`.
Other distributions can use different locations; look for the exact filenames
above in the installed game. The tools read extracted install files, not Steam
depots or store installer executables. They never download purchased assets.

## Pocket Sync plugin

1. Install the VVVVVV core release onto the SD card.
2. Create `Assets/vvvvvv/common/source/` on that card and copy your original
   files listed above into it, using their original names. `data.zip` alone
   is sufficient when it contains the original soundtrack.
3. In Pocket Sync, install the plugin from `https://github.com/zenibako/VVVVVV-pocket`.
   The repository's latest stable release must contain `plugin.wasm` and
   `plugin.json`, plus `plugin_icon.png` for its Captain Viridian icon; a source
   checkout, draft or prerelease is not sufficient.
4. Run **VVVVVV Game Data** and choose installation while keeping originals
   (the first/default choice). The plugin installs `data.zip` and
   `vvvvvv-music.bin` directly into `Assets/vvvvvv/common/`.

An explicit discard option removes only `common/source/` after successful
installation. The default keeps it. Installation errors preserve originals and
restore prior output files; cleanup failures are warnings. If restoration
itself fails, keep the reported hidden recovery directory for manual recovery.
Ordinary saves and the core's program/runtime files are never replaced by the
plugin. This rollback handles reported errors; it is not a power-loss-atomic
filesystem transaction.

The plugin can see the connected SD card, not arbitrary files on your computer.
Use its Help pages for source-file locations. No network hosts are allowed.

## Native preparation tool

Extract the bundle for your computer, then run:

```sh
prepare-data /path/to/original-files /path/to/new-output
```

The output directory must not already exist. Copy its `data.zip` and
`vvvvvv-music.bin` into `Assets/vvvvvv/common/`. `prepare-report.json` records
track counts, warnings and hashes; it is not required by the game. Original
files remain untouched. Keep the standard source folder flat: the three named
files should be immediately inside it, not nested in an app or installer ZIP.

Music uses the pinned MIT `pocket-music` converter: mono 48 kHz IMA ADPCM in
the OFM2 format, retaining soundtrack order, loop points and decoder state.
The optional MMMMMM tracks precede the PPPPPP tracks as the game expects.
Malformed or oversized input is rejected before outputs are published.
The Ogg preflight bounds continued packets to 1 MiB and 64 pages without a
completed packet before invoking the decoder.

## Development and verification

From this directory, use Go 1.25 and TinyGo 0.43.0 with Binaryen 133 on PATH.
These match the existing Pocket Sync plugins. If the installed Go is newer
than TinyGo supports, set `GOTOOLCHAIN=go1.25.0` for TinyGo builds.

```sh
make test
REQUIRE_PLUGIN_TESTS=1 go test ./integration -v
make all
```

`TINYGO` selects a compiler; `POCKET_PLUGIN_WASM` selects an existing plugin
for host tests. Mandatory integration tests execute actual Extism exports
with both card/host mount orders and compare native/plugin conversion bytes.
Tests use project-made synthetic assets and a project-made music tone.
No private installation files belong in tests, fixtures or source control.
To opt into a local owned-install parity check, set `VVVVVV_TEST_SOURCE` to
your installation folder when running the integration tests; nothing from
that installation is committed or included in release artifacts.

The standard-Go `prepare-data.wasm` is a WASI command-line tool, separate from
the TinyGo/Extism `plugin.wasm` used by Pocket Sync.

## Releases and catalog

Builds are prepared by `openfpga/release/assemble.py` and the explicit
openfpga release workflow. Every stable release must contain the complete core
ZIP, plugin assets, native bundles, WASI CLI, source/provenance/licenses and
checksums, including when only the tools change.

Catalog repository: `zenibako/VVVVVV-pocket`. ZIP asset filter:
`^vvvvvv-openfpga\.zip$`. The listing follows a tested stable release and a
separate maintainer submission; preparing these files does not itself publish
the core or add it to the catalog.

## Licenses and attribution

The preparation/plugin application code is MIT licensed (see `LICENSE`).
The plugin icon is the original checked-in VVVVVV app artwork, copied unchanged;
it retains the game's copyright and source license. See the
[artwork provenance](../assets/branding/README.md).
Pinned pocket-music and its patched decoders retain their original MIT
licenses and patch notes under `third_party/`. This tool is an altered companion
to VVVVVV, copyright Terry Cavanagh; the game's code and assets retain the
repository's [VVVVVV license](../../LICENSE.md) and exceptions. Preparing files
does not grant redistribution rights to purchased data or converted music.
Codex assisted with the preparation/plugin integration; inherited source and
third-party components retain their own attribution.
