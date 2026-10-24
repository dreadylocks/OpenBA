#pragma once
// ============================================================================
// src/render/common/create_on_demand/payload.h -- ext.create_on_demand: where a GPU payload lives
// in its package, and how to read it back (docs/extensions/handins/CREATEONDEMAND.md, issue #13)
// ============================================================================
// NOT RETAIL. Backend-neutral, engine-free (unit-tested: tests/unit/ext_create_on_demand_test.cpp).
//
// A compiled resource in a package is one compressed stream (External::Load opens it as a
// CompressedFileStream over the packed entry):
//     entry + 0         int32 block count n
//     entry + 4         n x { uint32 sourceOffset, uint16 compressedSize, uint16 uncompressedSize }
//     entry + offset    the blocks, LZ-compressed (compressedSize 0 = stored)
// and every block holds 32 KB of the DECOMPRESSED stream, so decompressed position p lies in block
// p >> 15. A GPU payload (a vertex / index buffer's SerializeBytes, a texture level's byte array) is
// a contiguous range [pos, pos + len) of that stream: a Locator { package mapping, entry offset,
// entry size, pos, len } is all it takes to produce the same bytes again later, decoding only the
// blocks that cover the range (Redecode). Proven on every entry of a MegaMod package by the spike
// (branch issue13/redecode-spike: 12,532 re-decodes, 0 mismatches).
//
// MAPPINGS. The engine's own package mappings (MAPREAD, filesystem_openfile.cpp) go away with the
// package handle (ClosePackages, a remount) while the resources loaded from it stay alive. So the
// extension maps each package itself, read-only, from the engine's handle the first time a payload
// of it is located (a view stays valid after the handle is closed, on Windows and POSIX alike), and
// counts its users: a mapping lives while any Locator holds it. When the engine releases that
// handle, the mapping is DETACHED (never handed out for that handle value again, which the OS may
// reuse) and unmapped once its last Locator goes.
// ============================================================================

#include <cstdint>
#include <mutex>
#include <vector>

#include "platform_file.h"

namespace CreateOnDemand {

constexpr int32_t kBlockBytes = 0x8000;   // RtIo::kCompressedStreamWindowSize

// One package mapping, shared by the Locators of its payloads.
struct PackageMap {
    Platform::FileHandle  handle = nullptr;   // the engine's handle it was made from; null = detached
    Platform::FileMapping map;
    int64_t               users = 0;          // Locators holding it (the table's mutex)
};

// The mappings, by the engine's package handle.
class PackageMaps {
public:
    // A user reference on the mapping of `h`, made on first use (null when the file does not map).
    PackageMap* Acquire(Platform::FileHandle h);
    // One user less; a detached mapping goes with its last user.
    void Release(PackageMap* m);
    // The engine is closing `h` (PackedMapRelease): its mapping is never handed out again.
    void Detach(Platform::FileHandle h);

    struct Stats {
        int64_t live     = 0;   // mappings held
        int64_t detached = 0;   // of them, detached and still used
        int64_t opened   = 0;   // made, run total
    };
    Stats GetStats();

    ~PackageMaps();

private:
    std::mutex               _mutex;
    std::vector<PackageMap*> _maps;
    int64_t                  _opened = 0;
};

// The process's one table (ext.create_on_demand's).
PackageMaps& Maps();

// Where a payload lives. Holds a user reference on `map` while `map` is set (Set / Clear).
struct Locator {
    PackageMap* map   = nullptr;
    uint64_t    entry = 0;   // the entry's absolute offset in the package
    uint32_t    size  = 0;   // the entry's byte size
    int32_t     pos   = 0;   // the payload's start in the entry's decompressed stream
    int32_t     len   = 0;   // its byte count

    Locator() = default;
    Locator(const Locator&) = delete;
    Locator& operator=(const Locator&) = delete;
    Locator(Locator&& other) noexcept;
    Locator& operator=(Locator&& other) noexcept;
    ~Locator();
    void Clear();
};

// True when [pos, pos + len) is a valid range of the stream whose entry bytes are
// `entry[0 .. entrySize)`: the block directory is inside the entry and covers the range.
bool RangeInStream(const uint8_t* entry, uint64_t entrySize, int32_t pos, int32_t len);

// Re-produce [pos, pos + len) of the entry's decompressed stream into `dst` from the entry's bytes
// alone: one Decompress (or copy, for a stored block) per covering block. False on a malformed
// directory or block (a block whose decoded size is not its header's); `dst` is then undefined.
bool RedecodeRange(const uint8_t* entry, uint64_t entrySize, int32_t pos, int32_t len, uint8_t* dst);

// The same through a Locator's mapping.
bool Redecode(const Locator& loc, uint8_t* dst);

}  // namespace CreateOnDemand
