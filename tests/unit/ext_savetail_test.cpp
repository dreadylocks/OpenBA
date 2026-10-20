// ============================================================================
// tests/unit/ext_savetail_test.cpp -- the class B / C save machinery
// ============================================================================
// src/ext/savetail.{h,cpp} with src/ext/registry.cpp (block EXTSAVE,
// docs/extensions/handins/EXTSAVE.md): the block-1 tail manifest's encode /
// decode round trip, ZERO bytes when no B / C extension is active, a cut or
// damaged tail detected, an unknown id / a newer format version rejected,
// the lock taken from a manifest overriding the requested state, and the
// load notice's verdict on an `ext:` pseudo-mod. The `unit_*` extensions
// exist only under BANISHED_UNIT_TEST (unit_b class B v1, unit_c class C v3).
// ============================================================================
#include "unit.h"
#include "ext/registry.h"
#include "ext/savetail.h"

#include <cstring>
#include <string>
#include <vector>

namespace {

using Ext::SaveTail::Record;
using Ext::SaveTail::Status;

const Ext::Entry& E(const char* id) {
    const Ext::Entry* e = Ext::FindEntry(id);
    if (e == nullptr) { std::printf("  no entry %s\n", id); std::abort(); }
    return *e;
}

// A requested state with the given extensions on (everything else default).
Ext::State With(std::initializer_list<const char*> on) {
    Ext::State s = Ext::Resolve(nullptr, nullptr, nullptr, nullptr);
    for (const char* id : on) E(id).slot(s) = true;
    return s;
}

Record Rec(const char* id, int32_t v) {
    Record r;
    r.id = id;
    r.formatVersion = v;
    return r;
}

}  // namespace

UNIT_TEST(ext_savetail_crc32_reference) {
    const uint8_t text[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CHECK_EQ(Ext::SaveTail::Crc32(text, sizeof text), 0xCBF43926u);
}

UNIT_TEST(ext_savetail_round_trip) {
    std::vector<Record> in = {Rec("unit_b", 1), Rec("unit_c", 3)};
    in[1].payload = {1, 2, 3, 0xff};
    const std::vector<uint8_t> bytes = Ext::SaveTail::Encode(in);
    // magic, version, count; the trailer at the end.
    CHECK(bytes.size() > 12 + Ext::SaveTail::kTrailerBytes);
    CHECK_EQ(bytes[0], 'E');
    CHECK_EQ(bytes[1], 'X');
    CHECK_EQ(bytes[2], 'T');
    CHECK_EQ(bytes[3], 'S');

    std::vector<Record> out;
    CHECK(Ext::SaveTail::Decode(bytes.data(), bytes.size(), out) == Status::Ok);
    CHECK_EQ(static_cast<int>(out.size()), 2);
    CHECK_STREQ(out[0].id.c_str(), "unit_b");
    CHECK_EQ(out[0].formatVersion, 1);
    CHECK(out[0].payload.empty());
    CHECK_STREQ(out[1].id.c_str(), "unit_c");
    CHECK_EQ(out[1].formatVersion, 3);
    CHECK(out[1].payload == in[1].payload);
    CHECK(Ext::SaveTail::Validate(out, nullptr) == Status::Ok);
}

UNIT_TEST(ext_savetail_zero_bytes_when_empty) {
    // Encode of nothing is nothing: no header.
    CHECK(Ext::SaveTail::Encode({}).empty());

    // No B / C active (class A on, fixes on): the save tail is zero bytes.
    Ext::Install(With({"unit_a"}), Ext::Profile::PerEntry, false);
    CHECK(Ext::SaveTail::ActiveRecords().empty());
    CHECK(Ext::SaveTail::EncodeActive().empty());

    // The faithful profile, whatever the per-entry keys say: zero bytes, and
    // no pseudo-mod provider at all.
    Ext::State all = With({"unit_a", "unit_b", "unit_c"});
    Ext::State faithful = Ext::Resolve(nullptr, nullptr, "faithful", nullptr);
    Ext::Install(faithful, Ext::Profile::Faithful, true);
    CHECK(Ext::SaveTail::EncodeActive().empty());
    CHECK(Ext::SaveModsProvider() == nullptr);

    // B and C on: both recorded, in table order; class A never.
    Ext::Install(all, Ext::Profile::PerEntry, false);
    const std::vector<Record> active = Ext::SaveTail::ActiveRecords();
    CHECK_EQ(static_cast<int>(active.size()), 2);
    CHECK_STREQ(active[0].id.c_str(), "unit_b");
    CHECK_EQ(active[0].formatVersion, 1);
    CHECK_STREQ(active[1].id.c_str(), "unit_c");
    CHECK_EQ(active[1].formatVersion, 3);
    std::vector<Record> back;
    const std::vector<uint8_t> bytes = Ext::SaveTail::EncodeActive();
    CHECK(Ext::SaveTail::Decode(bytes.data(), bytes.size(), back) == Status::Ok);
    CHECK_EQ(static_cast<int>(back.size()), 2);

    Ext::Install(With({}), Ext::Profile::PerEntry, false);
}

UNIT_TEST(ext_savetail_truncated_and_garbage) {
    const std::vector<uint8_t> good = Ext::SaveTail::Encode({Rec("unit_b", 1), Rec("unit_c", 3)});
    std::vector<Record> out;

    // Every cut of a real manifest is detected, never read as a shorter one.
    for (size_t n = 1; n < good.size(); ++n) {
        const Status st = Ext::SaveTail::Decode(good.data(), n, out);
        CHECK(st == Status::Truncated || st == Status::BadChecksum);
        CHECK(out.empty());
    }

    // A flipped byte in the body: the CRC.
    std::vector<uint8_t> bad = good;
    bad[14] ^= 0x20;
    CHECK(Ext::SaveTail::Decode(bad.data(), bad.size(), out) == Status::BadChecksum);
    CHECK(out.empty());

    // Extra bytes after the trailer: the trailer no longer matches.
    bad = good;
    bad.push_back(0);
    CHECK(Ext::SaveTail::Decode(bad.data(), bad.size(), out) == Status::Truncated);

    // Not ours (no magic): ignored, not an error.
    const uint8_t garbage[] = {0xAA, 0xAA, 0xAA, 0xAA, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    CHECK(Ext::SaveTail::Decode(garbage, sizeof garbage, out) == Status::NoManifest);
    CHECK(Ext::SaveTail::Decode(nullptr, 0, out) == Status::NoManifest);
    CHECK(Ext::SaveTail::Decode(garbage, 0, out) == Status::NoManifest);

    // A newer chunk version, CRC fixed up: refused.
    bad = good;
    bad[4] = 2;
    const uint32_t crc = Ext::SaveTail::Crc32(bad.data(), bad.size() - 8);
    for (int i = 0; i < 4; ++i) bad[bad.size() - 4 + i] = static_cast<uint8_t>(crc >> (8 * i));
    CHECK(Ext::SaveTail::Decode(bad.data(), bad.size(), out) == Status::NewerChunkVersion);

    // A bad id inside a well-formed chunk (CRC right): malformed.
    std::vector<uint8_t> dotted = Ext::SaveTail::Encode({Rec("unit_b", 1)});
    dotted[16] = '.';   // 12-byte header, 4-byte id length, then the id
    const uint32_t crc2 = Ext::SaveTail::Crc32(dotted.data(), dotted.size() - 8);
    for (int i = 0; i < 4; ++i) dotted[dotted.size() - 4 + i] = static_cast<uint8_t>(crc2 >> (8 * i));
    CHECK(Ext::SaveTail::Decode(dotted.data(), dotted.size(), out) == Status::Malformed);
}

UNIT_TEST(ext_savetail_validate_rejects) {
    std::string who;
    CHECK(Ext::SaveTail::Validate({Rec("no_such_ext", 1)}, &who) == Status::UnknownId);
    CHECK_STREQ(who.c_str(), "no_such_ext");
    CHECK(Ext::SaveTail::Validate({Rec("unit_b", 2)}, &who) == Status::NewerFormatVersion);
    CHECK_STREQ(who.c_str(), "unit_b");
    CHECK(Ext::SaveTail::Validate({Rec("unit_c", 3)}, nullptr) == Status::Ok);   // equal: fine
    CHECK(Ext::SaveTail::Validate({Rec("unit_c", 2)}, nullptr) == Status::Ok);   // older: fine
    CHECK(Ext::SaveTail::Validate({Rec("unit_a", 1)}, nullptr) == Status::NotLockedClass);
    CHECK(Ext::SaveTail::Validate({Rec("icon_slide", 1)}, nullptr) == Status::NotLockedClass);
    CHECK(Ext::SaveTail::Validate({Rec("unit_b", 1), Rec("unit_b", 1)}, nullptr) ==
          Status::DuplicateId);
    CHECK(Ext::SaveTail::Validate({}, nullptr) == Status::Ok);
}

UNIT_TEST(ext_savetail_lock_from_manifest) {
    // The player requests unit_b only.
    Ext::Install(With({"unit_b"}), Ext::Profile::PerEntry, false);
    int game = 0;
    Ext::LockForGame(&game);                    // GameUpdateLoop::Load's entry lock
    CHECK(Ext::Ext().unit_b && !Ext::Ext().unit_c);

    // The save says unit_c only: the game runs exactly the save's set.
    CHECK(Ext::LockForGameWithSet(&game, {"unit_c"}));
    CHECK(!Ext::Ext().unit_b);
    CHECK(Ext::Ext().unit_c);
    CHECK(Ext::Locked());
    CHECK(Ext::EntryLocked(E("unit_b")));
    CHECK(Ext::EntryLocked(E("unit_c")));
    CHECK(!Ext::EntryLocked(E("unit_a")));       // class A stays live
    // The request is untouched, and a toggle in game changes only the request.
    CHECK(Ext::Requested(E("unit_b")));
    CHECK(!Ext::Requested(E("unit_c")));
    CHECK(!Ext::SetRequested(E("unit_c"), false));
    CHECK(Ext::Ext().unit_c);
    // The manifest is what the next save writes.
    const std::vector<Record> active = Ext::SaveTail::ActiveRecords();
    CHECK_EQ(static_cast<int>(active.size()), 1);
    CHECK_STREQ(active[0].id.c_str(), "unit_c");

    // An id that is not a B / C extension: refused, nothing changed.
    CHECK(!Ext::LockForGameWithSet(&game, {"unit_b", "no_such_ext"}));
    CHECK(!Ext::LockForGameWithSet(&game, {"unit_a"}));
    CHECK(!Ext::Ext().unit_b && Ext::Ext().unit_c);

    // Unlock: back at the menu the active state is the request again.
    Ext::UnlockForGame(&game);
    CHECK(!Ext::Locked());
    CHECK(Ext::Ext().unit_b);
    CHECK(!Ext::Ext().unit_c);

    // An empty manifest set locks every B / C off.
    Ext::LockForGameWithSet(&game, {});
    CHECK(!Ext::Ext().unit_b && !Ext::Ext().unit_c);
    Ext::UnlockForGame(&game);
    Ext::Install(With({}), Ext::Profile::PerEntry, false);
}

UNIT_TEST(ext_savetail_load_notice_status) {
    Ext::Install(With({"unit_b"}), Ext::Profile::PerEntry, false);
    CHECK(Ext::IsPseudoModPath("ext:unit_b"));
    CHECK(!Ext::IsPseudoModPath("Mods/foo.pkm"));
    CHECK(!Ext::IsPseudoModPath("ex"));
    CHECK(!Ext::IsPseudoModPath(nullptr));
    CHECK_EQ(Ext::PseudoModLoadStatus("ext:unit_b", 1), 0);   // present, requested
    CHECK_EQ(Ext::PseudoModLoadStatus("ext:unit_b", 2), 1);   // saved by a newer format
    CHECK_EQ(Ext::PseudoModLoadStatus("ext:unit_c", 1), 1);   // present, not requested
    CHECK_EQ(Ext::PseudoModLoadStatus("ext:nope", 1), 1);     // missing
    CHECK_EQ(Ext::PseudoModLoadStatus("ext:unit_a", 1), 1);   // class A is never in a save
    CHECK_EQ(Ext::PseudoModLoadStatus("Mods/x.pkm", 1), 1);
    Ext::Install(With({}), Ext::Profile::PerEntry, false);
}

// RegisterPayload: an active class C extension's writer fills its record, the
// payload survives Encode / Decode, and DeliverPayloads hands it to the reader
// with the record's format version; no writer = an empty payload, as before.
namespace {
std::vector<uint8_t> g_unitCReceived;
int32_t              g_unitCVersion = 0;
std::vector<uint8_t> UnitCWrite() { return {7, 8, 9}; }
void UnitCRead(int32_t v, const std::vector<uint8_t>& p) { g_unitCVersion = v; g_unitCReceived = p; }
}  // namespace

UNIT_TEST(ext_savetail_payload_hooks) {
    Ext::Install(With({"unit_b", "unit_c"}), Ext::Profile::PerEntry, false);
    Ext::SaveTail::RegisterPayload("unit_c", &UnitCWrite, &UnitCRead);

    const std::vector<Record> active = Ext::SaveTail::ActiveRecords();
    CHECK_EQ(static_cast<int>(active.size()), 2);
    CHECK(active[0].payload.empty());                            // unit_b: no writer
    CHECK(active[1].payload == std::vector<uint8_t>({7, 8, 9}));  // unit_c: its writer

    std::vector<Record> back;
    const std::vector<uint8_t> bytes = Ext::SaveTail::EncodeActive();
    CHECK(Ext::SaveTail::Decode(bytes.data(), bytes.size(), back) == Status::Ok);
    g_unitCReceived.clear();
    g_unitCVersion = 0;
    Ext::SaveTail::DeliverPayloads(back);
    CHECK_EQ(g_unitCVersion, 3);
    CHECK(g_unitCReceived == std::vector<uint8_t>({7, 8, 9}));

    // Unregistering (null hooks) restores the empty payload.
    Ext::SaveTail::RegisterPayload("unit_c", nullptr, nullptr);
    CHECK(Ext::SaveTail::ActiveRecords()[1].payload.empty());
    Ext::Install(With({}), Ext::Profile::PerEntry, false);
}
