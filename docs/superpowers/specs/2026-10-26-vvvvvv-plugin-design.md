# VVVVVV preparation and Pocket Sync plugin

Approved in chat on 2026-10-26: keep the game, openfpgaOS port, preparation
tools, plugin and release automation in zenibako/VVVVVV-pocket. Tools live under
openfpga/tools; the game engine remains under desktop_version.

## Existing decisions

- The [port plan](https://github.com/zenibako/VVVVVV-pocket/issues/1) requires owned
  data.zip, offline music conversion, immutable runtime pins and source notices.
- The [Butterscotch install architecture](https://github.com/zenibako/butterscotch-pocket/blob/main/docs/superpowers/specs/2026-10-18-install-entrypoint-design.md)
  supplies the native/shared-library/plugin/release pattern. Its repository
  split is not needed here; both installers accept this source repository.
- openfpga/src/music_pack.h defines OFM2, mono 48 kHz IMA ADPCM, track order
  and loop predictor/index state. Pin pocket-music 9cc67a266e2b9faea62ea89a3216d2dc3ee3134d
  and preserve its patched MIT decoder dependencies and notices.
- Pocket Sync mounts the SD card at /pocket. Plugins cannot select arbitrary
  computer files. The latest stable release must include plugin.wasm and
  plugin.json alongside the core ZIP.

## User flow and contracts

Stage owned data.zip, plus external vvvvvvmusic.vvv/mmmmmm.vvv when supplied separately, in
Assets/vvvvvv/common/source/. The plugin explains where those files live
on Windows, macOS and Linux. It offers install/keep first, install/discard
second, help and exit. Keep is the default. It verifies data.zip and copies
it unchanged; music extraction/conversion produces vvvvvv-music.bin. Embedded
root music blobs in data.zip are supported; external blobs take precedence.
Ruling: handle the observed Steam macOS embedded-blob layout so installing
the owned game does not require a separate manual extraction step.

A shared Go prep package serves a native/WASI CLI and the TinyGo/Extism
plugin. CLI preparation refuses existing output directories. Plugin writes
are staged in a hidden common-folder directory, old outputs are backed up,
and failed replacement restores previous outputs. Originals and saves are
preserved on failure; discard applies only to the dedicated source folder
after successful verification. Symlinks, unsafe archive paths and oversized
inputs are rejected. This is rollback on reported errors, not power-loss atomicity.

Release assembly uses an explicit SD file allowlist and pinned runtime hashes.
It excludes purchased inputs, converted music, saves, benchmarks and local
metadata. Each release includes vvvvvv-openfpga.zip, plugin.wasm,
plugin.json, native preparation tools, checksums and source/provenance notices.
Catalog repository: zenibako/VVVVVV-pocket; ZIP filter: ^vvvvvv-openfpga\.zip$.
Publishing requires an explicit version/tag after reviewing the candidate.

## Verification

Test synthetic ZIP/music fixtures, missing/truncated/unsafe inputs, native
conversion parity with the pinned library, rollback/source/save preservation,
choice validation and cancellation. Build actual WASM and execute its exports
in Extism with both host/card mount orders; compare native/plugin bytes.
Check standard-Go WASI CLI separately. Exercise release allowlists, runtime
hash rejection and complete artifact checksums. Owned data stays ignored.
