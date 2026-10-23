// Dialog/RestrictedFoodsPage.rsc -- ext.restricted_foods's page in the extension window (NOT RETAIL; authored
// for this port, src/ext).
//
// docs/extensions/handins/RESTRICTEDFOODS.md, src/ext/restricted_foods/toolbar_page.cpp. One cell per edible
// food of the running town, a check box each; a checked food is not eaten by citizens, from the moment it is
// checked. The window around it is Dialog/ExtensionsWindow.rsc (one tab per active extension page); this file
// is only the page.
//
// The list is laid out like the Town Hall's Inventory tab (TownHall.rsc:inventoryTable / storageItem): a table
// of four columns of small cells, each a check box and a compact label (20 px icon, small font), so a modded
// game's hundreds of foods are a short scroll. The controller sizes the table and clones
// ribbonRestrictedFoodsCell into it, as TownHallUI::InitInventory does.
//
// Controller: src/ext/restricted_foods/toolbar_page.cpp. It looks up, by name: tableRestrictedFoods,
// checkRestrictedFoodsShowAll, and in each cell checkRestrictedFoodsRow and labelRestrictedFoodsRow.
//
// Texts: Dialog/ExtensionsStringTable.rsc (the RestrictedFoods* keys).

// The page: the hint, the list, the "Show all foods" check box; 4 px between them. Left-aligned, as most
// of the game is.
RibbonDescription pageRestrictedFoods
{
	Alignment _alignment = TopLeft;
	bool _vertical = true;
	int _cellPad = 4;
	ElementDescription _elements [ "textRestrictedFoodsHint" "ribbonRestrictedFoodsList" "ribbonRestrictedFoodsShowAll" ]
}

// The note under the tab: what the check boxes do.
TextDescription textRestrictedFoodsHint : "Extensions.rsc:textProfile"
{
	Alignment _alignment = MidLeft;
	int _leftPad = 4;		// the first letter in line with the check boxes of the list
	int _topPad = 10;		// as much room above the line as below it
	String _text = "RestrictedFoodsHint";
}

// The check box under the list: unchecked shows the foods the town has made or stored, checked
// shows every edible food.
RibbonDescription ribbonRestrictedFoodsShowAll
{
	Alignment _alignment = MidLeft;
	int _cellPad = 4;
	ElementDescription _elements [ "checkRestrictedFoodsShowAll" "textRestrictedFoodsShowAll" ]
}

CheckDescription checkRestrictedFoodsShowAll : "Extensions.rsc:checkLoaded"
{
	StringTable _toolTipStringTable = "ExtensionsStringTable.rsc";
	String _toolTipText = "RestrictedFoodsShowAllTip";
}

TextDescription textRestrictedFoodsShowAll : "Extensions.rsc:textProfile"
{
	Alignment _alignment = MidLeft;
	String _text = "RestrictedFoodsShowAll";
}

// The list frame: the Extensions dialog's (by reference) around the scrolling box of foods.
RibbonDescription ribbonRestrictedFoodsList : "Extensions.rsc:entryList"
{
	ElementDescription _elements [ "containerRestrictedFoodsBox" ]
}

// The scrolling box: the Extensions list's container (scroll bar, size) with the table as its content.
ContainerDescription containerRestrictedFoodsBox : "Extensions.rsc:tableList"
{
	ElementDescription _content = "tableRestrictedFoods";
}

// Four columns (the Town Hall's has three, wider, cells). The controller sizes it (rows = foods / 4) and
// fills it with ribbonRestrictedFoodsCell.
TableDescription tableRestrictedFoods
{
	int _width = 4;
	int _height = 1;
	int _spacing = 4;		// a gap between cells, so a cell never runs into the next column
	TableItem _items [ { int _x = -1; int _y = -1; ElementDescription _element = "ribbonRestrictedFoodsCell"; } ]
}

// One food: the check box and the compact label. The border is the Mods list's row border, tinted red by the
// controller while the food is not eaten.
RibbonDescription ribbonRestrictedFoodsCell
{
	ElementDescription _border = "Mod.rsc:borderLine";
	int _leftPad = 1;
	int _topPad = 1;
	int _bottomPad = 1;
	int _cellPad = 2;
	int _minWidth = 140;		// four of these fill the list's width
	ElementDescription _elements [ "checkRestrictedFoodsRow" "labelRestrictedFoodsRow" ]
}

CheckDescription checkRestrictedFoodsRow : "Extensions.rsc:checkLoaded"
{
	StringTable _toolTipStringTable = "ExtensionsStringTable.rsc";
	String _toolTipText = "RestrictedFoodsCheckTip";
}

// The Town Hall's inventory item label (20 px icon, small font, 132 px wide), its text from our table's "@0".
LabelDescription labelRestrictedFoodsRow : "TownHall.rsc:storageItem"
{
	int _minWidth = 119;		// fills the cell (_minWidth 140 = 1 + the check box + 2 + this): no empty tail
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "RestrictedFoodsParam";
}
