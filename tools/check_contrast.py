"""Measure every theme's real contrast instead of judging it by eye.

The panel is 172x320 at arm's length in a lit room. WCAG's ratios were written
for exactly this problem — small text on a coloured ground — and they give a
number to argue with, which "looks a bit dim" does not.

Targets used here:
  TEXT   vs BG     >= 7.0   (AAA for body text; this is the primary reading)
  DIM    vs BG     >= 4.5   (AA; it is small print, not decoration)
  DIM    vs PANEL  >= 4.5   (it is drawn on tiles as often as on the ground)
  accent vs BG     >= 3.0   (large glyphs and lines only)
"""
import io, os, re

# Relative to this file, not to a home directory: this started life as a
# scratch script with an absolute path and CI found that on the first run.
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
src = io.open(os.path.join(ROOT, 'src', 'ui', 'Theme.cpp'),
              encoding='utf-8').read()
i = src.index('static const Preset kPresets[THEME_PRESETS] = {')
j = src.index('\n};', i)
body = src[i:j]

# strip comments so rgb() triples inside them cannot be mistaken for data
body = re.sub(r'/\*.*?\*/', '', body, flags=re.S)

entries = re.findall(r'\{"([^"]+)",(.*?)\}(?=\s*,\s*(?:\{|$))', body, re.S)
FIELDS = ['bg', 'chrome', 'text', 'dim', 'panel',
          'good', 'warn', 'crit', 'info', 'accent']


def lum(c):
    def ch(v):
        v /= 255.0
        return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
    r, g, b = c
    return 0.2126 * ch(r) + 0.7152 * ch(g) + 0.0722 * ch(b)


def ratio(a, b):
    la, lb = lum(a), lum(b)
    hi, lo = max(la, lb), min(la, lb)
    return (hi + 0.05) / (lo + 0.05)


def quant(c):
    """RGB565 round-trip: the panel cannot show what the source file says."""
    r, g, b = c
    r = (r >> 3) << 3
    g = (g >> 2) << 2
    b = (b >> 3) << 3
    return (r, g, b)


themes = []
for name, rest in entries:
    trip = re.findall(r'rgb\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)', rest)
    if len(trip) != 10:
        print('!! %-12s parsed %d colours, expected 10' % (name, len(trip)))
        continue
    cols = {FIELDS[k]: quant(tuple(int(x) for x in trip[k])) for k in range(10)}
    themes.append((name, cols))

print('themes parsed: %d\n' % len(themes))
hdr = '%-3s %-11s %6s %6s %6s   %s' % ('#', 'тема', 'TEXT', 'DIM', 'DIM/p', 'проблемы')
print(hdr)
print('-' * len(hdr))

bad = []
for n, (name, c) in enumerate(themes):
    t = ratio(c['text'], c['bg'])
    d = ratio(c['dim'], c['bg'])
    dp = ratio(c['dim'], c['panel'])
    probs = []
    if t < 7.0:
        probs.append('TEXT %.1f' % t)
    if d < 4.5:
        probs.append('DIM %.1f' % d)
    if dp < 4.5:
        probs.append('DIM/panel %.1f' % dp)
    for k in ('good', 'warn', 'crit', 'info', 'chrome'):
        r = ratio(c[k], c['bg'])
        if r < 3.0:
            probs.append('%s %.1f' % (k, r))
    if probs:
        bad.append((n, name, probs))
    print('%-3d %-11s %6.1f %6.1f %6.1f   %s'
          % (n, name, t, d, dp, ', '.join(probs) if probs else 'ok'))

print('\nтем с проблемами: %d из %d' % (len(bad), len(themes)))
