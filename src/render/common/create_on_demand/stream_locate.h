#pragma once
// ============================================================================
// src/render/common/create_on_demand/stream_locate.h -- ext.create_on_demand: the Locator of the
// next bytes a loading serializer reads (docs/extensions/handins/CREATEONDEMAND.md, issue #13)
// ============================================================================
// NOT RETAIL. A resource file in a package is read by External::Load through a CompressedFileStream:
// a CompressedStream whose source is the FileStream over the packed entry. When a backend is about
// to read a GPU payload (GpuBuffer_Serialize's SerializeBytes, a texture level's byte array) from
// such a stream, Locate says where those bytes live (payload.h) and Skip moves the stream past them
// without decoding them: the stream's position is then what the read would have left, and its next
// Read decodes the block it needs, as every Read does (CompressedStream::Read starts with
// SetWindow(_position)). Anything else (a loose file, a nested package, a memory stream, a write)
// is not locatable: the caller reads the bytes as retail does.
// ============================================================================

#include <cstdint>

#include "create_on_demand/payload.h"

namespace IO { class Stream; }

namespace CreateOnDemand {

// The payload of `len` bytes at `stream`'s current position. True: `*out` holds it (and a reference
// on its package's mapping). The mapping's block directory is checked against the stream's own.
bool Locate(IO::Stream* stream, int32_t len, Locator* out);

// Advance a stream Locate accepted past `len` bytes, as a Read of them would.
void Skip(IO::Stream* stream, int32_t len);

// EXT SEAM (NOT RETAIL, docs/extensions/handins/STREAMCACHE.md): a stream that is not a
// CompressedStream but knows where its payloads live (ext.stream_cache's record stream, read in
// the decompressed stream's coordinates). Locate asks g_altLocate when the stream is not a
// CompressedStream; Skip asks g_altSkip first (true = done). Both null by default.
using AltLocateFn = bool (*)(IO::Stream* stream, int32_t len, Locator* out);
using AltSkipFn   = bool (*)(IO::Stream* stream, int32_t len);
extern AltLocateFn g_altLocate;
extern AltSkipFn   g_altSkip;

}  // namespace CreateOnDemand
