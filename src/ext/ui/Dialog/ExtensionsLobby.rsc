// Dialog/ExtensionsLobby.rsc -- the Multiplayer lobby and its main-menu button (NOT RETAIL; this
// fork, ext.neighbour M2, docs/fork/NEIGHBOUR_TOWNS.md).
//
// Shape: one column -- the title, the relay's address, a status line, the list of rooms (one button
// per room, from the template roomRow), a hint, the three fields (room name, password, your town) and the buttons
// (Refresh, Create room, Join, Back). Reuses the Neighbour panel's pieces (ExtensionsNeighbour.rsc:
// value, note, button, caption) and the New Game dialog's name box for the fields.
//
// Controller: src/ext/neighbour/lobby_panel.cpp. It looks up, by name: editRelay, textStatus, roomList,
// textHint, editRoom, editPass, editTown, buttonRefresh, buttonCreate, buttonJoin, buttonCancel.

Dialog resource
{
	Flags _flags = Modal | FirstDraw;
	ElementDescription _rootElement = "dialogRoot";
	ElementDescription _templates [ "roomRow" ]			// GetTemplate(0): one per room
}

GroupDescription dialogRoot
{
	Alignment _alignment = MidCenter; ElementDescription _elements [ "SharedElements.rsc:dialogBorder" "dialogBody" ]
}

RibbonDescription dialogBody
{
	bool _vertical = true; int _cellPad = 6;
	int _leftPad = 10; int _rightPad = 10; int _topPad = 8; int _bottomPad = 8;
	ElementDescription _elements [ "labelTitle" "rowRelay" "textStatus" "roomList" "textHint" "rowRoom" "rowPass" "rowTown" "buttonRow" ]
}

LabelDescription labelTitle : "Mod.rsc:labelTitle"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc"; String _text = "LobbyTitle";
	SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "ExtensionsIcon";
}

TextDescription textStatus : "ExtensionsNeighbour.rsc:note" { int _minWidth = 520; }
TextDescription textHint   : "ExtensionsNeighbour.rsc:note" { int _minWidth = 520; }

// The rooms: the controller adds one roomRow per room (at most 12).
RibbonDescription roomList
{
	bool _vertical = true; int _cellPad = 2;
}
ButtonDescription roomRow : "ExtensionsNeighbour.rsc:button" { ElementDescription _content = "roomCaption"; int _minWidth = 520; }
TextDescription roomCaption : "ExtensionsNeighbour.rsc:caption" { Alignment _alignment = MidLeft; }

// The fields: a label and the New Game dialog's name box, longer, any character.
TextDescription fieldLabel : "ExtensionsNeighbour.rsc:value" { int _minWidth = 120; }
TextDescription labelRoom : "fieldLabel" { String _text = "LobbyRoom"; }
TextDescription labelPass : "fieldLabel" { String _text = "LobbyPassword"; }
TextDescription labelTown : "fieldLabel" { String _text = "LobbyTown"; }
TextDescription labelRelay : "fieldLabel" { String _text = "LobbyRelay"; }
EditDescription field : "NewGame.rsc:editName"
{
	int _maxLength = 40; int _minWidth = 260; String _text = ""; String _filter = "";
}
EditDescription editRoom : "field" { }
EditDescription editPass : "field" { bool _password = true; }
EditDescription editTown : "field" { int _maxLength = 24; }
EditDescription editRelay : "field" { int _maxLength = 200; int _minWidth = 380; }
RibbonDescription rowRoom { int _cellPad = 6; ElementDescription _elements [ "labelRoom" "editRoom" ] }
RibbonDescription rowPass { int _cellPad = 6; ElementDescription _elements [ "labelPass" "editPass" ] }
RibbonDescription rowTown { int _cellPad = 6; ElementDescription _elements [ "labelTown" "editTown" ] }
RibbonDescription rowRelay { int _cellPad = 6; ElementDescription _elements [ "labelRelay" "editRelay" ] }

TextDescription captionRefresh : "ExtensionsNeighbour.rsc:caption" { String _text = "LobbyRefresh"; }
TextDescription captionCreate  : "ExtensionsNeighbour.rsc:caption" { String _text = "LobbyCreate"; }
TextDescription captionJoin    : "ExtensionsNeighbour.rsc:caption" { String _text = "LobbyJoin"; }
ButtonDescription buttonRefresh : "ExtensionsNeighbour.rsc:button" { ElementDescription _content = "captionRefresh"; }
ButtonDescription buttonCreate  : "ExtensionsNeighbour.rsc:button" { ElementDescription _content = "captionCreate"; }
ButtonDescription buttonJoin    : "ExtensionsNeighbour.rsc:button" { ElementDescription _content = "captionJoin"; }

RibbonDescription buttonRow
{
	Alignment _alignment = MidRight; int _cellPad = 4;
	ElementDescription _elements [ "buttonRefresh" "buttonCreate" "buttonJoin" "SharedElements.rsc:buttonCancel" ]
}

// ---------------------------------------------------------------------------------------------
// The main menu's Multiplayer button: the menu's own button shape (as the Extensions button).
// ---------------------------------------------------------------------------------------------
ButtonDescription buttonMultiplayer : "MainMenu.rsc:buttonMod"
{
	ElementDescription _content = "labelMultiplayer";
	StringTable _toolTipStringTable = "ExtensionsStringTable.rsc"; String _toolTipText = "LobbyTip";
}

LabelDescription labelMultiplayer : "MainMenu.rsc:labelMod"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc"; String _text = "LobbyButton";
	SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "ExtensionsIcon";
}
