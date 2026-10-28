#!/usr/bin/env python3
"""Export the repository's original artwork for Pocket and Pocket Sync.

Requires Pillow, like make-button-fonts.py. No game installation is needed.
"""
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
WIDTH, HEIGHT = 521, 165
PLUGIN_ICON_SIZE = (40, 40)


def main():
    """Export the original logo in APF format and a compact Pocket Sync icon."""
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
    # Match the official core-template: brightness first, then a zero byte.
    # The byte pair represents the documented 0xVV00 pixel in big-endian order.
    rotated = banner.transpose(Image.Transpose.ROTATE_90)
    encoded = bytearray(WIDTH * HEIGHT * 2)
    encoded[::2] = bytes(255 - value for value in rotated.tobytes())
    destination = ROOT / "dist/platforms/_images/vvvvvv.bin"
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(encoded)

    # Pocket Sync's compact plugin row uses the PNG's intrinsic dimensions.
    # Resize the original artwork without changing the desktop app icon.
    with Image.open(ROOT.parent / "desktop_version/AppIcon.xcassets/AppIcon.appiconset/AppIcon.png") as icon:
        icon.resize(PLUGIN_ICON_SIZE, Image.Resampling.LANCZOS).save(ROOT / "tools/plugin_icon.png")
    print(f"Exported {WIDTH}x{HEIGHT} platform banner and 40x40 Captain app icon.")


if __name__ == "__main__":
    main()
