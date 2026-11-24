// ============================================================================
// src/ext/neighbour/nature_sync.cpp -- NOT RETAIL. ext.neighbour: mending the shared map's nature
// (nature_sync.h)
// ============================================================================

#include "ext/neighbour/nature_sync.h"

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace Neighbour {
namespace NatureSync {

namespace {

uint64_t Mix(uint64_t h) {
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ull;
    h ^= h >> 33;
    return h;
}

std::vector<std::string> Split(const std::string& s, char sep) {
    std::vector<std::string> out;
    if (s.empty()) return out;
    size_t at = 0;
    while (true) {
        const size_t next = s.find(sep, at);
        out.push_back(s.substr(at, next == std::string::npos ? std::string::npos : next - at));
        if (next == std::string::npos) break;
        at = next + 1;
    }
    return out;
}

bool ToInt(const std::string& s, long long& v) {
    if (s.empty() || s.size() > 12) return false;
    char* end = nullptr;
    v = std::strtoll(s.c_str(), &end, 10);
    return end != nullptr && *end == '\0';
}

bool ToHash(const std::string& s, uint64_t& v) {
    if (s.empty() || s.size() > 16) return false;
    char* end = nullptr;
    v = std::strtoull(s.c_str(), &end, 16);
    return end != nullptr && *end == '\0';
}

}  // namespace

int BlockOf(int cell, int mapSize) {
    if (mapSize <= 0 || cell < 0) return -1;
    const int perRow = (mapSize + kBlock - 1) / kBlock;
    return (cell / mapSize / kBlock) * perRow + (cell % mapSize) / kBlock;
}

int HalfOf(int cell, int mapSize) {
    if (mapSize <= 0 || cell < 0) return -1;
    return (cell % mapSize) < mapSize / 2 ? 0 : 1;
}

uint64_t PlantHash(int cell, const std::string& tmpl) {
    uint64_t th = 1469598103934665603ull;
    for (unsigned char c : tmpl) th = (th ^ c) * 1099511628211ull;
    return Mix(th ^ (static_cast<uint64_t>(cell) << 1));
}

void AddTo(std::map<int, BlockSum>& sums, int mapSize, int cell, const std::string& tmpl) {
    BlockSum& s = sums[BlockOf(cell, mapSize)];
    ++s.count;
    s.hash += PlantHash(cell, tmpl);
}

std::vector<std::string> EncodeDigest(const std::map<int, BlockSum>& sums, size_t maxChars) {
    std::vector<std::string> out;
    std::string list;
    for (const auto& kv : sums) {
        char entry[64];
        std::snprintf(entry, sizeof entry, "%d,%u,%016" PRIx64, kv.first, kv.second.count, kv.second.hash);
        if (!list.empty() && list.size() + 1 + std::char_traits<char>::length(entry) > maxChars) {
            out.push_back(list);
            list.clear();
        }
        if (!list.empty()) list += ';';
        list += entry;
    }
    if (!list.empty() || out.empty()) out.push_back(list);
    return out;
}

bool DecodeDigest(const std::string& list, std::map<int, BlockSum>& out) {
    std::map<int, BlockSum> got;
    for (const std::string& entry : Split(list, ';')) {
        const std::vector<std::string> f = Split(entry, ',');
        long long block = 0, count = 0;
        uint64_t hash = 0;
        if (f.size() != 3 || !ToInt(f[0], block) || !ToInt(f[1], count) || !ToHash(f[2], hash) || block < 0 ||
            count < 0 || count > 0xFFFFFFFFll)
            return false;
        got[static_cast<int>(block)] = BlockSum{static_cast<uint32_t>(count), hash};
    }
    for (const auto& kv : got) out[kv.first] = kv.second;
    return true;
}

std::vector<int> Differing(const std::map<int, BlockSum>& owner, const std::map<int, BlockSum>& here,
                           const std::set<int>& skip, int* skipped) {
    std::set<int> blocks;
    for (const auto& kv : owner) blocks.insert(kv.first);
    for (const auto& kv : here) blocks.insert(kv.first);
    std::vector<int> out;
    const BlockSum none;
    for (int b : blocks) {
        const auto o = owner.find(b);
        const auto h = here.find(b);
        if ((o == owner.end() ? none : o->second) == (h == here.end() ? none : h->second)) continue;
        if (skip.count(b)) {
            if (skipped != nullptr) ++*skipped;
            continue;
        }
        out.push_back(b);
    }
    return out;
}

std::vector<BlockMessage> PackBlocks(const std::vector<int>& blocks,
                                     const std::map<int, std::vector<Plant>>& plants, size_t maxChars) {
    std::vector<BlockMessage> out;
    BlockMessage m;
    std::map<std::string, int> tmplIndex;
    size_t chars = 0;
    auto flush = [&]() {
        if (m.list.empty() && m.done.empty()) return;
        out.push_back(m);
        m = BlockMessage();
        tmplIndex.clear();
        chars = 0;
    };
    for (int block : blocks) {
        const auto it = plants.find(block);
        if (it != plants.end()) {
            for (const Plant& p : it->second) {
                const bool newTmpl = tmplIndex.find(p.tmpl) == tmplIndex.end();
                char entry[96];
                const int idx = newTmpl ? static_cast<int>(tmplIndex.size()) : tmplIndex[p.tmpl];
                std::snprintf(entry, sizeof entry, "%d,%d,%d,%d,%d", p.cell, idx, p.grp, p.var, p.prog);
                const size_t need =
                    std::char_traits<char>::length(entry) + 1 + (newTmpl ? p.tmpl.size() + 1 : 0);
                if (!m.list.empty() && chars + need > maxChars) {
                    flush();
                    // The template table starts again in the next message.
                    const int again = static_cast<int>(tmplIndex.size());
                    std::snprintf(entry, sizeof entry, "%d,%d,%d,%d,%d", p.cell, again, p.grp, p.var, p.prog);
                }
                if (tmplIndex.find(p.tmpl) == tmplIndex.end()) {
                    const int next = static_cast<int>(tmplIndex.size());
                    tmplIndex[p.tmpl] = next;
                    if (next > 0) m.tmpls += '\n';
                    m.tmpls += p.tmpl;
                    chars += p.tmpl.size() + 1;
                }
                if (!m.list.empty()) m.list += ';';
                m.list += entry;
                chars += std::char_traits<char>::length(entry) + 1;
            }
        }
        const std::string b = std::to_string(block);
        if (!m.done.empty() && chars + b.size() + 1 > maxChars) flush();
        if (!m.done.empty()) m.done += ',';
        m.done += b;
        chars += b.size() + 1;
    }
    flush();
    return out;
}

bool UnpackBlocks(const BlockMessage& m, int mapSize, std::map<int, std::vector<Plant>>& into,
                  std::vector<int>& done) {
    if (mapSize <= 0) return false;
    const std::vector<std::string> tmpls = m.tmpls.empty() ? std::vector<std::string>() : Split(m.tmpls, '\n');
    std::vector<std::pair<int, Plant>> got;
    for (const std::string& entry : Split(m.list, ';')) {
        const std::vector<std::string> f = Split(entry, ',');
        long long v[5] = {0, 0, 0, 0, 0};
        if (f.size() != 5) return false;
        for (size_t i = 0; i < 5; ++i)
            if (!ToInt(f[i], v[i])) return false;
        if (v[0] < 0 || v[0] >= static_cast<long long>(mapSize) * mapSize || v[1] < 0 ||
            v[1] >= static_cast<long long>(tmpls.size()) || tmpls[static_cast<size_t>(v[1])].empty())
            return false;
        Plant p;
        p.cell = static_cast<int>(v[0]);
        p.tmpl = tmpls[static_cast<size_t>(v[1])];
        p.grp = static_cast<int>(v[2]);
        p.var = static_cast<int>(v[3]);
        p.prog = static_cast<int>(v[4]);
        got.emplace_back(BlockOf(p.cell, mapSize), std::move(p));
    }
    std::vector<int> blocks;
    if (!ParseInts(m.done, blocks)) return false;
    for (auto& g : got) into[g.first].push_back(std::move(g.second));
    done.insert(done.end(), blocks.begin(), blocks.end());
    return true;
}

Mend PlanMend(const std::vector<Plant>& here, const std::vector<Plant>& owner) {
    using Key = std::pair<int, std::string>;
    std::map<Key, std::vector<size_t>> hereBy, ownerBy;
    for (size_t i = 0; i < here.size(); ++i) hereBy[Key(here[i].cell, here[i].tmpl)].push_back(i);
    for (size_t i = 0; i < owner.size(); ++i) ownerBy[Key(owner[i].cell, owner[i].tmpl)].push_back(i);
    Mend mend;
    for (const auto& kv : hereBy) {
        const auto o = ownerBy.find(kv.first);
        const size_t want = o == ownerBy.end() ? 0 : o->second.size();
        for (size_t i = want; i < kv.second.size(); ++i) mend.remove.push_back(kv.second[i]);
    }
    for (const auto& kv : ownerBy) {
        const auto h = hereBy.find(kv.first);
        const size_t have = h == hereBy.end() ? 0 : h->second.size();
        for (size_t i = have; i < kv.second.size(); ++i) mend.add.push_back(owner[kv.second[i]]);
    }
    return mend;
}

std::set<int> StillSettled(std::map<int, Settled>& settled, const std::map<int, BlockSum>& owner,
                           const std::map<int, BlockSum>& here) {
    std::set<int> still;
    const BlockSum none;
    for (auto it = settled.begin(); it != settled.end();) {
        const auto o = owner.find(it->first);
        const auto h = here.find(it->first);
        const BlockSum& ownerSum = o == owner.end() ? none : o->second;
        const BlockSum& hereSum = h == here.end() ? none : h->second;
        if (ownerSum != it->second.owner || (it->second.hereKnown && hereSum != it->second.here)) {
            it = settled.erase(it);
            continue;
        }
        it->second.here = hereSum;
        it->second.hereKnown = true;
        still.insert(it->first);
        ++it;
    }
    return still;
}

bool ParseInts(const std::string& s, std::vector<int>& out) {
    std::vector<int> got;
    for (const std::string& f : Split(s, ',')) {
        long long v = 0;
        if (!ToInt(f, v) || v < -2147483647ll || v > 2147483647ll) return false;
        got.push_back(static_cast<int>(v));
    }
    out.insert(out.end(), got.begin(), got.end());
    return true;
}

std::string JoinInts(const std::vector<int>& v) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) out += ',';
        out += std::to_string(v[i]);
    }
    return out;
}

}  // namespace NatureSync
}  // namespace Neighbour
