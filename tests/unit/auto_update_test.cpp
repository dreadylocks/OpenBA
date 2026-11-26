// ============================================================================
// tests/unit/auto_update_test.cpp -- ext.auto_update's pure parts (docs/fork/AUTO_UPDATE.md)
// ============================================================================
// The version compare, the manifest's parse and Ed25519 signature (Monocypher, checked against
// RFC 8032's first test vector), SHA-256 against the FIPS 180-4 vectors, the launch decisions for
// both install models, and the staging folder's state files (in a temporary folder). Every platform.
// ============================================================================
#include "unit.h"

#include <filesystem>
#include <string>

#include "ext/auto_update/launch_plan.h"
#include "ext/auto_update/manifest.h"
#include "ext/auto_update/sha256.h"
#include "ext/auto_update/staging.h"
#include "ext/auto_update/update_key.h"

#include "monocypher-ed25519.h"

using namespace AutoUpdate;

namespace {

// A fixed test key (never a release key): seed 0x01..0x20.
void TestKey(uint8_t secret[64], uint8_t pub[32]) {
    uint8_t seed[32];
    for (int i = 0; i < 32; ++i) seed[i] = static_cast<uint8_t>(i + 1);
    crypto_ed25519_key_pair(secret, pub, seed);
}

Manifest SampleManifest() {
    Manifest m;
    m.version = "1.2.1";
    m.commit = "77a86958f3c1";
    m.platform = "macos-arm64";
    m.url = "https://github.com/dreadylocks/OpenBA/releases/download/v1.2.1/OpenBA-v1.2.1-mac-arm64.zip";
    m.size = 12812809;
    m.sha256 = std::string(64, 'a');
    return m;
}

std::string Signed(const Manifest& m) {
    uint8_t secret[64];
    uint8_t pub[32];
    TestKey(secret, pub);
    return SignManifest(ManifestBody(m), secret);
}

bool Parses(const std::string& text, std::string* why = nullptr) {
    Manifest    m;
    std::string error;
    const bool  ok = ParseManifest(text, m, error);
    if (why != nullptr) *why = error;
    return ok;
}

}  // namespace

UNIT_TEST(auto_update_version_parse) {
    Version v;
    CHECK(ParseVersion("1.2.3", v));
    CHECK_EQ(v.major, 1u);
    CHECK_EQ(v.minor, 2u);
    CHECK_EQ(v.patch, 3u);
    CHECK(v.suffix.empty());
    CHECK(ParseVersion("10.0.12-rc.1", v));
    CHECK_STREQ(v.suffix.c_str(), "rc.1");
    CHECK(!ParseVersion("", v));
    CHECK(!ParseVersion("1.2", v));
    CHECK(!ParseVersion("1.2.3.4", v));
    CHECK(!ParseVersion("v1.2.3", v));
    CHECK(!ParseVersion("1.2.3-", v));
    CHECK(!ParseVersion("1.2.3-rc..1", v));
    CHECK(!ParseVersion("1.2.3-rc 1", v));
    CHECK(!ParseVersion("1.2.x", v));
    CHECK(!ParseVersion("1234567.0.0", v));
}

UNIT_TEST(auto_update_version_compare) {
    CHECK(IsNewer("1.2.1", "1.2.0"));
    CHECK(IsNewer("1.10.0", "1.9.9"));
    CHECK(IsNewer("2.0.0", "1.99.99"));
    CHECK(!IsNewer("1.2.0", "1.2.0"));
    CHECK(!IsNewer("1.1.9", "1.2.0"));
    // semver: a suffix sorts before the release, numbers before words, numbers numerically.
    CHECK(IsNewer("1.2.0", "1.2.0-rc.1"));
    CHECK(!IsNewer("1.2.0-rc.1", "1.2.0"));
    CHECK(IsNewer("1.2.0-rc.2", "1.2.0-rc.1"));
    CHECK(IsNewer("1.2.0-rc.10", "1.2.0-rc.9"));
    CHECK(IsNewer("1.2.0-rc", "1.2.0-beta"));
    CHECK(IsNewer("1.2.0-alpha.1", "1.2.0-alpha"));
    CHECK(IsNewer("1.2.0-alpha.beta", "1.2.0-alpha.1"));
    // Either side unparsable: never newer.
    CHECK(!IsNewer("garbage", "1.0.0"));
    CHECK(!IsNewer("9.9.9", "garbage"));
}

UNIT_TEST(auto_update_sha256_fips_vectors) {
    CHECK_STREQ(Sha256::HexOf("", 0).c_str(), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK_STREQ(Sha256::HexOf("abc", 3).c_str(), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const std::string two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    CHECK_STREQ(Sha256::HexOf(two.data(), two.size()).c_str(),
                "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    // One million 'a', fed in uneven pieces: the streaming path across block edges.
    Sha256            s;
    const std::string piece(997, 'a');
    size_t            left = 1000000;
    while (left > 0) {
        const size_t n = left < piece.size() ? left : piece.size();
        s.Add(piece.data(), n);
        left -= n;
    }
    uint8_t d[32];
    s.Finish(d);
    CHECK_STREQ(Sha256::Hex(d).c_str(), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

UNIT_TEST(auto_update_ed25519_rfc8032_vector) {
    // RFC 8032 §7.1 TEST 1: the empty message.
    uint8_t seed[32];
    CHECK(FromHex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60", seed, 32));
    uint8_t secret[64];
    uint8_t pub[32];
    crypto_ed25519_key_pair(secret, pub, seed);
    CHECK_STREQ(ToHex(pub, 32).c_str(), "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
    uint8_t sig[64];
    crypto_ed25519_sign(sig, secret, nullptr, 0);
    CHECK_STREQ(ToHex(sig, 64).c_str(),
                "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");
    CHECK(crypto_ed25519_check(sig, pub, nullptr, 0) == 0);
}

UNIT_TEST(auto_update_manifest_round_trip) {
    const Manifest    in = SampleManifest();
    const std::string text = Signed(in);
    Manifest          m;
    std::string       error;
    CHECK(ParseManifest(text, m, error));
    CHECK_STREQ(m.version.c_str(), "1.2.1");
    CHECK_STREQ(m.commit.c_str(), "77a86958f3c1");
    CHECK_STREQ(m.platform.c_str(), "macos-arm64");
    CHECK_STREQ(m.url.c_str(), in.url.c_str());
    CHECK_EQ(m.size, static_cast<uint64_t>(12812809));
    CHECK_STREQ(m.sha256.c_str(), in.sha256.c_str());
    CHECK_EQ(m.signedBytes, ManifestBody(in).size());
    uint8_t secret[64];
    uint8_t pub[32];
    TestKey(secret, pub);
    CHECK(VerifyManifest(text, m, pub, error));
    // The release key does not sign the test key's manifests.
    CHECK(!VerifyManifest(text, m, kUpdatePublicKey, error));
}

UNIT_TEST(auto_update_manifest_tamper) {
    uint8_t secret[64];
    uint8_t pub[32];
    TestKey(secret, pub);
    const std::string text = Signed(SampleManifest());
    // Every changed byte of the signed part breaks the signature (or the parse).
    for (size_t i = 0; i < text.size(); i += 7) {
        std::string t = text;
        t[i] = static_cast<char>(t[i] ^ 0x01);
        Manifest    m;
        std::string error;
        if (!ParseManifest(t, m, error)) continue;
        CHECK(!VerifyManifest(t, m, pub, error));
    }
    // CRLF line ends parse, and the signature covers the bytes as sent: a CRLF copy of an LF-signed
    // manifest does not verify.
    std::string crlf;
    for (char c : text) {
        if (c == '\n') crlf += '\r';
        crlf += c;
    }
    Manifest    m;
    std::string error;
    CHECK(ParseManifest(crlf, m, error));
    CHECK(!VerifyManifest(crlf, m, pub, error));
}

UNIT_TEST(auto_update_manifest_rejects) {
    const std::string good = Signed(SampleManifest());
    std::string       why;
    CHECK(Parses(good));
    CHECK(!Parses(""));
    CHECK(!Parses("not a manifest\n"));
    CHECK(!Parses("openba-update 2\nversion=1.2.1\n", &why));
    CHECK_STREQ(why.c_str(), "unknown format 2");
    // A key twice, a line without '=', an empty line, text after the signature.
    std::string twice = good;
    twice.insert(twice.find("platform="), "version=1.2.2\n");
    CHECK(!Parses(twice, &why));
    CHECK(why.find("twice") != std::string::npos);
    std::string noEq = good;
    noEq.insert(noEq.find("platform="), "nonsense\n");
    CHECK(!Parses(noEq));
    std::string empty = good;
    empty.insert(empty.find("platform="), "\n");
    CHECK(!Parses(empty));
    CHECK(!Parses(good + "more=1\n"));
    CHECK(Parses(good + "\n"));   // a blank line after the signature is fine
    // Unknown keys are allowed (a later format may add some).
    std::string extra = good;
    extra.insert(extra.find("platform="), "notes=whatever\n");
    CHECK(Parses(extra));
    // Field shapes.
    Manifest bad = SampleManifest();
    bad.version = "1.2";
    CHECK(!Parses(Signed(bad)));
    bad = SampleManifest();
    bad.url = "ftp://example.com/x.zip";
    CHECK(!Parses(Signed(bad)));
    bad = SampleManifest();
    bad.sha256 = std::string(64, 'A');   // upper case
    CHECK(!Parses(Signed(bad)));
    bad = SampleManifest();
    bad.size = 0;
    CHECK(!Parses(Signed(bad)));
    bad = SampleManifest();
    bad.size = kMaxPackageBytes + 1;
    CHECK(!Parses(Signed(bad)));
    // No signature line.
    CHECK(!Parses(ManifestBody(SampleManifest()), &why));
    CHECK_STREQ(why.c_str(), "missing signature");
    // A control character in a value.
    std::string ctl = good;
    ctl.insert(ctl.find("macos-arm64"), "\x01");
    CHECK(!Parses(ctl));
}

UNIT_TEST(auto_update_plan_in_place) {
    StagingState s;
    CHECK(PlanInPlace(s, "1.2.0", true).step == InPlaceStep::Nothing);
    s.ready = "1.2.1";
    s.readyComplete = true;
    InPlacePlan p = PlanInPlace(s, "1.2.0", true);
    CHECK(p.step == InPlaceStep::Install);
    CHECK_STREQ(p.version.c_str(), "1.2.1");
    CHECK(PlanInPlace(s, "1.2.0", false).step == InPlaceStep::Discard);   // updates off
    CHECK(PlanInPlace(s, "1.2.1", true).step == InPlaceStep::Discard);    // not newer
    CHECK(PlanInPlace(s, "1.3.0", true).step == InPlaceStep::Discard);    // the player installed a newer one
    s.bad.insert("1.2.1");
    CHECK(PlanInPlace(s, "1.2.0", true).step == InPlaceStep::Discard);    // failed before
    s.bad.clear();
    s.readyComplete = false;
    CHECK(PlanInPlace(s, "1.2.0", true).step == InPlaceStep::Discard);    // incomplete
}

UNIT_TEST(auto_update_plan_versioned_promote_and_hand_over) {
    // The player's own bundle (1.2.0) with 1.2.1 staged: promote, hand over, first start pending.
    StagingState s;
    s.ready = "1.2.1";
    s.readyComplete = true;
    VersionedPlan p = PlanVersioned(s, "1.2.0", false, true);
    CHECK(p.promoteReady);
    CHECK(!p.discardReady);
    CHECK_STREQ(p.current.c_str(), "1.2.1");
    CHECK_STREQ(p.previous.c_str(), "");
    CHECK(p.stateChanged);
    CHECK_STREQ(p.run.c_str(), "1.2.1");
    CHECK(p.writePending);
    CHECK(p.markBad.empty());
    CHECK(p.notice.empty());

    // That hand-over's own launch (1.2.1 installed, pending 1.2.1): carry on.
    StagingState after;
    after.current = "1.2.1";
    after.currentComplete = true;
    after.pending = "1.2.1";
    p = PlanVersioned(after, "1.2.1", true, true);
    CHECK(p.run.empty());
    CHECK(p.markBad.empty());
    CHECK(!p.clearPending);

    // A later launch from the player's bundle once 1.2.1 has started: hand over, no pending.
    after.pending.clear();
    after.started = "1.2.1";
    p = PlanVersioned(after, "1.2.0", false, true);
    CHECK_STREQ(p.run.c_str(), "1.2.1");
    CHECK(!p.writePending);

    // Updates off: nothing new is promoted, but the installed build still runs.
    StagingState off = after;
    off.ready = "1.2.2";
    off.readyComplete = true;
    p = PlanVersioned(off, "1.2.0", false, false);
    CHECK(!p.promoteReady);
    CHECK(p.discardReady);
    CHECK_STREQ(p.run.c_str(), "1.2.1");

    // 1.2.2 staged while 1.2.1 is current: promoted with 1.2.1 as previous.
    p = PlanVersioned(off, "1.2.0", false, true);
    CHECK(p.promoteReady);
    CHECK_STREQ(p.current.c_str(), "1.2.2");
    CHECK_STREQ(p.previous.c_str(), "1.2.1");
    CHECK_STREQ(p.run.c_str(), "1.2.2");
    CHECK(p.writePending);

    // The player's bundle is newer than everything installed: it runs itself.
    p = PlanVersioned(after, "1.3.0", false, true);
    CHECK(p.run.empty());
    StagingState older = after;
    older.ready = "1.2.5";
    older.readyComplete = true;
    p = PlanVersioned(older, "1.3.0", false, true);
    CHECK(p.discardReady);
    CHECK(p.run.empty());
}

UNIT_TEST(auto_update_plan_versioned_rollback) {
    // 1.2.2 was handed over to and never reached its menu: bad, back to 1.2.1.
    StagingState s;
    s.current = "1.2.2";
    s.currentComplete = true;
    s.previous = "1.2.1";
    s.pending = "1.2.2";
    s.started = "1.2.1";
    VersionedPlan p = PlanVersioned(s, "1.2.0", false, true);
    CHECK_STREQ(p.markBad.c_str(), "1.2.2");
    CHECK(p.clearPending);
    CHECK_STREQ(p.current.c_str(), "1.2.1");
    CHECK_STREQ(p.previous.c_str(), "");
    CHECK(p.stateChanged);
    CHECK_STREQ(p.run.c_str(), "1.2.1");
    CHECK(!p.writePending);   // 1.2.1 has started before (started = 1.2.1)
    CHECK_STREQ(p.notice.c_str(), "OpenBA 1.2.2 did not start, so this is 1.2.1 again.");

    // The first install fails: back to the player's own bundle.
    StagingState first;
    first.current = "1.2.1";
    first.currentComplete = true;
    first.pending = "1.2.1";
    p = PlanVersioned(first, "1.2.0", false, true);
    CHECK_STREQ(p.markBad.c_str(), "1.2.1");
    CHECK_STREQ(p.current.c_str(), "");
    CHECK(p.run.empty());
    CHECK_STREQ(p.notice.c_str(), "OpenBA 1.2.1 did not start, so this is 1.2.0 again.");

    // A bad build staged again is not promoted; a newer one is.
    StagingState again;
    again.bad.insert("1.2.1");
    again.ready = "1.2.1";
    again.readyComplete = true;
    p = PlanVersioned(again, "1.2.0", false, true);
    CHECK(p.discardReady);
    CHECK(p.run.empty());
    again.ready = "1.2.2";
    p = PlanVersioned(again, "1.2.0", false, true);
    CHECK(p.promoteReady);
    CHECK_STREQ(p.run.c_str(), "1.2.2");

    // A bad build that runs anyway (Restart now re-execs it) hands over to current.
    StagingState stuck;
    stuck.current = "1.2.1";
    stuck.currentComplete = true;
    stuck.bad.insert("1.2.2");
    stuck.started = "1.2.1";
    p = PlanVersioned(stuck, "1.2.2", true, true);
    CHECK_STREQ(p.run.c_str(), "1.2.1");
}

UNIT_TEST(auto_update_staging_files) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path  dir = fs::temp_directory_path(ec) / "openba_auto_update_test";
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    CHECK(!ec);
    CHECK_STREQ(Staging::ReadLine(dir, "ready").c_str(), "");
    CHECK(Staging::WriteLine(dir, "ready", "1.2.1"));
    CHECK_STREQ(Staging::ReadLine(dir, "ready").c_str(), "1.2.1");
    CHECK(Staging::AddBad(dir, "1.2.2"));
    CHECK(Staging::AddBad(dir, "1.2.3"));
    CHECK(Staging::AddBad(dir, "1.2.2"));
    CHECK_EQ(Staging::ReadBad(dir).size(), static_cast<size_t>(2));
    // ready names a folder that does not hold the program yet: incomplete.
    StagingState s = Staging::Read(dir, "OpenBA.app", true);
    CHECK_STREQ(s.ready.c_str(), "1.2.1");
    CHECK(!s.readyComplete);
    fs::create_directories(dir / "staged" / "1.2.1" / "OpenBA.app", ec);
    s = Staging::Read(dir, "OpenBA.app", true);
    CHECK(s.readyComplete);
    CHECK(Staging::WriteLine(dir, "current", "1.2.0"));
    fs::create_directories(dir / "1.2.0" / "OpenBA.app", ec);
    s = Staging::Read(dir, "OpenBA.app", true);
    CHECK_STREQ(s.current.c_str(), "1.2.0");
    CHECK(s.currentComplete);
    Staging::Remove(dir, "ready");
    CHECK_STREQ(Staging::ReadLine(dir, "ready").c_str(), "");
    for (int i = 0; i < 1600; ++i) Staging::AppendLog(dir, "a line of the update log, number " + std::to_string(i));
    CHECK(fs::file_size(dir / "update.log", ec) <= 64 * 1024);
    fs::remove_all(dir, ec);
}
