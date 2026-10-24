# Ferry docks: design and record

A **Ferry Dock** is a building you put on a shore, one on each side of a lake, a river or the sea.
Citizens use the pair the way they use a road: a citizen whose job, home or errand is across the
water walks to the near dock, rows across in a small boat and walks on from the far dock. Nothing
waits: each traveller takes a boat, like stepping onto a bridge.

Status (2026-10-24): built on branch `claude/ferries-b99825` (no PR yet). The user took the
recommendations in §7 the same day. On a Mac, unattended: the Ferry Dock is built from the Fisherman's
Dock, citizens cross in boats (hundreds of crossings, none stuck), a save with docks loads with its
links, and a click on a dock opens its panel. Not done: the panel's "Ferry to ..." line, the harbour
for families sent next door (§4, later by the user's choice). What was built and tested: §9.

Everything new is a class B extension, `ext.ferries` (on screen: "Ferry docks"), under
`src/ext/ferries/`, off for ordinary games. Single-player with the extension off stays faithful.

---------------------------------------------------------------------------------------------

## 1. How a citizen finds its way today

Think of the map as graph paper. Each square (cell) holds a **terrain type** (a small number), and
every walker carries a list of the types it may step on.

| Piece | What it is | Where |
|---|---|---|
| Terrain type | one byte per cell: 0/1 road, 2 open ground, 4 resource, 5 quarry pit, 6 high ground, **7 shallow water, 8 deep water**, **9 road or bridge** (over water too), 10 low ground, 11 unknown (walkers on it are held at height 0) | `Game/Map.h:76-92` (`MapCell::terrainType` +0x29), classified by height in `Component/keystone_a_map.cpp:110-135` |
| Walk masks | citizens step on types 0-4, 9, 10, 11 (`0xe1f`); boats on 7, 8, 9 (`0x380`). Type 9 is the only type in both: that is how a boat sails under a bridge | `Action/citizenai_actionctors.cpp:170-179`, `Component/boatcomponent_route.cpp:238-243` |
| Step cost | per type entered, for citizens: road / bridge 3, open ground 4, water 16 (but water is not in the mask), type 11 keeps 1 | `Action/action_base_move.cpp:235-287` |
| **Zones** | a flood fill over walkable land numbers each "island" of walkable cells. A path between two zones is refused before anyone searches; about ten choices (homes, jobs, leisure, markets, builders) compare zones too. Water cells are zone 0 | `Map::FillZones` `Action/action_pf_roots_road.cpp:61-98`, `Map::RequestPath` `Action/action_base_move.cpp:333-365` |
| **The path worker** | one background thread that keeps its **own copy** of every cell's type and runs a plain grid A* (8 neighbours) from the citizen's cell to the goal | `Game/PathfinderThread.h`, `Action/action_pf_astar.cpp:273-388` |
| Walking | `MoveTo` takes at most 256 cells of the answer and steers toward each in turn; when they run out it asks again from where it stands. The step itself does not check walkability. Height comes from the ground under the walker (a bridge deck by a ray cast) | `Action/moveto.cpp:279-482`, `Component/citizenai_executor.cpp:401-510` |
| **Regions** | the map cut into 8x8 blocks, each block's walkable cells grouped into regions with a centre and a list of neighbouring regions. Only used for **distance estimates** when a citizen picks a job, a storage or a home (`Map::PathDistance`), never for the walk | `Action/PathSearch.h:412-491`, `Action/action_pf_region.cpp:174-244`, `Action/action_pf_roots_route.cpp:114-146` |

Roads and bridges change walkability by changing a cell's type: a finished road tile becomes its
road type (`Map::FinishRoadCell`); a bridge's segment entities stamp type 9 (very likely; the value
is game data) onto the water cells under them (`Component/keystone_a_map.cpp:250-257`).
`Map::SetTerrainType` (`Action/action_pf_roots_road.cpp:110-139`) tells the path worker and, when a
cell flips between walkable and not, marks the zones and that cell's region block for rebuilding.

**Bridges have no length limit** (`Player/bridgetool_validate.cpp:108-251`): only a straight row or
column, bank, water, bank, and a clear ring round it. So a long straight bridge can already cross a
lake. A ferry is for water a bridge cannot cross well: winding, very wide, or where a bridge would
cost too much.

Precedent in this fork: ext.neighbour makes the neighbour's buildings solid by writing type 31 into
the **path worker's copy only** (`ext/neighbour/shared_map.cpp:53, 155-167`), so nothing of it is
saved. The ferry uses the same trick the other way round.

## 2. Boats and docks that exist

- **Trader boats** (`Component/boatcomponent_*.cpp`) are ordinary entities with a `BoatComponent` and
  a `CharacterComponent`. The trading post creates one from its template's `_merchantTemplate`
  (`Component/tradecomponent_states.cpp:366-424`). The boat steers like a citizen; the shared move
  tick moves it and, for a description with the fixed-height flag (`_b3c`), holds it at the
  description's height `_f40`: the water surface (`Component/citizenai_executor.cpp:509-510`).
- **Boat routes**: `BoatRoute_FindWaterPath` (`Component/boatcomponent_route.cpp:227-245`), an A*
  over types 7, 8, 9 with the boat costs (deep water 1, shallow 5, bridge 5). It runs on the path
  worker and the caller waits for it.
- **Models in the player's install** (read at run time, never copied): `Models\Buildings\TradingPost\BoatMesh`
  (the trader's boat) with `BoatAnims`, reached through `Template\TradingPost.rsc`; the Fisherman's
  Dock, `Template\FishermansDock.rsc`, with its model, construction stages, footprint and shore
  placement rule. A template is a list of parts (`Component/ComponentDescription.h`: `_subDescs`).
- **Shore placement** is per cell, from the template's `CreatePlacedDescription` code map: the pier
  cells allow water types (`Player/tool_base_tile.cpp:95-151`, `Player/tool_base_path.cpp:102-162`).
  There is no "needs water" flag.
- **Nothing carries or hides a citizen**: fishermen stand on the pier and never go on water
  (`Work/work_act_rawmat.cpp:212-340`); no rowing or sitting animation was found. The nearest pieces
  are a mesh attached to the citizen's tool node (`CitizenComponent::ShowTool`) and taking a model
  out of the scene (`ModelComponent::OnRemove` / `OnAdded`).
- **No water-body id** exists. Two landings need a ferry exactly when their **land zones differ**.

## 3. The design (recommended): a ferry lane

### 3.1 What the player does and sees
- A **Ferry Dock** button in the build menu beside the Bridge. It looks and places like a Fisherman's
  Dock (its model, construction stages and shore rule, by path), has no workers, and costs what the
  Fisherman's Dock costs (first version). As built, its workers' parts are the Well's (§9).
- Build one on each shore. Each dock **links to the nearest Ferry Dock on other land across the same
  water**, up to 150 tiles of water. Planned, not built yet: the dock's panel saying "Ferry to the
  dock 34 tiles across" or "No dock across the water yet" (today it is the Fisherman's Dock's panel).
- A route **opens as soon as one of its two docks is built**: the builders row over to build the
  other one (otherwise the far dock could never be reached).
- A citizen bound across walks to the dock, glides across standing in a small boat (the trader's boat
  model, scaled down) and walks off at the other dock. Many can cross at once. Trader boats still sail
  through the lane.

### 3.2 How it works inside
1. **Pairing and the lane** (standard C++, unit tested, `src/ext/ferries/lanes.{h,cpp}`): from the
   map's cell types and each dock's water end, pair every dock with its nearest dock in a different
   land zone reachable through water, and trace the **lane**: the cells of a water route between the
   two (a small A* of our own over types 7 / 8, preferring deep water and straight lines).
2. **The path worker** gets the lane: every lane cell is written as type 9 (bridge) into the worker's
   own copy only (`PathfinderThread::SetCellTerrain`), as ext.neighbour does. Citizens may now step
   on it at road cost; boats still pass (9 is in their mask). The two lane cells at each end are type
   4 instead (cost 16 each, "waiting for a boat"), so a short walk round is preferred to a pointless
   ride. A crossing therefore costs about what walking the same distance on a road costs, plus about
   eight tiles' walk for getting in and out. `Map::cells` is never changed, so nothing of it is saved
   and the faithful A* runs untouched (and so do ext.fast_paths and ext.path_workers, which read the
   same copy).
3. **Zones** (EXT SEAM at the end of `Map::FillZones`): the two land zones of every open link are
   joined (one pass over the cells with a small remap table). Every zone check in the game then
   treats the two shores as one island, with no other change.
4. **Regions** (EXT SEAM at the end of `RegionGraph::Flush`, as built; `RebuildAdjacency` wipes the
   lists every time): the region at each dock's landing gets the other landing's region as a
   neighbour, so job and storage choice see the crossing. A link that opens or closes marks its
   landing cells dirty, so the next distance query flushes (which drops the region-search memo).
5. **Riding** (EXT SEAM in the move tick, `citizenai_executor.cpp` after the height is set): a
   citizen standing on a water cell (`Map::cells` type 7 / 8: in retail no citizen ever does) is held
   at the water's height and a boat mesh is drawn under them, facing their heading (a bare
   `Graphics::MeshInstance`, as the neighbour's mirrors are drawn). The faithful tick pays one null
   test.
6. **Keeping it current**: links are recomputed when a dock is finished or removed and on load
   (after the path worker is seeded from the saved map); a `SetTerrainType` on a lane cell (a bridge
   built over it, then removed) puts the lane back in the worker's copy.

### 3.3 Class and saves
- **Class B**: the links are worked out again from the docks and the map, so the save needs no new
  state; play differs from retail (citizens cross water), so the switch is locked per game.
- The docks are real entities of our own template (`Template\ExtFerryDock.rsc`, built in memory at
  boot from the Fisherman's Dock's parts plus our button and text; no retail data copied). A save with
  docks therefore needs OpenBA with ext.ferries, which the save's extension manifest already enforces;
  retail warns about the missing "mod" and cannot show the docks, as with any building mod.
- A citizen mid-crossing in a save carries on after loading (its path is in `MoveTo`'s saved cells).

### 3.4 Alternatives considered
- **One ferry boat per pair** that waits at a dock, takes up to N passengers and sails across: closer
  to a real ferry, but citizens must queue and stand still (MoveTo has no "wait"), the boat needs a
  schedule and a state machine, and a crowd waiting at a dock looks like a bug. Possible later on top
  of the same links.
- **A jump in the path search** (an extra A* neighbour from dock to dock): the cost would be exact,
  but it is a seam inside the hottest loop of the game (`GridPathSearch::FindPath` and ext.fast_paths'
  own copy), which hook rule 4 forbids. The lane needs no change to any search.
- **A Fisherman's Dock with a "ferry" switch** (no new building): the fallback if a new building
  template turns out too hard (§6 step 1).

## 4. Neighbor towns and the Continents map

The Continents map (branch `claude/continents-d6295d`, `NEIGHBOUR_TOWNS.md` §12; facts from that
session, 2026-10-24): exactly two lands, each ringed by sea, no small islands (rivers can still split
a land into several zones). From each coast about 1-2 tiles of shallow water (7), then deep water (8);
the floor reaches -2.5 within 5 tiles. The gap between the lands averages 45 tiles on a small map, 67
on a medium and 90 on a large one, never under 20 (scaled by map size), typically 30-60 on a small
map. The sea is one body of water, between the lands, all round them and out to every map edge;
inland lakes are separate. No type 9 is ever made by the classifier. Each town starts on its own land
(town 1, the room's creator, west or north). The trader boats' lane (path 0) runs straight down the
middle of the gap; a ferry lane crossing it does not block them. Reusable there:
`Continents::LandDistance`, `Continents::RingedBySea` (works on a loaded save).

- **Inside one town** the ferry is the same everywhere: across a river or lake on an ordinary map,
  between your own land and an island, or from your continent to the other one.
- **Each machine simulates only its own town**, so a citizen can never walk into the neighbour's
  town. On a shared map the land itself is shared (first come, first served), so your ferry can take
  your citizens to unclaimed land on the other continent, and they may build there. Your citizens do
  not appear in the neighbour's game (they never did).
- **The neighbour's docks** are drawn in your game like their other buildings (mirrors, by template
  path), so both towns need ext.ferries on: a lobby room turns it on for both, as it does
  ext.neighbour. A mirror dock is never linked: your citizens ride only between your own docks.
- **Later, between towns** (not in the first version): a dock becomes the harbour for what you send
  next door. A family you send walks to your dock and rows away; a family arriving from the neighbour
  rows in to your dock (when you have one) and walks into town instead of appearing among the
  townspeople. Goods could arrive by boat the same way. The relay's parcels (`NEIGHBOUR_PROTOCOL.md`)
  do not change; only where a family leaves and arrives does.

## 5. What does not change
The faithful game code changes only through one-line EXT SEAMs (the list as built: §9), each doing
nothing with ext.ferries off. The build-menu entry is a `Toolbar` object the toolbar finds as it finds
retail's. No new `BANISHED_*` flag; the test aid is a settings key, `ferries.testdocks` (as
`neighbour.autolobby` is).

## 6. Build plan (after the user's OK; done except step 5, see §9)
1. **Spike: the building.** Build `Template\ExtFerryDock.rsc` in memory from the Fisherman's Dock's
   parts (placement, model, construction, picking, highlight, decal, destroy; not fishing, work,
   storage or its fishing panel) plus our toolbar entry and text; place, build, save and load it on
   the Mac. If this fails: the fallback in §3.4.
2. `lanes.{h,cpp}` (pairing, lane tracing) and its unit tests.
3. The path worker lane, the zone and region seams, re-applying on load and on `SetTerrainType`.
4. Riding: height and the boat mesh.
5. The dock's panel line ("Ferry to ...").
6. An unattended Mac run: `ext.ferries_testdocks=1` places two finished docks across the water
   nearest the Town Hall and sends a few citizens to the far shore; `[Ferries]` lines on stderr count
   links, boardings and landings; a screenshot of a crossing (`BANISHED_SCREENSHOT_AT`, camera pin).
7. Docs: this file's record section, `docs/extensions/LIST.MD`, the registry row's text.

## 7. Decided by the user (2026-10-24: "go ahead with your recommendations")
1. The ferry style: **each traveller rows their own boat** (§3), not one ferry boat that waits.
2. On a shared map your ferry may take your citizens to free land on the neighbour's continent.
3. The harbour for things sent to the neighbour (§4, last point): later, after ferries work.
4. The crossing's price: about a road's walk plus a short wait.

## 8. Risks
- The new building is the biggest unknown: no extension has added a building before. The panel a
  player opens on the dock may need its own simple layout if the Fisherman's Dock's panel expects its
  fishing parts.
- The boat model is a merchant ship; scaled down it may look odd. Then a plainer stand-in (the dock's
  own model has none) or hiding the citizen during the crossing.
- The water's height: every water surface is drawn at about -0.75 world height (the water mesh's
  vertex z is in 1/256 units: `featureIdx - 191` for lakes, and the Continents sea the same way,
  e.g. -0.738 with two lakes; rivers `-192 - body`). The rider takes the trader boat's own fixed
  height (`CharacterDescription::_f40`) or -0.75; to be checked on screen.
- A citizen whose 256-cell piece of path ends mid-lane asks again from a water cell, and the game then
  lets that one search cross any water (`Map::RequestPath` adds the water types). They would still be
  drawn in a boat; the lane's lower cost keeps them on it.

---------------------------------------------------------------------------------------------

## 9. As built (2026-10-24)

Files: `src/ext/ferries/ferries.{h,cpp}` (the game glue), `lanes.{h,cpp}` (pairing and lanes,
standard C++), `test_docks.{h,cpp}` (the test aid), `tests/unit/ext_ferries_test.cpp`. Registry row
`ferries` (class B, off by default, formatVersion 1); strings in `Dialog/ExtensionsStringTable.rsc`
(`FerryDock*`, `FerryCreate*`).

- **The building** (`Template\ExtFerryDock.rsc`, served by our resource provider, built once per
  process): the Fisherman's Dock's parts by reference, in their order (the state machine chains the
  parts' controllers in that order: with the parts reordered the docks never started construction),
  less its radius and fishing, with its work and workplace swapped for the **Well's** (builders while
  it is built, no workers after; the dock's own would hire fishermen), and its build-menu text ours.
  Its storage stays: a construction site keeps the delivered materials there (`GatherRawMaterials`;
  without it the game crashed on placement). The icon is still the Fisherman's Dock's. Cost: the
  Fisherman's Dock's.
- **The button** (`Template\ExtFerryDock.rsc:ferrydock`, a `Toolbar` item): in the Transport group at
  priority 500, after the Bridge (300) and the Tunnel (400); shown only in a game with ext.ferries on.
  The template and button share one provider prefix because the provider table holds 8 entries and
  was full. Not checked by eye: the toolbar's group never opened in an unattended run.
- **Saves**: a dock is an ordinary entity naming our template; the template is provided when the
  save's entities load (before the save tail's manifest locks ext.ferries on). Under a command-line
  faithful profile `SaveLoadDialog::DoLoad` registers the provider beside `InitText` (EXT SEAM).
- **Finding the docks**: the town manager's per-template list (`CitizenBehavior` +0x160, filled by
  `ZoneComponent`), looked at twice a second; a dock counts as finished when its model shows its
  group's last mesh. Any change marks the zones stale, so the next path request works the links out
  again.
- **Seams** (each a one-line EXT SEAM citing this file, each idle with the extension off):
  `Map::FillZones` (end of the rebuild: links, lanes, zones joined), `Map::SetTerrainType` (a lane
  cell keeps its lane type in the worker), `RegionGraph::Flush` (the landing blocks' adjacency
  rebuilt, then each link's two regions made neighbours), the character move tick just past its COPY-PINNED tail,
  on the last iteration (`if (Ext::Ferries::g_riding && rootOnly == 0) Ext::Ferries::Ride(...)`: one bool
  test per tick; inside the tail it broke `copy_drift_pinned_regions_unchanged`), `GameUpdateLoop::Update`
  (`BeginFrame`), `~GameUpdateLoop` (`EndGame`), `GameUpdateLoop::Load` (`SelectForGame`),
  ext boot (`InitProviders`).
- **The ride**: a citizen on a water cell of `Map::cells` is held at the trader boat's fixed height
  (`Template\TraderBoat.rsc` `CharacterDescription::_f40`, read at run time: 0.0 in 1.0.7) and a
  boat is drawn under them: `Models\Buildings\TradingPost\BoatMesh` at 0.32 scale, turned to the
  citizen's heading, as a bare `Graphics::MeshInstance` (the neighbour mirrors' way), moved once a
  frame from notes the tick leaves. In the screenshot it reads as a rowboat with its oars out and the
  citizen standing in it.
- **Multiplayer**: the lobby switches ext.ferries on with ext.neighbour for the room's game and gives
  the player's own setting back afterwards (`neighbour.cpp` `SetLobbyStart` / `ResetForGame` /
  `SelectForGame`). The neighbour's docks arrive as mirrors by template path, as any building.
- **Test aid** `ferries.testdocks=1` (settings.cfg; a new game whose town has no dock): from about
  frame 90, every valid dock site within 70 tiles of the town by the game's own footprint test, the
  nearest one in the town's zone that links (`LinkFrom`) to a site in another zone; both docks are
  placed with `PlacementTool::CreateAt` inside retail's instant-build bracket (GameUpdateLoop +0xd0
  bit 0, as the start town's founding buildings), so the route opens at once; a house beside the far
  dock is placed as a construction site; 120 frames later the near dock is selected with the edit
  tool (its panel opens, as a click does).

### Tested (Mac, native build, seed 12345, small Valleys map, speed 10)
- Unit (`tools/build.sh unit`, Mac): 1422 tests, 0 failures, 17 skipped; 10 new tests in
  `ext_ferries_test.cpp`: two shores linked, straight across; a link needs one finished dock; no link
  on one land; the lane length cap; no corner cutting; each dock takes its nearest partner, the same
  input gives the same lanes, `LinkFrom` agrees; deep water preferred; a dock with no shore ignored;
  the lane's worker types; zones joined.
- The test aid put the docks either side of the river beside the start town, 9 cells of water apart;
  `[Ferries] link: dock (103,120) to dock (97,109), 9 cells of water, zones 2 and 1`. Citizens began
  crossing at once (builders and haulers for the house): 59 boardings in the first game minute, 259
  boarded and 259 landed by the autosave, at most 7 on the water at once, none left there. Empty crash
  logs. Screenshots (camera pinned at the lane): the two docks and a citizen standing in a rowboat
  mid-river; the selected near dock outlined.
- **Save / load**: the autosave with two docks loaded with `BANISHED_AUTOLOAD`: `[ExtSave] manifest: ...
  ferries(v1, 0 payload bytes)`, `2 Ferry Dock(s) in this town`, the same link, crossings resumed.
  (That run also showed the test aid firing again on a loaded game; it is armed only for a town with
  no dock now.) With four docks (two more placed in that run) there were three links, one of them a
  71-cell crossing between zones 5 and 1.
- With ext.ferries off: no `[Ferries]` line, the ordinary seed-12345 land
  (`[WorldHash] height f32=627a733aa4109739`), no crash. `tools/acceptance/acceptance.sh`: 0 off target.
- CI on the branch (run 40115573084, started by hand: CI runs on pull requests): Windows (MSYS2
  UCRT64), Linux, macOS and Acceptance all passed.
- Not run: the faithful gate (the PC); the game on Windows; a Multiplayer room with ferries; a Continents map; a player placing a dock by hand (the button was
  not clicked); a trader boat sailing through a lane.

### Open
- The panel line ("Ferry to the dock N tiles across"): the panel is the Fisherman's Dock's own.
- A crossing's speed is the citizen's walking speed (water cells are not road cells in `Map::cells`).
- The neighbour's mirrored dock marks its footprint solid in the worker (type 31); a lane passing
  under their pier would be cut there.
- Citizens walk (their walking animation) while standing in the boat.
- Lane ends are type 4 in the worker, which boats cannot sail: a trader boat's approach right
  beside a Ferry Dock could be cut.
- Any walker whose walk mask has type 9 could use a lane (a deer, say) and would be drawn in a boat.
  Trader boats are left out (they sail at a fixed height).

