// src/ext/restricted_foods/restricted_foods.cpp -- NOT RETAIL. ext.restricted_foods (class C): the list of
// blocked foods and the flat array the seams scan. See the header and
// docs/extensions/handins/RESTRICTEDFOODS.md. Standard C++ only (the game side is restricted_foods_town.cpp).
#include "ext/restricted_foods/restricted_foods.h"

#include <algorithm>
#include <cctype>

namespace Ext {
namespace RestrictedFoods {

bool g_on = false;
std::vector<const void*> g_blocked;

namespace {

std::vector<std::string> s_paths;   // the list, in the order checked
uint32_t s_version = 1;             // advances whenever the list changes
uint32_t s_resolvedVersion = 0;     // the list version g_blocked was built from
uint32_t s_resolvedEpoch = ~0u;     // the lock epoch g_blocked was built in

std::string Fold(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

// The index of `path` in the list, or -1.
int IndexOf(const std::string& path) {
    const std::string key = Fold(path);
    for (size_t i = 0; i < s_paths.size(); ++i) {
        if (Fold(s_paths[i]) == key) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

}  // namespace

const std::vector<std::string>& BlockedPaths() {
    return s_paths;
}

bool IsPathBlocked(const std::string& path) {
    return IndexOf(path) >= 0;
}

bool SetPathBlocked(const std::string& path, bool blocked) {
    if (path.empty()) {
        return false;
    }
    const int at = IndexOf(path);
    if (blocked) {
        if (at >= 0) {
            return false;
        }
        s_paths.push_back(path);
    } else {
        if (at < 0) {
            return false;
        }
        s_paths.erase(s_paths.begin() + at);
    }
    ++s_version;
    return true;
}

void ReplaceBlockedPaths(const std::vector<std::string>& paths) {
    s_paths = paths;
    ++s_version;
}

void ResetState() {
    s_paths.clear();
    ++s_version;
    g_blocked.clear();
    g_on = false;
}

bool NeedsResolve(uint32_t epoch) {
    return s_resolvedVersion != s_version || s_resolvedEpoch != epoch;
}

void Resolve(uint32_t epoch, const std::vector<Candidate>& edibleMaterials) {
    g_blocked.clear();
    for (const Candidate& c : edibleMaterials) {
        if (IsPathBlocked(c.path)) {
            g_blocked.push_back(c.material);
        }
    }
    s_resolvedVersion = s_version;
    s_resolvedEpoch = epoch;
}

void SetRowActive(bool rowOn) {
    g_on = rowOn && !g_blocked.empty();
}

}  // namespace RestrictedFoods
}  // namespace Ext
