#pragma once
// src/ext/decode_ahead/decode_ahead.h -- ext.decode_ahead (class A, next start)
//
// NOT RETAIL (docs/extensions/handins/DECODEAHEAD.md). Retail's loader decodes a resource's 32 KB
// LZF blocks one at a time, on the loader thread, as CompressedStream::Read reaches them. With the
// row on, when a stream over a mapped package decodes block b, the next kAhead compressed blocks of
// that stream are queued for a small worker pool; when the stream reaches one of them, the decoded
// bytes are copied into the window instead of decoding there. The bytes are the same (the same
// Decompress over the same compressed bytes, which FileSystem::PackedMapSpan proves a ReadFile would
// have returned in full), the window's start / end and the source stream's cursor are what retail's
// body leaves, and the order of every read, Serialize and External::Load is unchanged: only WHERE
// a block is decoded moves.
//
// WHERE IT RUNS. The engine seam IO::CompressedStream::_extDecodeAhead (Stream.h; null by default,
// DESIGN.md §3 hook rule 5), set once by Sync at boot from the row's value: DecompressFromSource asks
// it for the block (point kExtDecodeBlock), Release drops the stream's blocks (kExtDecodeRelease), so
// no job outlives its stream, and none outlives the package mapping it reads (the mapping is released
// at CloseFile, after every stream over it is gone).

namespace Ext { namespace DecodeAhead {

// The seam follows Ext().decode_ahead (boot only: a toggle takes effect at the next start). With
// ext.create_on_demand on as well the seam stays null (one [DecodeAhead] line says so): the loader
// then reads only the blocks around the payloads it skips, and a block queued ahead is a block the
// loader waits for (docs/extensions/handins/STREAMCACHE.md section 2).
void Sync();

}}  // namespace Ext::DecodeAhead
