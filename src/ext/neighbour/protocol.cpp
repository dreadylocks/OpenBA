// ============================================================================
// src/ext/neighbour/protocol.cpp -- NOT RETAIL. ext.neighbour's wire messages (protocol.h)
// ============================================================================
// Standard C++ only (the unit tests link it alone). No exceptions: every failure is a
// return value.
// ============================================================================

#include "ext/neighbour/protocol.h"

#include <cstring>

namespace Neighbour {

// ---------------------------------------------------------------------------
// Message
// ---------------------------------------------------------------------------

Message::Field* Message::Find(const std::string& key) {
    for (Field& f : fields_)
        if (f.key == key) return &f;
    return nullptr;
}
const Message::Field* Message::Find(const std::string& key) const {
    for (const Field& f : fields_)
        if (f.key == key) return &f;
    return nullptr;
}

Message& Message::Set(const std::string& key, const std::string& value) {
    Field* f = Find(key);
    if (f == nullptr) { fields_.push_back(Field{key, Kind::String, {}, 0}); f = &fields_.back(); }
    f->kind = Kind::String; f->s = value; f->i = 0;
    return *this;
}
Message& Message::SetInt(const std::string& key, int64_t value) {
    Field* f = Find(key);
    if (f == nullptr) { fields_.push_back(Field{key, Kind::Int, {}, 0}); f = &fields_.back(); }
    f->kind = Kind::Int; f->s.clear(); f->i = value;
    return *this;
}
Message& Message::SetBool(const std::string& key, bool value) {
    Field* f = Find(key);
    if (f == nullptr) { fields_.push_back(Field{key, Kind::Bool, {}, 0}); f = &fields_.back(); }
    f->kind = Kind::Bool; f->s.clear(); f->i = value ? 1 : 0;
    return *this;
}

bool Message::Get(const std::string& key, std::string& out) const {
    const Field* f = Find(key);
    if (f == nullptr || f->kind != Kind::String) return false;
    out = f->s;
    return true;
}
bool Message::GetInt(const std::string& key, int64_t& out) const {
    const Field* f = Find(key);
    if (f == nullptr || f->kind != Kind::Int) return false;
    out = f->i;
    return true;
}
bool Message::GetBool(const std::string& key, bool& out) const {
    const Field* f = Find(key);
    if (f == nullptr || f->kind != Kind::Bool) return false;
    out = f->i != 0;
    return true;
}
std::string Message::GetOr(const std::string& key, const std::string& fallback) const {
    std::string v;
    return Get(key, v) ? v : fallback;
}

namespace {

void AppendQuoted(std::string& out, const std::string& s) {
    static const char kHex[] = "0123456789abcdef";
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (c < 0x20) {
                out += "\\u00";
                out += kHex[c >> 4];
                out += kHex[c & 15];
            } else {
                out += static_cast<char>(c);   // UTF-8 bytes pass through
            }
        }
    }
    out += '"';
}

}  // namespace

std::string Message::Encode() const {
    std::string out = "{\"t\":";
    AppendQuoted(out, type);
    for (const Field& f : fields_) {
        out += ',';
        AppendQuoted(out, f.key);
        out += ':';
        switch (f.kind) {
        case Kind::String: AppendQuoted(out, f.s); break;
        case Kind::Int:    out += std::to_string(f.i); break;
        case Kind::Bool:   out += f.i ? "true" : "false"; break;
        }
    }
    out += '}';
    return out;
}

// ---------------------------------------------------------------------------
// Parse: a bounded recursive-descent reader over one object.
// ---------------------------------------------------------------------------

namespace {

constexpr int kMaxDepth  = 16;
constexpr int kMaxFields = 64;

struct Cursor {
    const char* p;
    const char* end;

    void Ws() { while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p; }
    bool Eat(char c) { Ws(); if (p < end && *p == c) { ++p; return true; } return false; }
    bool Literal(const char* lit) {
        const size_t n = std::strlen(lit);
        if (static_cast<size_t>(end - p) < n || std::memcmp(p, lit, n) != 0) return false;
        p += n;
        return true;
    }
};

int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool Hex4(Cursor& c, uint32_t& v) {
    if (c.end - c.p < 4) return false;
    v = 0;
    for (int i = 0; i < 4; ++i) {
        const int d = HexDigit(c.p[i]);
        if (d < 0) return false;
        v = (v << 4) | static_cast<uint32_t>(d);
    }
    c.p += 4;
    return true;
}

void AppendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// A string, the opening quote not yet eaten.
bool ParseString(Cursor& c, std::string& out) {
    out.clear();
    if (!c.Eat('"')) return false;
    while (c.p < c.end) {
        const char ch = *c.p++;
        if (ch == '"') return true;
        if (static_cast<unsigned char>(ch) < 0x20) return false;   // raw control character
        if (ch != '\\') { out += ch; continue; }
        if (c.p >= c.end) return false;
        const char e = *c.p++;
        switch (e) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
            uint32_t cp = 0;
            if (!Hex4(c, cp)) return false;
            if (cp >= 0xD800 && cp <= 0xDBFF) {   // a surrogate pair
                uint32_t lo = 0;
                if (!c.Literal("\\u") || !Hex4(c, lo) || lo < 0xDC00 || lo > 0xDFFF) return false;
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                return false;   // a lone low surrogate
            }
            AppendUtf8(out, cp);
            break;
        }
        default: return false;
        }
    }
    return false;   // unterminated
}

// A number token. `isInt` and `value` are set when it is a plain integer in int64 range.
bool ParseNumber(Cursor& c, bool& isInt, int64_t& value) {
    const char* start = c.p;
    bool neg = false;
    if (c.p < c.end && *c.p == '-') { neg = true; ++c.p; }
    if (c.p >= c.end || *c.p < '0' || *c.p > '9') return false;
    uint64_t mag = 0;
    bool overflow = false;
    while (c.p < c.end && *c.p >= '0' && *c.p <= '9') {
        const uint64_t d = static_cast<uint64_t>(*c.p - '0');
        if (mag > (UINT64_C(0x7FFFFFFFFFFFFFFF) - d) / 10) overflow = true;
        else mag = mag * 10 + d;
        ++c.p;
    }
    isInt = !overflow;
    if (c.p < c.end && *c.p == '.') {
        isInt = false;
        ++c.p;
        if (c.p >= c.end || *c.p < '0' || *c.p > '9') return false;
        while (c.p < c.end && *c.p >= '0' && *c.p <= '9') ++c.p;
    }
    if (c.p < c.end && (*c.p == 'e' || *c.p == 'E')) {
        isInt = false;
        ++c.p;
        if (c.p < c.end && (*c.p == '+' || *c.p == '-')) ++c.p;
        if (c.p >= c.end || *c.p < '0' || *c.p > '9') return false;
        while (c.p < c.end && *c.p >= '0' && *c.p <= '9') ++c.p;
    }
    if (c.p == start) return false;
    value = neg ? -static_cast<int64_t>(mag) : static_cast<int64_t>(mag);
    return true;
}

bool SkipValue(Cursor& c, int depth);

bool SkipContainer(Cursor& c, int depth, char open, char close) {
    if (depth > kMaxDepth || !c.Eat(open)) return false;
    if (c.Eat(close)) return true;
    for (;;) {
        if (open == '{') {
            std::string key;
            c.Ws();
            if (!ParseString(c, key) || !c.Eat(':')) return false;
        }
        if (!SkipValue(c, depth + 1)) return false;
        if (c.Eat(',')) continue;
        return c.Eat(close);
    }
}

bool SkipValue(Cursor& c, int depth) {
    c.Ws();
    if (c.p >= c.end) return false;
    switch (*c.p) {
    case '"': { std::string s; return ParseString(c, s); }
    case '{': return SkipContainer(c, depth, '{', '}');
    case '[': return SkipContainer(c, depth, '[', ']');
    case 't': return c.Literal("true");
    case 'f': return c.Literal("false");
    case 'n': return c.Literal("null");
    default: { bool isInt = false; int64_t v = 0; return ParseNumber(c, isInt, v); }
    }
}

}  // namespace

bool Parse(const std::string& text, Message& out) {
    if (text.size() > kMaxMessageBytes) return false;
    Message msg;
    bool haveType = false;
    Cursor c{text.data(), text.data() + text.size()};
    if (!c.Eat('{')) return false;
    if (!c.Eat('}')) {
        int fields = 0;
        for (;;) {
            if (++fields > kMaxFields) return false;
            std::string key;
            c.Ws();
            if (!ParseString(c, key) || !c.Eat(':')) return false;
            c.Ws();
            if (c.p >= c.end) return false;
            const char ch = *c.p;
            if (ch == '"') {
                std::string v;
                if (!ParseString(c, v)) return false;
                if (key == "t") { msg.type = v; haveType = true; }
                else if (msg.Find(key) == nullptr) msg.Set(key, v);
            } else if (ch == 't' || ch == 'f') {
                const bool v = ch == 't';
                if (!c.Literal(v ? "true" : "false")) return false;
                if (key != "t" && msg.Find(key) == nullptr) msg.SetBool(key, v);
            } else if (ch == '-' || (ch >= '0' && ch <= '9')) {
                bool isInt = false;
                int64_t v = 0;
                if (!ParseNumber(c, isInt, v)) return false;
                if (isInt && key != "t" && msg.Find(key) == nullptr) msg.SetInt(key, v);
            } else {
                if (!SkipValue(c, 1)) return false;   // null, nested: skipped
            }
            if (c.Eat(',')) continue;
            if (c.Eat('}')) break;
            return false;
        }
    }
    c.Ws();
    if (c.p != c.end || !haveType || msg.type.empty()) return false;
    out = std::move(msg);
    return true;
}

// ---------------------------------------------------------------------------
// Sanitize
// ---------------------------------------------------------------------------

std::string Sanitize(const std::string& text, size_t maxChars) {
    std::string out;
    bool pendingSpace = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        char put;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!out.empty()) pendingSpace = true;
            continue;
        } else if (c < 0x20 || c == 0x7F) {
            continue;                                       // other control characters: dropped
        } else if (c >= 0x80) {
            // One '?' per UTF-8 sequence: skip its continuation bytes.
            while (i + 1 < text.size() && (static_cast<unsigned char>(text[i + 1]) & 0xC0) == 0x80) ++i;
            put = '?';
        } else {
            put = static_cast<char>(c);
        }
        if (pendingSpace) {
            if (out.size() + 1 >= maxChars) break;
            out += ' ';
            pendingSpace = false;
        }
        if (out.size() >= maxChars) break;
        out += put;
    }
    return out;
}

std::string RoomUrl(const std::string& server, const std::string& room) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string out = server;
    while (!out.empty() && out.back() == '/') out.pop_back();
    out += "/room/";
    for (unsigned char c : room) {
        const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                           c == '-' || c == '_' || c == '.' || c == '~';
        if (plain) {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 15];
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Parcel contents
// ---------------------------------------------------------------------------

namespace {

// A whole decimal integer in [lo, hi]; nothing else in the text.
bool ParseIntIn(const std::string& s, int lo, int hi, int& out) {
    if (s.empty() || s.size() > 9) return false;
    int v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
    }
    if (v < lo || v > hi) return false;
    out = v;
    return true;
}

std::vector<std::string> Split(const std::string& s, char sep) {
    std::vector<std::string> parts;
    size_t start = 0;
    for (;;) {
        const size_t at = s.find(sep, start);
        parts.push_back(s.substr(start, at == std::string::npos ? std::string::npos : at - start));
        if (at == std::string::npos) break;
        start = at + 1;
    }
    return parts;
}

}  // namespace

std::string EncodeGoods(const std::vector<std::pair<std::string, int>>& goods) {
    std::string out;
    for (const auto& g : goods) {
        if (!out.empty()) out += ',';
        out += g.first;
        out += ':';
        out += std::to_string(g.second);
    }
    return out;
}

bool DecodeGoods(const std::string& text, std::vector<std::pair<std::string, int>>& out) {
    std::vector<std::pair<std::string, int>> goods;
    if (text.empty()) return false;
    const std::vector<std::string> lines = Split(text, ',');
    if (static_cast<int>(lines.size()) > kMaxParcelLines) return false;
    for (const std::string& line : lines) {
        const size_t colon = line.rfind(':');
        if (colon == std::string::npos || colon == 0 || colon > 255) return false;
        const std::string path = line.substr(0, colon);
        for (unsigned char c : path)
            if (c < 0x20 || c == 0x7F) return false;   // a path is printable text
        int count = 0;
        if (!ParseIntIn(line.substr(colon + 1), 1, kMaxParcelCount, count)) return false;
        goods.emplace_back(path, count);
    }
    out = std::move(goods);
    return true;
}

std::string EncodeMembers(const std::vector<FamilyMember>& members) {
    std::string out;
    for (const FamilyMember& m : members) {
        if (!out.empty()) out += ';';
        // Ages in tenths of a year, as an integer: no locale, no float formatting.
        int tenths = static_cast<int>(m.age * 10.0f + 0.5f);
        if (tenths < 0) tenths = 0;
        out += std::to_string(tenths / 10) + "." + std::to_string(tenths % 10);
        out += '/';
        out += m.gender ? '1' : '0';
        out += '/';
        out += m.child ? '1' : '0';
    }
    return out;
}

bool DecodeMembers(const std::string& text, std::vector<FamilyMember>& out) {
    std::vector<FamilyMember> members;
    if (text.empty()) return false;
    const std::vector<std::string> parts = Split(text, ';');
    if (static_cast<int>(parts.size()) > kMaxFamily) return false;
    for (const std::string& part : parts) {
        const std::vector<std::string> f = Split(part, '/');
        if (f.size() != 3) return false;
        const size_t dot = f[0].find('.');
        int whole = 0, tenth = 0;
        if (dot == std::string::npos) {
            if (!ParseIntIn(f[0], 0, 200, whole)) return false;
        } else {
            if (!ParseIntIn(f[0].substr(0, dot), 0, 200, whole) || !ParseIntIn(f[0].substr(dot + 1), 0, 9, tenth))
                return false;
        }
        int gender = 0, child = 0;
        if (!ParseIntIn(f[1], 0, 1, gender) || !ParseIntIn(f[2], 0, 1, child)) return false;
        FamilyMember m;
        m.age = static_cast<float>(whole) + static_cast<float>(tenth) / 10.0f;
        m.gender = gender;
        m.child = child != 0;
        members.push_back(m);
    }
    bool anyAdult = false;
    for (const FamilyMember& m : members) anyAdult |= !m.child;
    if (!anyAdult) return false;   // a family has an adult
    out = std::move(members);
    return true;
}

// ---------------------------------------------------------------------------
// The save payload
// ---------------------------------------------------------------------------

namespace {

void PutU32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
void PutU64(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

struct ByteReader {
    const uint8_t* p;
    size_t         left;
    bool U32(uint32_t& v) {
        if (left < 4) return false;
        v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(p[i]) << (8 * i);
        p += 4; left -= 4;
        return true;
    }
    bool U64(uint64_t& v) {
        if (left < 8) return false;
        v = 0;
        for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(p[i]) << (8 * i);
        p += 8; left -= 8;
        return true;
    }
    bool Str(std::string& s, size_t maxLen) {
        uint32_t n = 0;
        if (!U32(n) || n > maxLen || left < n) return false;
        s.assign(p, p + n);
        p += n; left -= n;
        return true;
    }
};

constexpr size_t kMaxRoomChars = 64;

}  // namespace

bool ValidParcelId(const std::string& id) {
    if (id.empty() || id.size() > kMaxParcelIdChars) return false;
    for (char c : id) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

namespace {

void PutStr(std::vector<uint8_t>& out, const std::string& s) {
    PutU32(out, static_cast<uint32_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}
void PutParcels(std::vector<uint8_t>& out, const std::vector<Parcel>& list) {
    const size_t n = list.size() < kMaxSavedParcels ? list.size() : kMaxSavedParcels;
    PutU32(out, static_cast<uint32_t>(n));
    for (size_t i = list.size() - n; i < list.size(); ++i) {   // the newest kMaxSavedParcels
        PutStr(out, list[i].id);
        PutStr(out, list[i].kind);
        PutStr(out, list[i].body);
        PutStr(out, list[i].from);
    }
}
bool GetParcels(ByteReader& r, std::vector<Parcel>& list) {
    uint32_t n = 0;
    if (!r.U32(n) || n > kMaxSavedParcels) return false;
    for (uint32_t i = 0; i < n; ++i) {
        Parcel p;
        if (!r.Str(p.id, kMaxParcelIdChars) || !r.Str(p.kind, 16) || !r.Str(p.body, kMaxMessageBytes) ||
            !r.Str(p.from, 64))
            return false;
        if (!ValidParcelId(p.id) || (p.kind != "goods" && p.kind != "family")) return false;
        list.push_back(std::move(p));
    }
    return true;
}

}  // namespace

std::vector<uint8_t> EncodeState(const SavedState& s) {
    std::vector<uint8_t> out;
    const std::string room = s.room.size() > kMaxRoomChars ? s.room.substr(0, kMaxRoomChars) : s.room;
    PutStr(out, room);
    PutU64(out, static_cast<uint64_t>(s.lastSeq));
    // v2
    PutParcels(out, s.outbox);
    const size_t n = s.received.size() < kKeepReceivedIds ? s.received.size() : kKeepReceivedIds;
    PutU32(out, static_cast<uint32_t>(n));
    for (size_t i = s.received.size() - n; i < s.received.size(); ++i) PutStr(out, s.received[i]);
    PutParcels(out, s.waiting);
    return out;
}

bool DecodeState(int32_t formatVersion, const std::vector<uint8_t>& bytes, SavedState& out) {
    if (formatVersion < 1 || formatVersion > kPayloadFormat) return false;
    ByteReader r{bytes.data(), bytes.size()};
    SavedState s;
    uint64_t seq = 0;
    if (!r.Str(s.room, kMaxRoomChars) || !r.U64(seq)) return false;
    if (seq > static_cast<uint64_t>(INT64_MAX)) return false;
    s.lastSeq = static_cast<int64_t>(seq);
    if (formatVersion >= 2) {
        uint32_t n = 0;
        if (!GetParcels(r, s.outbox) || !r.U32(n) || n > kKeepReceivedIds) return false;
        for (uint32_t i = 0; i < n; ++i) {
            std::string id;
            if (!r.Str(id, kMaxParcelIdChars) || !ValidParcelId(id)) return false;
            s.received.push_back(std::move(id));
        }
        if (!GetParcels(r, s.waiting)) return false;
    }
    if (r.left != 0) return false;
    out = std::move(s);
    return true;
}

}  // namespace Neighbour
