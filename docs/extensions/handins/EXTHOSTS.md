# EXTHOSTS: host records in the save tail, and the extension window reopening with the save

Ruled 2026-10-21 after PR #11 (the first class C extension and the shared toolbar window). The rule
itself is DESIGN.md §3, "Host records"; this is what was built.

## What retail does

A utility window (the Town Hall, the map) goes through `PlayerToolManager::ToggleUtilityDialog`. The
tool manager keeps a set keyed by the window's template with `x`, `y` and an open byte; dragging updates
the position, closing clears the byte and keeps the record. The set is written in the main save
(`gameupdateloop_serialize.cpp`, `GoalRefTree_Serialize_e2e0`) and `PlayerToolManager::Startup`
(`entityui.cpp`) reopens every open entry where it was. The extension window cannot be a member: its key
would be a non-retail path inside retail's part of the save.

## What was built

* **Tail version 2** (`src/ext/savetail.{h,cpp}`): each record carries a kind byte and a flags byte
  (`kFlagMustUnderstand`) before its id. Version-1 tails are still read (every record an extension's,
  must-understand). Older builds refuse a version-2 tail as a newer chunk version: saves with a B / C
  extension active made by this build do not load in builds before it. The kind byte means no later kind
  needs a version 3.
* **Two kinds.** Extension records (the lock set, unchanged rules) and host records (presentation only,
  never fatal). `SplitKinds` sorts them right after `Decode`; only extension records reach `Validate`,
  the lock, the in-game-toggle filter and `ApplyState`. `ApplyHostState` hands each host record to its
  `HostCodec` (`src/ext/host_codecs.cpp`) and resets a host it cannot apply.
* **Written only beside an extension record** (`EncodeActive`): a town with only class A pages, or no
  extension, writes zero bytes, as before.
* **The toolbar window's record** (`src/ext/toolbar/window_state.{h,cpp}`, host id `toolbar`, wire v1:
  `u8 open, i32 x, i32 y, String page`): open, the upper-left corner, and the showing page by its
  Contribution id. The window writes it on open, close, drag and tab switch; a quit or a load deletes the
  window without `OnClosed`, so the saved `open` stays for the save that holds it. Reset with the class C
  state (`ResetAllState`: a new game, the lock owner's unlock).
* **The reopen** (`PlayerToolManager::Startup`, one EXT SEAM line after retail's loop):
  `Ext::Toolbar::RestoreWindow` opens the window when the record says open and some page is offered (a
  reload that turned the last contributor off opens nothing). Placed where it was, unclamped, as retail
  places a pinned window; the saved page is selected, the first page when it is gone.

## Gates

Unit 1254 / 0 / 3 kit skips (new: a version-1 chunk read, kind split with an unknown droppable and an
unknown must-understand kind, the toolbar record's round trip through `EncodeActive`, no host record
without an extension record, host records never fatal, the codec's refusals). The faithful resave
golden is unchanged (no extension active: zero bytes).
