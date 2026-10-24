// Dialog/EnhancedOptions.rsc -- the Options dialog's fifth tab "Enhanced" (NOT RETAIL; blocks AOMENU,
// FAA-MENU, BETTER-TREES: five rows, docs/extensions/AO.md §6.3; was AoOptions.rsc). Added at run time by UI::TabElement::AddTab from
// OptionsDialog::OnOpened (src/ext/enhanced_tab/enhanced_tab.cpp). Every name here is NEW:
// a loose .crs never overrides a name the game's packages hold (IO::FileSystem::OpenFile looks
// in the packfile index first, handins/AOSPIKE.md S5). The look inherits the kit's own
// Options.rsc / SharedElements.rsc objects (resolved from data*.pkg at compile and run time).

StringTable aoStrings
{
	Entry _strings
	[
		{ String _name = "AoTab";			String _text = "Enhanced"; }
		{ String _name = "AoQuality";		String _text = "Ambient occlusion"; }
		{ String _name = "AoOpacity";		String _text = "AO strength"; }
		{ String _name = "BetterTrees";		String _text = "Better trees"; }
		{ String _name = "MaxShadowRes";	String _text = "Max shadow resolution"; }
		{ String _name = "SeamlessLoops";	String _text = "Seamless ambient loops"; }
		{ String _name = "DerivedCache";	String _text = "Data cache on disk: @0"; }
		{ String _name = "DerivedCacheClear";	String _text = "Clear cache"; }
	]
}

// The combo's items, in Params::quality order (0 = off, 1 Low, 2 Medium, 3 High).
StringTable aoQualityLevels
{
	Entry _strings
	[
		{ String _name = "Level0";		String _text = "Off";		}
		{ String _name = "Level1";		String _text = "Low";		}
		{ String _name = "Level2";		String _text = "Medium";	}
		{ String _name = "Level3";		String _text = "High";		}
	]
}

// Better trees, in BetterTrees::Mode order (0 = the row off, 1 on = smooth edges + stable shading,
// 2 = + distance fill).
StringTable betterTreesModes
{
	Entry _strings
	[
		{ String _name = "Mode0";		String _text = "Off";							}
		{ String _name = "Mode1";		String _text = "On";					}
		{ String _name = "Mode2";		String _text = "On + distance fill";	}
	]
}

// Max shadow resolution: 0 = the bool row ext.max_shadow_res off, 1 / 2 = on with ext.max_shadow_size 4096 / 8192.
StringTable shadowResItems
{
	Entry _strings
	[
		{ String _name = "Item0";		String _text = "Off";	}
		{ String _name = "Item1";		String _text = "4096";	}
		{ String _name = "Item2";		String _text = "8192";	}
	]
}

// Seamless ambient loops: 0 = the bool row ext.seamless_loops off, 1 = on (heard after the next start).
StringTable seamlessLoopsItems
{
	Entry _strings
	[
		{ String _name = "Item0";		String _text = "Off";					}
		{ String _name = "Item1";		String _text = "On (next start)";		}
	]
}

ButtonDescription buttonEnhanced : "Options.rsc:buttonGame"
{
	Alignment _alignment = MidCenter;
	ElementDescription _content = "labelEnhanced";
}

// The tab button, the combos and the slider derive from the Options tabs' own objects
// (Options.rsc:buttonGame, comboAutosave, sliderUI: the packages hold only objects a game dialog
// reaches, and the exe derives at run time, KITFREE.md); each base sets only fields overridden
// here, so the compiled result is the SharedElements template's.
// Options.rsc:labelOptions's look, with our string table and our icon (ExtensionsSprites.rsc).
LabelDescription labelEnhanced
{
	Alignment _alignment = MidCenter;

	int _bottomPad = 5;
	int _leftPad = 5;
	int _rightPad = 5;
	int _topPad = 5;

	int _minWidth = 40;
	int _minHeight = 40;

	Font _font = "Font\FontSmall.rsc";
	StringTable _stringTable = "EnhancedOptions.rsc:aoStrings";
	SpriteSheet _spriteSheet = "ExtensionsSprites.rsc";

	int _spacing = 2;
	LabelPosition _labelPosition = TextBottom;
	Alignment _textAlignment = MidCenter;

	String _text = "AoTab";
	String _spriteName = "OptionsEnhanced";
}

// The page: Options.rsc:gameOptionsTable's shape, six rows (AO combo, AO slider, Better trees, shadow,
// loops, the derived-data cache's size + its "Clear cache" button: COMPRESSAO).
TableDescription enhancedOptions
{
	int _leftPad = 6;
	int _topPad = 4;
	int _rightPad = 2;
	int _bottomPad = 4;

	Alignment _alignment = TopLeft;

	int _width = 2;
	int _height = 6;

	TableItem _items
	[
		{ int _x = 0; int _y = 0; ElementDescription _element = "textAoQuality"; }
		{ int _x = 0; int _y = 1; ElementDescription _element = "textAoOpacity"; }
		{ int _x = 0; int _y = 2; ElementDescription _element = "textBetterTrees"; }
		{ int _x = 0; int _y = 3; ElementDescription _element = "textMaxShadowRes"; }
		{ int _x = 0; int _y = 4; ElementDescription _element = "textSeamlessLoops"; }
		{ int _x = 0; int _y = 5; ElementDescription _element = "textDerivedCache"; }

		{ int _x = 1; int _y = 0; ElementDescription _element = "comboAoQuality"; }
		{ int _x = 1; int _y = 1; ElementDescription _element = "sliderAoOpacity"; }
		{ int _x = 1; int _y = 2; ElementDescription _element = "comboBetterTrees"; }
		{ int _x = 1; int _y = 3; ElementDescription _element = "comboMaxShadowRes"; }
		{ int _x = 1; int _y = 4; ElementDescription _element = "comboSeamlessLoops"; }
		{ int _x = 1; int _y = 5; ElementDescription _element = "buttonDerivedCacheClear"; }
	]
}

TextDescription textAoOption
{
	Font _font = "Font\FontMedium.rsc";
	StringTable _stringTable = "EnhancedOptions.rsc:aoStrings";
	int _minWidth = 200;
	Color _color = 0xFFC0C0C0;
}

TextDescription textAoQuality : "textAoOption" { String _text = "AoQuality"; }
TextDescription textAoOpacity : "textAoOption" { String _text = "AoOpacity"; }
TextDescription textBetterTrees : "textAoOption" { String _text = "BetterTrees"; }
TextDescription textMaxShadowRes : "textAoOption" { String _text = "MaxShadowRes"; }
TextDescription textSeamlessLoops : "textAoOption" { String _text = "SeamlessLoops"; }
TextDescription textDerivedCache : "textAoOption" { String _text = "DerivedCache"; }

// The derived-data cache's "Clear cache" (COMPRESSAO): SharedElements.rsc's Apply button with our
// caption (the Options dialog's own buttons are that object, so the packages hold it).
ButtonDescription buttonDerivedCacheClear : "SharedElements.rsc:buttonApply"
{
	ElementDescription _content = "textDerivedCacheClear";
}

TextDescription textDerivedCacheClear : "SharedElements.rsc:buttonApplyText"
{
	StringTable _stringTable = "EnhancedOptions.rsc:aoStrings";
	String _text = "DerivedCacheClear";
}

ComboDescription comboAoQuality : "Options.rsc:comboAutosave"
{
	int _minWidth = 180;
	int _bottomPad = 3;
	StringTable _initialData = "EnhancedOptions.rsc:aoQualityLevels";
}

SliderDescription sliderAoOpacity : "Options.rsc:sliderUI"
{
	int _bottomPad = 2;
	int _minWidth = 180;
	ElementDescription _number = "Options.rsc:number";
}

ComboDescription comboBetterTrees : "Options.rsc:comboAutosave"
{
	int _minWidth = 180;
	int _bottomPad = 3;
	StringTable _initialData = "EnhancedOptions.rsc:betterTreesModes";
}

ComboDescription comboMaxShadowRes : "Options.rsc:comboAutosave"
{
	int _minWidth = 180;
	int _bottomPad = 3;
	StringTable _initialData = "EnhancedOptions.rsc:shadowResItems";
}

ComboDescription comboSeamlessLoops : "Options.rsc:comboAutosave"
{
	int _minWidth = 180;
	int _bottomPad = 3;
	StringTable _initialData = "EnhancedOptions.rsc:seamlessLoopsItems";
}
