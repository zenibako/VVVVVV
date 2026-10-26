#!/usr/bin/env python3
"""Export the repository's original artwork for Pocket and Pocket Sync.

Requires Pillow, like make-button-fonts.py. No game installation is needed.
"""
from pathlib import Path
import shutil

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
WIDTH, HEIGHT = 521, 165


def main():
    # The source GIF cycles colours; use its first (cyan) frame for a static logo.
    with Image.open(ROOT.parent / "logo.gif") as original:
        logo = original.convert("RGBA")
    logo.thumbnail((WIDTH - 20, HEIGHT - 20), Image.Resampling.BOX)
    canvas = Image.new("RGBA", (WIDTH, HEIGHT), (0, 0, 0, 255))
    canvas.alpha_composite(logo, ((WIDTH - logo.width) // 2, (HEIGHT - logo.height) // 2))
    banner = canvas.convert("L")
    preview = ROOT / "assets/branding/vvvvvv-platform.png"
    preview.parent.mkdir(parents=True, exist_ok=True)
    banner.save(preview)

    # APF platform images store the banner rotated 90 degrees counterclockwise.
    # Each little-endian 16-bit pixel is inverted luminance followed by zero.
    rotated = banner.transpose(Image.Transpose.ROTATE_90)
    encoded = bytearray(WIDTH * HEIGHT * 2)
    encoded[::2] = bytes(255 - value for value in rotated.tobytes())
    destination = ROOT / "dist/platforms/_images/vvvvvv.bin"
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(encoded)

    # Keep Captain Viridian exactly as shipped in the checked-in app artwork.
    shutil.copyfile(
        ROOT.parent / "desktop_version/AppIcon.xcassets/AppIcon.appiconset/AppIcon.png",
        ROOT / "tools/plugin_icon.png",
    )
    print(f"Exported {WIDTH}x{HEIGHT} platform banner and original Captain app icon.")


if __name__ == "__main__":
    main()
