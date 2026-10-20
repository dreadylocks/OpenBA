// ============================================================================
// src/ext/savetail.h -- the class B / C manifest at the tail of block 1
// ============================================================================
// NOT RETAIL. docs/extensions/DESIGN.md §2, handins/SAVETAIL.md §1 / §4,
// handins/EXTSAVE.md.
//
// Every ACTIVE class B / C extension is recorded in a small chunk written
// into the save's game-state block (block 1) AFTER the last faithful
// Serialize and before the compressed stream closes
// (GameUpdateLoop::SaveRestore, src/game/Game/gameupdateloop_serialize.cpp).
// Retail's reader stops where it always did and never sees it; the block's
// size and the file CRC cover it automatically (SAVETAIL §1). Our loader
// reads it back and locks the game's B / C set from it
// (Ext::LockForGameWithSet), which is the one route every load takes -- the
// scenario / tutorial / autoload routes never read block 0's mod list.
//
// NO B / C EXTENSION ACTIVE = ZERO BYTES. Encode of an empty set is an empty
// vector: no header, nothing. The resave gate byte-compares the whole
// decompressed block 1, so the faithful profile must not write one byte.
//
// THE WIRE FORMAT (little-endian; version 1):
//   u32  magic          0x53545845 ("EXTS" as bytes)
//   u32  version        1 (kVersion)
//   u32  count
//   count x {
//     u32  idLength     1..kMaxIdLength, the bytes of the id, no NUL
//     u8   id[idLength] [a-z0-9_]+ (Ext::ValidId)
//     i32  formatVersion  the extension's format version, >= 1
//     u32  payloadLength  0 unless the extension registered a payload writer
//     u8   payload[payloadLength]   (RegisterPayload below)
//   }
//   u32  bodyLength     the byte count of everything above (magic .. last payload)
//   u32  crc            CRC-32 (IEEE, reflected 0xEDB88320) of those bytes
// The 8-byte trailer is fixed for every version: a reader finds it from the
// END, so a cut tail (bodyLength != what is there) reads as Truncated and a
// damaged one as BadChecksum before a single record is trusted.
//
// Standard C++ only: tests/unit/ext_savetail_test.cpp links this file with
// registry.cpp alone.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Ext {
namespace SaveTail {

constexpr uint32_t kMagic        = 0x53545845u;   // 'E' 'X' 'T' 'S'
constexpr uint32_t kVersion      = 1;
constexpr uint32_t kMaxIdLength  = 64;
constexpr uint32_t kTrailerBytes = 8;

struct Record {
    std::string          id;
    int32_t              formatVersion = 0;
    std::vector<uint8_t> payload;   // the extension's own state (RegisterPayload), else empty
};

// A class C extension's own state in its record. The writer runs when a save is
// written -- possibly on the background-save worker thread, so it must only read
// state it can copy safely -- and returns the payload bytes. The reader runs when
// a save's manifest is accepted (ExtSave_ReadTail), on the main thread, with the
// record's formatVersion; it must treat the bytes as untrusted. One pair per id,
// registered once at boot; registering an id again replaces the pair.
using PayloadWriter = std::vector<uint8_t> (*)();
using PayloadReader = void (*)(int32_t formatVersion, const std::vector<uint8_t>& payload);
void RegisterPayload(const char* id, PayloadWriter writer, PayloadReader reader);
// Hand each record's payload to its id's reader (records with no reader: skipped).
void DeliverPayloads(const std::vector<Record>& records);

enum class Status : uint8_t {
    NoManifest,          // nothing there, or bytes that do not start with kMagic: not ours, ignored
    Ok,
    Truncated,           // starts with kMagic but shorter than its trailer says
    BadChecksum,         // the trailer's CRC does not match the body
    NewerChunkVersion,   // the chunk's version is newer than kVersion
    Malformed,           // a record overruns the body, a bad id, bytes left over, formatVersion < 1
    UnknownId,           // Validate: no registered extension has this id
    NotLockedClass,      // Validate: the id is a fix or a class A extension
    NewerFormatVersion,  // Validate: the save's formatVersion is newer than ours
    DuplicateId,         // Validate: an id named twice
};
const char* StatusName(Status s);

// The chunk for `records`; EMPTY (zero bytes) when `records` is empty.
std::vector<uint8_t> Encode(const std::vector<Record>& records);

// Parse `size` bytes (everything after the last faithful Serialize). On Ok
// `out` holds the records; on anything else it is cleared.
Status Decode(const uint8_t* data, size_t size, std::vector<Record>& out);

// Check decoded records against the registry: every id a registered class B /
// C extension, its formatVersion not newer than ours, no id twice. On failure
// `offending` (when given) names the record.
Status Validate(const std::vector<Record>& records, std::string* offending);

// The registry's ACTIVE class B / C extensions as records, in table order
// (the same set Ext::PseudoModsForSave reports), each with its registered
// writer's payload.
std::vector<Record> ActiveRecords();
// Encode(ActiveRecords()): zero bytes while no B / C extension is active.
std::vector<uint8_t> EncodeActive();

// CRC-32 (IEEE 802.3, reflected, init/xorout 0xFFFFFFFF). "123456789" -> 0xCBF43926.
uint32_t Crc32(const uint8_t* data, size_t size);

}  // namespace SaveTail
}  // namespace Ext
