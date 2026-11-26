// src/ext/auto_update/manifest.cpp -- NOT RETAIL. ext.auto_update: versions and the manifest
// (manifest.h; docs/fork/AUTO_UPDATE.md §4-§5).
#include "ext/auto_update/manifest.h"

#include "monocypher-ed25519.h"

#include <cstring>
#include <set>
#include <vector>

namespace AutoUpdate {

namespace {

bool IsDigit(char c) {
    return c >= '0' && c <= '9';
}

bool ParseNumber(const std::string& s, size_t& at, uint32_t& out) {
    const size_t start = at;
    uint32_t     v = 0;
    while (at < s.size() && IsDigit(s[at])) {
        if (at - start >= 6) return false;
        v = v * 10 + uint32_t(s[at] - '0');
        ++at;
    }
    if (at == start) return false;
    out = v;
    return true;
}

std::vector<std::string> SplitDots(const std::string& s) {
    std::vector<std::string> parts;
    size_t                   start = 0;
    for (;;) {
        const size_t dot = s.find('.', start);
        parts.push_back(s.substr(start, dot == std::string::npos ? std::string::npos : dot - start));
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    return parts;
}

bool AllDigits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!IsDigit(c)) return false;
    }
    return true;
}

// semver §11: identifiers compared one by one; numeric ones numerically and below alphanumeric
// ones; a shorter list that matches so far sorts first.
int CompareSuffixes(const std::string& a, const std::string& b) {
    const std::vector<std::string> pa = SplitDots(a);
    const std::vector<std::string> pb = SplitDots(b);
    for (size_t i = 0; i < pa.size() && i < pb.size(); ++i) {
        const bool na = AllDigits(pa[i]);
        const bool nb = AllDigits(pb[i]);
        if (na && nb) {
            if (pa[i].size() != pb[i].size()) return pa[i].size() < pb[i].size() ? -1 : 1;
            const int c = pa[i].compare(pb[i]);
            if (c != 0) return c < 0 ? -1 : 1;
        } else if (na != nb) {
            return na ? -1 : 1;
        } else {
            const int c = pa[i].compare(pb[i]);
            if (c != 0) return c < 0 ? -1 : 1;
        }
    }
    if (pa.size() == pb.size()) return 0;
    return pa.size() < pb.size() ? -1 : 1;
}

bool IsLowerHex(const std::string& s, size_t n) {
    if (s.size() != n) return false;
    for (char c : s) {
        if (!IsDigit(c) && !(c >= 'a' && c <= 'f')) return false;
    }
    return true;
}

int HexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool PrintableValue(const std::string& v) {
    for (char c : v) {
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) return false;
    }
    return true;
}

}  // namespace

bool ParseVersion(const std::string& text, Version& out) {
    Version v;
    size_t  at = 0;
    if (!ParseNumber(text, at, v.major) || at >= text.size() || text[at] != '.') return false;
    ++at;
    if (!ParseNumber(text, at, v.minor) || at >= text.size() || text[at] != '.') return false;
    ++at;
    if (!ParseNumber(text, at, v.patch)) return false;
    if (at < text.size()) {
        if (text[at] != '-') return false;
        v.suffix = text.substr(at + 1);
        if (v.suffix.empty() || v.suffix.size() > 32) return false;
        for (char c : v.suffix) {
            const bool ok = IsDigit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '-';
            if (!ok) return false;
        }
        for (const std::string& part : SplitDots(v.suffix)) {
            if (part.empty()) return false;
        }
    }
    out = v;
    return true;
}

int CompareVersions(const Version& a, const Version& b) {
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    if (a.suffix.empty() || b.suffix.empty()) {
        if (a.suffix.empty() && b.suffix.empty()) return 0;
        return a.suffix.empty() ? 1 : -1;   // 1.2.0-rc.1 < 1.2.0
    }
    return CompareSuffixes(a.suffix, b.suffix);
}

bool IsNewer(const std::string& candidate, const std::string& current) {
    Version a;
    Version b;
    if (!ParseVersion(candidate, a) || !ParseVersion(current, b)) return false;
    return CompareVersions(a, b) > 0;
}

bool FromHex(const std::string& hex, uint8_t* out, size_t n) {
    if (hex.size() != 2 * n) return false;
    for (size_t i = 0; i < n; ++i) {
        const int hi = HexValue(hex[2 * i]);
        const int lo = HexValue(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = uint8_t(hi * 16 + lo);
    }
    return true;
}

std::string ToHex(const uint8_t* p, size_t n) {
    static const char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(2 * n);
    for (size_t i = 0; i < n; ++i) {
        out += kDigits[p[i] >> 4];
        out += kDigits[p[i] & 15];
    }
    return out;
}

bool ParseManifest(const std::string& text, Manifest& out, std::string& error) {
    error.clear();
    if (text.size() > kMaxManifestBytes) {
        error = "the manifest is too long";
        return false;
    }
    Manifest              m;
    std::set<std::string> seen;
    size_t                at = 0;
    bool                  first = true;
    bool                  signatureSeen = false;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        const size_t next = end == std::string::npos ? text.size() : end + 1;
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t lineStart = at;
        at = next;
        if (signatureSeen) {
            if (!line.empty()) {
                error = "text after the signature";
                return false;
            }
            continue;
        }
        if (first) {
            first = false;
            const std::string magic = "openba-update ";
            if (line.compare(0, magic.size(), magic) != 0) {
                error = "not an OpenBA update manifest";
                return false;
            }
            const std::string format = line.substr(magic.size());
            if (!AllDigits(format) || format.size() > 4) {
                error = "bad format line";
                return false;
            }
            m.format = std::stoi(format);
            if (m.format != kManifestFormat) {
                error = "unknown format " + format;
                return false;
            }
            continue;
        }
        if (line.empty()) {
            error = "empty line";
            return false;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos || eq == 0) {
            error = "a line without key=value";
            return false;
        }
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (!PrintableValue(value)) {
            error = "a control character in " + key;
            return false;
        }
        if (!seen.insert(key).second) {
            error = "the key " + key + " is given twice";
            return false;
        }
        if (key == "signature") {
            m.signature = value;
            m.signedBytes = lineStart;
            signatureSeen = true;
        } else if (key == "version") {
            m.version = value;
        } else if (key == "commit") {
            m.commit = value;
        } else if (key == "platform") {
            m.platform = value;
        } else if (key == "url") {
            m.url = value;
        } else if (key == "sha256") {
            m.sha256 = value;
        } else if (key == "size") {
            if (!AllDigits(value) || value.size() > 12) {
                error = "bad size";
                return false;
            }
            m.size = std::stoull(value);
        }
    }
    if (first) {
        error = "empty manifest";
        return false;
    }
    Version v;
    if (!ParseVersion(m.version, v)) {
        error = "bad or missing version";
        return false;
    }
    if (m.platform.empty()) {
        error = "missing platform";
        return false;
    }
    if (m.url.compare(0, 8, "https://") != 0 && m.url.compare(0, 7, "http://") != 0) {
        error = "the package address must start with https:// or http://";
        return false;
    }
    if (m.size == 0 || m.size > kMaxPackageBytes) {
        error = "bad or missing size";
        return false;
    }
    if (!IsLowerHex(m.sha256, 64)) {
        error = "bad or missing sha256";
        return false;
    }
    if (!signatureSeen) {
        error = "missing signature";
        return false;
    }
    if (!IsLowerHex(m.signature, 128)) {
        error = "bad signature";
        return false;
    }
    out = m;
    return true;
}

bool VerifyManifest(const std::string& text, const Manifest& m, const uint8_t publicKey[32], std::string& error) {
    uint8_t sig[64];
    if (!FromHex(m.signature, sig, sizeof(sig)) || m.signedBytes > text.size()) {
        error = "bad signature";
        return false;
    }
    const int bad = crypto_ed25519_check(sig, publicKey, reinterpret_cast<const uint8_t*>(text.data()), m.signedBytes);
    if (bad != 0) {
        error = "the signature does not match this game's update key";
        return false;
    }
    error.clear();
    return true;
}

std::string ManifestBody(const Manifest& m) {
    std::string out = "openba-update " + std::to_string(kManifestFormat) + "\n";
    out += "version=" + m.version + "\n";
    out += "commit=" + m.commit + "\n";
    out += "platform=" + m.platform + "\n";
    out += "url=" + m.url + "\n";
    out += "size=" + std::to_string(m.size) + "\n";
    out += "sha256=" + m.sha256 + "\n";
    return out;
}

std::string SignManifest(const std::string& body, const uint8_t secretKey[64]) {
    uint8_t sig[64];
    crypto_ed25519_sign(sig, secretKey, reinterpret_cast<const uint8_t*>(body.data()), body.size());
    return body + "signature=" + ToHex(sig, sizeof(sig)) + "\n";
}

}  // namespace AutoUpdate
