// Pure-logic test of the Metal better_trees variant key (src/render/metal/passes/tree_variant_key.h):
// a variant is keyed on the retail shader's identity string and the settings, never on an address.
#include "unit.h"

#include <string>

#include "../../src/render/metal/passes/tree_variant_key.h"

namespace {
using VideoMetal::TreePasses::PixelSettings;
using VideoMetal::TreePasses::PixelVariantKey;
using VideoMetal::TreePasses::VertexSettings;
using VideoMetal::TreePasses::VertexVariantKey;
}  // namespace

UNIT_TEST(metal_tree_key_follows_identity_not_address) {
    PixelSettings settings;
    settings.mode = 2;
    const std::string first = "abc123";
    const std::string copy(first);          // another object, the same identity
    CHECK(PixelVariantKey(first, settings) == PixelVariantKey(copy, settings));
    CHECK(PixelVariantKey(first, settings) != PixelVariantKey("abc124", settings));
}

UNIT_TEST(metal_tree_key_pixel_settings_split_variants) {
    PixelSettings base;
    base.mode = 2;
    base.fillK = 0.15f;
    base.fillFloor = 0.15f;
    const std::string key = PixelVariantKey("id", base);
    PixelSettings other = base;
    other.mode = 1;
    CHECK(PixelVariantKey("id", other) != key);
    other = base;
    other.msaa = true;
    CHECK(PixelVariantKey("id", other) != key);
    other = base;
    other.fillK = 0.08f;
    CHECK(PixelVariantKey("id", other) != key);
    other = base;
    other.fillFloor = 0.40f;
    CHECK(PixelVariantKey("id", other) != key);
    other = base;
    other.dither.noise += 0.01f;
    CHECK(PixelVariantKey("id", other) != key);
}

UNIT_TEST(metal_tree_key_vertex_ignores_fill_and_splits_on_params) {
    VertexSettings base;
    const std::string key = VertexVariantKey("id", base);
    VertexSettings other = base;
    other.params.jitter += 0.1f;
    CHECK(VertexVariantKey("id", other) != key);
    CHECK(VertexVariantKey("id", base) == key);
    PixelSettings pixel;
    CHECK(PixelVariantKey("id", pixel) != key);   // a vertex and a pixel variant never collide
}
