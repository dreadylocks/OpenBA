#include "app/options.h"
#include "VideoMetalData.h"
#include "device_support.h"
#include "shader_cache.h"
#import <Foundation/Foundation.h>
#include <CommonCrypto/CommonDigest.h>
#include <dlfcn.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <vector>

namespace VideoMetal {

namespace {

struct Airconv {
    void* handle = nullptr;
    std::string path;
    std::string fingerprint;
    SM50InitializeFn SM50Initialize = nullptr;
    SM50DestroyFn SM50Destroy = nullptr;
    SM50CompileFn SM50Compile = nullptr;
    SM50GetArgumentsInfoFn SM50GetArgumentsInfo = nullptr;
    SM50GetCompiledBitcodeFn SM50GetCompiledBitcode = nullptr;
    SM50DestroyBitcodeFn SM50DestroyBitcode = nullptr;
    SM50GetErrorMessageFn SM50GetErrorMessage = nullptr;
    SM50FreeErrorFn SM50FreeError = nullptr;

    bool load() {
        if (handle)
            return true;
        std::string why;
        handle = OpenConverter(&path, &why);
        if (!handle) {
            fprintf(stderr, "[Metal] airconv: %s\n", why.c_str());
            return false;
        }
        // OpenConverter already resolved every name in kConverterSymbols, so none can be missing here.
        void** slots[] = {
            reinterpret_cast<void**>(&SM50Initialize),
            reinterpret_cast<void**>(&SM50Destroy),
            reinterpret_cast<void**>(&SM50Compile),
            reinterpret_cast<void**>(&SM50GetArgumentsInfo),
            reinterpret_cast<void**>(&SM50GetCompiledBitcode),
            reinterpret_cast<void**>(&SM50DestroyBitcode),
            reinterpret_cast<void**>(&SM50GetErrorMessage),
            reinterpret_cast<void**>(&SM50FreeError),
        };
        size_t index = 0;
        for (const char* const* name = kConverterSymbols; *name; ++name) {
            *slots[index++] = dlsym(handle, *name);
        }
        fingerprint = ComputeFingerprint();
        return true;
    }

    std::string ComputeFingerprint() const {
        ShaderCache::ConverterIdentity identity;
        identity.path = path;
        struct stat st;
        if (stat(path.c_str(), &st) == 0) {
            identity.size = (uint64_t)st.st_size;
            identity.mtimeNs = (int64_t)st.st_mtimespec.tv_sec * 1000000000ll + st.st_mtimespec.tv_nsec;
        }
        NSOperatingSystemVersion os = NSProcessInfo.processInfo.operatingSystemVersion;
        std::string osVersion = std::to_string(os.majorVersion) + "." + std::to_string(os.minorVersion) + "." +
                                std::to_string(os.patchVersion);
        return ShaderCache::Fingerprint(identity, osVersion, "metal" + std::to_string((int)SM50_SHADER_METAL_320));
    }

    void error(sm50_error_t e) {
        if (!e)
            return;
        char message[2048] = {};
        SM50GetErrorMessage(e, message, sizeof(message) - 1);
        fprintf(stderr, "[Metal] shader: %s\n", message);
        SM50FreeError(e);
    }
};

// The home directory from the passwd database, as the rest of the port does.
std::string HomeDirectory() {
    struct passwd pw;
    struct passwd* result = nullptr;
    std::vector<char> buffer(16384);
    if (getpwuid_r(getuid(), &pw, buffer.data(), buffer.size(), &result) != 0 || result == nullptr)
        return std::string();
    if (pw.pw_dir == nullptr || pw.pw_dir[0] == 0)
        return std::string();
    std::string home = pw.pw_dir;
    while (home.size() > 1 && home.back() == '/')
        home.pop_back();
    return home;
}

// Empty when no home can be found: the cache is then simply off.
NSString* CacheDirectory(const std::string& fingerprint) {
    static std::string home = ShaderCache::ResolveHome(Opt().userProfile ? Opt().userProfile : "", HomeDirectory());
    if (home.empty())
        return nil;
    return [NSString stringWithUTF8String:ShaderCache::EntryDirectory(home, fingerprint).c_str()];
}

std::string Sha256Hex(const std::vector<uint8_t>& bytes) {
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(bytes.data(), (CC_LONG)bytes.size(), digest);
    char hex[65];
    for (int i = 0; i < 32; ++i)
        snprintf(hex + 2 * i, 3, "%02x", digest[i]);
    return hex;
}

bool LoadLibrary(NSData* data, id<MTLDevice> device, Shader& out) {
    NSError* error = nil;
    dispatch_data_t dd = dispatch_data_create(data.bytes, data.length, dispatch_get_global_queue(0, 0),
                                              DISPATCH_DATA_DESTRUCTOR_DEFAULT);
    out.library = [device newLibraryWithData:dd error:&error];
    out.function = out.library ? [out.library newFunctionWithName:@"shader_main"] : nil;
    if (!out.function)
        fprintf(stderr, "[Metal] library load: %s\n", error ? error.localizedDescription.UTF8String : "no shader_main");
    return out.function != nil;
}

}  // namespace

Shader Translate(const std::vector<uint8_t>& bytes, id<MTLDevice> device) {
    @autoreleasepool {
        static Airconv api;
        static std::mutex lock;
        std::lock_guard<std::mutex> guard(lock);
        Shader out;
        if (!api.load() || bytes.empty())
            return out;
        sm50_shader_t shader = nullptr;
        sm50_error_t err = nullptr;
        if (api.SM50Initialize(bytes.data(), bytes.size(), &shader, &out.reflection, &err)) {
            api.error(err);
            return out;
        }
        out.constants.resize(out.reflection.NumConstantBuffers);
        out.arguments.resize(out.reflection.NumArguments);
        api.SM50GetArgumentsInfo(shader, out.constants.data(), out.arguments.data());
        out.identity = Sha256Hex(bytes);

        NSString* dir = CacheDirectory(api.fingerprint);
        NSString* file = nil;
        if (dir) {
            [[NSFileManager defaultManager] createDirectoryAtPath:dir
                                      withIntermediateDirectories:YES
                                                       attributes:nil
                                                            error:nil];
            file = [dir
                stringByAppendingPathComponent:[NSString stringWithUTF8String:ShaderCache::EntryFileName(out.identity)
                                                                                  .c_str()]];
        }

        bool cached = false;
        bool loaded = false;
        NSData* data = file ? [NSData dataWithContentsOfFile:file] : nil;
        if (data) {
            cached = true;
            loaded = LoadLibrary(data, device, out);
            if (!loaded) {
                // Corrupt or unreadable entry: drop it and convert again.
                [[NSFileManager defaultManager] removeItemAtPath:file error:nil];
                cached = false;
                data = nil;
            }
        }
        if (!loaded) {
            SM50_SHADER_COMMON_DATA args{};
            args.type = SM50_SHADER_COMMON;
            args.metal_version = SM50_SHADER_METAL_320;
            sm50_bitcode_t code = nullptr;
            if (api.SM50Compile(shader, reinterpret_cast<SM50_SHADER_COMPILATION_ARGUMENT_DATA*>(&args), "shader_main",
                                &code, &err)) {
                api.error(err);
            } else {
                SM50_COMPILED_BITCODE bitcode{};
                api.SM50GetCompiledBitcode(code, &bitcode);
                data = [NSData dataWithBytes:bitcode.Data length:bitcode.Size];
                api.SM50DestroyBitcode(code);
                loaded = LoadLibrary(data, device, out);
                if (loaded && file)
                    [data writeToFile:file atomically:YES];
            }
        }
        api.SM50Destroy(shader);

        static unsigned count = 0;
        if (++count <= 20)
            fprintf(stderr, "[Metal] shader %u: %s %s cb=%u args=%u\n", count, out.function ? "loaded" : "FAILED",
                    cached ? "cache" : "converted", out.reflection.NumConstantBuffers, out.reflection.NumArguments);
        RegisterPipelineShader(out);
        return out;
    }
}

}  // namespace VideoMetal
