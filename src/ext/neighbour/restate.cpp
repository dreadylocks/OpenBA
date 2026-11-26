// ============================================================================
// src/ext/neighbour/restate.cpp -- NOT RETAIL. ext.neighbour: the restate (restate.h)
// ============================================================================
#include "ext/neighbour/restate.h"

#include <cstdlib>

namespace Neighbour {
namespace Restate {

namespace {

size_t Chars(const Part& p) {
    return p.builds.size() + p.roads.size() + p.land.size();
}

std::vector<std::string> SplitComma(const std::string& list) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= list.size()) {
        const size_t comma = list.find(',', start);
        const size_t end = comma == std::string::npos ? list.size() : comma;
        if (end > start) out.push_back(list.substr(start, end - start));
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return out;
}

bool IdChar(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || c == '_' || c == '-';
}

}  // namespace

std::vector<Part> Pack(const std::vector<std::string>& builds, const std::vector<std::string>& roads,
                       const std::vector<std::string>& land, size_t maxChars) {
    std::vector<Part> parts(1);
    const std::vector<std::string>* lists[3] = {&builds, &roads, &land};
    for (int k = 0; k < 3; ++k) {
        for (const std::string& entry : *lists[k]) {
            Part* p = &parts.back();
            std::string* field = k == 0 ? &p->builds : (k == 1 ? &p->roads : &p->land);
            const size_t add = entry.size() + (field->empty() ? 0 : 1);
            if (Chars(*p) > 0 && Chars(*p) + add > maxChars) {
                parts.emplace_back();
                p = &parts.back();
                field = k == 0 ? &p->builds : (k == 1 ? &p->roads : &p->land);
            }
            if (!field->empty()) *field += ';';
            *field += entry;
        }
    }
    return parts;
}

std::vector<std::string> RowRuns(const std::vector<uint8_t>& mask, int dim) {
    std::vector<std::string> out;
    if (dim <= 0 || mask.size() < static_cast<size_t>(dim) * static_cast<size_t>(dim)) return out;
    for (int y = 0; y < dim; ++y) {
        int start = -1;
        for (int x = 0; x <= dim; ++x) {
            const bool in = x < dim && mask[static_cast<size_t>(y) * dim + static_cast<size_t>(x)] != 0;
            if (in && start < 0) {
                start = x;
            } else if (!in && start >= 0) {
                out.push_back(std::to_string(start) + "," + std::to_string(y) + "," + std::to_string(x - start) +
                              ",1");
                start = -1;
            }
        }
    }
    return out;
}

std::vector<int> StaleRoads(const std::vector<uint8_t>& peer, const std::vector<uint8_t>& road,
                            const std::vector<uint32_t>& seen, uint32_t join) {
    std::vector<int> out;
    const size_t n = peer.size() < road.size() ? peer.size() : road.size();
    for (size_t i = 0; i < n; ++i) {
        if (peer[i] == 0 || road[i] == 0) continue;
        if (i < seen.size() && seen[i] == join) continue;
        out.push_back(static_cast<int>(i));
    }
    return out;
}

std::vector<std::string> WantedIds(const std::string& list) {
    std::vector<std::string> out;
    for (const std::string& id : SplitComma(list)) {
        bool ok = id.size() <= 40;
        for (char c : id) ok = ok && IdChar(c);
        if (ok) out.push_back(id);
    }
    return out;
}

std::vector<int> WantedCells(const std::string& list, int cellCount) {
    std::vector<int> out;
    for (const std::string& s : SplitComma(list)) {
        if (s.size() > 9) continue;
        bool digits = true;
        for (char c : s) digits = digits && c >= '0' && c <= '9';
        if (!digits) continue;
        const long v = std::strtol(s.c_str(), nullptr, 10);
        if (v >= 0 && v < cellCount) out.push_back(static_cast<int>(v));
    }
    return out;
}

}  // namespace Restate
}  // namespace Neighbour
