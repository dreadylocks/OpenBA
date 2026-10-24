#pragma once
// ============================================================================
// src/ext/neighbour/save_state.h -- NOT RETAIL. ext.neighbour's per-town state in the save tail
// ============================================================================
// docs/fork/NEIGHBOUR_TOWNS.md, docs/extensions/DESIGN.md "Class C state: the contract". The state
// is protocol.h's SavedState (the room, how to get back in, parcels in flight); its wire format is
// EncodeState / DecodeState there. The save tail (src/ext/savetail.h) reaches it only through
// Codec(), registered in src/ext/state_codecs.cpp.
//
// THREADS. The codec's encode may run on the background-save worker; neighbour.cpp reads and
// changes the state on the main thread. Every access holds SavedMutex().
//
// Standard C++ only: the unit tests link this file with protocol.cpp and savetail.cpp.
// ============================================================================

#include <mutex>

#include "ext/neighbour/protocol.h"

namespace Ext { namespace SaveTail { struct StateCodec; } }

namespace Neighbour {

std::mutex& SavedMutex();
SavedState& Saved();          // guarded by SavedMutex()
bool&       LoadedFromSave(); // guarded by SavedMutex(): the codec decoded a save's record

// The save-tail codec ("neighbour", formatVersion kPayloadFormat). Decode refuses a payload
// DecodeState cannot read (the save tail then refuses the save: the town's state is damaged).
const Ext::SaveTail::StateCodec& Codec();

}  // namespace Neighbour
