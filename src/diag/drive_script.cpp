// src/diag/drive_script.cpp -- the input script of BANISHED_DRIVE (drive_script.h has the language)
#include "drive_script.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace Drive {

namespace {

// Frames a cursor glides over in a drag, so the game sees the motion in between.
constexpr int kDragGlideFrames = 20;

struct Named {
    const char* name;
    uint8_t dik;
};

// DirectInput scan codes (dinput.h DIK_*), the bytes Platform::Input::ReadKeyboard fills.
constexpr Named kKeys[] = {
    {"esc", 0x01},       {"escape", 0x01},    {"1", 0x02},          {"2", 0x03},
    {"3", 0x04},         {"4", 0x05},         {"5", 0x06},          {"6", 0x07},
    {"7", 0x08},         {"8", 0x09},         {"9", 0x0A},          {"0", 0x0B},
    {"minus", 0x0C},     {"equals", 0x0D},    {"back", 0x0E},       {"backspace", 0x0E},
    {"tab", 0x0F},       {"q", 0x10},         {"w", 0x11},          {"e", 0x12},
    {"r", 0x13},         {"t", 0x14},         {"y", 0x15},          {"u", 0x16},
    {"i", 0x17},         {"o", 0x18},         {"p", 0x19},          {"lbracket", 0x1A},
    {"rbracket", 0x1B},  {"enter", 0x1C},     {"return", 0x1C},     {"ctrl", 0x1D},
    {"a", 0x1E},         {"s", 0x1F},         {"d", 0x20},          {"f", 0x21},
    {"g", 0x22},         {"h", 0x23},         {"j", 0x24},          {"k", 0x25},
    {"l", 0x26},         {"semicolon", 0x27}, {"apostrophe", 0x28}, {"grave", 0x29},
    {"shift", 0x2A},     {"backslash", 0x2B}, {"z", 0x2C},          {"x", 0x2D},
    {"c", 0x2E},         {"v", 0x2F},         {"b", 0x30},          {"n", 0x31},
    {"m", 0x32},         {"comma", 0x33},     {"period", 0x34},     {"slash", 0x35},
    {"rshift", 0x36},    {"alt", 0x38},       {"space", 0x39},      {"f1", 0x3B},
    {"f2", 0x3C},        {"f3", 0x3D},        {"f4", 0x3E},         {"f5", 0x3F},
    {"f6", 0x40},        {"f7", 0x41},        {"f8", 0x42},         {"f9", 0x43},
    {"f10", 0x44},       {"f11", 0x57},       {"f12", 0x58},        {"rctrl", 0x9D},
    {"ralt", 0xB8},      {"pause", 0xC5},     {"home", 0xC7},       {"up", 0xC8},
    {"pgup", 0xC9},      {"left", 0xCB},      {"right", 0xCD},      {"end", 0xCF},
    {"down", 0xD0},      {"pgdn", 0xD1},      {"insert", 0xD2},     {"delete", 0xD3},
};

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool Number(const std::string& token, double* out) {
    if (token.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(token.c_str(), &end);
    if (end == nullptr || *end != '\0' || !std::isfinite(v)) return false;
    *out = v;
    return true;
}

// left / right / middle, or none given: left. -1 when it names no button.
int ButtonOf(const std::vector<std::string>& tokens, size_t at) {
    if (at >= tokens.size()) return 0;
    const std::string b = Lower(tokens[at]);
    if (b == "left" || b == "l") return 0;
    if (b == "right" || b == "r") return 1;
    if (b == "middle" || b == "m") return 2;
    return -1;
}

Step Make(Op op, int line) {
    Step s;
    s.op = op;
    s.line = line;
    return s;
}

}  // namespace

uint8_t KeyCode(const std::string& name) {
    const std::string n = Lower(name);
    for (const Named& k : kKeys) {
        if (n == k.name) return k.dik;
    }
    return 0;
}

bool CharKey(char c, uint8_t* dik, bool* shift) {
    static const char* const kPlain = "1234567890-=qwertyuiop[]asdfghjkl;'`\\zxcvbnm,./ ";
    static const char* const kShift = "!@#$%^&*()_+QWERTYUIOP{}ASDFGHJKL:\"~|ZXCVBNM<>? ";
    static const uint8_t kCodes[] = {
        0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,   // 1..0 - =
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B,   // q..p [ ]
        0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29,   // a..l ; ' `
        0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x39,   // \ z..m , . / space
    };
    for (int i = 0; kPlain[i] != '\0'; ++i) {
        if (c == kPlain[i]) {
            *dik = kCodes[i];
            *shift = false;
            return true;
        }
        if (c == kShift[i] && c != ' ') {
            *dik = kCodes[i];
            *shift = true;
            return true;
        }
    }
    return false;
}

Parsed Parse(const std::string& text) {
    Parsed out;
    std::istringstream in(text);
    std::string raw;
    int line = 0;
    while (std::getline(in, raw)) {
        ++line;
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        std::istringstream words(raw);
        std::vector<std::string> t;
        for (std::string w; words >> w;) t.push_back(w);
        if (t.empty() || t[0][0] == '#') continue;
        const std::string cmd = Lower(t[0]);
        const auto fail = [&](const std::string& why) {
            out.errors.push_back("line " + std::to_string(line) + ": " + why + " (" + raw + ")");
        };
        ++out.commands;

        const bool fraction = cmd == "movef" || cmd == "clickf" || cmd == "dclickf" || cmd == "dragf";
        const std::string base = fraction ? cmd.substr(0, cmd.size() - 1) : cmd;

        if (base == "move" || base == "click" || base == "dclick") {
            double x = 0.0;
            double y = 0.0;
            if (t.size() < 3 || !Number(t[1], &x) || !Number(t[2], &y)) {
                fail("needs X Y");
                continue;
            }
            const int button = base == "click" ? ButtonOf(t, 3) : 0;
            if (button < 0) {
                fail("unknown button");
                continue;
            }
            Step m = Make(Op::Move, line);
            m.x = static_cast<float>(x);
            m.y = static_cast<float>(y);
            m.fraction = fraction;
            out.steps.push_back(m);
            const int clicks = base == "move" ? 0 : (base == "click" ? 1 : 2);
            for (int i = 0; i < clicks; ++i) {
                Step p = Make(Op::Press, line);
                p.button = button;
                out.steps.push_back(p);
                Step r = Make(Op::Release, line);
                r.button = button;
                out.steps.push_back(r);
            }
        } else if (base == "drag") {
            double v[4] = {};
            bool ok = t.size() >= 5;
            for (int i = 0; ok && i < 4; ++i) ok = Number(t[1 + i], &v[i]);
            const int button = ok ? ButtonOf(t, 5) : 0;
            if (!ok || button < 0) {
                fail("needs X0 Y0 X1 Y1 [button]");
                continue;
            }
            Step a = Make(Op::Move, line);
            a.x = static_cast<float>(v[0]);
            a.y = static_cast<float>(v[1]);
            a.fraction = fraction;
            out.steps.push_back(a);
            Step p = Make(Op::Press, line);
            p.button = button;
            out.steps.push_back(p);
            Step b = Make(Op::Move, line);
            b.x = static_cast<float>(v[2]);
            b.y = static_cast<float>(v[3]);
            b.fraction = fraction;
            b.glideFrames = kDragGlideFrames;
            out.steps.push_back(b);
            Step r = Make(Op::Release, line);
            r.button = button;
            out.steps.push_back(r);
        } else if (cmd == "press" || cmd == "release") {
            const int button = ButtonOf(t, 1);
            if (button < 0) {
                fail("unknown button");
                continue;
            }
            Step s = Make(cmd == "press" ? Op::Press : Op::Release, line);
            s.button = button;
            out.steps.push_back(s);
        } else if (cmd == "wheel") {
            double n = 0.0;
            if (t.size() < 2 || !Number(t[1], &n) || n == 0.0 || std::fabs(n) > 100.0) {
                fail("needs N, 1..100 notches either way");
                continue;
            }
            Step s = Make(Op::Wheel, line);
            s.notches = static_cast<int>(n);
            out.steps.push_back(s);
        } else if (cmd == "key" || cmd == "hold" || cmd == "unhold") {
            std::vector<uint8_t> codes;
            bool ok = t.size() >= 2;
            for (size_t i = 1; ok && i < t.size(); ++i) {
                const uint8_t k = KeyCode(t[i]);
                if (k == 0) {
                    fail("unknown key '" + t[i] + "'");
                    ok = false;
                }
                codes.push_back(k);
            }
            if (!ok) {
                if (t.size() < 2) fail("needs a key name");
                continue;
            }
            if (cmd != "unhold") {
                for (uint8_t k : codes) {
                    Step d = Make(Op::KeyDown, line);
                    d.key = k;
                    out.steps.push_back(d);
                }
            }
            if (cmd != "hold") {
                for (auto it = codes.rbegin(); it != codes.rend(); ++it) {
                    Step u = Make(Op::KeyUp, line);
                    u.key = *it;
                    out.steps.push_back(u);
                }
            }
        } else if (cmd == "type") {
            const size_t at = raw.find_first_not_of(" \t", raw.find_first_of(" \t", raw.find_first_not_of(" \t")));
            const std::string what = at == std::string::npos ? std::string() : raw.substr(at);
            if (what.empty()) {
                fail("needs text");
                continue;
            }
            bool ok = true;
            std::vector<Step> keys;
            for (char c : what) {
                uint8_t k = 0;
                bool shift = false;
                if (!CharKey(c, &k, &shift)) {
                    fail(std::string("cannot type '") + c + "'");
                    ok = false;
                    break;
                }
                if (shift) {
                    Step s = Make(Op::KeyDown, line);
                    s.key = 0x2A;
                    keys.push_back(s);
                }
                Step d = Make(Op::KeyDown, line);
                d.key = k;
                keys.push_back(d);
                Step u = Make(Op::KeyUp, line);
                u.key = k;
                keys.push_back(u);
                if (shift) {
                    Step s = Make(Op::KeyUp, line);
                    s.key = 0x2A;
                    keys.push_back(s);
                }
            }
            if (ok) out.steps.insert(out.steps.end(), keys.begin(), keys.end());
        } else if (cmd == "wait") {
            double s = 0.0;
            if (t.size() < 2 || !Number(t[1], &s) || s < 0.0 || s > 3600.0) {
                fail("needs SECONDS, 0..3600");
                continue;
            }
            Step w = Make(Op::Wait, line);
            w.seconds = s;
            out.steps.push_back(w);
        } else {
            fail("unknown command '" + t[0] + "'");
        }
    }
    return out;
}

void Runner::Start(const Parsed& parsed) {
    _steps = parsed.errors.empty() ? parsed.steps : std::vector<Step>();
    _index = 0;
    _begun = false;
    _framesLeft = 0;
    // The cursor stays where the last script left it; buttons and keys start released.
    _held.buttons = 0;
    _held.wheel = 0;
    std::fill(std::begin(_held.keys), std::end(_held.keys), uint8_t{0});
}

int Runner::CurrentLine() const {
    return _index < _steps.size() ? _steps[_index].line : 0;
}

void Runner::Begin(const Client& client, double now) {
    _begun = true;
    const Step& s = _steps[_index];
    switch (s.op) {
    case Op::Move: {
        const float px = s.fraction ? s.x * static_cast<float>(client.widthPx) : s.x * client.density;
        const float py = s.fraction ? s.y * static_cast<float>(client.heightPx) : s.y * client.density;
        const int32_t maxX = std::max(client.widthPx - 1, 0);
        const int32_t maxY = std::max(client.heightPx - 1, 0);
        _fromX = _held.x;
        _fromY = _held.y;
        _toX = std::min(std::max(static_cast<int32_t>(std::lround(px)), 0), maxX);
        _toY = std::min(std::max(static_cast<int32_t>(std::lround(py)), 0), maxY);
        _glideTotal = std::max(s.glideFrames, 0);
        if (_glideTotal == 0) {
            _held.x = _toX;
            _held.y = _toY;
        }
        _framesLeft = _glideTotal + kHoldFrames;   // the glide, then a settle on the target
        break;
    }
    case Op::Press:
        _held.buttons = static_cast<uint8_t>(_held.buttons | (1u << s.button));
        _framesLeft = kHoldFrames;
        break;
    case Op::Release:
        _held.buttons = static_cast<uint8_t>(_held.buttons & ~(1u << s.button));
        _framesLeft = kHoldFrames;
        break;
    case Op::Wheel:
        _framesLeft = std::abs(s.notches) * (1 + kHoldFrames);
        break;
    case Op::KeyDown:
        _held.keys[s.key] = 0x80;
        _framesLeft = kHoldFrames;
        break;
    case Op::KeyUp:
        _held.keys[s.key] = 0;
        _framesLeft = kHoldFrames;
        break;
    case Op::Wait:
        _waitUntil = now + s.seconds;
        _framesLeft = 0;
        break;
    }
}

Frame Runner::Next(const Client& client, double now) {
    for (;;) {
        if (_index >= _steps.size()) {
            Frame idle = _held;
            idle.wheel = 0;
            return idle;
        }
        if (!_begun) Begin(client, now);
        Frame f = _held;   // after Begin: a jump or a press shows on this very frame
        f.wheel = 0;
        const Step& s = _steps[_index];
        if (s.op == Op::Wait) {
            if (now < _waitUntil) return f;
            ++_index;
            _begun = false;
            continue;   // a finished wait takes no frame
        }
        if (s.op == Op::Move && _glideTotal > 0) {
            const int done = (_glideTotal + kHoldFrames) - _framesLeft + 1;
            if (done <= _glideTotal) {
                _held.x = _fromX + (_toX - _fromX) * done / _glideTotal;
                _held.y = _fromY + (_toY - _fromY) * done / _glideTotal;
            }
            f.x = _held.x;
            f.y = _held.y;
        }
        if (s.op == Op::Wheel) {
            const int total = std::abs(s.notches) * (1 + kHoldFrames);
            const int k = total - _framesLeft;
            if (k % (1 + kHoldFrames) == 0) f.wheel = s.notches > 0 ? 1 : -1;
        }
        if (--_framesLeft <= 0) {
            ++_index;
            _begun = false;
        }
        return f;
    }
}

}  // namespace Drive
