// ============================================================================
// src/engine/object/expand_memo.cpp -- the ExpandRelativePath memo
//                                       (block SAVELOAD, NOT RETAIL, exact)
// ============================================================================
// The proof is in expand_memo.h. Plain std containers on the CRT heap: the
// memo never hands out or holds a game-allocator String.
// ============================================================================

#include "object/expand_memo.h"

#include <cstring>
#include <string>
#include <unordered_map>

namespace RtObject {

namespace {

// More distinct requests than this only means something is generating names;
// drop everything rather than grow. (Measured, block PATHINDEX: the retail game
// asks for ~20k, the seven-part MegaMod between 262k and 524k; the old 65,536
// bound emptied the memo over and over during a MegaMod load.)
constexpr size_t kMaxEntries = 1u << 20;

struct Memo {
    // An EMPTY value means "the answer is the request's own characters", which is
    // what nearly every request gets (measured: all but 4 of 262,144 under
    // MegaMod). An answer is never empty (it holds at least the '.'), so the
    // two cannot be confused.
    std::unordered_map<std::string, std::string> entries;
    std::string key;             // reused scratch: no allocation per lookup
};

// Never destroyed: a loader thread still inside FindOrLoad at process exit must
// not meet a destructed map.
Memo& State() {
    static Memo* const m = new Memo;
    return *m;
}

void BuildKey(Memo& m, const char* path, int len, bool appendIdentifier) {
    m.key.assign(path, static_cast<size_t>(len));
    m.key.push_back(static_cast<char>(appendIdentifier ? 1 : 0));
}

}  // namespace

const char* ExpandMemoFind(const char* path, int len, bool appendIdentifier) {
    Memo& m = State();
    if (m.entries.empty())
        return nullptr;
    BuildKey(m, path, len, appendIdentifier);
    auto it = m.entries.find(m.key);
    if (it == m.entries.end())
        return nullptr;
    return it->second.empty() ? path : it->second.c_str();
}

void ExpandMemoStore(const char* path, int len, bool appendIdentifier, const char* result, int resultLen) {
    Memo& m = State();
    if (m.entries.size() >= kMaxEntries)
        m.entries.clear();
    BuildKey(m, path, len, appendIdentifier);
    std::string& value = m.entries[m.key];
    if (resultLen == len && std::memcmp(result, path, static_cast<size_t>(len)) == 0)
        value.clear();
    else
        value.assign(result, static_cast<size_t>(resultLen));
}

size_t ExpandMemoSize() {
    return State().entries.size();
}

void ExpandMemoClear() {
    State().entries.clear();
}

}  // namespace RtObject
