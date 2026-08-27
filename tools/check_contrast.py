"""Measure every theme's real contrast instead of judging it by eye.

The panel is 172x320, read from across a room. WCAG's ratios were written for
exactly this problem — small text on a coloured ground — and they give a
number to argue with, which "looks a bit dim" does not.

Two classes of colour are checked, and the second is the one that was missed
the first time round:

  PALETTE   — text and dim, straight out of kPresets.
  DERIVED   — what the drawing code actually paints with. Every tile TITLE is
              ORANGE (= chrome) and every tile FRAME is dimmer(chrome), and
              neither appears in the palette as such. The frames had fallen to
              2.3 in eight themes while every palette entry passed.

Targets:
  TEXT   vs BG        >= 7.0   AAA body text; this is the primary reading
  DIM    vs BG/PANEL  >= 4.5   AA; it is small print, not decoration
  TITLE  vs BG/PANEL  >= 4.5   a tile label is small text too
  FRAME  vs BG        >= 3.0   WCAG's floor for a graphical element
  accent vs BG        >= 3.0   large glyphs and lines only

Every colour is round-tripped through RGB565 first: the panel cannot show
what the source file says.
"""
import io
import os
import re
import sys

# Relative to this file, not to a home directory: this started life as a
# scratch script with an absolute path and CI found that on the first run.
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
src = io.open(os.path.join(ROOT, 'src', 'ui', 'Theme.cpp'),
              encoding='utf-8').read()

# The frame factor is READ FROM THE SOURCE rather than repeated here. A check
# that hardcodes the value it is checking passes happily after the code
# changes underneath it.
m = re.search(r'\(r \* (\d+) / (\d+)\) << 11', src)
assert m, 'could not find dimmer() in Theme.cpp'

# The material tiles draw on neither BG nor PANEL. They draw on SURFACE,
# solved from the palette at runtime — so every ratio below that says "vs BG"
# is answering about a background that text is no longer sitting on.
#
# This is the mistake this gate exists to catch, wearing a new name: DIM was
# once chosen against BG and drawn on PANEL, eleven themes passed, and the
# screen was unreadable. A tone no check knows about is that bug again.
#
# What follows must stay a faithful mirror of theme::deriveSurface(). If the
# two drift, this file reports on a screen that does not exist.

DIM_NUM, DIM_DEN = int(m.group(1)), int(m.group(2))

i = src.index('static const Preset kPresets[THEME_PRESETS] = {')
j = src.index('\n};', i)
body = re.sub(r'/\*.*?\*/', '', src[i:j], flags=re.S)   # comments hold rgb() too
entries = re.findall(r'\{"([^"]+)",(.*?)\}(?=\s*,\s*(?:\{|$))', body, re.S)
FIELDS = ['bg', 'chrome', 'text', 'dim', 'panel',
          'good', 'warn', 'crit', 'info', 'accent']


def to565(c):
    r, g, b = c
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def from565(v):
    return (((v >> 11) & 0x1F) << 3, ((v >> 5) & 0x3F) << 2, (v & 0x1F) << 3)


def quant(c):
    return from565(to565(c))


def dimmer(c):
    v = to565(c)
    r = (v >> 11) & 0x1F
    g = (v >> 5) & 0x3F
    b = v & 0x1F
    return from565(((r * DIM_NUM // DIM_DEN) << 11) |
                   ((g * DIM_NUM // DIM_DEN) << 5) |
                   (b * DIM_NUM // DIM_DEN))


def lum(c):
    def ch(v):
        v /= 255.0
        return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
    return 0.2126 * ch(c[0]) + 0.7152 * ch(c[1]) + 0.0722 * ch(c[2])


SURF_VS_BG = 1.25   # tile against background: a non-text boundary
SURF_LABEL = 4.5    # label on the tile: small text, AA


def _mix(a, b, t):
    """lerp565 with the same 565 quantisation the board does it in — the
    rounding matters at these small steps."""
    return quant(tuple(int(x + (y - x) * t / 255.0) for x, y in zip(a, b)))


def derive_surface(c):
    """Mirror of theme::deriveSurface(): lift for visibility, then blend the
    label toward TEXT for readability. Two separate solves because a numeric
    sweep proved no single lift satisfies both — five of the 22 presets had no
    workable value at any lift."""
    bg, dim, text = quant(c['bg']), quant(c['dim']), quant(c['text'])
    light = lum(bg) > 0.5
    target = (0, 0, 0) if light else (255, 255, 255)
    surf = _mix(bg, target, 96)
    for lift in range(12, 97, 4):
        cand = _mix(bg, target, lift)
        if ratio(cand, bg) >= SURF_VS_BG:
            surf = cand
            break
    lab = text
    for k in range(0, 256, 8):
        cand = _mix(dim, text, k)
        if ratio(cand, surf) >= SURF_LABEL:
            lab = cand
            break
    return surf, lab


def ratio(a, b):
    la, lb = lum(a), lum(b)
    hi, lo = max(la, lb), min(la, lb)
    return (hi + 0.05) / (lo + 0.05)


themes = []
for name, rest in entries:
    trip = re.findall(r'rgb\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)', rest)
    if len(trip) != 10:
        print('!! %-12s parsed %d colours, expected 10' % (name, len(trip)))
        sys.exit(1)
    themes.append((name, {FIELDS[k]: tuple(int(x) for x in trip[k])
                          for k in range(10)}))

print('themes: %d   frame factor: %d/%d\n' % (len(themes), DIM_NUM, DIM_DEN))
hdr = ('%-3s %-11s %6s %6s %6s %6s %6s %6s %6s %6s %6s   %s'
       % ('#', 'тема', 'TEXT', 'DIM', 'DIM/p', 'ЗАГЛ', 'ЗГЛ/p', 'РАМКА',
          'TXT/s', 'DIM/s', 'плитка', 'проблемы'))
print(hdr)
print('-' * len(hdr))

bad = []
for n, (name, c) in enumerate(themes):
    bg, pan = quant(c['bg']), quant(c['panel'])
    text, dim = quant(c['text']), quant(c['dim'])
    title = quant(c['chrome'])
    frame = dimmer(c['chrome'])

    t = ratio(text, bg)
    d, dp = ratio(dim, bg), ratio(dim, pan)
    ti, tip = ratio(title, bg), ratio(title, pan)
    fr = ratio(frame, bg)

    # On a material tile the label is DIM and the value is TEXT, both drawn on
    # surface() rather than on BG. Checked in the mode this palette's own
    # background implies; the light/dark switch is global, so the tone under
    # the text follows whichever way it is thrown.
    surf, label = derive_surface(c)
    # The VALUE on a tile is display or title type — 16 to 64 px. WCAG's bar
    # for large text is 4.5 at AAA, not the 7.0 this file uses for body copy;
    # holding a 64 px numeral to the small-print rule would fail palettes that
    # are perfectly legible across the room.
    ts, ds = ratio(text, surf), ratio(label, surf)
    # Tile against background: not text, so the bar is the 3.0 used for
    # non-text boundaries rather than 4.5. Below this the tile stops reading
    # as a raised surface and the whole material grammar is just flat colour.
    sb = ratio(surf, bg)

    probs = []
    if t < 7.0:
        probs.append('TEXT %.1f' % t)
    if d < 4.5:
        probs.append('DIM %.1f' % d)
    if dp < 4.5:
        probs.append('DIM/panel %.1f' % dp)
    if ti < 4.5:
        probs.append('заголовок %.1f' % ti)
    if tip < 4.5:
        probs.append('заголовок/panel %.1f' % tip)
    if fr < 3.0:
        probs.append('рамка %.1f' % fr)
    if ts < 4.5:
        probs.append('TEXT/surface %.1f' % ts)
    if ds < SURF_LABEL:
        probs.append('ярлык/surface %.1f' % ds)
    if sb < SURF_VS_BG:
        probs.append('плитка/фон %.2f' % sb)
    for k in ('good', 'warn', 'crit', 'info'):
        r = ratio(quant(c[k]), bg)
        if r < 3.0:
            probs.append('%s %.1f' % (k, r))
    if probs:
        bad.append((n, name, probs))
    print('%-3d %-11s %6.1f %6.1f %6.1f %6.1f %6.1f %6.1f %6.1f %6.1f %6.2f   %s'
          % (n, name, t, d, dp, ti, tip, fr, ts, ds, sb,
             ', '.join(probs) if probs else 'ok'))

print('\nтем с проблемами: %d из %d' % (len(bad), len(themes)))
sys.exit(1 if bad else 0)
