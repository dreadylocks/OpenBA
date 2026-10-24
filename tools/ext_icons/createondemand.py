"""tools/ext_icons/createondemand.py -- the white pictogram for the create_on_demand row (programmer art, our own).

Same conventions as sharegpudata.py: draw(px) paints the 12 x 12 interior (x, y in [2, 13]) of a 16 x 16 tile.
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


def draw_create_on_demand(px):
    """A dotted (not yet made) tile and a solid one, an arrow from the first to the second: made when asked."""
    _paint(px, [
        "#.#.#.......",
        "......######",
        "#...#.#kkkk#",
        "......#kkkk#",
        "#.#.#.#kkkk#",
        "......######",
        "............",
        "...#........",
        "...##.......",
        "######......",
        "...##.......",
        "...#........",
    ])


ICONS = {
    "create_on_demand": ("A", draw_create_on_demand),
}
