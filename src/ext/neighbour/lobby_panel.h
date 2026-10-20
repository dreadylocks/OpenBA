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

namespace Neighbour {

constexpr int32_t kMultiplayerMenuButtonId = 14;   // after Extensions (13)

// EXT SEAM, MainMenuDialog::OnOpened (beside ExtensionsDialog_AttachMenuButton): the button, on
// the front-end menu only (not the pause menu, not under the gate's forced faithful profile).
void Lobby_AttachMenuButton(MainMenuDialog* menu);
// The button's click (MainMenuDialog::OnElementEvent, id 14), or a refused create coming back:
// `note` (when not empty) is shown in the lobby's status line.
void Lobby_Open(MainMenuDialog* menu, const std::string& note);

}  // namespace Neighbour
