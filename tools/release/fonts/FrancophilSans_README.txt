FrancophilSans -- the typeface beside OpenBA.exe
=================================================

Files: FrancophilSans.ttf (Regular), FrancophilSans-Bold.ttf (Bold)
Version 1.0, 2005-03-24. Author: Manfred Klein (1932-2018).
Copyright (c) M Klein -- the line carried by the fonts' own name tables.

Why it is here
--------------
Banished's user interface is set in FrancophilSans. OpenBA's "Sharp UI
scaling" extension (ext.sharp_ui) draws that interface crisp at any UI scale
by rasterising the game's fonts at the exact pixel height the screen needs,
at run time, from this typeface. OpenBA looks for the face installed on the
machine first; when it is not installed it loads the .ttf files beside the
executable privately, for its own process only (nothing is installed, nothing
is changed on the machine). Without the files and without the face installed,
Sharp UI falls back to the game's own glyph sheets.

Terms of use (Manfred Klein's "Simple Terms of Use", summarised)
------------------------------------------------------------------
Manfred Klein released his fonts under his Simple Terms of Use:
  * free for private AND commercial use;
  * when a use makes a profit, a donation to Doctors Without Borders
    (Medecins Sans Frontieres, https://www.msf.org/) is asked for;
  * the fonts may be passed on, but only together with the font files as
    they are, and not bundled into font compilations (CDs, disks, products)
    for sale without the author's permission.
These two files are passed on unchanged, with this notice, as the terms ask.
If you redistribute OpenBA, keep this file with them.

Not part of OpenBA's code
-------------------------
The typeface is NOT part of OpenBA and is NOT covered by OpenBA's GPL-3.0
licence (LICENSE). It is Manfred Klein's work under his own terms above;
OpenBA only reads it at run time. The two files are kept unchanged under
tools/release/fonts/ in OpenBA's source repository, with this notice, so the
release packages can be built from the repository alone.
