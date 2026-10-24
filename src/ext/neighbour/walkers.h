#pragma once
// ============================================================================
// src/ext/neighbour/walkers.h -- NOT RETAIL. ext.neighbour: the neighbour's villagers, seen
// ============================================================================
// docs/fork/NEIGHBOUR_PROTOCOL.md `walkers`. Each town simulates only its own people. Once a second it
// sends where they are: position, facing, the animation each plays, and the body (man or woman, child
// size, clothes). The neighbour draws them as see-through villagers with the game's own model and
// animations, moving smoothly between the samples (so they show about a second late). Nothing of them
// is simulated here: they cannot be clicked, do not block paths and are not saved. A lobby game only
// (the shared map).
// ============================================================================

namespace Neighbour {

class Link;
class Message;

namespace Walkers {

// A game starts (a lobby game: `on`) or ends. Begin drops every villager of the last game; End frees
// them without touching the scene (it goes with the game).
void Begin(bool on);
void End();

// Main thread, every frame: our villagers go out once a second while the neighbour is online; the
// neighbour's move and play their animations (standing still while `paused`).
void Tick(Link* link, bool peerOnline, bool paused);

// The neighbour's `walkers`: true when it was one.
bool OnMessage(const Message& m);

// The neighbour left: its villagers go (the scene stays).
void Clear();

}  // namespace Walkers
}  // namespace Neighbour
