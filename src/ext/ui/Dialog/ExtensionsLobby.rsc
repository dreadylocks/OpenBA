// Dialog/ExtensionsLobby.rsc -- the Multiplayer lobby and its main-menu button (NOT RETAIL; this
// fork, ext.neighbour M2, docs/fork/NEIGHBOUR_TOWNS.md).
//
// Shape: a header (title, the relay's state, Change relay), the relay row (shown on Change relay or
// when there is no relay), two columns and a footer. Left: the room search box, the scrolling room
// list (the Mods dialog's list box, Mod.rsc:tableList, one roomRow per room) and a line under it.
// Right: the chosen room (or the new room) -- a title, a note, the fields and the main button, and a
// status line. Footer: New room and Back. Reuses the Neighbour panel's pieces (ExtensionsNeighbour.rsc:
// value, note, button, caption) and the New Game dialog's name box for the fields.
//
// Controller: src/ext/neighbour/lobby_panel.cpp. It looks up, by name: textConnection, buttonRelay,
// rowRelay, editRelay, buttonConnect, editSearch, buttonRefresh, roomTable, textListNote, textPanelTitle,
// textPanelNote, fieldRoom, editRoom, fieldPass, editPass, fieldTown, editTown, buttonLoad, buttonPrimary,
// captionPrimary, textStatus, buttonNew, buttonBack; in each row (template 0 = roomRow) roomName,
// roomSeats, roomDetails.

Dialog resource
{
	Flags _flags = Modal | FirstDraw;
	ElementDescription _rootElement = "dialogRoot";
	ElementDescription _templates [ "roomRow" ]			// GetTemplate(0): one per room shown
}

GroupDescription dialogRoot
{
	Alignment _alignment = MidCenter; ElementDescription _elements [ "SharedElements.rsc:dialogBorder" "dialogBody" ]
}

RibbonDescription dialogBody
{
	bool _vertical = true; int _cellPad = 10;
	int _leftPad = 14; int _rightPad = 14; int _topPad = 10; int _bottomPad = 12;
	ElementDescription _elements [ "headerRow" "rowRelay" "columns" "footerRow" ]
}

// ---------------------------------------------------------------------------------------------
// Text styles: a value (the Mods list's name font), a small note, and the same note dimmed.
// ---------------------------------------------------------------------------------------------
TextDescription small : "ExtensionsNeighbour.rsc:note" { }
TextDescription muted : "small" { Color _color = 0xFFB4AB9C; }
TextDescription label : "muted" { StringTable _stringTable = "ExtensionsStringTable.rsc"; }

// ---------------------------------------------------------------------------------------------
// The header and the relay row.
// ---------------------------------------------------------------------------------------------
RibbonDescription headerRow
{
	int _cellPad = 8; ElementDescription _elements [ "labelTitle" "textConnection" "buttonRelay" ]
}
LabelDescription labelTitle : "Mod.rsc:labelTitle"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc"; String _text = "LobbyTitle";
	SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "ExtensionsIcon";
	int _minWidth = 430; Alignment _alignment = MidLeft;
}
TextDescription textConnection : "small" { int _minWidth = 190; Alignment _alignment = MidRight; }

TextDescription captionRelay : "ExtensionsNeighbour.rsc:caption" { String _text = "LobbyChangeRelay"; }
ButtonDescription buttonRelay : "ExtensionsNeighbour.rsc:button" { ElementDescription _content = "captionRelay"; int _minWidth = 110; }

RibbonDescription rowRelay
{
	int _cellPad = 6; ElementDescription _elements [ "labelRelay" "editRelay" "buttonConnect" ]
}
TextDescription labelRelay : "label" { String _text = "LobbyRelay"; int _minWidth = 50; }
EditDescription field : "NewGame.rsc:editName"
{
	int _maxLength = 40; int _minWidth = 270; String _text = ""; String _filter = "";
}
EditDescription editRelay : "field" { int _maxLength = 200; int _minWidth = 480; }
TextDescription captionConnect : "ExtensionsNeighbour.rsc:caption" { String _text = "LobbyConnect"; }
ButtonDescription buttonConnect : "ExtensionsNeighbour.rsc:button" { ElementDescription _content = "captionConnect"; int _minWidth = 100; }

// ---------------------------------------------------------------------------------------------
// The two columns.
// ---------------------------------------------------------------------------------------------
RibbonDescription columns
{
	int _cellPad = 20; ElementDescription _elements [ "leftColumn" "rightColumn" ]
}

// Left: "Open rooms", the search row, the list box, a line under it.
RibbonDescription leftColumn
{
	bool _vertical = true; int _cellPad = 6; Alignment _alignment = TopLeft;
	ElementDescription _elements [ "labelRooms" "rowSearch" "roomBox" "rowListFoot" ]
}
TextDescription labelRooms : "label" { String _text = "LobbyOpenRooms"; }
RibbonDescription rowSearch
{
	int _cellPad = 6; ElementDescription _elements [ "labelSearch" "editSearch" ]
}
TextDescription labelSearch : "label" { String _text = "LobbySearch"; int _minWidth = 50; }
EditDescription editSearch : "field" { int _minWidth = 360; }

// The scrolling list, as the Mods dialog has it; the controller appends one roomRow per room.
ContainerDescription roomBox : "Mod.rsc:tableList"
{
	int _requestedWidth = 430; int _requestedHeight = 270; ElementDescription _content = "roomTable";
}
RibbonDescription roomTable : "Mod.rsc:tableMods" { }

RibbonDescription rowListFoot
{
	int _cellPad = 8; ElementDescription _elements [ "textListNote" "buttonRefresh" ]
}
TextDescription textListNote : "muted" { int _minWidth = 322; }
TextDescription captionRefresh : "ExtensionsNeighbour.rsc:caption" { String _text = "LobbyRefresh"; }
ButtonDescription buttonRefresh : "ExtensionsNeighbour.rsc:button" { ElementDescription _content = "captionRefresh"; int _minWidth = 100; }

// A room: its name and seats on one line, the creator and the map under them.
ButtonDescription roomRow : "ExtensionsNeighbour.rsc:button" { ElementDescription _content = "roomBody"; int _minWidth = 405; }
RibbonDescription roomBody
{
	bool _vertical = true; int _cellPad = 0; Alignment _alignment = MidLeft;
	int _leftPad = 6; int _rightPad = 6; int _topPad = 3; int _bottomPad = 3;
	ElementDescription _elements [ "roomLine" "roomDetails" ]
}
RibbonDescription roomLine { int _cellPad = 4; ElementDescription _elements [ "roomName" "roomSeats" ] }
TextDescription roomName : "ExtensionsNeighbour.rsc:value" { int _minWidth = 320; }
TextDescription roomSeats : "small" { int _minWidth = 62; Alignment _alignment = MidRight; }
TextDescription roomDetails : "muted" { int _minWidth = 386; }

// Right: what the chosen room (or the new room) needs.
RibbonDescription rightColumn
{
	bool _vertical = true; int _cellPad = 6; int _minWidth = 290; Alignment _alignment = TopLeft;
	ElementDescription _elements [ "textPanelTitle" "textPanelNote" "fieldRoom" "fieldPass" "fieldTown" "buttonLoad" "buttonPrimary" "textStatus" ]
}
TextDescription textPanelTitle : "ExtensionsNeighbour.rsc:value" { int _minWidth = 290; }
TextDescription textPanelNote : "muted" { int _minWidth = 290; }

RibbonDescription fieldRoom { bool _vertical = true; int _cellPad = 2; ElementDescription _elements [ "labelRoom" "editRoom" ] }
RibbonDescription fieldPass { bool _vertical = true; int _cellPad = 2; ElementDescription _elements [ "labelPass" "editPass" ] }
RibbonDescription fieldTown { bool _vertical = true; int _cellPad = 2; ElementDescription _elements [ "labelTown" "editTown" ] }
TextDescription labelRoom : "label" { String _text = "LobbyRoom"; }
TextDescription labelPass : "label" { String _text = "LobbyPassword"; }
TextDescription labelTown : "label" { String _text = "LobbyTown"; }
EditDescription editRoom : "field" { }
EditDescription editPass : "field" { bool _password = true; }
EditDescription editTown : "field" { int _maxLength = 24; }

TextDescription captionPrimary : "ExtensionsNeighbour.rsc:caption" { }
TextDescription captionLoad : "ExtensionsNeighbour.rsc:caption" { String _text = "LobbyLoad"; }
ButtonDescription buttonLoad : "ExtensionsNeighbour.rsc:button" { ElementDescription _content = "captionLoad"; int _minWidth = 150; }
ButtonDescription buttonPrimary : "ExtensionsNeighbour.rsc:button" { ElementDescription _content = "captionPrimary"; int _minWidth = 150; }
TextDescription textStatus : "small" { int _minWidth = 290; }

// ---------------------------------------------------------------------------------------------
// The footer: a line, New room, Back.
// ---------------------------------------------------------------------------------------------
RibbonDescription footerRow
{
	int _cellPad = 6; ElementDescription _elements [ "textFooter" "buttonNew" "buttonBack" ]
}
TextDescription textFooter : "label" { String _text = "LobbyFooter"; int _minWidth = 517; }
TextDescription captionNew : "ExtensionsNeighbour.rsc:caption" { String _text = "LobbyNewRoom"; }
TextDescription captionBack : "ExtensionsNeighbour.rsc:caption" { String _text = "LobbyBack"; }
ButtonDescription buttonNew : "ExtensionsNeighbour.rsc:button" { ElementDescription _content = "captionNew"; int _minWidth = 110; }
ButtonDescription buttonBack : "ExtensionsNeighbour.rsc:button" { ElementDescription _content = "captionBack"; int _minWidth = 110; }

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
