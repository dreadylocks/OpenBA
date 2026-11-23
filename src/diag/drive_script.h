#pragma once
// src/diag/drive_script.h -- the input script of BANISHED_DRIVE (src/diag/drive_harness.cpp)
//
// Standard C++ only (no engine, no SDL), unit tested (tests/unit/drive_script_test.cpp).
//
// A script is text, one command per line; blank lines and lines starting with '#' are skipped.
// Coordinates are client-area POINTS of the game window (what a screenshot of the window shows,
// without the title bar), with (0, 0) the top-left corner; `f`-suffixed commands take fractions
// of the client area instead (0..1). Buttons: left (default), right, middle.
//
//   move X Y               the cursor to (X, Y)            movef FX FY
//   click X Y [button]     move, press, release            clickf FX FY [button]
//   dclick X Y             two left clicks                 dclickf FX FY
//   press [button]         button down where the cursor is
//   release [button]       button up
//   drag X0 Y0 X1 Y1 [button]   press at the first point, glide to the second, release
//   dragf FX0 FY0 FX1 FY1 [button]
//   wheel N                N notches; positive is wheel up (zooms in), negative down
//   key NAME [NAME...]     a chord: every key down, then every key up ("key ctrl s", "key n")
//   hold NAME / unhold NAME     keep a key down until unhold
//   type TEXT              the rest of the line, one key per character (shift where needed;
//                          printable ASCII that a US keyboard types)
//   wait SECONDS           do nothing for that long (fractions allowed)
//   camera X Y [DIST [YAW]]     not input: put the camera's focus on map tile (X, Y), optionally at a
//                          distance and heading (degrees), as a player would after panning there
//                          (the harness applies it; the game's own camera code moves on from there)
//   shot                   not input: in game, retail's screenshot with the UI (the quickload key's
//                          TakeScreenshot, after UI::UserInterface::Draw): a new ScreenshotN.bmp in the
//                          save folder's Screenshots
//
// Key names: a-z, 0-9, esc, enter, space, tab, back(space), delete, insert, home, end, pgup, pgdn,
// up, down, left, right, shift, rshift, ctrl, rctrl, alt, ralt, f1-f12, minus, equals, comma,
// period, slash, semicolon, apostrophe, grave, lbracket, rbracket, backslash, pause.
//
// The Runner turns a parsed script into one input Frame per game frame. A button or key is held
// for kHoldFrames frames and released for kHoldFrames before the next step, so every press and
// release lands on its own poll whatever the frame rate.
#include <cstdint>
#include <string>
#include <vector>

namespace Drive {

constexpr int kHoldFrames = 2;

// The input the game sees on one frame: the cursor in client pixels, the buttons (bit 0 left,
// 1 right, 2 middle), the wheel (+1 / -1 / 0) and the keys held, by DirectInput scan code.
struct Frame {
    int32_t x = 0;
    int32_t y = 0;
    uint8_t buttons = 0;
    int32_t wheel = 0;
    uint8_t keys[256] = {};
    // A `camera` step, on its one frame: the focus tile, and the distance / yaw when given (< 0: keep).
    bool camera = false;
    float cameraX = 0.0f;
    float cameraY = 0.0f;
    float cameraDist = -1.0f;
    float cameraYaw = -1.0f;
    bool shot = false;   // a `shot` step, on its one frame
};

// The client area, for points -> pixels and fractions.
struct Client {
    int32_t widthPx = 0;
    int32_t heightPx = 0;
    float density = 1.0f;   // pixels per point
};

enum class Op { Move, Press, Release, Wheel, KeyDown, KeyUp, Wait, Camera, Shot };

// One primitive step. Moves carry the target in points (or fractions when `fraction`).
struct Step {
    Op op = Op::Wait;
    float x = 0.0f;
    float y = 0.0f;
    bool fraction = false;
    int glideFrames = 0;    // Move: frames to glide there (0 = jump)
    int button = 0;         // Press / Release: 0 left, 1 right, 2 middle
    int notches = 0;        // Wheel
    uint8_t key = 0;        // KeyDown / KeyUp: DIK code
    double seconds = 0.0;   // Wait
    float dist = -1.0f;     // Camera: distance, < 0 = keep
    float yaw = -1.0f;      // Camera: heading in degrees, < 0 = keep
    int line = 0;           // the script line it came from (1-based)
};

struct Parsed {
    std::vector<Step> steps;
    std::vector<std::string> errors;   // "line N: why"; a script with errors is not run
    int commands = 0;
};

Parsed Parse(const std::string& text);

// The DIK code for a key name, 0 when unknown.
uint8_t KeyCode(const std::string& name);
// The DIK code and whether shift is needed for a typed character, false when it cannot be typed.
bool CharKey(char c, uint8_t* dik, bool* shift);

// Plays steps, one Frame per call. Waits are measured on the clock the caller passes (seconds).
class Runner {
public:
    void Start(const Parsed& parsed);
    bool Running() const { return _index < _steps.size(); }
    // The input for this frame. `now` is seconds on any monotonic clock.
    Frame Next(const Client& client, double now);
    // The line of the step being played, 0 when idle.
    int CurrentLine() const;

private:
    void Begin(const Client& client, double now);
    std::vector<Step> _steps;
    size_t _index = 0;
    bool _begun = false;
    int _framesLeft = 0;
    double _waitUntil = 0.0;
    int32_t _fromX = 0;
    int32_t _fromY = 0;
    int32_t _toX = 0;
    int32_t _toY = 0;
    int _glideTotal = 0;
    Frame _held;   // the cursor, buttons and keys as they stand
    bool _placed = false;   // the cursor has a place: the middle of the window until a move says otherwise
};

}  // namespace Drive
