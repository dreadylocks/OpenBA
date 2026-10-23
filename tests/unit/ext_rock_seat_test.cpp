// ============================================================================
// tests/unit/ext_rock_seat_test.cpp -- fix.rock_seat's pure seating math
// ============================================================================
// src/ext/rock_seat/rock_seat_math.{h,cpp} without the world: the footprint
// from a mesh AABB, the least-squares plane (slopes recovered, degenerate
// refused), the normal from the slopes, the tilt rotation in the engine's
// quaternion convention (and its cap), the engine's product order, and both
// variants on a planar slope (no ring point above the ground, the lowest one
// on it) and on flat ground (retail's transform unchanged but for z).
// ============================================================================
#include "unit.h"
#include "ext/rock_seat/rock_seat_math.h"
#include "ext/registry.h"
#include "math/Math.h"

#include <cmath>
#include <initializer_list>

namespace {

using namespace Ext::RockSeat;

bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

bool NearV(const Math::Vector3& a, const Math::Vector3& b, float eps = 1e-4f) {
    return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps);
}

// A planar terrain z = c + sx * x + sy * y.
struct Plane {
    float c;
    float sx;
    float sy;
};

float PlaneHeight(void* ctx, float x, float y) {
    const Plane* p = static_cast<const Plane*>(ctx);
    return p->c + p->sx * x + p->sy * y;
}

Math::Transform Placed(float yawDeg, float x, float y, float z) {
    return Math::Transform::FromYawPitchOffset(Math::Scalar(yawDeg), Math::Scalar(0.0f), Math::Vector3(x, y, z));
}

// A 2 x 1.6 rock whose box is centred a little off the origin.
Footprint Rock() {
    return FootprintFromBounds(Math::Vector3(-1.1f, -0.7f, -0.05f), Math::Vector3(0.9f, 0.9f, 1.0f), 1.0f);
}

}  // namespace

UNIT_TEST(ext_rock_seat_footprint_from_bounds) {
    const Footprint f = FootprintFromBounds(Math::Vector3(-1.0f, -0.5f, 0.0f), Math::Vector3(3.0f, 0.5f, 2.0f), 2.0f);
    CHECK(Near(f.cx, 2.0f));
    CHECK(Near(f.cy, 0.0f));
    CHECK(Near(f.ax, 4.0f));
    CHECK(Near(f.ay, 1.0f));
    // The default (empty) Volume::Box: min +1e8, max -1e8 -> a point, not a negative ellipse.
    const Footprint e = FootprintFromBounds(Math::Vector3(1e8f, 1e8f, 1e8f), Math::Vector3(-1e8f, -1e8f, -1e8f), 1.0f);
    CHECK(Near(e.ax, 0.0f));
    CHECK(Near(e.ay, 0.0f));
    CHECK(Near(e.height, 0.0f));
    CHECK(Near(f.height, 4.0f));
    // The tilt sink: kTiltSink for a boulder, a fraction of the height for a small ore piece.
    CHECK(Near(TiltSink(Rock()), kTiltSink));
    const Footprint ore = FootprintFromBounds(Math::Vector3(-0.46f, -0.61f, -0.02f),
                                              Math::Vector3(0.35f, 0.53f, 0.24f), 1.0f);
    CHECK(Near(TiltSink(ore), kTiltSinkFraction * 0.26f));
    CHECK(TiltSink(ore) < kTiltSink);
    // The ring lies on the ellipse, in the z = 0 plane.
    for (int i = 0; i < kRingSamples; ++i) {
        const Math::Vector3 p = RingPoint(f, i);
        const float u = (p.x - f.cx) / f.ax;
        const float v = (p.y - f.cy) / f.ay;
        CHECK(Near(u * u + v * v, 1.0f));
        CHECK(Near(p.z, 0.0f));
    }
}

UNIT_TEST(ext_rock_seat_fit_plane) {
    const float dx[] = { 0.0f, 1.0f, -1.0f, 0.5f, 0.2f, -0.7f };
    const float dy[] = { 0.0f, 0.3f, 0.8f, -1.0f, 0.6f, -0.4f };
    float z[6];
    for (int i = 0; i < 6; ++i) z[i] = 2.0f + 0.3f * dx[i] - 0.1f * dy[i];
    float sx = 0.0f;
    float sy = 0.0f;
    CHECK(FitPlane(dx, dy, z, 6, &sx, &sy));
    CHECK(Near(sx, 0.3f));
    CHECK(Near(sy, -0.1f));
    // Collinear points span no plane: refused, slopes 0.
    const float lx[] = { 0.0f, 1.0f, 2.0f, 3.0f };
    const float ly[] = { 0.0f, 1.0f, 2.0f, 3.0f };
    const float lz[] = { 0.0f, 1.0f, 2.0f, 3.0f };
    CHECK(!FitPlane(lx, ly, lz, 4, &sx, &sy));
    CHECK(Near(sx, 0.0f) && Near(sy, 0.0f));
    CHECK(!FitPlane(lx, ly, lz, 2, &sx, &sy));
}

UNIT_TEST(ext_rock_seat_normal_from_slopes) {
    const Math::Vector3 n = NormalFromSlopes(0.3f, -0.2f);
    CHECK(Near(n.x * n.x + n.y * n.y + n.z * n.z, 1.0f));
    // Perpendicular to the plane's two tangents (1, 0, sx) and (0, 1, sy).
    CHECK(Near(n.x * 1.0f + n.z * 0.3f, 0.0f));
    CHECK(Near(n.y * 1.0f + n.z * -0.2f, 0.0f));
    CHECK(n.z > 0.0f);
    CHECK(NearV(NormalFromSlopes(0.0f, 0.0f), Math::Vector3(0.0f, 0.0f, 1.0f)));
}

UNIT_TEST(ext_rock_seat_tilt_turns_up_onto_the_normal) {
    const Math::Vector3 up(0.0f, 0.0f, 1.0f);
    for (float sx : { 0.0f, 0.1f, -0.25f, 0.4f }) {
        for (float sy : { 0.0f, 0.15f, -0.3f }) {
            const Math::Vector3 n = NormalFromSlopes(sx, sy);
            const Math::Quaternion q = TiltTo(n, 89.0f);
            CHECK(NearV(q.Rotate(up), n));
            CHECK(Near(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w, 1.0f));
        }
    }
    // Up and down-facing normals: identity.
    const Math::Quaternion id = TiltTo(up, kMaxTiltDeg);
    CHECK(Near(id.w, 1.0f) && Near(id.x, 0.0f) && Near(id.y, 0.0f) && Near(id.z, 0.0f));
    const Math::Quaternion down = TiltTo(Math::Vector3(0.6f, 0.0f, -0.8f), kMaxTiltDeg);
    CHECK(Near(down.w, 1.0f));
    // The cap: a 45 degree slope tilts by kMaxTiltDeg, towards the slope.
    const Math::Vector3 steep = NormalFromSlopes(1.0f, 0.0f);   // 45 degrees, leaning to -x
    const Math::Vector3 r = TiltTo(steep, kMaxTiltDeg).Rotate(up);
    CHECK(Near(std::acos(r.z) * 180.0f / 3.14159265f, kMaxTiltDeg, 1e-2f));
    CHECK(r.x < 0.0f);
    CHECK(Near(r.y, 0.0f));
}

UNIT_TEST(ext_rock_seat_engine_product_order) {
    // (a * b).Rotate(v) == a.Rotate(b.Rotate(v)): Seat composes tilt * yaw = yaw first.
    const Math::Quaternion yaw = Placed(73.0f, 0.0f, 0.0f, 0.0f).rotation;
    const Math::Quaternion tilt = TiltTo(NormalFromSlopes(0.2f, -0.35f), kMaxTiltDeg);
    for (const Math::Vector3& v : { Math::Vector3(1.0f, 0.0f, 0.0f), Math::Vector3(0.3f, -0.8f, 0.5f) }) {
        CHECK(NearV((tilt * yaw).Rotate(v), tilt.Rotate(yaw.Rotate(v))));
    }
}

UNIT_TEST(ext_rock_seat_flat_ground_keeps_retail) {
    Plane flat = { 1.25f, 0.0f, 0.0f };
    const Math::Transform placed = Placed(40.0f, 3.0f, -2.0f, 1.25f);
    for (int32_t mode : { kSink, kTilt }) {
        const Math::Transform s = Seat(placed, Rock(), mode, &PlaneHeight, &flat);
        CHECK(Near(s.translation.x, 3.0f));
        CHECK(Near(s.translation.y, -2.0f));
        CHECK(Near(s.rotation.x, placed.rotation.x));
        CHECK(Near(s.rotation.y, placed.rotation.y));
        CHECK(Near(s.rotation.z, placed.rotation.z));
        CHECK(Near(s.rotation.w, placed.rotation.w));
        const float expectZ = mode == kSink ? 1.25f : 1.25f - TiltSink(Rock());
        CHECK(Near(s.translation.z, expectZ));
    }
}

UNIT_TEST(ext_rock_seat_sink_on_a_slope) {
    Plane slope = { 0.5f, 0.12f, -0.07f };   // ~8 degrees, NaturalResourceRock's _maxAngle 0.99
    const Footprint f = Rock();
    for (float yaw : { 0.0f, 33.0f, 90.0f, -150.0f }) {
        // Retail: the cell centre's height, the rock jittered off it.
        const Math::Transform placed = Placed(yaw, 2.3f, 1.4f, PlaneHeight(&slope, 2.0f, 1.0f));
        const Math::Transform s = Seat(placed, f, kSink, &PlaneHeight, &slope);
        CHECK(Near(s.rotation.w, placed.rotation.w) && Near(s.rotation.z, placed.rotation.z));
        // No ring point floats, and the lowest one is on the ground.
        float lowestGap = 1e9f;
        for (int i = 0; i < kRingSamples; ++i) {
            const Math::Vector3 w = s * RingPoint(f, i);
            const float gap = w.z - PlaneHeight(&slope, w.x, w.y);   // > 0 = floating
            CHECK(gap <= 1e-4f);
            if (-gap < lowestGap) lowestGap = -gap;
        }
        CHECK(Near(lowestGap, 0.0f));
        CHECK(s.translation.z <= PlaneHeight(&slope, 2.3f, 1.4f) + 1e-5f);
    }
}

UNIT_TEST(ext_rock_seat_tilt_on_a_slope) {
    Plane slope = { -0.3f, -0.2f, 0.25f };
    const Footprint f = Rock();
    const Math::Vector3 n = NormalFromSlopes(slope.sx, slope.sy);
    for (float yaw : { 0.0f, 33.0f, 120.0f, -75.0f }) {
        const Math::Transform placed = Placed(yaw, -4.0f, 6.5f, PlaneHeight(&slope, -4.0f, 7.0f));
        const Math::Transform s = Seat(placed, f, kTilt, &PlaneHeight, &slope);
        // Up is the terrain normal; the yaw is kept (the model's x axis, flattened onto the plane,
        // keeps about retail's heading: the minimal tilt turns it a little).
        CHECK(NearV(s.rotation.Rotate(Math::Vector3(0.0f, 0.0f, 1.0f)), n));
        const Math::Vector3 ax = s.rotation.Rotate(Math::Vector3(1.0f, 0.0f, 0.0f));
        const Math::Vector3 rx = placed.rotation.Rotate(Math::Vector3(1.0f, 0.0f, 0.0f));
        const float la = std::sqrt(ax.x * ax.x + ax.y * ax.y);
        const float lr = std::sqrt(rx.x * rx.x + rx.y * rx.y);
        CHECK((ax.x * rx.x + ax.y * rx.y) / (la * lr) > 0.995f);   // within ~6 degrees of retail's heading
        // On a plane the whole ring is TiltSink under the ground: none floats, none buried deeper.
        for (int i = 0; i < kRingSamples; ++i) {
            const Math::Vector3 w = s * RingPoint(f, i);
            const float gap = w.z - PlaneHeight(&slope, w.x, w.y);
            CHECK(Near(gap, -TiltSink(f), 1e-3f));
        }
    }
}

UNIT_TEST(ext_rock_seat_row_and_mode_key) {
    const Ext::Entry* e = Ext::FindEntry("rock_seat");
    CHECK(e != nullptr);
    CHECK(e->tier == Ext::Tier::Fix);
    CHECK(e->defaultOn);
    const Ext::ValueKey* k = Ext::FindValueKey("rock_seat_mode");
    CHECK(k != nullptr);
    CHECK_STREQ(k->owner, "rock_seat");
    CHECK_EQ(k->minValue, kSink);
    CHECK_EQ(k->maxValue, kTilt);
    CHECK_STREQ(Ext::SettingsKey(*k).c_str(), "ext.rock_seat_mode");
    // An unknown mode is clamped into 1..2.
    CHECK_EQ(Ext::ClampValue(*k, 7), kTilt);
    CHECK_EQ(Ext::ClampValue(*k, 0), kSink);
}
