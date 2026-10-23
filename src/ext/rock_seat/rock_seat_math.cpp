// ============================================================================
// src/ext/rock_seat/rock_seat_math.cpp -- fix.rock_seat: the pure seating math
// ============================================================================
// NOT RETAIL. rock_seat_math.h has the contract; docs/extensions/handins/ROCKSEAT.md
// the reasoning.
// ============================================================================
#include "ext/rock_seat/rock_seat_math.h"

#include <cmath>

namespace Ext {
namespace RockSeat {

namespace {

constexpr float kPi = 3.14159265358979f;

}  // namespace

Footprint FootprintFromBounds(const Math::Vector3& boundsMin, const Math::Vector3& boundsMax, float scale) {
    Footprint f;
    f.cx = (boundsMin.x + boundsMax.x) * 0.5f * scale;
    f.cy = (boundsMin.y + boundsMax.y) * 0.5f * scale;
    f.ax = (boundsMax.x - boundsMin.x) * 0.5f * scale;
    f.ay = (boundsMax.y - boundsMin.y) * 0.5f * scale;
    f.height = (boundsMax.z - boundsMin.z) * scale;
    if (f.ax < 0.0f) f.ax = 0.0f;   // an empty box (min > max): a point
    if (f.ay < 0.0f) f.ay = 0.0f;
    if (f.height < 0.0f) f.height = 0.0f;
    return f;
}

float TiltSink(const Footprint& f) {
    const float cap = kTiltSinkFraction * f.height;
    return cap < kTiltSink ? cap : kTiltSink;
}

Math::Vector3 RingPoint(const Footprint& f, int i) {
    const float a = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(kRingSamples);
    return Math::Vector3(f.cx + f.ax * std::cos(a), f.cy + f.ay * std::sin(a), 0.0f);
}

float MinHeightUnder(const Math::Quaternion& rot, float x, float y, const Footprint& f, HeightFn height, void* ctx) {
    float lowest = height(ctx, x, y);
    for (int i = 0; i < kRingSamples; ++i) {
        const Math::Vector3 d = rot.Rotate(RingPoint(f, i));
        const float h = height(ctx, x + d.x, y + d.y);
        if (h < lowest) lowest = h;
    }
    return lowest;
}

bool FitPlane(const float* dx, const float* dy, const float* z, int n, float* sx, float* sy) {
    *sx = 0.0f;
    *sy = 0.0f;
    if (n < 3) return false;
    // Centre the samples, then solve the 2x2 normal equations for the slopes.
    double mx = 0.0;
    double my = 0.0;
    double mz = 0.0;
    for (int i = 0; i < n; ++i) {
        mx += dx[i];
        my += dy[i];
        mz += z[i];
    }
    mx /= n;
    my /= n;
    mz /= n;
    double sxx = 0.0;
    double syy = 0.0;
    double sxy = 0.0;
    double sxz = 0.0;
    double syz = 0.0;
    for (int i = 0; i < n; ++i) {
        const double ux = dx[i] - mx;
        const double uy = dy[i] - my;
        const double uz = z[i] - mz;
        sxx += ux * ux;
        syy += uy * uy;
        sxy += ux * uy;
        sxz += ux * uz;
        syz += uy * uz;
    }
    const double det = sxx * syy - sxy * sxy;
    if (!(std::fabs(det) > 1e-12)) return false;
    *sx = static_cast<float>((sxz * syy - syz * sxy) / det);
    *sy = static_cast<float>((syz * sxx - sxz * sxy) / det);
    return true;
}

Math::Vector3 NormalFromSlopes(float sx, float sy) {
    const float inv = 1.0f / std::sqrt(sx * sx + sy * sy + 1.0f);
    return Math::Vector3(-sx * inv, -sy * inv, inv);
}

Math::Quaternion TiltTo(const Math::Vector3& n, float maxTiltDeg) {
    // axis = up x n = (-n.y, n.x, 0); |axis| = sin(angle), n.z = cos(angle).
    const float ax = -n.y;
    const float ay = n.x;
    const float s = std::sqrt(ax * ax + ay * ay);
    if (s < 1e-6f || n.z <= 0.0f) return Math::Quaternion();
    float angle = std::atan2(s, n.z);
    const float cap = maxTiltDeg * kPi / 180.0f;
    if (angle > cap) angle = cap;
    const float sh = std::sin(angle * 0.5f);
    const float ch = std::cos(angle * 0.5f);
    // The engine's Rotate(v) is the textbook rotation by the CONJUGATE
    // (transform.cpp: (q * v) * conj(q) with its reversed product), so the
    // vector part is negated: Rotate(up) then turns up towards n by `angle`.
    return Math::Quaternion(-(ax / s) * sh, -(ay / s) * sh, 0.0f, ch);
}

float SeatHeight(const Math::Quaternion& rot, float x, float y, const Footprint& f, HeightFn height, void* ctx) {
    float origin = height(ctx, x, y);
    for (int i = 0; i < kRingSamples; ++i) {
        const Math::Vector3 d = rot.Rotate(RingPoint(f, i));
        const float z = height(ctx, x + d.x, y + d.y) - d.z;
        if (z < origin) origin = z;
    }
    return origin;
}

Math::Transform Seat(const Math::Transform& placed, const Footprint& f, int32_t mode, HeightFn height, void* ctx) {
    Math::Transform seated = placed;
    const float x = placed.translation.x;
    const float y = placed.translation.y;
    if (mode != kTilt) {
        seated.translation.z = MinHeightUnder(placed.rotation, x, y, f, height, ctx);
        return seated;
    }
    // The heights under the ring as retail turned it (yaw only), and the centre.
    float dx[kRingSamples + 1];
    float dy[kRingSamples + 1];
    float hz[kRingSamples + 1];
    for (int i = 0; i < kRingSamples; ++i) {
        const Math::Vector3 d = placed.rotation.Rotate(RingPoint(f, i));
        dx[i] = d.x;
        dy[i] = d.y;
        hz[i] = height(ctx, x + d.x, y + d.y);
    }
    dx[kRingSamples] = 0.0f;
    dy[kRingSamples] = 0.0f;
    hz[kRingSamples] = height(ctx, x, y);
    float sx = 0.0f;
    float sy = 0.0f;
    FitPlane(dx, dy, hz, kRingSamples + 1, &sx, &sy);
    const Math::Quaternion tilt = TiltTo(NormalFromSlopes(sx, sy), kMaxTiltDeg);
    seated.rotation = tilt * placed.rotation;   // yaw first, then the tilt
    seated.translation.z = SeatHeight(seated.rotation, x, y, f, height, ctx) - TiltSink(f);
    return seated;
}

}  // namespace RockSeat
}  // namespace Ext
