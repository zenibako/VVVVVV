#!/usr/bin/env bash
# Assemble the Pocket SD card tree in build/sd from build/app.elf, the
# pinned runtime and dist/.  Copy its contents to the root of the card,
# then add your own data.zip as Assets/vvvvvv/common/data.zip.
#
# With BENCH=1 the tree holds a separate "VVVVVV Benchmark" core instead
# (zenibako.vvvvvvbench, a build made with `make BENCH=1`). It sits beside
# the game's core and shares its platform, data.zip and music. Its own
# files, its instance json and save too, are named vvvvvv-bench* so no
# file of the game's has the same name, wherever a copy puts it.
set -euo pipefail

OPENFPGA_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GAME_ROOT="$(cd "$OPENFPGA_ROOT/../desktop_version" && pwd)"
RUNTIME_DIR="$OPENFPGA_ROOT/.deps/pocket-runtime"
ELF="${PACKAGE_ELF:-$OPENFPGA_ROOT/build/app.elf}"
OUT="${PACKAGE_OUT:-$OPENFPGA_ROOT/build/sd}"
CORE_ID=zenibako.vvvvvv
PLATFORM_ID=vvvvvv
APP_NAME=vvvvvv
verify_makeandplay_binary() {
    python3 - "$1" <<'PY'
from pathlib import Path
import sys
path = Path(sys.argv[1])
if not path.is_file() or b"MAKE AND PLAY EDITION" not in path.read_bytes():
    raise SystemExit("Bundled assets require a built Make and Play edition binary")
PY
}
if [[ "${MAKEANDPLAY:-0}" == 1 ]]; then
    [[ "${BENCH:-0}" != 1 ]] || { echo "Make and Play cannot use the campaign benchmark" >&2; exit 1; }
    CORE_ID=zenibako.vvvvvvmp
    PLATFORM_ID=vvvvvvmp
    APP_NAME=vvvvvv-mp
    ELF="${PACKAGE_ELF:-$OPENFPGA_ROOT/build/makeandplay/app.elf}"
    OUT="${PACKAGE_OUT:-$OPENFPGA_ROOT/build/makeandplay/sd}"
    MAKEANDPLAY_ASSETS="${MAKEANDPLAY_ASSETS:-$OPENFPGA_ROOT/.deps/makeandplay/prepared}"
    python3 "$OPENFPGA_ROOT/release/makeandplay.py" --verify "$MAKEANDPLAY_ASSETS"
    verify_makeandplay_binary "$ELF"
fi
if [[ "${BENCH:-0}" == 1 ]]; then
    CORE_ID=zenibako.vvvvvvbench
    APP_NAME=vvvvvv-bench
fi

[[ -f "$ELF" ]] || { echo "Missing $ELF; run make first" >&2; exit 1; }
for file in os20.rbf_r loader.bin os.bin; do
    [[ -f "$RUNTIME_DIR/$file" ]] || { echo "Missing the Pocket runtime; run make deps" >&2; exit 1; }
done

FINAL_OUT="$OUT"
if [[ "${MAKEANDPLAY:-0}" == 1 ]]; then
    mkdir -p "$(dirname "$FINAL_OUT")"
    OUT="$(mktemp -d "$FINAL_OUT.stage.XXXXXX")"
    trap 'rm -rf "$OUT"' EXIT
else
    rm -rf "$OUT"
fi
CORE_DIR="$OUT/Cores/$CORE_ID"
COMMON_DIR="$OUT/Assets/$PLATFORM_ID/common"
INSTANCE_DIR="$OUT/Assets/$PLATFORM_ID/$CORE_ID"
mkdir -p "$CORE_DIR" "$COMMON_DIR" "$INSTANCE_DIR" "$OUT/Platforms/_images"

cp "$OPENFPGA_ROOT"/dist/core/*.json "$CORE_DIR/"
cp "$RUNTIME_DIR/os20.rbf_r" "$RUNTIME_DIR/loader.bin" "$CORE_DIR/"
cp "$GAME_ROOT/../LICENSE.md" "$CORE_DIR/LICENSE.txt"
cp "$OPENFPGA_ROOT/dist/platforms/vvvvvv.json" "$OUT/Platforms/$PLATFORM_ID.json"
cp "$OPENFPGA_ROOT/dist/platforms/_images/vvvvvv.bin" "$OUT/Platforms/_images/$PLATFORM_ID.bin"
cp "$OPENFPGA_ROOT/dist/instance/vvvvvv.json" "$INSTANCE_DIR/$APP_NAME.json"
cp "$RUNTIME_DIR/os.bin" "$COMMON_DIR/"
sed "s/^ELF=vvvvvv\.elf$/ELF=$APP_NAME.elf/" "$OPENFPGA_ROOT/dist/vvvvvv.ini" > "$COMMON_DIR/$APP_NAME.ini"
cp "$ELF" "$COMMON_DIR/$APP_NAME.elf"

if [[ "$APP_NAME" != vvvvvv ]]; then
    # The core's slots and instance name the benchmark's instance json (the
    # Game slot, which the core launches straight into), ini, elf and save, and
    # the core is named for the Pocket's core list.
    python3 - "$CORE_DIR" "$INSTANCE_DIR/$APP_NAME.json" "$APP_NAME" "$PLATFORM_ID" "$OUT/Platforms/$PLATFORM_ID.json" <<'PY'
import json
import pathlib
import sys

core_dir, instance, app = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), sys.argv[3]
platform, platform_json = sys.argv[4], pathlib.Path(sys.argv[5])
renames = {"vvvvvv.ini": app + ".ini", "vvvvvv.elf": app + ".elf",
           "vvvvvv_0.sav": app + "_0.sav", "vvvvvv.json": app + ".json"}

def rename_slots(path, key):
    doc = json.loads(path.read_text())
    for slot in doc[key]["data_slots"]:
        slot["filename"] = renames.get(slot.get("filename"), slot.get("filename"))
    path.write_text(json.dumps(doc, indent=4) + "\n")

rename_slots(core_dir / "data.json", "data")
rename_slots(instance, "instance")
core = json.loads((core_dir / "core.json").read_text())
metadata = core["core"]["metadata"]
metadata["shortname"] = "vvvvvvmp" if app == "vvvvvv-mp" else "vvvvvvbench"
metadata["platform_ids"] = [platform]
metadata["description"] = ("VVVVVV: Make and Play, with bundled player levels."
                           if app == "vvvvvv-mp" else
                           "VVVVVV benchmark: a scripted run that times its frames and shows the results.")
(core_dir / "core.json").write_text(json.dumps(core, indent=4) + "\n")
if app == "vvvvvv-mp":
    info = json.loads(platform_json.read_text())
    info["platform"]["name"] = "VVVVVV: Make and Play"
    platform_json.write_text(json.dumps(info, indent=4) + "\n")
PY
fi

if [[ "${MAKEANDPLAY:-0}" == 1 ]]; then
    cp "$GAME_ROOT/../License exceptions.md" "$CORE_DIR/License-exceptions.txt"
    cat > "$CORE_DIR/MAKE-AND-PLAY.txt" <<'EOF'
VVVVVV: Make and Play for Analogue Pocket (altered port).
Based on VVVVVV, copyright Terry Cavanagh 2010-2020.
Compiled with MAKEANDPLAY; the original campaign is excluded.
Public Make and Play assets are included under the repository's license exception.
See LICENSE.txt, License-exceptions.txt and common/notices for attribution.
Official edition: https://thelettervsixtim.es/makeandplay/
EOF
    cp "$MAKEANDPLAY_ASSETS/data.zip" "$MAKEANDPLAY_ASSETS/vvvvvv-music.bin" \
        "$MAKEANDPLAY_ASSETS/makeandplay-report.json" "$COMMON_DIR/"
    cp -R "$MAKEANDPLAY_ASSETS/notices" "$COMMON_DIR/notices"
fi

# The fonts and lang folders in one zip, laid out as the Android build's
# repo.zip: the Pocket fonts replace the game's button fonts.  The Chinese,
# Japanese and Korean fonts are left out for now: as ARGB8888 textures they
# would take about 31 MB of the app's 54 MB, so those languages fall back
# to the 8x8 font until the renderer keeps alpha-only textures.
python3 - "$GAME_ROOT" "$OPENFPGA_ROOT/fonts" "$COMMON_DIR/vvvvvv-repo.zip" <<'PY'
import pathlib
import sys
import zipfile

game, pocket_fonts, out = (pathlib.Path(p) for p in sys.argv[1:])
entries = {}
LEFT_OUT = ("font_ja.", "font_ko.", "font_sc.", "font_tc.")
for path in sorted((game / "fonts").rglob("*")):
    if path.is_file() and not path.name.startswith(LEFT_OUT):
        entries["graphics/" + path.relative_to(game / "fonts").as_posix()] = path
for path in sorted(pocket_fonts.glob("*")):
    if path.is_file():
        entries["graphics/" + path.name] = path
for path in sorted((game / "lang").rglob("*")):
    if path.is_file():
        entries["lang/" + path.relative_to(game / "lang").as_posix()] = path
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as archive:
    for name in sorted(entries):
        info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        archive.writestr(info, entries[name].read_bytes())
PY

if [[ "${MAKEANDPLAY:-0}" == 1 ]]; then
    # Check the copied bytes too; changing a source after the first check must
    # not replace a previously valid SD package with unverified game assets.
    python3 "$OPENFPGA_ROOT/release/makeandplay.py" --verify "$COMMON_DIR"
    verify_makeandplay_binary "$COMMON_DIR/$APP_NAME.elf"
    rm -rf "$FINAL_OUT"
    mv "$OUT" "$FINAL_OUT"
    trap - EXIT
fi
echo "SD card tree ready: $FINAL_OUT"
if [[ "${MAKEANDPLAY:-0}" != 1 ]]; then
    echo "Add your data.zip as $COMMON_DIR/data.zip"
fi
