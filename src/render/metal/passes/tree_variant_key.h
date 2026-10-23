#pragma once
// ============================================================================
// src/render/metal/passes/tree_variant_key.h -- the key of a better_trees shader variant
// ============================================================================
// NOT RETAIL. Pure C++ (no Metal types, so the host unit tests build it). A transformed
// shader is cached under the retail shader's IDENTITY STRING (Shader::identity: the
// SHA-256 of the retail DXBC) plus every setting that changes the emitted
// code. It is never keyed on a Shader address: the Shader objects live in the program's
// vector, which PlatformCreate clears and refills, so an address outlives its shader.
// ============================================================================

#include <cstring>
#include <string>

#include "better_trees/receiver_rewrite.h"   // BbsRewrite::Params (pure, no D3D)

namespace VideoMetal {
namespace TreePasses {

// The settings a pixel (cutout / edge dither) variant is made with.
struct PixelSettings {
    int mode = 0;
    bool msaa = false;
    float fillK = 0.0f;
    float fillFloor = 0.0f;
    BbsRewrite::Params dither = BbsRewrite::kDefaultParams;
};

// The settings a vertex (billboard receiver) variant is made with.
struct VertexSettings {
    BbsRewrite::Params params = BbsRewrite::kDefaultParams;
};

inline void AppendBytes(std::string& key, const void* data, size_t size) {
    key.append(static_cast<const char*>(data), size);
}

inline std::string PixelVariantKey(const std::string& identity, const PixelSettings& settings) {
    std::string key = identity;
    key.push_back('P');
    AppendBytes(key, &settings.mode, sizeof(settings.mode));
    key.push_back(settings.msaa ? '1' : '0');
    AppendBytes(key, &settings.fillK, sizeof(settings.fillK));
    AppendBytes(key, &settings.fillFloor, sizeof(settings.fillFloor));
    AppendBytes(key, &settings.dither.noise, sizeof(settings.dither.noise));
    AppendBytes(key, &settings.dither.noiseScale, sizeof(settings.dither.noiseScale));
    return key;
}

inline std::string VertexVariantKey(const std::string& identity, const VertexSettings& settings) {
    std::string key = identity;
    key.push_back('V');
    AppendBytes(key, &settings.params.basis, sizeof(settings.params.basis));
    AppendBytes(key, &settings.params.margin, sizeof(settings.params.margin));
    AppendBytes(key, &settings.params.jitter, sizeof(settings.params.jitter));
    return key;
}

}  // namespace TreePasses
}  // namespace VideoMetal
