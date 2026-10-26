# Pocket artwork

The platform banner uses the first frame of the original repository-root
`logo.gif`. The Pocket displays it in monochrome at 521×165; the encoded
`dist/platforms/_images/vvvvvv.bin` stores that image rotated counterclockwise,
with inverted brightness in the low byte of each 16-bit pixel, following the
[Analogue platform-image format](https://www.analogue.co/developer/docs/openfpga/packaging-a-core).
`vvvvvv-platform.png` previews the decoded banner.

The Pocket Sync `tools/plugin_icon.png` is a byte-for-byte copy of
`desktop_version/AppIcon.xcassets/AppIcon.appiconset/AppIcon.png`. It features
Captain Viridian in the original game artwork. These images retain the game's
copyright and [source license](../../../LICENSE.md); the tooling's MIT license
does not relicense the game artwork. No generated character art is used.

Regenerate the checked-in exports from the existing repository assets:

```sh
python3 openfpga/scripts/make-branding.py
```

This requires Pillow, like the button-font exporter. Normal builds copy the
checked-in exports and do not require Pillow.
