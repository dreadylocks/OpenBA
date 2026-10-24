// ============================================================================
// src/ext/neighbour/world_maths.cpp -- NOT RETAIL. ext.neighbour's portable-worldgen switch (world_maths.h)
// ============================================================================

#include "ext/neighbour/world_maths.h"

namespace Neighbour {

namespace {
bool s_portable = false;   // main thread: set in Load before worldgen, read by worldgen's callers
}  // namespace

bool PortableWorldMaths() { return s_portable; }

void SetPortableWorldMaths(bool on) { s_portable = on; }

}  // namespace Neighbour
