#pragma once
// ============================================================================
// src/ext/no_fog/no_fog.h -- ext.no_fog (class A)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/NOFOG.md. Takes the fog out of the picture.
//
// RETAIL. Each frame the game blends the lighting record of the month (and, while it is shown, of every
// live weather effect) into one EnvironmentSetting: ambient colour, light colour, fog colour, fog near,
// fog far, fog maximum and the sun's heading and pitch (WeatherSimulation::BlendLighting, retail
// 0x140093ac0). The fog colour's fourth channel is the fog maximum, and the lighting shader mixes
// min((fogNear' + viewDepth) * fogScale, fogMax) of the fog colour into every lit pixel. Zoomed far out, the
// view depth is large, the mix reaches the maximum (0.95 by default) and the far map fades into haze.
//
// THE EXTENSION. With the row on, the blended record's fog maximum is 0, so the mix is 0 whatever the
// depth: no fog. One seam, at the end of BlendLighting, which both the shipped draw path
// (WeatherSimulation::DrawUpdate) and ext.render_pipe's recorded path call. The fog colour, near and far
// are left alone. With the row off the function is retail's own.
//
// CLASS A. It changes the picture only: no simulation state, nothing serialized, no lock. The row is read
// on each call, which is once per drawn frame, so a toggle takes effect at the next frame.
//
// Standard C++ only: tests/unit/ext_no_fog_test.cpp links nothing but this header.
// ============================================================================

namespace Ext {
namespace NoFog {

constexpr float kFogMax = 0.0f;   // the fog maximum with the row on: no fog is mixed in

// The fog maximum to use: `retail` when the row is off, kFogMax when it is on.
inline float FogMax(float retail, bool on) {
    return on ? kFogMax : retail;
}

}  // namespace NoFog
}  // namespace Ext
