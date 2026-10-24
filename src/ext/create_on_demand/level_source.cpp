// ============================================================================
// src/ext/create_on_demand/level_source.cpp -- NOT RETAIL. ext.create_on_demand's level reader
// ============================================================================
// Class A (docs/extensions/handins/CREATEONDEMAND.md). Installed as Video::ImageBuffer::
// _extLevelSource (ImageBuffer.h has the seam's contract) while the row is on.
//
// Retail's PlatformSerialize reads, per level, the blob's size (Serialize(int&)), sizes the level
// array and reads the bytes into it (RtVideo_SerializeBlobArray), then calls Create with the levels.
// For a texture seeded from a packed resource file (usage 2, not degenerate: the images whose
// ImageBufferData::Create makes a texture from levels) this reads each size the same way and skips
// the bytes instead, leaving one Locator per level for the backend's Create on this thread
// (pending_levels.h). The level arrays stay empty: their memory is never allocated.
//
// A level the reader cannot locate (an impossible size) ends the skipping: it and every later level
// are read as retail reads them, and the levels already skipped are re-decoded into their arrays,
// so Create sees every byte, the eager way. Anything not a packed compressed stream is left to
// retail's read untouched (false, before anything is consumed).
// ============================================================================

#include "ext/create_on_demand/level_source.h"

#include "create_on_demand/pending_levels.h"
#include "create_on_demand/stream_locate.h"
#include "io/Serializer.h"
#include "video/ImageBuffer.h"

namespace Ext { namespace CreateOnDemand {

bool ReadLevels(IO::Serializer& ser, Video::ImageBuffer& image, int32_t count, Video::ImageBuffer::LevelArray& levels) {
    // The images ImageBufferData::Create turns into a texture from their levels.
    if (count <= 0 || image._kind != 2 || image.Width() == 0 || image.Height() == 0) return false;
    IO::Stream* const stream = ser._stream;
    {
        ::CreateOnDemand::Locator probe;
        if (!::CreateOnDemand::Locate(stream, 0, &probe)) return false;
    }
    ::CreateOnDemand::PendingLevels& pending = ::CreateOnDemand::ThreadPendingLevels();
    pending.Clear();
    pending.levels.reserve(static_cast<size_t>(count));
    Collection::Array<uint8_t>* const e = levels.GetData();
    for (int32_t i = 0; i < count; ++i) {
        // Retail's size read (RtVideo_SerializeBlobArray seeds it with the empty array's size).
        int32_t size = e[i]._size;
        ser.Serialize(size, nullptr);
        ::CreateOnDemand::Locator loc;
        if (size >= 0 && ::CreateOnDemand::Locate(stream, size, &loc)) {
            ::CreateOnDemand::Skip(stream, size);
            pending.levels.push_back(std::move(loc));
            continue;
        }
        // Not locatable: this level and the rest the retail way, the skipped ones re-decoded.
        e[i].SetSize(size);
        ser.Serialize(size, e[i]._data, nullptr);
        RtVideo_SerializeBlobArray(ser, count - i - 1, e + i + 1);
        for (int32_t k = 0; k < i; ++k) {
            const ::CreateOnDemand::Locator& skipped = pending.levels[static_cast<size_t>(k)];
            e[k].SetSize(skipped.len);
            ::CreateOnDemand::Redecode(skipped, e[k]._data);
        }
        pending.Clear();
        return true;
    }
    pending.image = &image;
    return true;
}

}}  // namespace Ext::CreateOnDemand
