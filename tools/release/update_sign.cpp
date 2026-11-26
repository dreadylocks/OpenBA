// ============================================================================
// tools/release/update_sign.cpp -- ext.auto_update's release tool (docs/fork/AUTO_UPDATE.md §5, §10)
// ============================================================================
// Not part of the game. CMake target update_sign (EXCLUDE_FROM_ALL), built by
// tools/release/publish_update.sh on the publishing machine.
//
//   update_sign keygen <key file>
//       A new Ed25519 key: 32 random bytes as 64 hex digits in <key file> (created 0600, refused if
//       it exists). Prints the public key as the C array src/ext/auto_update/update_key.h holds.
//   update_sign public <key file>
//       Prints that key's public half (64 hex digits).
//   update_sign manifest <key file> <package> <version> <commit> <platform> <url> <out file>
//       Writes <out file>: the manifest for <package> (its size and SHA-256), signed with the key.
//   update_sign verify <public key hex> <manifest file> [<package>]
//       Checks a manifest's shape and signature, and the package's size and SHA-256 when given.
//
// The key file is the release's one secret (keep it in ~/.config/openba/update_signing.key and in a
// password manager; never in the repo). A lost key means players install one build by hand, the one
// that carries a new public key.
// ============================================================================

#include "ext/auto_update/manifest.h"
#include "ext/auto_update/sha256.h"

#include "monocypher-ed25519.h"
#include "monocypher.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

using namespace AutoUpdate;

int Fail(const std::string& why) {
    std::fprintf(stderr, "update_sign: %s\n", why.c_str());
    return 1;
}

bool RandomBytes(uint8_t* out, size_t n) {
#ifdef _WIN32
    return BCryptGenRandom(nullptr, out, static_cast<ULONG>(n), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
    const int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return false;
    size_t got = 0;
    while (got < n) {
        const ssize_t r = read(fd, out + got, n - got);
        if (r <= 0) break;
        got += static_cast<size_t>(r);
    }
    close(fd);
    return got == n;
#endif
}

bool ReadFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream s;
    s << in.rdbuf();
    out = s.str();
    return true;
}

bool WriteFile(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << text;
    return static_cast<bool>(out);
}

// The key file's seed -> the 64-byte secret key and the public key.
bool LoadKey(const std::string& path, uint8_t secret[64], uint8_t pub[32], std::string& error) {
    std::string text;
    if (!ReadFile(path, text)) {
        error = "cannot read the key file " + path;
        return false;
    }
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
    uint8_t seed[32];
    if (!FromHex(text, seed, sizeof(seed))) {
        error = path + " is not a key file (64 hex digits)";
        return false;
    }
    crypto_ed25519_key_pair(secret, pub, seed);   // wipes `seed`
    return true;
}

int Keygen(const std::string& path) {
#ifdef _WIN32
    if (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES) return Fail(path + " exists: not overwriting a key");
    std::ofstream out(path, std::ios::binary);
    if (!out) return Fail("cannot create " + path);
#else
    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return Fail("cannot create " + path + " (it may exist already: not overwriting a key)");
#endif
    uint8_t seed[32];
    if (!RandomBytes(seed, sizeof(seed))) return Fail("no random bytes");
    const std::string hex = ToHex(seed, sizeof(seed)) + "\n";
#ifdef _WIN32
    out << hex;
    out.close();
#else
    const bool written = write(fd, hex.data(), hex.size()) == static_cast<ssize_t>(hex.size());
    close(fd);
    if (!written) return Fail("cannot write " + path);
#endif
    uint8_t secret[64];
    uint8_t pub[32];
    crypto_ed25519_key_pair(secret, pub, seed);
    crypto_wipe(secret, sizeof(secret));
    std::printf("public key %s\n", ToHex(pub, sizeof(pub)).c_str());
    std::printf("for src/ext/auto_update/update_key.h:\n");
    for (int i = 0; i < 32; ++i) std::printf("0x%02x,%s", pub[i], (i % 8 == 7) ? "\n" : " ");
    return 0;
}

int Public(const std::string& path) {
    uint8_t     secret[64];
    uint8_t     pub[32];
    std::string error;
    if (!LoadKey(path, secret, pub, error)) return Fail(error);
    crypto_wipe(secret, sizeof(secret));
    std::printf("%s\n", ToHex(pub, sizeof(pub)).c_str());
    return 0;
}

int MakeManifest(int argc, char** argv) {
    const std::string keyFile = argv[2];
    const std::string package = argv[3];
    Manifest          m;
    m.version = argv[4];
    m.commit = argv[5];
    m.platform = argv[6];
    m.url = argv[7];
    const std::string outFile = argv[8];
    (void)argc;
    std::string bytes;
    if (!ReadFile(package, bytes)) return Fail("cannot read " + package);
    m.size = bytes.size();
    m.sha256 = Sha256::HexOf(bytes.data(), bytes.size());
    uint8_t     secret[64];
    uint8_t     pub[32];
    std::string error;
    if (!LoadKey(keyFile, secret, pub, error)) return Fail(error);
    const std::string text = SignManifest(ManifestBody(m), secret);
    crypto_wipe(secret, sizeof(secret));
    Manifest check;
    if (!ParseManifest(text, check, error)) return Fail("the manifest does not parse: " + error);
    if (!VerifyManifest(text, check, pub, error)) return Fail("the manifest does not verify: " + error);
    if (!WriteFile(outFile, text)) return Fail("cannot write " + outFile);
    std::printf("%s", text.c_str());
    return 0;
}

int Verify(int argc, char** argv) {
    uint8_t pub[32];
    if (!FromHex(argv[2], pub, sizeof(pub))) return Fail("the public key is not 64 hex digits");
    std::string text;
    if (!ReadFile(argv[3], text)) return Fail(std::string("cannot read ") + argv[3]);
    Manifest    m;
    std::string error;
    if (!ParseManifest(text, m, error)) return Fail("the manifest does not parse: " + error);
    if (!VerifyManifest(text, m, pub, error)) return Fail(error);
    if (argc > 4) {
        std::string bytes;
        if (!ReadFile(argv[4], bytes)) return Fail(std::string("cannot read ") + argv[4]);
        if (bytes.size() != m.size) return Fail("the package's size does not match");
        if (Sha256::HexOf(bytes.data(), bytes.size()) != m.sha256) return Fail("the package's SHA-256 does not match");
    }
    std::printf("ok: %s %s for %s\n", m.version.c_str(), m.commit.c_str(), m.platform.c_str());
    return 0;
}

int Usage() {
    std::fprintf(stderr,
                 "usage: update_sign keygen <key file>\n"
                 "       update_sign public <key file>\n"
                 "       update_sign manifest <key file> <package> <version> <commit> <platform> <url> <out file>\n"
                 "       update_sign verify <public key hex> <manifest file> [<package>]\n");
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return Usage();
    const std::string cmd = argv[1];
    if (cmd == "keygen" && argc == 3) return Keygen(argv[2]);
    if (cmd == "public" && argc == 3) return Public(argv[2]);
    if (cmd == "manifest" && argc == 9) return MakeManifest(argc, argv);
    if (cmd == "verify" && (argc == 4 || argc == 5)) return Verify(argc, argv);
    return Usage();
}
