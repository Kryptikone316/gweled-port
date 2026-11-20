#!/usr/bin/env python3
"""Bakes the run-time assets of the SF3500 Gweled port into one pack file (assets/gweled.pak).

Inputs (all from the original Gweled 1.0-beta2 source tree, GPL-2.0-or-later, plus DejaVu Sans Bold):
  pixmaps/gem01..07.svg  -> gem sprites at 56 and 72 px + a large diamond for the title screen
  pixmaps/cursor.svg     -> selection cursor at 56 px
  sounds/*.ogg           -> 48 kHz stereo S16 PCM
  DejaVuSans-Bold.ttf    -> glyph atlases (Latin-1) at several sizes, plain and with a 2 px outline

Needs: rsvg-convert, ffmpeg, Pillow.  Run:  tools/bake.py <gweled-source-dir> <out.pak>

Pack layout (little endian): "GWPK", u32 n, then n x { char name[24]; u32 off; u32 size }, then the blobs.
Blob kinds:
  sprite : u16 w, u16 h, then w*h RGBA bytes (straight alpha)
  sound  : u32 frames, then frames*2 s16 (interleaved stereo, 48 kHz, peak-normalised)
  font   : u16 height, u16 ascent, u16 first, u16 count, u16 outline(0/1), u16 pad,
           then count x { u16 w, u16 h, s16 xoff, s16 yoff, s16 adv, u16 pad, u32 off_fill, u32 off_outline },
           then the alpha bytes (offsets are from the start of the font blob)
"""
import os, struct, subprocess, sys, io
from PIL import Image, ImageFont, ImageDraw

SRC = sys.argv[1]
OUT = sys.argv[2]
FONT = '/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf'
TILE = 56

entries = []   # (name, bytes)


def rsvg(path, px):
    png = subprocess.check_output(['rsvg-convert', '-w', str(px), '-h', str(px), path])
    return Image.open(io.BytesIO(png)).convert('RGBA')


def sprite(im):
    w, h = im.size
    return struct.pack('<HH', w, h) + im.tobytes()      # straight (not premultiplied) RGBA8888


# gems: 56 px (the board tile) and 72 px (source for zoomed-in frames); the title screen diamond is large
for i in range(7):
    svg = os.path.join(SRC, 'pixmaps', 'gem%02d.svg' % (i + 1))
    entries.append(('g%d' % i, sprite(rsvg(svg, TILE))))
    entries.append(('gz%d' % i, sprite(rsvg(svg, 72))))
entries.append(('gtitle', sprite(rsvg(os.path.join(SRC, 'pixmaps', 'gem02.svg'), 128))))
entries.append(('cursor', sprite(rsvg(os.path.join(SRC, 'pixmaps', 'cursor.svg'), TILE))))

# sounds
for n in ('click', 'swap', 'explode'):
    raw = subprocess.check_output(['ffmpeg', '-v', 'error', '-i', os.path.join(SRC, 'sounds', n + '.ogg'),
                                   '-f', 's16le', '-ar', '48000', '-ac', '2', '-'])
    # peak-normalise to -1 dBFS (the originals are quiet: swap peaks at -15 dB); the game scales each one back down at play time
    import array
    a = array.array('h'); a.frombytes(raw)
    peak = max(1, max(abs(v) for v in a))
    k = 29000.0 / peak
    a = array.array('h', [max(-32768, min(32767, int(round(v * k)))) for v in a])
    raw = a.tobytes()
    entries.append(('snd_' + n, struct.pack('<I', len(raw) // 4) + raw))


# fonts
def bake_font(px, outline):
    first, last = 32, 255
    f = ImageFont.truetype(FONT, px, layout_engine=ImageFont.Layout.BASIC)
    asc, desc = f.getmetrics()
    pad = 2 if outline else 0
    glyphs = []
    data = bytearray()
    for cp in range(first, last + 1):
        ch = chr(cp)
        if 127 <= cp <= 159:
            glyphs.append((0, 0, 0, 0, 0, 0, 0))
            continue
        adv = int(round(f.getlength(ch)))
        bbox = f.getbbox(ch, anchor='ls')           # relative to baseline origin
        if ch == ' ' or bbox[2] <= bbox[0]:
            glyphs.append((0, 0, 0, 0, adv, 0, 0))
            continue
        x0, y0, x1, y1 = bbox[0] - pad, bbox[1] - pad, bbox[2] + pad, bbox[3] + pad
        w, h = x1 - x0, y1 - y0
        fill = Image.new('L', (w, h), 0)
        ImageDraw.Draw(fill).text((-x0, -y0), ch, font=f, fill=255, anchor='ls')
        off_fill = len(data)
        data += fill.tobytes()
        off_out = 0
        if outline:
            out = Image.new('L', (w, h), 0)
            ImageDraw.Draw(out).text((-x0, -y0), ch, font=f, fill=255, anchor='ls', stroke_width=pad, stroke_fill=255)
            off_out = len(data)
            data += out.tobytes()
        glyphs.append((w, h, x0, y0, adv, off_fill, off_out))
    head = struct.pack('<HHHHHH', asc + desc + pad * 2, asc, first, last - first + 1, 1 if outline else 0, 0)
    table = bytearray()
    base = len(head) + len(glyphs) * 20
    for (w, h, xo, yo, adv, of, oo) in glyphs:
        table += struct.pack('<HHhhhHII', w, h, xo, yo, adv, 0, base + of if w else 0, base + oo if (w and outline) else 0)
    return head + bytes(table) + bytes(data)


for px in (14, 18, 24, 32, 44):
    entries.append(('f%d' % px, bake_font(px, False)))
for px in (24, 36, 52):
    entries.append(('o%d' % px, bake_font(px, True)))

# pack
hdr = b'GWPK' + struct.pack('<I', len(entries))
tab_size = len(entries) * 32
off = len(hdr) + tab_size
table = bytearray()
blob = bytearray()
for name, b in entries:
    while len(blob) % 4:
        blob += b'\0'
    table += name.encode().ljust(24, b'\0') + struct.pack('<II', off + len(blob), len(b))
    blob += b
os.makedirs(os.path.dirname(os.path.abspath(OUT)), exist_ok=True)
with open(OUT, 'wb') as fo:
    fo.write(hdr + bytes(table) + bytes(blob))
print('wrote', OUT, len(hdr) + len(table) + len(blob), 'bytes,', len(entries), 'entries')
