"""tools/ext_icons/drawbatch.py -- the white pictogram for the draw_batch row (programmer art, our own).

Same conventions as highlimits.py: draw(px) paints the 12 x 12 interior (x, y in [2, 13]) of a 16 x 16 tile.
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


def draw_draw_batch(px):
    """Three small blocks, an arrow down, one long block: many small draws become one."""
    _paint(px, [
        "............",
        ".##..##..##.",
        ".##..##..##.",
        "............",
        ".....##.....",
        ".....##.....",
        "...######...",
        "....####....",
        ".....##.....",
        "............",
        ".##########.",
        ".##########.",
    ])


ICONS = {
    "draw_batch": ("A", draw_draw_batch),
}
