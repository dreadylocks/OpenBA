#include "device_support.h"
#include "shader_cache.h"
#include "app/options.h"
#import <Foundation/Foundation.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <climits>
#include <cstdlib>
#include <sys/stat.h>
#include <mutex>
#include <vector>

namespace VideoMetal {

namespace {

std::string ExecutableDirectory() {
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> raw(size + 1, 0);
    if (_NSGetExecutablePath(raw.data(), &size) != 0)
        return ".";
    char resolved[PATH_MAX];
    std::string path = realpath(raw.data(), resolved) ? resolved : raw.data();
    size_t slash = path.rfind('/');
    return slash == std::string::npos ? "." : path.substr(0, slash);
}

struct ConverterOpen {
    void* handle = nullptr;
    std::string path;
    std::string why;
};

}  // namespace

const char* const kConverterSymbols[] = {
    "SM50Initialize",
    "SM50Destroy",
    "SM50Compile",
    "SM50GetArgumentsInfo",
    "SM50GetCompiledBitcode",
    "SM50DestroyBitcode",
    "SM50GetErrorMessage",
    "SM50FreeError",
    nullptr,
};

void* OpenConverter(std::string* path, std::string* why) {
    static std::mutex openMutex;
    static ConverterOpen opened;
    static bool attempted = false;
    std::lock_guard<std::mutex> guard(openMutex);
    if (!attempted) {
        attempted = true;
        std::string tried;
        std::string found = ResolveConverterPath(&tried);
        opened.path = found;
        if (found.empty()) {
            opened.why = "The shader converter libairconv.dylib was not found. Looked in:\n" + tried;
        } else {
            void* handle = dlopen(found.c_str(), RTLD_NOW | RTLD_LOCAL);
            if (!handle) {
                const char* reason = dlerror();
                opened.why =
                    "The shader converter " + found + " could not be loaded: " + (reason ? reason : "unknown error");
            } else {
                const char* missing = nullptr;
                for (const char* const* name = kConverterSymbols; *name && !missing; ++name) {
                    if (!dlsym(handle, *name))
                        missing = *name;
                }
                if (missing) {
                    opened.why = "The shader converter " + found + " lacks the required entry point " + missing + ".";
                    dlclose(handle);
                } else {
                    opened.handle = handle;
                }
            }
        }
    }
    if (path)
        *path = opened.path;
    if (!opened.handle && why)
        *why = opened.why;
    return opened.handle;
}

std::string ResolveConverterPath(std::string* tried) {
    const char* override_path = Opt().airconv;
    std::vector<std::string> order =
        ShaderCache::ConverterSearchOrder(override_path ? override_path : "", ExecutableDirectory());
    std::string found;
    for (const std::string& candidate : order) {
        if (tried) {
            *tried += candidate;
            *tried += "\n";
        }
        struct stat st;
        if (found.empty() && stat(candidate.c_str(), &st) == 0 && S_ISREG(st.st_mode))
            found = candidate;
    }
    return found;
}

bool CheckDeviceSupport(void* device, std::string* why) {
    return CheckDeviceSupport((__bridge id<MTLDevice>)device, why);
}

bool CheckDeviceSupport(id<MTLDevice> device, std::string* why) {
    std::string unused;
    std::string& message = why ? *why : unused;
    NSOperatingSystemVersion os = NSProcessInfo.processInfo.operatingSystemVersion;
    if (!ShaderCache::OsAtLeast((int)os.majorVersion, (int)os.minorVersion, ShaderCache::kMinimumMacOSMajor,
                                ShaderCache::kMinimumMacOSMinor)) {
        message = "OpenBA needs macOS " + std::to_string(ShaderCache::kMinimumMacOSMajor) + "." +
                  std::to_string(ShaderCache::kMinimumMacOSMinor) + " or newer for the Metal renderer; this Mac runs " +
                  std::to_string(os.majorVersion) + "." + std::to_string(os.minorVersion) + ".";
        return false;
    }
    if (device == nil) {
        message = "No Metal graphics device was found on this Mac.";
        return false;
    }
    if (![device supportsFamily:MTLGPUFamilyMetal3]) {
        message = std::string("The graphics chip \"") + device.name.UTF8String +
                  "\" does not support Metal 3, which OpenBA's Metal renderer needs "
                  "(Apple silicon, or a recent AMD GPU).";
        return false;
    }
    return OpenConverter(nullptr, &message) != nullptr;
}

}  // namespace VideoMetal
