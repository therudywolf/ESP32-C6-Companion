"""Generate src/pet/pet_sprites_gen.h from ASCII art.

The wolf's four frames are hand-drawn XBM bitmaps in wolf_sprites.h. The other
species (dog, cat, fox) are drawn here as 32x32 ASCII grids - '#' is ink -
and the three non-idle frames are DERIVED from the idle one by small edits:
blink closes the eyes, aggro adds brows and fangs, funny adds a tongue. Same
four frame ids the whole UI already uses, so a species is a data change.

Run:  python tools/make_pet_sprites.py
"""
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "src" / "pet" / "pet_sprites_gen.h"

# (name, idle grid, eye rows (y0,y1), eye columns [(x0,x1),(x0,x1)],
#  mouth centre (x,y))
DOG = """
................................
..........############..........
........###..........###........
.......##..............##.......
..###.##................##.###..
.#...##..................##...#.
.#..##....................##..#.
.#..#......................#..#.
.#..#..####..........####..#..#.
.#..#.#....#........#....#.#..#.
.#..#.#.##.#........#.##.#.#..#.
.#..#.#.##.#........#.##.#.#..#.
.#..#..####..........####..#..#.
.#..#......................#..#.
.#..#......................#..#.
.#..#......................#..#.
.#..#.......................#.#.
.#..#.......######..........#.#.
.#.#.......#......#..........##.
.#.#.......#.####.#..........#..
.##........#.####.#..........#..
.#..........######...........#..
.#............##.............#..
.##...........##............##..
..##....#.....##.....#.....##...
...##....#....##....#.....##....
....##....########.......##.....
.....###...............###......
.......###...........###........
.........#############..........
................................
................................
"""

CAT = """
................................
....#....................#......
....##..................##......
....###................###......
....#.##..............##.#......
....#..##............##..#......
....#...##..........##...#......
....#....############....#......
....#....................#......
...#......................#.....
...#..####..........####..#.....
...#.#....#........#....#.#.....
...#.#.##.#........#.##.#.#.....
...#.#.##.#........#.##.#.#.....
...#..####..........####..#.....
...#......................#.....
...#......................#.....
...#......................#.....
##.#..........##..........#.###.
..##.........#..#.........##....
...#.........####.........#.....
##.#..........##..........#.##..
...#.........#..#.........#.....
...#........#....#........#.....
....#........................#..
....#........................#..
.....#......................#...
......##..................##....
........##..............##......
..........##############........
................................
................................
"""

FOX = """
................................
...#......................#.....
...##....................##.....
...###..................###.....
...####................####.....
...#.###..............###.#.....
...#..###............###..#.....
...#...###..........###...#.....
...#....####........####..#.....
...#......############....#.....
...#......................#.....
...#..####..........####..#.....
...#.#....#........#....#.#.....
...#.#.##.#........#.##.#.#.....
...#.#.##.#........#.##.#.#.....
...#..####..........####..#.....
....#....................#......
....#....................#......
.....#..................#.......
.....#....##......##....#.......
......#...#..####..#...#........
......#...#......#.....#........
.......#..#.####.#....#.........
.......#...#....#.....#.........
........#...####.....#..........
........#....##......#..........
.........#...##.....#...........
..........#.........#...........
...........#.......#............
............#######.............
................................
................................
"""

SPECIES = [
    ("dog", DOG, (10, 11), [(8, 9), (22, 23)], (14, 19)),
    ("cat", CAT, (12, 13), [(7, 8), (21, 22)], (14, 21)),
    ("fox", FOX, (13, 14), [(7, 8), (21, 22)], (14, 22)),
]


def grid(s: str) -> list[list[bool]]:
    rows = [r for r in s.strip("\n").split("\n")]
    assert len(rows) == 32, f"need 32 rows, got {len(rows)}"
    out = []
    for r in rows:
        assert len(r) == 32, f"row width {len(r)}: {r!r}"
        out.append([c == "#" for c in r])
    return out


def blink(g, eyes, cols):
    g = [row[:] for row in g]
    y0, y1 = eyes
    for (x0, x1) in cols:
        for y in (y0, y1):
            for x in range(x0, x1 + 1):
                g[y][x] = False
        # a closed eye: one line across the middle of the socket
        for x in range(x0 - 1, x1 + 2):
            g[y1][x] = True
    return g


def aggro(g, eyes, cols, mouth):
    g = [row[:] for row in g]
    y0, y1 = eyes
    for (x0, x1) in cols:
        # brow: three pixels sloping down toward the nose, two rows above
        left = x0 < 16
        for i in range(3):
            x = (x0 - 2 + i) if left else (x1 + 2 - i)
            y = y0 - 4 + i
            if 0 <= x < 32 and 0 <= y < 32:
                g[y][x] = True
    mx, my = mouth
    # fangs under the mouth
    for dx in (-3, 3):
        for dy in range(3):
            x = mx + dx
            y = my + 3 + dy
            if 0 <= y < 32:
                g[y][x] = True
    return g


def funny(g, eyes, cols, mouth):
    g = [row[:] for row in g]
    y0, y1 = eyes
    # one winking eye (the right one)
    x0, x1 = cols[1]
    for y in (y0, y1):
        for x in range(x0, x1 + 1):
            g[y][x] = False
    for x in range(x0 - 1, x1 + 2):
        g[y1][x] = True
    mx, my = mouth
    # tongue: a small filled blob under the mouth
    for dy in range(4):
        w = 2 if dy < 3 else 1
        for dx in range(-w, w + 1):
            y = my + 4 + dy
            if 0 <= y < 32:
                g[y][mx + dx] = True
    return g


def xbm(g) -> list[int]:
    out = []
    for y in range(32):
        for bx in range(4):
            b = 0
            for bit in range(8):
                if g[y][bx * 8 + bit]:
                    b |= 1 << bit
            out.append(b)
    return out


def emit(name: str, data: list[int]) -> str:
    lines = [f"static const unsigned char {name}[] = {{"]
    for i in range(0, len(data), 12):
        chunk = ", ".join(f"0x{v:02x}" for v in data[i:i + 12])
        lines.append("    " + chunk + ",")
    lines[-1] = lines[-1].rstrip(",")
    lines.append("};")
    return "\n".join(lines)


def main() -> None:
    parts = [
        "/* GENERATED by tools/make_pet_sprites.py - do not edit by hand.",
        " * 32x32 XBM (LSB-first), four frames per species: idle, blink,",
        " * aggressive, funny - the same ids wolf_sprites.h uses. */",
        "#ifndef NOCT_PET_SPRITES_GEN_H",
        "#define NOCT_PET_SPRITES_GEN_H",
        "",
    ]
    for name, art, eyes, cols, mouth in SPECIES:
        g = grid(art)
        frames = {
            "idle": g,
            "blink": blink(g, eyes, cols),
            "aggressive": aggro(g, eyes, cols, mouth),
            "funny": funny(g, eyes, cols, mouth),
        }
        for fname, fg in frames.items():
            parts.append(emit(f"{name}_{fname}", xbm(fg)))
            parts.append("")
    parts.append("#endif")
    OUT.write_text("\n".join(parts) + "\n", encoding="utf-8")
    print(f"wrote {OUT} ({len(SPECIES)} species x 4 frames)")


if __name__ == "__main__":
    main()
