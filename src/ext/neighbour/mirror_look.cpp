// ============================================================================
// src/ext/neighbour/mirror_look.cpp -- NOT RETAIL. ext.neighbour: how a mirror looks (mirror_look.h)
// ============================================================================

#include "ext/neighbour/mirror_look.h"

namespace Neighbour {
namespace MirrorLook {

int StagePercent(int meshIndex, int stageCount) {
    if (meshIndex < 0) return 0;
    if (stageCount <= 1) return 100;
    const int idx = meshIndex >= stageCount ? stageCount - 1 : meshIndex;
    const int percent = ((idx + 1) * 100) / stageCount;
    return percent < 1 ? 1 : percent;   // a model of over 100 stages: its first is still shown
}

int StageOf(int64_t percent, int stageCount) {
    if (percent <= 0 || stageCount <= 0) return -1;
    if (percent > 100) percent = 100;
    int stage = static_cast<int>((percent * stageCount + 99) / 100) - 1;
    if (stage < 0) stage = 0;
    if (stage >= stageCount) stage = stageCount - 1;
    return stage;
}

std::string PileList(const std::vector<Pile>& piles) {
    std::string out;
    out.reserve(piles.size() * 4);
    for (size_t i = 0; i < piles.size(); ++i) {
        if (i > 0) out += ',';
        if (piles[i].good < 0) continue;
        out += std::to_string(piles[i].good);
        out += '.';
        out += std::to_string(piles[i].mesh);
    }
    return out;
}

namespace {

// A small non-negative number of at most three digits.
bool ReadNumber(const std::string& s, size_t from, size_t to, int* out) {
    if (to <= from || to - from > 3) return false;
    int v = 0;
    for (size_t i = from; i < to; ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        v = v * 10 + (s[i] - '0');
    }
    *out = v;
    return true;
}

}  // namespace

bool ParsePileList(const std::string& list, size_t maxTiles, int goodCount, std::vector<Pile>* out) {
    out->clear();
    std::vector<Pile> piles;
    size_t at = 0;
    while (true) {
        const size_t comma = list.find(',', at);
        const size_t end = comma == std::string::npos ? list.size() : comma;
        Pile p;
        if (end > at) {
            const size_t dot = list.find('.', at);
            if (dot == std::string::npos || dot >= end) return false;
            if (!ReadNumber(list, at, dot, &p.good) || !ReadNumber(list, dot + 1, end, &p.mesh)) return false;
            if (p.good >= goodCount || p.mesh > kMaxPileMesh) return false;
        }
        piles.push_back(p);
        if (piles.size() > maxTiles) return false;
        if (comma == std::string::npos) break;
        at = comma + 1;
    }
    out->swap(piles);
    return true;
}

std::string GoodList(const std::vector<std::string>& goods) {
    std::string out;
    for (size_t i = 0; i < goods.size(); ++i) {
        if (i > 0) out += '|';
        out += goods[i];
    }
    return out;
}

bool ParseGoodList(const std::string& list, size_t maxGoods, size_t maxChars, std::vector<std::string>* out) {
    out->clear();
    if (list.empty()) return true;
    std::vector<std::string> goods;
    size_t at = 0;
    while (true) {
        const size_t bar = list.find('|', at);
        const size_t end = bar == std::string::npos ? list.size() : bar;
        if (end == at || end - at > maxChars) return false;
        goods.push_back(list.substr(at, end - at));
        if (goods.size() > maxGoods) return false;
        if (bar == std::string::npos) break;
        at = bar + 1;
    }
    out->swap(goods);
    return true;
}

}  // namespace MirrorLook
}  // namespace Neighbour
