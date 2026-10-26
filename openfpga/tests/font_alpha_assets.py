"""Add deterministic font fixtures to a private copy of the game's data.zip.

Usage: python3 font_alpha_assets.py input-data.zip output-data.zip
Run the host smoke with POCKET_TEST_FONT_ALPHA=1 and the output assets.
"""
import struct
import sys
import zlib
import zipfile


def png(partial, shaped=False):
    def chunk(kind, payload):
        return (struct.pack('>I', len(payload)) + kind + payload
                + struct.pack('>I', zlib.crc32(kind + payload)))

    rows = bytearray()
    for y in range(64):
        rows.append(0)
        for x in range(128):
            glyph = y // 8 * 16 + x // 8
            translucent = glyph == 1 if shaped else glyph in (ord('A'), ord('1'), ord('?'))
            alpha = 128 if partial and translucent else 255
            if x % 8 in (0, 7) or y % 8 in (0, 7):
                alpha = 0
            rows.extend((255, 255, 255, alpha))
    return (b'\x89PNG\r\n\x1a\n'
            + chunk(b'IHDR', struct.pack('>IIBBBBB', 128, 64, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


with zipfile.ZipFile(sys.argv[1]) as source, zipfile.ZipFile(sys.argv[2], 'w', zipfile.ZIP_DEFLATED) as out:
    for entry in source.infolist():
        out.writestr(entry, source.read(entry))
    for name, partial, chars, fallback in (
            ('cachealpha', True, '<range start="0" end="127"/>', ''),
            ('cachefallback', False, '<range start="66" end="66"/>', 'cachealpha'),
            ('cacheshaped', True, '<range start="1576" end="1576"/><range start="65167" end="65167"/>', '')):
        out.writestr('graphics/' + name + '.png', png(partial, name == 'cacheshaped'))
        out.writestr('graphics/' + name + '.fontmeta',
                     '<font_metadata><width>8</width><height>8</height>'
                     '<white_teeth>1</white_teeth><chars>' + chars + '</chars>'
                     + ('<fallback>' + fallback + '</fallback>' if fallback else '')
                     + '</font_metadata>')
