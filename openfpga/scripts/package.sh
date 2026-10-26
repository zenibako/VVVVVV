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
if [[ "${BENCH:-0}" == 1 ]]; then
    CORE_ID=zenibako.vvvvvvbench
    APP_NAME=vvvvvv-bench
fi

[[ -f "$ELF" ]] || { echo "Missing $ELF; run make first" >&2; exit 1; }
for file in os20.rbf_r loader.bin os.bin; do
    [[ -f "$RUNTIME_DIR/$file" ]] || { echo "Missing the Pocket runtime; run make deps" >&2; exit 1; }
done

rm -rf "$OUT"
CORE_DIR="$OUT/Cores/$CORE_ID"
COMMON_DIR="$OUT/Assets/$PLATFORM_ID/common"
INSTANCE_DIR="$OUT/Assets/$PLATFORM_ID/$CORE_ID"
mkdir -p "$CORE_DIR" "$COMMON_DIR" "$INSTANCE_DIR" "$OUT/Platforms"

cp "$OPENFPGA_ROOT"/dist/core/*.json "$CORE_DIR/"
cp "$RUNTIME_DIR/os20.rbf_r" "$RUNTIME_DIR/loader.bin" "$CORE_DIR/"
cp "$GAME_ROOT/../LICENSE.md" "$CORE_DIR/LICENSE.txt"
cp "$OPENFPGA_ROOT/dist/platforms/$PLATFORM_ID.json" "$OUT/Platforms/"
cp "$OPENFPGA_ROOT/dist/instance/vvvvvv.json" "$INSTANCE_DIR/$APP_NAME.json"
cp "$RUNTIME_DIR/os.bin" "$COMMON_DIR/"
sed "s/^ELF=vvvvvv\.elf$/ELF=$APP_NAME.elf/" "$OPENFPGA_ROOT/dist/vvvvvv.ini" > "$COMMON_DIR/$APP_NAME.ini"
cp "$ELF" "$COMMON_DIR/$APP_NAME.elf"

if [[ "$APP_NAME" != vvvvvv ]]; then
    # The core's slots and instance name the benchmark's instance json (the
    # Game slot, which the core launches straight into), ini, elf and save, and
    # the core is named for the Pocket's core list.
    python3 - "$CORE_DIR" "$INSTANCE_DIR/$APP_NAME.json" "$APP_NAME" <<'PY'
import json
import pathlib
import sys

core_dir, instance, app = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), sys.argv[3]
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
core["core"]["metadata"]["shortname"] = "vvvvvvbench"
core["core"]["metadata"]["description"] = "VVVVVV benchmark: a scripted run that times its frames and shows the results."
(core_dir / "core.json").write_text(json.dumps(core, indent=4) + "\n")
PY
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

echo "SD card tree ready: $OUT"
echo "Add your data.zip as $COMMON_DIR/data.zip"
