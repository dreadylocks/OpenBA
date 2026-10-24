"""tools/ext_icons/compressao.py -- the white pictogram for the compress_ao row (programmer art, our own).

Same conventions as loadprogress.py: draw(px) paints the 12 x 12 interior (x, y in [2, 13]) of a 16 x 16 tile.
'#' = white, 'k' = the dark accent, '.' = leave the tier colour showing.
"""

WHITE = (255, 255, 255, 255)
DARK = (42, 42, 42, 255)


def _paint(px, rows):
    assert len(rows) == 12 and all(len(r) == 12 for r in rows), rows
    for j, row in enumerate(rows):
        for i, ch in enumerate(row):
            if ch == "#":
                px[2 + i, 2 + j] = WHITE
            elif ch == "k":
                px[2 + i, 2 + j] = DARK


def draw_compress_ao(px):
    """A shaded map cut into 4 x 4 blocks, and the same map at half the size: the AO maps compressed."""
    _paint(px, [
        "#########...",
        "#kk.#k..#...",
        "#k..#...#...",
        "#...#..k#...",
        "#########...",
        "#k..#...#...",
        "#...#..k#...",
        "#..k#.kk#...",
        "#########...",
        "......#####.",
        "......#k#k#.",
        "......#####.",
    ])


ICONS = {
    "compress_ao": ("A", draw_compress_ao),
}
