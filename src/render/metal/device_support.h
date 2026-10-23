#pragma once
// Can this Mac run the Metal backend? Plain-language answer for System::Error, and where the
// converter dylib lives. Safe to include from non-Objective-C code.
#include <string>

#ifdef __OBJC__
#import <Metal/Metal.h>
#endif

namespace VideoMetal {

// False with a plain message in *why (may be null) when the OS is older than the minimum, the
// GPU lacks MTLGPUFamilyMetal3, or libairconv.dylib cannot be found or loaded (the message
// names the paths tried). `device` is an id<MTLDevice> passed as void* for non-ObjC callers.
bool CheckDeviceSupport(void* device, std::string* why);
#ifdef __OBJC__
bool CheckDeviceSupport(id<MTLDevice> device, std::string* why);
#endif

// The first existing file of the search order (Opt override, <exe>/../Frameworks, <exe>), or an
// empty string. *tried receives every path looked at, one per line, when not null.
std::string ResolveConverterPath(std::string* tried);

// The SM50 entry points the backend resolves from the converter, null-terminated. The check and
// the loader use this one list, so a dylib that passes the check can translate.
extern const char* const kConverterSymbols[];

// The single dlopen of the converter, shared by the support check and the shader loader: the
// first call opens it and resolves every name in kConverterSymbols (the handle lives for the
// process, never closed); later calls return the same result. Null with a plain message in
// *why (may be null) naming the path and the missing symbol or the dlopen error. *path
// receives the dylib path when not null.
void* OpenConverter(std::string* path, std::string* why);

}  // namespace VideoMetal
