"""tools/ext_icons/sharegpudata.py -- the white pictogram for the share_gpu_data row (programmer art, our own).

Same conventions as compressao.py: draw(px) paints the 12 x 12 interior (x, y in [2, 13]) of a 16 x 16 tile.
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


def draw_share_gpu_data(px):
    """Two small equal tiles whose lines meet in one larger tile: two copies kept as one."""
    _paint(px, [
        "###.........",
        "#k#.........",
        "####........",
        "....#...####",
        ".....#..#kk#",
        "......###kk#",
        "......###kk#",
        ".....#..#kk#",
        "....#...####",
        "####........",
        "#k#.........",
        "###.........",
    ])


ICONS = {
    "share_gpu_data": ("A", draw_share_gpu_data),
}
