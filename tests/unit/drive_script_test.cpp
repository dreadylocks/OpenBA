// tests/unit/drive_script_test.cpp -- BANISHED_DRIVE's input script (src/diag/drive_script.h):
// the parser, the key tables, and the frames the runner plays.
#include "unit.h"

#include "diag/drive_script.h"

#include <string>
#include <vector>

namespace {

Drive::Client Retina() {
    Drive::Client c;
    c.widthPx = 2048;
    c.heightPx = 1280;
    c.density = 2.0f;
    return c;
}

// Plays a script to its end (at most `limit` frames, the clock advancing 1/60 s a frame).
std::vector<Drive::Frame> Play(const std::string& text, int limit = 2000) {
    const Drive::Parsed parsed = Drive::Parse(text);
    Drive::Runner r;
    r.Start(parsed);
    std::vector<Drive::Frame> frames;
    double now = 0.0;
    while (r.Running() && static_cast<int>(frames.size()) < limit) {
        frames.push_back(r.Next(Retina(), now));
        now += 1.0 / 60.0;
    }
    return frames;
}

}  // namespace

UNIT_TEST(drive_script_click_is_points_times_density_pressed_then_released) {
    const std::vector<Drive::Frame> f = Play("click 100 50\n");
    CHECK(!f.empty());
    bool sawDown = false;
    bool upAfterDown = false;
    for (const Drive::Frame& x : f) {
        CHECK_EQ(x.x, 200);
        CHECK_EQ(x.y, 100);
        if (x.buttons & 1) sawDown = true;
        if (sawDown && !(x.buttons & 1)) upAfterDown = true;
    }
    CHECK(sawDown);
    CHECK(upAfterDown);
    CHECK_EQ(static_cast<int>(f.back().buttons), 0);
    // The move settles before the press: the first frames are at the target with no button.
    CHECK_EQ(static_cast<int>(f.front().buttons), 0);
}

UNIT_TEST(drive_script_fractions_and_clamping) {
    std::vector<Drive::Frame> f = Play("movef 0.5 0.25\n");
    CHECK_EQ(f.back().x, 1024);
    CHECK_EQ(f.back().y, 320);
    f = Play("move 5000 -10\n");   // outside the window: clamped to its last pixel / 0
    CHECK_EQ(f.back().x, 2047);
    CHECK_EQ(f.back().y, 0);
}

UNIT_TEST(drive_script_drag_glides_with_the_button_held) {
    const std::vector<Drive::Frame> f = Play("drag 10 10 110 10\n");
    int heldMoving = 0;
    int lastX = -1;
    for (const Drive::Frame& x : f) {
        if ((x.buttons & 1) && lastX >= 0 && x.x != lastX) ++heldMoving;
        lastX = x.x;
    }
    CHECK(heldMoving >= 10);          // the cursor moves in steps while the button is down
    CHECK_EQ(f.back().x, 220);
    CHECK_EQ(static_cast<int>(f.back().buttons), 0);
}

UNIT_TEST(drive_script_wheel_notches_are_separated) {
    const std::vector<Drive::Frame> f = Play("wheel -3\n");
    int notches = 0;
    int prev = 0;
    for (const Drive::Frame& x : f) {
        if (x.wheel != 0 && prev == 0) ++notches;
        CHECK(x.wheel == 0 || x.wheel == -1);
        prev = x.wheel;
    }
    CHECK_EQ(notches, 3);
}

UNIT_TEST(drive_script_key_chords_and_typing) {
    CHECK_EQ(static_cast<int>(Drive::KeyCode("n")), 0x31);
    CHECK_EQ(static_cast<int>(Drive::KeyCode("Esc")), 0x01);
    CHECK_EQ(static_cast<int>(Drive::KeyCode("enter")), 0x1C);
    CHECK_EQ(static_cast<int>(Drive::KeyCode("nope")), 0);
    uint8_t k = 0;
    bool shift = false;
    CHECK(Drive::CharKey('H', &k, &shift));
    CHECK_EQ(static_cast<int>(k), 0x23);
    CHECK(shift);
    CHECK(Drive::CharKey('?', &k, &shift));
    CHECK_EQ(static_cast<int>(k), 0x35);
    CHECK(shift);
    CHECK(Drive::CharKey(' ', &k, &shift));
    CHECK(!shift);
    CHECK(!Drive::CharKey('\t', &k, &shift));

    // A chord holds both keys on one frame, and ends with every key up.
    std::vector<Drive::Frame> f = Play("key ctrl s\n");
    bool both = false;
    for (const Drive::Frame& x : f) both = both || (x.keys[0x1D] && x.keys[0x1F]);
    CHECK(both);
    CHECK_EQ(static_cast<int>(f.back().keys[0x1D]), 0);
    CHECK_EQ(static_cast<int>(f.back().keys[0x1F]), 0);

    // "Hi!" = shift+h, i, shift+1: each key goes down then up, in order.
    f = Play("type Hi!\n");
    std::vector<int> downs;
    uint8_t prev[256] = {};
    for (const Drive::Frame& x : f) {
        for (int i = 0; i < 256; ++i) {
            if (x.keys[i] && !prev[i] && i != 0x2A) downs.push_back(i);
            prev[i] = x.keys[i];
        }
    }
    CHECK_EQ(downs.size(), static_cast<size_t>(3));
    if (downs.size() == 3) {
        CHECK_EQ(downs[0], 0x23);
        CHECK_EQ(downs[1], 0x17);
        CHECK_EQ(downs[2], 0x02);
    }
}

UNIT_TEST(drive_script_wait_uses_the_clock_and_errors_stop_the_script) {
    const std::vector<Drive::Frame> f = Play("wait 0.5\nclick 1 1\n");
    CHECK(f.size() >= 30);   // half a second at 60 frames a second, then the click
    const Drive::Parsed bad = Drive::Parse("click 1\nfly 2 3\nkey hyperspace\n# a comment\n\nwait -1\n");
    CHECK_EQ(bad.errors.size(), static_cast<size_t>(4));
    Drive::Runner r;
    r.Start(bad);
    CHECK(!r.Running());
    const Drive::Parsed ok = Drive::Parse("# comment\n\nmove 1 2\r\ntype a b\n");
    CHECK(ok.errors.empty());
    CHECK_EQ(ok.commands, 2);
}

UNIT_TEST(drive_script_camera_step_fires_once) {
    const std::vector<Drive::Frame> f = Play("camera 144 201 60\n");
    int fired = 0;
    for (const Drive::Frame& x : f) {
        if (!x.camera) continue;
        ++fired;
        CHECK_EQ(static_cast<int>(x.cameraX), 144);
        CHECK_EQ(static_cast<int>(x.cameraY), 201);
        CHECK_EQ(static_cast<int>(x.cameraDist), 60);
        CHECK(x.cameraYaw < 0.0f);   // not given: keep the heading
    }
    CHECK_EQ(fired, 1);
    CHECK_EQ(Drive::Parse("camera 1\n").errors.size(), static_cast<size_t>(1));
    CHECK_EQ(Drive::Parse("camera -3 4\n").errors.size(), static_cast<size_t>(1));
}

UNIT_TEST(drive_script_cursor_starts_in_the_middle_not_the_corner) {
    // A script with no move (here a key) must not hold the cursor at (0, 0): the camera's edge scroll
    // would run into the map's corner.
    const std::vector<Drive::Frame> f = Play("key n\n");
    CHECK(!f.empty());
    CHECK_EQ(f.front().x, 1024);
    CHECK_EQ(f.front().y, 640);
}

UNIT_TEST(drive_script_shot_fires_once) {
    const std::vector<Drive::Frame> f = Play("shot\nwait 0.1\nshot\n");
    int shots = 0;
    for (const Drive::Frame& x : f) shots += x.shot ? 1 : 0;
    CHECK_EQ(shots, 2);
}
