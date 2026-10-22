#!/usr/bin/env bash
# Assemble the Pocket SD card tree in build/sd from build/app.elf, the
# pinned runtime and dist/.  Copy its contents to the root of the card,
# then add your own data.zip as Assets/vvvvvv/common/data.zip.
set -euo pipefail

POCKET_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GAME_ROOT="$(cd "$POCKET_ROOT/.." && pwd)"
RUNTIME_DIR="$POCKET_ROOT/.deps/pocket-runtime"
ELF="$POCKET_ROOT/build/app.elf"
OUT="$POCKET_ROOT/build/sd"
CORE_ID=zenibako.vvvvvv
PLATFORM_ID=vvvvvv

[[ -f "$ELF" ]] || { echo "Missing $ELF; run make first" >&2; exit 1; }
for file in os20.rbf_r loader.bin os.bin; do
    [[ -f "$RUNTIME_DIR/$file" ]] || { echo "Missing the Pocket runtime; run make deps" >&2; exit 1; }
done

rm -rf "$OUT"
CORE_DIR="$OUT/Cores/$CORE_ID"
COMMON_DIR="$OUT/Assets/$PLATFORM_ID/common"
INSTANCE_DIR="$OUT/Assets/$PLATFORM_ID/$CORE_ID"
mkdir -p "$CORE_DIR" "$COMMON_DIR" "$INSTANCE_DIR" "$OUT/Platforms"

cp "$POCKET_ROOT"/dist/core/*.json "$CORE_DIR/"
cp "$RUNTIME_DIR/os20.rbf_r" "$RUNTIME_DIR/loader.bin" "$CORE_DIR/"
cp "$GAME_ROOT/../LICENSE.md" "$CORE_DIR/LICENSE.txt"
cp "$POCKET_ROOT/dist/platforms/$PLATFORM_ID.json" "$OUT/Platforms/"
cp "$POCKET_ROOT/dist/instance/vvvvvv.json" "$INSTANCE_DIR/"
cp "$RUNTIME_DIR/os.bin" "$POCKET_ROOT/dist/vvvvvv.ini" "$COMMON_DIR/"
cp "$ELF" "$COMMON_DIR/vvvvvv.elf"

# The fonts and lang folders in one zip, laid out as the Android build's
# repo.zip: the Pocket fonts replace the game's button fonts.  The Chinese,
# Japanese and Korean fonts are left out for now: as ARGB8888 textures they
# would take about 31 MB of the app's 54 MB, so those languages fall back
# to the 8x8 font until the renderer keeps alpha-only textures.
python3 - "$GAME_ROOT" "$POCKET_ROOT/fonts" "$COMMON_DIR/vvvvvv-repo.zip" <<'PY'
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
