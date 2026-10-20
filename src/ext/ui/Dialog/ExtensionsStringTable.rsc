// Dialog/ExtensionsStringTable.rsc -- the Extensions dialog's and menu button's texts (NOT RETAIL).
// Our own table, so the game's Dialog/StringTable.rsc is never edited (the kit's README, section 5:
// "Take care with resources that might be shared"). Compiled by tools/ext_resources.sh.
// The rows' names and descriptions come from the registry (src/ext/registry.cpp) through the
// general table's "Parameter" string (@0); everything fixed is here.

StringTable resource
{
	Entry _strings
	[
		// the menu button (main menu and pause menu)
		{ String _name = "Extensions";				String _text = "Extensions"; }
		{ String _name = "ExtensionsTip";			String _text = "Turn fixes for the game's known bugs and new features on or off."; }

		// the dialog's chrome
		{ String _name = "ExtensionsTitle";			String _text = "Fixes and Extensions"; }
		{ String _name = "ExtensionsProfile";		String _text = "(profile: @0)"; }
		{ String _name = "ExtensionsTab";			String _text = "Fixes and extensions"; }
		{ String _name = "ExtensionsHeaderName";	String _text = "Name"; }
		{ String _name = "ExtensionsHeaderDesc";	String _text = "Description"; }
		{ String _name = "ExtensionsHeaderOn";		String _text = "On/Off"; }
		{ String _name = "ExtensionsToggleTip";		String _text = "Turn this fix or extension on or off. Press OK to apply."; }

		// a row's kind: the tier and the class letter (docs/extensions/DESIGN.md section 2)
		{ String _name = "ExtensionsKindFix";		String _text = "Bug fix"; }
		{ String _name = "ExtensionsKindA";			String _text = "Class A: takes effect immediately"; }
		{ String _name = "ExtensionsKindB";			String _text = "Class B: changes play, set per game"; }
		{ String _name = "ExtensionsKindC";			String _text = "Class C: extends the save, set per game"; }
		{ String _name = "ExtensionsKindOther";		String _text = "Extension"; }

		// a row's lock state
		{ String _name = "ExtensionsStateEnabled";			String _text = "Enabled"; }
		{ String _name = "ExtensionsStateDisabled";			String _text = "Disabled"; }
		{ String _name = "ExtensionsStateLockedEnabled";	String _text = "Enabled, locked for this game"; }
		{ String _name = "ExtensionsStateLockedDisabled";	String _text = "Disabled, locked for this game"; }
		{ String _name = "ExtensionsStateLockedOn";			String _text = "Disabled for this game, enabled from the next"; }
		{ String _name = "ExtensionsStateLockedOff";		String _text = "Enabled for this game, disabled from the next"; }

		// the load notices (Extensions.rsc:extension*, shown by SaveLoadDialog::DoLoad in place of
		// retail's ModMissing): @0 name, @1 the save's `ext:<id>`, @2 the saved format version
		{ String _name = "ExtensionNotInBuild";		String _text = "The following extension is not part of this version of the game.~n~n@0 [@1]~nSaved with extension version @2~n~nA save keeps the extensions it was made with, so this one cannot be played here: loading it will stop the game with an error. Do you still want to load the save game?"; }
		{ String _name = "ExtensionNewer";			String _text = "The following extension was saved by a newer version of it than this game has.~n~n@0 [@1]~nSaved with extension version @2~n~nThis version of the game cannot read it: loading this save will stop the game with an error. Do you still want to load the save game?"; }
		// the Neighbour panel (ExtensionsNeighbour.rsc, ext.neighbour); its texts show @0
		{ String _name = "NeighbourParam";			String _text = "@0"; }
		{ String _name = "NeighbourTitle";			String _text = "Neighbour Towns"; }
		{ String _name = "NeighbourSend";			String _text = "Send"; }
		{ String _name = "NeighbourPrev";			String _text = "<"; }
		{ String _name = "NeighbourNext";			String _text = ">"; }
		{ String _name = "NeighbourLess";			String _text = "-"; }
		{ String _name = "NeighbourMore";			String _text = "+"; }
		{ String _name = "NeighbourSendGoods";		String _text = "Send goods"; }
		{ String _name = "NeighbourSendFamily";		String _text = "Send this family"; }

		// the Multiplayer lobby (ExtensionsLobby.rsc, ext.neighbour M2)
		{ String _name = "LobbyButton";			String _text = "Multiplayer"; }
		{ String _name = "LobbyTip";			String _text = "Play next to a friend's town: create a room or join one."; }
		{ String _name = "LobbyTitle";			String _text = "Multiplayer: Neighbour Towns"; }
		{ String _name = "LobbyRoom";			String _text = "Room name"; }
		{ String _name = "LobbyPassword";		String _text = "Password"; }
		{ String _name = "LobbyTown";			String _text = "Your town"; }
		{ String _name = "LobbyRefresh";		String _text = "Refresh"; }
		{ String _name = "LobbyCreate";			String _text = "Create room"; }
		{ String _name = "LobbyJoin";			String _text = "Join"; }

		{ String _name = "ExtensionTurnedOff";		String _text = "The following extension is turned off, but this save was made with it.~n~n@0 [@1]~nSaved with extension version @2~n~nA save keeps the extensions it was made with, so it will be turned on for this game. Do you still want to load the save game?"; }
	]
}
