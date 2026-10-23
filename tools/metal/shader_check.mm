// Headless DXBC conversion and Metal load check. Pass extracted shader blob paths
// as arguments; select the separate converter dylib through Options (airconv).
// Uses VideoMetalData.h's local airconv_abi.h declarations, not DXMT headers.
#include "app/options.h"
#include "VideoMetalData.h"

#include <cstdio>
#include <fstream>
#include <iterator>

// The check tool has no pipeline cache: the backend registers converted shaders there.
namespace VideoMetal {
void RegisterPipelineShader(const Shader&) {}
}  // namespace VideoMetal

int main(int argc, char** argv) {
    OptionsInit();
    @autoreleasepool {
        auto device = MTLCreateSystemDefaultDevice();
        unsigned ok = 0;
        unsigned bad = 0;
        for (int i = 1; i < argc; ++i) {
            @autoreleasepool {
                std::ifstream in(argv[i], std::ios::binary);
                std::vector<uint8_t> bytes{ std::istreambuf_iterator<char>(in), {} };
                auto shader = VideoMetal::Translate(bytes, device);
                if (shader.function) {
                    ++ok;
                } else {
                    ++bad;
                    fprintf(stderr, "FAIL %s\n", argv[i]);
                }
            }
        }
        printf("Native arm64 DXBC conversion + Metal load: %u passed, %u failed\n", ok, bad);
        return bad ? 1 : 0;
    }
}
