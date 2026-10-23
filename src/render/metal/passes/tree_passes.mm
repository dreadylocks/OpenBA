// NOT RETAIL: reuse the unchanged, platform-independent DXBC transformations.
// Retail inputs and transformed DXBC exist only in memory / the local airconv cache.
#include "tree_passes.h"
#include "tree_variant_key.h"
#include "better_trees/cutout_rewrite.h"
#include "better_trees/receiver_rewrite.h"
#include "better_trees/edge_dither.h"
#include <algorithm>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace VideoMetal::TreePasses {
namespace {

// What the extension asked for (main thread writes, the render thread reads).
struct Request {
    int mode = 0;
    float fillK = 0.0f;
    float fillFloor = 0.0f;
    BbsRewrite::Params dither = BbsRewrite::kDefaultParams;
    bool billboardShadow = false;
};

std::mutex requestMutex;
Request requested;

// The render thread's copy for the current frame.
Request frame;
bool msaa = false;

std::unordered_set<const Shader*> coverage;
id<MTLDevice> cachedDevice = nil;

// A transformed shader. `retail` is a copy of the shader it replaces (ARC keeps its function
// alive), so the blend guard never points back into a program's shader vector.
struct Variant {
    Shader shader;
    Shader retail;
    bool changed = false;
    bool coverage = false;
};

std::unordered_map<std::string, Variant> cache;
std::unordered_map<const Shader*, Shader*> originals;

}  // namespace

void SetFoliage(int mode, float fillK, float fillFloor, const BbsRewrite::Params* dither) {
    std::lock_guard<std::mutex> guard(requestMutex);
    requested.mode = mode;
    requested.fillK = fillK;
    requested.fillFloor = fillFloor;
    requested.dither = dither ? *dither : BbsRewrite::kDefaultParams;
}

void SetBillboardShadow(bool on) {
    std::lock_guard<std::mutex> guard(requestMutex);
    requested.billboardShadow = on;
}

bool IsCoverage(const Shader* pixel) {
    return pixel && coverage.count(pixel) != 0;
}

BlendGuard::BlendGuard(Shader*& pixel, bool blending)
    : slot(pixel), saved(pixel) {
    if (!blending) {
        return;
    }
    auto found = originals.find(pixel);
    if (found != originals.end()) {
        slot = found->second;
    }
}

void BeginFrame() {
    Request next;
    {
        std::lock_guard<std::mutex> guard(requestMutex);
        next = requested;
    }
    const bool nextMsaa = CurrentSamples() > 1;
    if (next.mode != frame.mode || nextMsaa != msaa) {
        std::fprintf(stderr, "[MetalPasses] better_trees mode %d (%s)\n", next.mode,
                     nextMsaa ? "multisample: alpha-to-coverage" : "single sample: cutout retained");
    }
    frame = next;
    msaa = nextMsaa;
}

namespace {

// The pixel half: cutout rewrite, then the billboard edge dither on colour programs.
bool MakePixelVariant(const std::vector<uint8_t>& bytes, std::vector<uint8_t>& output, bool& coverageOut) {
    FaaRewrite::Match match;
    FaaRewrite::Analyse(bytes.data(), bytes.size(), &match);
    auto options = FaaRewrite::OptionsFor(frame.mode, frame.fillK, msaa, frame.fillFloor);
    bool changed = FaaRewrite::Transform(match, options, &output);
    coverageOut = changed && options.coverage;
    if (!match.colour) {
        return changed;
    }
    const auto& input = changed ? output : bytes;
    BbsDither::Match dither;
    BbsDither::Analyse(input.data(), input.size(), &dither);
    std::vector<uint8_t> dithered;
    if (BbsDither::Transform(dither, &dithered, frame.dither)) {
        output = std::move(dithered);
        changed = true;
    }
    return changed;
}

// The vertex half: the billboard receiver's shadow lookup.
bool MakeVertexVariant(const std::vector<uint8_t>& bytes, std::vector<uint8_t>& output) {
    BbsRewrite::Match match;
    BbsRewrite::Analyse(bytes.data(), bytes.size(), &match);
    return BbsRewrite::TransformWithParams(match, &output, frame.dither);
}

}  // namespace

Shader* Select(Shader* retail, const std::vector<uint8_t>& bytes,
               id<MTLDevice> device, bool vertex) {
    const bool wanted = vertex ? frame.billboardShadow : frame.mode != 0;
    if (!wanted || !retail || retail->identity.empty()) {
        return retail;
    }
    if (cachedDevice != device) {
        originals.clear();
        cache.clear();
        coverage.clear();
        cachedDevice = device;
    }
    std::string key;
    if (vertex) {
        VertexSettings settings;
        settings.params = frame.dither;
        key = VertexVariantKey(retail->identity, settings);
    } else {
        PixelSettings settings;
        settings.mode = frame.mode;
        settings.msaa = msaa;
        settings.fillK = frame.fillK;
        settings.fillFloor = frame.fillFloor;
        settings.dither = frame.dither;
        key = PixelVariantKey(retail->identity, settings);
    }
    auto found = cache.find(key);
    if (found == cache.end()) {
        Variant variant;
        std::vector<uint8_t> output;
        bool changed = false;
        if (vertex) {
            changed = MakeVertexVariant(bytes, output);
        } else {
            changed = MakePixelVariant(bytes, output, variant.coverage);
        }
        if (changed) {
            variant.shader = Translate(output, device);
            variant.changed = variant.shader.function != nil;
            if (!variant.changed) {
                variant.coverage = false;
            }
            variant.retail = *retail;
            std::fprintf(stderr, "[MetalPasses] %s rewrite %zu -> %zu: %s\n",
                         vertex ? "billboard receiver" : "cutout/dither", bytes.size(), output.size(),
                         variant.changed ? "loaded" : "FAILED; using retail");
        }
        found = cache.emplace(std::move(key), std::move(variant)).first;
        if (found->second.changed && !vertex) {
            originals[&found->second.shader] = &found->second.retail;
            if (found->second.coverage) {
                coverage.insert(&found->second.shader);
            }
        }
    }
    if (!found->second.changed) {
        return retail;
    }
    return &found->second.shader;
}

}  // namespace VideoMetal::TreePasses
