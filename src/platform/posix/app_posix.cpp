// ============================================================================
// src/platform/posix/app_posix.cpp -- Platform::App lifecycle hooks (POSIX / Linux)
// ============================================================================
// The POSIX half of src/platform/common/platform_app.h. Every body is empty: Linux has
// no step of its own at these spots. main()'s SDL hints stay inline in
// src/app/main.cpp, in their order. A Darwin twin (src/platform/darwin/app_darwin.cpp)
// replaces this file whole on Apple.
// ============================================================================

#include "platform_app.h"

namespace Platform {
namespace App {

void PreSdlInitHints() {
}

void PostSdlInit() {
}

void Shutdown() {
}

}  // namespace App
}  // namespace Platform
