#!/usr/bin/env python3
"""Every character the UI draws must exist in the font that draws it.

The board's three Cyrillic faces are narrow subsets: ASCII 0x20..0x7E plus
U+0400..U+045F, and nothing else. No em dash, no en dash, no «», no °, no ±,
no ellipsis, no ×, no №. A character outside that set draws a hollow box and
says nothing about it — the screen just quietly grows a rectangle where a
punctuation mark should be, and the fault looks like a rendering bug rather
than a font choice.

haxrcorp4089_t_cyrillic is narrower still: it is missing lowercase э, Ё and ё
despite the name. That one shipped as "что [] то значит" on the pressure
screen and was found by eye, on a photograph, which is not a process.

This reads the fonts' own glyph tables out of U8g2's font blob and checks
every string literal in src/ui against them. It cannot know WHICH font a given
literal will be drawn in, so it applies the intersection: a character that is
missing from any Cyrillic face is reported. That is the right side to err on —
the alternative is tracking setFont() through the call graph, and a false
report costs one look while a missed one ships.

The on-device check in Theme.cpp catches the same thing at draw time and knows
the font exactly; this catches it before the build, which is cheaper.
"""
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FONTS_C = os.path.join(ROOT, '.pio', 'libdeps', 'nocturne-c6', 'U8g2', 'src',
                       'clib', 'u8g2_fonts.c')

CYRILLIC_FONTS = {
    'F_SMALL': 'u8g2_font_5x8_t_cyrillic',
    'F_TEXT': 'u8g2_font_haxrcorp4089_t_cyrillic',
    'F_MED': 'u8g2_font_10x20_t_cyrillic',
}

# Characters that are fine to see in source but never reach a font: the
# comment art and the box-drawing used in file headers.
SKIP_IN_COMMENTS = True


def font_bytes(name):
    """Pull one font's array out of the 400k-line C file without parsing it."""
    pat = re.compile(r'^const uint8_t %s\[\d+\]' % re.escape(name))
    out = bytearray()
    started = False
    with io.open(FONTS_C, encoding='utf-8', errors='replace') as f:
        for line in f:
            if not started:
                if pat.match(line):
                    started = True
                continue
            s = line.strip()
            if s.startswith('"'):
                body = s[1:s.rindex('"')]
                i = 0
                while i < len(body):
                    c = body[i]
                    if c != '\\':
                        out.append(ord(c))
                        i += 1
                        continue
                    n = body[i + 1]
                    if n == 'x':
                        j = i + 2
                        while j < len(body) and \
                                body[j] in '0123456789abcdefABCDEF':
                            j += 1
                        out.append(int(body[i + 2:j], 16) & 0xFF)
                        i = j
                    elif n.isdigit():
                        j = i + 1
                        while j < len(body) and j < i + 4 and \
                                body[j] in '01234567':
                            j += 1
                        out.append(int(body[i + 1:j], 8) & 0xFF)
                        i = j
                    else:
                        out.append({'n': 10, 't': 9, 'r': 13, '\\': 92,
                                    '"': 34, "'": 39, '0': 0}.get(n, ord(n)))
                        i += 2
            if s.endswith(';'):
                break
    return bytes(out)


def glyphs(data):
    """Every encoding the font carries.

    Two blocks. The one-byte block starts at 23; the unicode block starts at
    23 + word(21) and begins with a LOOKUP TABLE of 4-byte (cumulative offset,
    last encoding) entries — u8g2 issue 596 — so the glyphs themselves start
    at the first entry's offset. Walking from the table itself parses the
    index as if it were character data, which is how a first attempt at this
    concluded that 10x20_t_cyrillic has no Cyrillic.
    """
    have = set()
    pos = 23
    while pos + 1 < len(data):
        enc = data[pos]
        jump = data[pos + 1]
        if enc == 0 or jump == 0:
            break
        have.add(enc)
        pos += jump
    base = 23 + ((data[21] << 8) | data[22])
    pos = base + ((data[base] << 8) | data[base + 1])
    while pos + 2 < len(data):
        enc = (data[pos] << 8) | data[pos + 1]
        jump = data[pos + 2]
        if enc == 0 or jump == 0:
            break
        have.add(enc)
        pos += jump
    return have


def literals(path):
    """(line, text) for every C string literal, comments stripped.

    Crude on purpose: a real C parser would be the wrong amount of machinery
    for a check whose whole job is to be run on every build.
    """
    out = []
    src = io.open(path, encoding='utf-8').read()
    # Strip block and line comments so file-header box art is not scanned.
    src = re.sub(r'/\*.*?\*/', lambda m: '\n' * m.group(0).count('\n'),
                 src, flags=re.S)
    src = re.sub(r'//[^\n]*', '', src)
    for i, line in enumerate(src.splitlines(), 1):
        for m in re.finditer(r'"((?:[^"\\]|\\.)*)"', line):
            out.append((i, m.group(1)))
    return out


NAMES = {0x2014: 'длинное тире', 0x2013: 'короткое тире', 0x00B7: 'средняя точка',
         0x00AB: 'левая кавычка', 0x00BB: 'правая кавычка', 0x00B0: 'градус',
         0x2026: 'многоточие', 0x00B1: 'плюс-минус', 0x00D7: 'знак умножения',
         0x2116: 'номер', 0x2192: 'стрелка'}


def main():
    if not os.path.exists(FONTS_C):
        print('шрифтов нет — соберите проект хотя бы раз')
        return 0
    cover = None
    per = {}
    for role, name in CYRILLIC_FONTS.items():
        g = glyphs(font_bytes(name))
        per[role] = g
        cover = g if cover is None else (cover & g)

    ui = os.path.join(ROOT, 'src', 'ui')
    # src/pet: only the phrase table is ever drawn; PetBrain's literals are
    # LLM prompts and may use any punctuation they like.
    pet = os.path.join(ROOT, 'src', 'pet')
    bad = 0
    warn = 0
    for d, fn in sorted([(ui, f) for f in os.listdir(ui)] + [(pet, 'PhraseCache.cpp')]):
        if not fn.endswith(('.cpp', '.h')):
            continue
        for line, text in literals(os.path.join(d, fn)):
            seen = set()
            for ch in text:
                cp = ord(ch)
                if cp < 0x20 or cp in cover or cp in seen:
                    continue
                seen.add(cp)
                who = [r for r in CYRILLIC_FONTS if cp not in per[r]]
                # Missing from EVERY face is a certainty; missing from F_TEXT
                # alone depends on which font the caller selects, and this
                # scanner cannot know that. Both are printed, only the first
                # fails the build — otherwise the menu strings (drawn in
                # F_MED, which has 'э') would keep it red forever and the
                # gate would stop being read.
                sure = len(who) == len(CYRILLIC_FONTS)
                print('%s %s:%d  нет %s (U+%04X) в %s: "%s"'
                      % ('ОШИБКА ' if sure else 'может быть',
                         fn, line, NAMES.get(cp, repr(ch)), cp,
                         '/'.join(who), text[:60]))
                if sure:
                    bad += 1
                else:
                    warn += 1
    print()
    print('точно нет глифа: %d, зависит от шрифта: %d' % (bad, warn))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
