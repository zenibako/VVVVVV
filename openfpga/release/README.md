# Release assembly

## Publication approval

The `publish` job uses the GitHub environment named `release`. Configure that
environment with required reviewers and administrator bypass disabled before
publishing. Merely naming an environment in YAML does not add protection rules.
The job waits after the candidate artifact is uploaded, so its reviewer can
download and inspect the exact candidate before approving publication. The
repository owner may approve a run they dispatched; self-review is allowed.
Candidate-only runs (`publish: false`) do not require this approval.

Build/test checkouts do not persist repository credentials. The separate
protected publication job retains the authenticated checkout needed to push
a new tag without force.

## Local assembly

`assemble.py` uses Python 3.9+ and its standard library. It assembles a local
candidate and never creates a tag, release, catalog submission or external text.
Build the normal game SD tree with `openfpga/scripts/package.sh` first. Build the
tools separately. Invoke from any directory:

```sh
python3 openfpga/release/assemble.py \
  --sd openfpga/build/sd \
  --tools openfpga/tools/build \
  --output /tmp/vvvvvv-candidate-v1.2.3 \
  --version v1.2.3 \
  --runtime-source /path/to/pinned/openfpgaCore
```

The output directory must not exist. The version must be a stable `vX.Y.Z`.
`--source-commit` defaults to the actual source repository HEAD, derived from the
script location. Both the metadata release date and archive source timestamps use
that commit's committer timestamp in UTC. ZIP timestamps, gzip timestamps, file
ordering, ownership and modes are normalized for byte reproducibility with the
same inputs/toolchain. Compression output may differ across Python/zlib versions.

The tools directory must contain these ten regular files:

- `prepare-data-darwin-amd64`, `prepare-data-darwin-arm64`
- `prepare-data-linux-amd64`, `prepare-data-linux-arm64`
- `prepare-data-windows-amd64.exe`, `prepare-data-windows-arm64.exe`
- `plugin.wasm`, `prepare-data.wasm`, `plugin.json`, `plugin_icon.png`

Both WASM files must have the WebAssembly v1 header. The manifest must be valid
JSON with `allowed_hosts: []`. This checks artifact identity and manifest scope;
it does not replace the native/WASI/Extism execution tests in the tools build.
Native files are packaged as supplied; run the target builds before assembly.

Only 17 explicit SD paths are read: seven JSON files, LICENSE.txt, os20.rbf_r and
loader.bin in `Cores/zenibako.vvvvvv/`; os.bin, vvvvvv.elf, vvvvvv.ini and
vvvvvv-repo.zip in `Assets/vvvvvv/common/`; the game instance JSON and platform
JSON, plus `Platforms/_images/vvvvvv.bin`. Symlinks in those paths are rejected.
The three runtime files must match `runtime.lock.json`. The ELF must identify
itself as a 32-bit little-endian RISC-V
executable. The core JSON version/date are updated in memory; IDs, framework and
other SD inputs are preserved. Generated info.txt and updaters.json acknowledge
the altered port, licensing, hardware limits and AI assistance.

Extras are never recursively copied from the SD or tools trees. Owned data,
converted music, source staging, saves, logs, benchmark files and local metadata
are excluded. Game resources already included in the source repository (the
fonts/language repo ZIP) remain in the game package.

The candidate contains the core ZIP, four loose plugin/WASI assets (including
`plugin_icon.png`), six native `.tar.gz` bundles, `GAME-DATA.md`,
`runtime.lock.json`, `PROVENANCE.json`,
`SOURCE-AVAILABILITY.json`, `RUNTIME-SOURCE.md`, `source.tar.gz`,
`licenses.tar.gz`, and `SHA256SUMS`. The core ZIP has the SD directories at its
root. Each native bundle has `prepare-data` (mode 0755) or `prepare-data.exe`,
README.md, the game source license/exceptions and gathered third-party notices.
`SHA256SUMS` covers every other release artifact and includes optional runtime
source. PROVENANCE.json hashes the original collected input bytes in
`inputs_sha256`; `packaged_metadata_sha256` separately hashes JSON after version/date
updates and generated metadata. Inputs are read once, so subsequent source-tree
changes cannot rewrite the recorded input identity. Verify it with `sha256sum -c SHA256SUMS` (Linux) or
`shasum -a 256 -c SHA256SUMS` (macOS).

## Make and Play candidate

The release workflow builds both editions and retains the complete tools,
source, runtime-source, notices and checksum inventory. Local assembly keeps its
standard-only behavior unless `--makeandplay-sd` is supplied. Build the native
converter and prepare the pinned public distribution before the isolated build:

```sh
make -C openfpga/tools cli
python3 openfpga/release/makeandplay.py \
  --converter openfpga/tools/build/prepare-data \
  --output openfpga/.deps/makeandplay/prepared
make -C openfpga MAKEANDPLAY=1
make -C openfpga MAKEANDPLAY=1 package \
  MAKEANDPLAY_ASSETS="$PWD/openfpga/.deps/makeandplay/prepared"
python3 openfpga/release/assemble.py \
  --sd openfpga/build/sd \
  --makeandplay-sd openfpga/build/makeandplay/sd \
  --tools openfpga/tools/build \
  --output /tmp/vvvvvv-candidate-with-makeandplay \
  --version v1.2.3 \
  --runtime-source /path/to/pinned/openfpgaCore
```

Build/package the standard tree and all ten tool files as described above too.
The public preparation directory must not already exist. The authoritative
`makeandplay.lock.json` pins the official Linux Make and Play 2.4.4 archive,
its root `data.zip`, the deterministic converted soundtrack, exactly 17 player
levels, and the selected original distribution notices. The sanitized report
contains public source identities and hashes, with no host paths. Cached public
inputs stay under ignored `.deps/`; they are not source files.

Supplying `--makeandplay-sd` prepares two ZIPs for the same core
`zenibako.vvvvvv` and platform `vvvvvv`. Shared files must be byte-identical;
conflicting overlays fail assembly. The first slot requests a JSON launch file
rather than fixing a default, so Pocket can offer both edition launch files.

- `vvvvvv-openfpga.zip`: the main catalog download, with both executables and
  launch files, but no purchased or public game assets. Its Make and Play
  instance declares the uniquely named free-edition required files.
- `vvvvvv-makeandplay-openfpga.zip`: the self-contained free edition, with only
  its MAKEANDPLAY executable/launch file and the verified public assets. It
  excludes the full-game executable, full-game launch file and original levels.
  Installing it alone offers Make and Play; installing it alongside the main
  package adds its assets to the same existing core.

This packaging boundary follows the Make and Play license exception: the
asset-bearing package compiles with MAKEANDPLAY and excludes original levels.
Original campaign room arrays are compiled into the full-game executable; that
executable must not enter the public asset package. The main package's full-game
filenames and save remain unchanged. Make and Play uses `vvvvvv-mp_0.sav`.

The public allowlist admits the locked data/music bytes under unique basenames
`vvvvvv-mp-data.zip` and `vvvvvv-mp-music.bin`, plus `makeandplay-report.json` and
original distribution notices within `Assets/vvvvvv/common/mp/`. The report
retains original source filenames and hashes. Verify installed assets with
`makeandplay.py --verify-packaged DIRECTORY`. The INI passes
`-assets mp/vvvvvv-mp-data.zip`; the instance maps slot 6 to
`mp/vvvvvv-mp-music.bin`. Device SDL initialization binds the player's canonical
music name to that selected slot. SDK and runtime pins remain unchanged.

These inputs are snapshotted before validation, and the same bytes are archived
and recorded in `PROVENANCE.json`. The MP ELF must be ELF32 RISC-V with upstream's
`MAKE AND PLAY EDITION` marker; shared runtime binaries must match the runtime
lock. Both package inventories, source/provenance, original license/exception,
public notices, and every accompanying artifact are covered by `SHA256SUMS`.
Complete tracked engine/port/tool sources remain in `source.tar.gz`; notices are
also retained in `licenses.tar.gz`. An ELF string alone cannot establish source;
matched build and real-engine smoke evidence are required too.

The existing catalog entry and `^vvvvvv-openfpga\.zip$` filter keep identifying
the main download. Pocket Sync expands the local instance definitions to show
required files, including the Make and Play level bundle (all 17 levels) and
converted music. The catalog API only exports core-level required slots; it
does not expand instance files or accept arbitrary download URLs/hashes. Install
the self-contained Make and Play ZIP to supply its files. Download/repair through
Sync's configured archive provider separately requires those unique files in
that provider's inventory; the catalog alone cannot add this capability. Never
supply purchased full-game data to that provider. No second core listing or
external submission is performed here.

Hardware file-picker selection, launch, controls/audio and save/reload remain
acceptance checks. Test both options with owned full-game data installed, and
Make and Play without it. Protected publication still requires review, explicit
release notes/version approval, matching source HEAD and the complete candidate;
local assembly publishes nothing.

The shared contract was reconciled against
[`pocket-dev-tools` release guidance at b5eafef](https://github.com/zenibako/pocket-dev-tools/blob/b5eafef17f9ebc3e61fef0bf4b825311dede32c0/docs/RELEASING.md).
This local validation does not claim a run of a shared-checkout validator.

## Source and publication limits

Source is archived from exact Git objects, including every initialized submodule
recursively at its recorded commit. Missing, dirty or mismatched submodules fail
assembly. Run `git submodule update --init --recursive` before building.
Untracked/ignored files are never included. The committed tools README supplies
GAME-DATA.md and bundle instructions; commit new tools and vendored dependencies
before assembling the final candidate. License, copying, notice and copyright
files from these tracked sources are collected with their original paths. Source
headers retain any inline notices in the complete source archive.

Dirty tracked source is rejected by default. `--allow-dirty` enables an explicitly
unpublishable local candidate: its source archive still contains only the
requested committed revision, and PROVENANCE.json records that built inputs may
not correspond to it. A historical `--source-commit` also prevents a candidate
from being marked publishable. Untracked private inputs do not make the tracked
source dirty and are always excluded.

`--runtime-source` must be a clean checkout of the exact runtime.lock.json commit
with all submodules initialized at their pins. The archive is built from its full
Git tree, so a sparse filesystem checkout is usable only if Git can retrieve every
required source object. The assembler adds `runtime-source.tar.gz`, copies the
runtime notices into licenses.tar.gz and records its inclusion. An API-only
filesystem copy without the complete Git objects is insufficient.

Without `--runtime-source`, the candidate includes the exact source URL/commit,
fetch/rebuild instructions and an explicit missing-source/notices disclosure. It
is marked unpublishable; a URL is not represented as a bundled source archive.
Including source verifies identity and preserves notices, but does not prove a
runtime rebuild produces the pinned binaries or grant rights to proprietary FPGA
IP/toolchains. Read the pinned runtime's README, Makefile, NOTICE, REUSE.toml and
LICENSES; follow its os20 build instructions. Inspect all licenses and build/test
evidence before publishing.

PROVENANCE.json's `publishable` field only summarizes clean-source/runtime-source
assembly prerequisites. Publication still requires review and the user's explicit
version/tag approval. This script cannot prove which source or compiler produced
supplied native/ELF/WASM binaries; CI must build and execute them from the recorded
revision before invoking assembly.

Run the synthetic release tests without private data:

```sh
python3 -m unittest discover -s openfpga/release -p 'test_*.py'
```
