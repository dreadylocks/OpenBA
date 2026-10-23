// Dialog/ExtensionsWindow.rsc -- the in-game extension window (NOT RETAIL; authored for this port, src/ext).
//
// The one window the toolbar's extension button opens (docs/extensions/DESIGN.md "Surfaces", src/ext/toolbar/toolbar.h):
// a utility-style window like Limits and Status Bar -- not modal, it does not pause the game, it has the game's own title
// bar (StandardDialog.rsc:titleBar, with its close button and drag handle). Its body is ONE tab control with no tabs of
// its own: the controller (src/ext/toolbar/toolbar_window.cpp) adds one tab per active extension page at run time with
// UI::TabElement::AddTab, the way the Options dialog's Enhanced tab is added. Each page is an object of the extension's
// own .rsc (RestrictedFoodsPage.rsc:pageRestrictedFoods); every tab's button is tabButtonPage below, its caption set by the
// controller from the page's string key.
//
// Controller: src/ext/toolbar/toolbar_window.cpp. It looks up, by name: labelTitle, buttonClose, borderTitle
// (the title bar's) and tabPages.
//
// Texts: Dialog/ExtensionsStringTable.rsc (ExtensionsWindow* keys); sprites: Dialog/ExtensionsSprites.rsc (ToolbarExtensions,
// the toolbar button's icon).

Dialog resource		// the dialog object: Dialog/ExtensionsWindow.rsc
{
	// No flags: not Modal, not PauseGame and NOT FirstDraw. FirstDraw (instance bit 4) makes the dialog the topmost
	// barrier of UserInterface::Draw: every dialog below it is not drawn, which is how the pause menu hides the toolbar
	// (live 2026-10-19: this window made the toolbar disappear). The game's own utility windows carry no flags.
	ElementDescription _rootElement = "dialogRoot";
}

// TopLeft like StandardDialog's own root: the controller places the window with SetUpperLeft.
GroupDescription dialogRoot
{
	Alignment _alignment = TopLeft; ElementDescription _elements [ "SharedElements.rsc:dialogBorder" "dialogBody" ]
}

// The column: the game's title bar and the tabs; 4 px between them, 8 px to the border.
RibbonDescription dialogBody
{
	bool _vertical = true; int _cellPad = 4;
	int _leftPad = 8; int _rightPad = 8; int _topPad = 8; int _bottomPad = 8;
	ElementDescription _elements [ "StandardDialog.rsc:titleBar" "tabPages" ]
}

// The Mods dialog's tab frame with no tabs: the controller adds them.
TabDescription tabPages : "Mod.rsc:tabMod"
{
	Tab _tabs [ ]
}

// Every page's tab button: the Mods dialog's, with a caption the controller replaces.
ButtonDescription tabButtonPage : "Mod.rsc:buttonMod"
{
	ElementDescription _content = "tabCaptionPage";
}

TextDescription tabCaptionPage : "Mod.rsc:labelMod"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc"; String _text = "ExtensionsWindowTitle";
}
