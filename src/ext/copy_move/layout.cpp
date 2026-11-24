// ============================================================================
// src/ext/copy_move/layout.cpp -- the clipboard's geometry (layout.h)
// ============================================================================
// NOT RETAIL. Standard C++ only.
// ============================================================================
#include "ext/copy_move/layout.h"

#include <algorithm>
#include <climits>

namespace Ext {
namespace CopyMove {

Cell TurnCell(const Cell& c, int boxW, int boxH) {
    (void)boxW;
    Cell out;
    out.col = boxH - 1 - c.row;
    out.row = c.col;
    return out;
}

Rect TurnRect(const Rect& r, int boxW, int boxH) {
    (void)boxW;
    // The rect's far row (row + h - 1) becomes its new min column.
    Rect out;
    out.col = boxH - (r.row + r.h);
    out.row = r.col;
    out.w = r.h;
    out.h = r.w;
    return out;
}

int CentredStart(int cursor, int dim) {
    return cursor - (dim - 1) / 2;
}

void Layout::Clear() {
    _items.clear();
    _boxW = 0;
    _boxH = 0;
    _turns = 0;
}

void Layout::Add(const Item& absolute) {
    _items.push_back(absolute);
}

void Layout::End() {
    if (_items.empty()) {
        _boxW = 0;
        _boxH = 0;
        _turns = 0;
        return;
    }
    int minCol = INT_MAX;
    int minRow = INT_MAX;
    int maxCol = INT_MIN;
    int maxRow = INT_MIN;
    for (const Item& it : _items) {
        minCol = std::min(minCol, it.rect.col);
        minRow = std::min(minRow, it.rect.row);
        maxCol = std::max(maxCol, it.rect.col + it.rect.w - 1);
        maxRow = std::max(maxRow, it.rect.row + it.rect.h - 1);
    }
    for (Item& it : _items) {
        it.rect.col -= minCol;
        it.rect.row -= minRow;
    }
    _boxW = maxCol - minCol + 1;
    _boxH = maxRow - minRow + 1;
    _turns = 0;
}

void Layout::Turn(int dir) {
    const int steps = dir > 0 ? 1 : 3;
    for (int s = 0; s < steps; ++s) {
        for (Item& it : _items) {
            it.rect = TurnRect(it.rect, _boxW, _boxH);
            if (it.kind == Kind::Building || it.kind == Kind::Pasture) {
                it.turn = (it.turn + 1) & 3;
            }
        }
        std::swap(_boxW, _boxH);
        _turns = (_turns + 1) & 3;
    }
}

Cell Layout::OriginFor(const Cell& cursor) const {
    Cell o;
    o.col = CentredStart(cursor.col, _boxW);
    o.row = CentredStart(cursor.row, _boxH);
    return o;
}

Rect Layout::Placed(const Item& item, const Cell& origin) {
    Rect r = item.rect;
    r.col += origin.col;
    r.row += origin.row;
    return r;
}

Rect DragRect(const Cell& a, const Cell& b) {
    Rect r;
    r.col = std::min(a.col, b.col);
    r.row = std::min(a.row, b.row);
    r.w = std::max(a.col, b.col) - r.col + 1;
    r.h = std::max(a.row, b.row) - r.row + 1;
    return r;
}

bool Overlaps(const Rect& a, const Rect& b) {
    return a.col < b.col + b.w && b.col < a.col + a.w && a.row < b.row + b.h && b.row < a.row + a.h;
}

bool Contains(const Rect& r, const Cell& c) {
    return c.col >= r.col && c.col < r.col + r.w && c.row >= r.row && c.row < r.row + r.h;
}

}  // namespace CopyMove
}  // namespace Ext
