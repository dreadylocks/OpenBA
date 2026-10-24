// Dialog/ExtensionsNeighbour.rsc -- the Neighbour panel (NOT RETAIL; this fork, ext.neighbour,
// docs/fork/NEIGHBOUR_TOWNS.md). Press N in game to open or close it.
//
// Shape: one column -- the title, a status line, then three rows (chat; goods; a family) and the
// close button. Everything derives from game elements the packages carry (the shared buttons and
// dialog border, the Mods dialog's texts, the New Game dialog's name box) and overrides only what
// differs; the texts are ours (ExtensionsStringTable.rsc).
//
// Controller: src/ext/neighbour/neighbour_panel.cpp. It looks up, by name: textStatus, editChat,
// buttonSend, buttonPrev, textGood, buttonNext, textStock, buttonLess, textAmount, buttonMore,
// buttonSendGoods, textFamily, buttonSendFamily, buttonCancel. Every text named text* shows the
// table's "NeighbourParam" (@0), which the controller fills.

Dialog resource
{
	Flags _flags = Modal | FirstDraw;
	ElementDescription _rootElement = "dialogRoot";
}

GroupDescription dialogRoot
{
	Alignment _alignment = MidCenter; ElementDescription _elements [ "SharedElements.rsc:dialogBorder" "dialogBody" ]
}

RibbonDescription dialogBody
{
	bool _vertical = true; int _cellPad = 6;
	int _leftPad = 10; int _rightPad = 10; int _topPad = 8; int _bottomPad = 8;
	ElementDescription _elements [ "labelTitle" "textStatus" "chatRow" "goodsRow" "familyRow" "buttonRow" ]
}

LabelDescription labelTitle : "Mod.rsc:labelTitle"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc"; String _text = "NeighbourTitle";
	SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "ExtensionsIcon";
}

// The texts: the Mods list's name text (normal) and a small note, both showing @0.
TextDescription value : "Mod.rsc:textModName"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc"; String _text = "NeighbourParam";
	Alignment _alignment = MidLeft;
}
TextDescription note
{
	Font _font = "Font/FontSmall.rsc";
	StringTable _stringTable = "ExtensionsStringTable.rsc"; String _text = "NeighbourParam";
	Alignment _alignment = MidLeft;
}

TextDescription textStatus : "note" { int _minWidth = 460; }

// A button with our caption: the shared Ok button's frame, a text in it.
ButtonDescription button : "SharedElements.rsc:buttonOk"
{
	ElementDescription _content = "caption";
}
TextDescription caption : "Mod.rsc:labelMod"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc"; String _text = "NeighbourParam";
	Alignment _alignment = MidCenter;
}
TextDescription captionSend   : "caption" { String _text = "NeighbourSend"; }
TextDescription captionPrev   : "caption" { String _text = "NeighbourPrev"; }
TextDescription captionNext   : "caption" { String _text = "NeighbourNext"; }
TextDescription captionLess   : "caption" { String _text = "NeighbourLess"; }
TextDescription captionMore   : "caption" { String _text = "NeighbourMore"; }
TextDescription captionGoods  : "caption" { String _text = "NeighbourSendGoods"; }
TextDescription captionFamily : "caption" { String _text = "NeighbourSendFamily"; }

ButtonDescription buttonSend       : "button" { ElementDescription _content = "captionSend"; }
ButtonDescription buttonPrev       : "button" { ElementDescription _content = "captionPrev"; }
ButtonDescription buttonNext       : "button" { ElementDescription _content = "captionNext"; }
ButtonDescription buttonLess       : "button" { ElementDescription _content = "captionLess"; }
ButtonDescription buttonMore       : "button" { ElementDescription _content = "captionMore"; }
ButtonDescription buttonSendGoods  : "button" { ElementDescription _content = "captionGoods"; }
ButtonDescription buttonSendFamily : "button" { ElementDescription _content = "captionFamily"; }

// Chat: the New Game dialog's name box, longer.
RibbonDescription chatRow
{
	int _cellPad = 6; ElementDescription _elements [ "editChat" "buttonSend" ]
}
EditDescription editChat : "NewGame.rsc:editName"
{
	int _maxLength = 120; int _minWidth = 360; String _text = "";
	String _filter = "";		// any character (the name box allows letters only)
}

// Goods: < name > (stock)  - amount +  [Send goods]
RibbonDescription goodsRow
{
	int _cellPad = 6;
	ElementDescription _elements [ "buttonPrev" "textGood" "buttonNext" "textStock" "buttonLess" "textAmount" "buttonMore" "buttonSendGoods" ]
}
TextDescription textGood   : "value" { int _minWidth = 120; }
TextDescription textStock  : "note"  { int _minWidth = 90; }
TextDescription textAmount : "value" { int _minWidth = 40; Alignment _alignment = MidCenter; }

// A family: what would leave, and the button.
RibbonDescription familyRow
{
	int _cellPad = 6; ElementDescription _elements [ "textFamily" "buttonSendFamily" ]
}
TextDescription textFamily : "value" { int _minWidth = 330; }

RibbonDescription buttonRow
{
	Alignment _alignment = MidRight; int _cellPad = 4;
	ElementDescription _elements [ "SharedElements.rsc:buttonCancel" ]
}
