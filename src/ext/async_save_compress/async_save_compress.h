#pragma once
// src/ext/async_save_compress/async_save_compress.h -- ext.async_save_compress (class A)
//
// NOT RETAIL (docs/extensions/handins/HITCH_SAVE.md). A save's 32 KB windows are compressed on one
// worker thread, in retail's order, with the stream's own Compression object, while the state is
// written; the archive Commit writes is byte-identical to the synchronous one.
//
// WHERE IT RUNS. The engine keeps retail's bodies and one engine-side seam,
// `IO::CompressedStream::_extAsyncCompress` (null by default; DESIGN.md §3 hook rule 5): Begin sets
// it for the attached stream, the end of the save (Commit, or Release without a Commit) clears it.
// Its three points (io/Stream.h): CompressWindow queues the window, SetWindow's in-memory branch
// serves a still-queued block from the job's copy, Commit / Release store every queued block and
// detach. The game side calls Begin in the save arm of GameUpdateLoop::SaveRestore and in
// ext.background_save's head, right after CompressedMemoryStream::Create, when the row is on.

namespace IO { class CompressedStream; }

namespace Ext { namespace AsyncSaveCompress {

// Attach `cs` (a writing stream with its Compression object; one stream at a time, any other case
// keeps retail's synchronous body): from here on its CompressWindow is a job for the worker.
void Begin(IO::CompressedStream& cs);

// The last attached save's counters, for the stress harness's [Stress::save] line.
struct Stats { int jobs; int copies; int maxQueued; double mainCopyMs; double fullWaitMs; double endWaitMs; double workerMs; };
Stats LastStats();

}}  // namespace Ext::AsyncSaveCompress
