#pragma once
// ============================================================================
// src/ext/neighbour/lobby_panel.h -- NOT RETAIL. ext.neighbour M2: the Multiplayer lobby's UI
// ============================================================================
// Dialog/ExtensionsLobby.rsc. The main menu (front end only) gets a Multiplayer button that opens
// the lobby: every room on the relay, Create room (then the game's own New Game dialog; the room
// is made with what the player picks there, MainMenuDialog::StartNewGame's EXT SEAM), Join (the
// room's seed and settings start the new game directly). See neighbour.h LobbyStart.
// ============================================================================

#include <cstdint>
#include <string>

class MainMenuDialog;
class MapGenerator;
class TerrainGenerator;
class ClimateGenerator;

namespace Neighbour {

constexpr int32_t kMultiplayerMenuButtonId = 14;   // after Extensions (13)

// EXT SEAM, MainMenuDialog::OnOpened (beside ExtensionsDialog_AttachMenuButton): the button, on
// the front-end menu only (not the pause menu, not under the gate's forced faithful profile).
void Lobby_AttachMenuButton(MainMenuDialog* menu);
// The button's click (MainMenuDialog::OnElementEvent, id 14), or a refused create coming back:
// `note` (when not empty) is shown in the lobby's status line.
void Lobby_Open(MainMenuDialog* menu, const std::string& note);

// EXT SEAM, MainMenuDialog::StartNewGame (its first lines): a game joining a lobby room starts with the
// ROOM's seed and settings whoever asked for the new game (the lobby's Join already passes them; an
// unattended join, neighbour.autolobby=join with BANISHED_AUTOSTART, comes through the New Game dialog's
// defaults). No pending join, or settings this game cannot resolve: nothing changes.
void Lobby_RoomSettingsForJoin(uint32_t* seed, uint32_t* terrainSize, bool* disastersOff, MapGenerator** mapGen,
                               TerrainGenerator** terrainGen, ClimateGenerator** climateGen);

}  // namespace Neighbour
