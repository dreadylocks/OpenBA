# Neighbour towns: the fork's plan

This fork (`dreadylocks/OpenBA`, a just-for-fun fork of `fran-cap/OpenBA`) adds a two-player
"neighbour towns" mode: each player runs their own town on one shared map, and the towns trade,
swap migrants and send messages through a small relay server the user hosts. An optional
`island_maps` generator comes later.

Ground rules (from the user, 2026-10-19):
- Everything new is an **extension in `src/ext`** per `docs/extensions/DESIGN.md`: class B/C,
  off by default, so plain single-player stays faithful and the faithful gates still apply.
- If a repo rule (CLAUDE.md, DESIGN.md, DIRECTION.md) blocks a fork goal, **ask the user**;
  don't work around it silently. Known clashes are in §5.
- Explain things to the user in plain words (simple analogies), not jargon.
- Never push or add remotes without asking.

Status: research and toolchain only. No feature code exists yet. Next steps are in §6.

---------------------------------------------------------------------------------------------

## Decisions so far (2026-10-19)

- **Split world** (option B in §2) for neighbour towns.
- **No islands needed for it.** Neighbour towns start on a normal map split in two by a border
  line. `island_maps` is a separate, optional feature for later.
- **Machines**: the user's Windows PC (their normal Banished PC) is where the game is built,
  tested and played; development moves there (clone of this fork, Claude Code on the PC). The
  user's Apple Silicon Mac can cross-build the Windows exe (`tools/build.sh build --cross`) and
  will host / test the relay server work if convenient.
- **Fork**: `dreadylocks/OpenBA`, public (also covers the GPL when the exe is shared with the
  friend). Work branch: `neighbour-towns`.

---------------------------------------------------------------------------------------------

## 1. Island maps (later, optional)

### How a Banished map is made

Think of a pie dish. The generator makes a terrain **twice as wide as the playable map**
(`src/game/Game/worldgen.cpp:198`, `gridSize = 2 * mapSize`; heightmap index `x + gridSize/2`,
`mapquery.cpp:62-66`). The playable square sits in the middle; the ring around it is scenery
that is drawn (`geometry_draw.cpp:180-230`) but not used.

1. **Base shape = a bowl** (`wg_BaseHeightmap_e5d0` -> `WgCells_BaseHeightmap`,
   `src/game/Game/worldgen_cells.cpp:38-66`). Each point blends gentle "valley" noise
   (`base = (pn1+1)*8`) with rough "mountain" noise (`h = (pn2+0.25)*48 + pn1`, **clamped to at
   least 2.0**). The weight `w = 1/(1.1^((dim*0.25+16) - dist) + 1)` is about 0 inside a circle of
   radius `mapSize/2 + 16` and about 1 outside it. The tile classifier (`maptilegrid.cpp:47-57`):
   height < -1.4 deep water, < -0.25 shore water, -0.25..0.75 and flat buildable, else mountain.
   **So everything outside that circle is >= 2.0 and can never be water.** The playable square's
   corners reach into the mountain ring; its edge midpoints are mostly valley.
2. **One main river, edge to edge** (`wg_f390_coastline`, `worldgen_f390_coastline.cpp:615`).
   Despite its name ("coastline ring") it is an open wiggly line from one side of the big
   terrain, through the middle, out another side; retried until one fits.
3. **Side valleys** (`wg_f390_paths`, `worldgen_f390_ranges.cpp:571`) branch off it to the border.
4. **Lake pits** (`wg_f390_ranges`, misnamed "mountain ranges", `worldgen_f390_ranges.cpp:491`)
   and river points (`wg_f390_rivers`) inside the map.
5. **Carving** (`wg_f390_stamp`, `wg_f390_carve`) presses land down along all of those; then
   erosion and two smoothing passes.

"Land at the edges" is rule 1 (the radial blend plus the clamp). Nothing else forces it.

**Why it exists** (best reading): it hides the end of the world behind natural-looking
mountains; it puts the good land in the middle, where the start-town search begins
(`starttown.cpp:366`, a spiral from the map centre); it gives the river and valleys somewhere to
come from, which is how traders and nomads arrive.

### What depends on it

| Thing | Depends? | Detail |
|---|---|---|
| Nomads | **Yes** | `ImmigrationComponent::Spawn` (`Component/immigrationcomponent_states.cpp:275-383`) picks a random side, `_spawnEdgeDistance` (10) tiles in, and needs walkable land in the **same zone as the Town Hall** (`FindSpawnCell`, :201-234). None found: `return`, and the hall retries every frame forever, silently. |
| Trader boats | **Yes** | Route = polyline 0, the main river (`Component/boatcomponent_route.cpp:312-466`): approach lane, A* through water types 7/8/9 (buffer 0x1000 cells) to the dock and back. `poly->size` on record 0 is unguarded (:349). Unreachable dock: the boat sails past and the trader is silently lost. |
| Camera | No | Clamps only its focus point to the playable square (`Game/camera_input.cpp:225-235`). |
| Water drawing | **Backwards** | There is no sea plane. Water is drawn only as strips along the path centrelines and as shore patches around `waterFeatures` (`Game/water_contour_mesh.cpp`, phases 1/2). Lowered edges would be a dry seabed. |
| Zones | Yes | `FillZones` (`Action/action_pf_roots_road.cpp:56-93`) floods walkable land; water splits zones; `Map::RequestPath` rejects cross-zone paths. |

Nothing else enters from the edge (livestock appear at the trading post, wild animals live
inside, no citizen ever emigrates). River sounds sit on the river line (`starttown.cpp:4935-4960`).

### Proposal: `island_maps` (class B)

Class B, not C: the save already stores the finished terrain and water mesh
(`geometry_serialize.cpp`), so an island save needs no new data.
1. New base shape: land in the middle, below -1.4 past a noise-wobbled coastline, sea to the rim.
2. Path 0 becomes a sea lane: a closed ring in deep water a few tiles off the shore, so the
   trader route code works unchanged with short A* legs.
3. Draw the sea: try (a) one huge `waterFeatures` entry so the phase-2 mesher covers every
   underwater cell; else (b) a flat sea quad with the existing water material.
4. Nomads land on the beach: walk inward from a random sea edge point to the first walkable
   cell in the Town Hall's zone. Later: a boat drops them off.
5. An "Islands" choice in the new-game menu while the extension is on.

---------------------------------------------------------------------------------------------

## 2. Neighbour towns

### Facts that decide the design
- **There is no Player object.** Everything hangs off `GameUpdateLoop::_singleton`
  (`Game/GameUpdateLoop.h:116`); "the town" is one town manager (`CitizenBehavior`,
  `m_citizenList`): citizens, houses (`field_0x20`), storage tally (re-counted every tick,
  `Entity/citizenbehavior.cpp:640`), trade credit (`field_0x1AC`). One sim = one town.
- **Same seed = same map, on the same build.** `[WorldHash]` (`gameupdateloop_load.cpp:1235`,
  `worldgen.cpp:400`) was 9/9 identical (LOADPLAN.md:13). Inputs: seed, map size row (0/1/2 =
  256/384/512), the TerrainGenerator and MapGenerator (start condition) resources, the exe and the
  B/C extension set. Some worldgen maths uses the platform libm (`worldgen_f390_erosion.cpp:84`,
  `starttown.cpp:381`), so both players must run the same exe; compare the hash on join.
  The seed comes from `NewGameDialog::GenerateRandomSeed` (`Dialog/newgamedialog.cpp:204`).
- **The sim after that is not repeatable** (`newgamedialog.cpp:216-230`), and there is no command
  queue: tools and ~70 dialogs change game objects directly.

### Options
- **A. Lockstep** (both PCs run the whole map and swap only clicks): needs a fully repeatable
  sim, two towns in one sim (a second town manager touches storage, census, jobs, the work
  scheduler, every dialog), every click queued, desync detection. Months, fragile. Rejected.
- **C. Host runs both towns, friend streams a view**: still two towns in one sim, plus streaming
  thousands of entities; laggy for the friend. Rejected.
- **B. Split world (chosen)**: each PC runs an ordinary single-player game containing **only
  its own town**; the server is the postman.

### Split world design
- **Map**: same seed and settings from the server; the joining client checks the world hash.
- **Border**: a line splitting the map in two, placed so the river runs through both halves
  (both players get a trading post and fishing). Rules: no buildings, roads, bridges or fences
  past it (`Map::TestPlacement`, `Player/tool_base_path.cpp:101`, plus the road tool
  `roadtool.cpp:~150`, bridge and fence tools; the on-map check is `tool_base_ray.cpp:357`).
  Citizens may still wander over in your own copy; harmless, each copy is separate.
- **Start towns**: the start spiral (`StartTown_FindSpawnTile`, `starttown.cpp:368`) starts from
  the centre of the player's own half. Note the tree/stone scatter draws from the gameplay RNG
  after the founding spawns (`starttown.cpp:4619, 4640-4652`), so trees in the other half can
  differ between the two screens. Harmless scenery.
- **Trade**: you pack goods into an export at your Trading Post (removed from storage at once,
  so nothing is duplicated). The neighbour's post gets a merchant boat carrying exactly those
  goods (write its `manifest` / `tradeId` directly, `Component/tradecomponent_dock.cpp:245`) and
  barters with it in the normal Trade window (`Dialog/tradeui*.cpp`). Payment sails back;
  unsold goods come home.
- **Migrants**: "send a family" from the Town Hall: they walk to the border and leave. They
  arrive next door as an ordinary nomad group spawned at the border on that side, accepted or
  refused in the usual window (`ImmigrationComponent`, Accept :459, `NomadComponent::ConvertToCitizen`
  :406). Adding citizens directly: `Game_SpawnCitizens_Impl` (`Dialog/debugui.cpp:409`).
- **Messages**: a chat line and neighbour news in the event log.
- **Ghosts**: every ~10 s each side sends {template name, position, rotation, built %} for its
  buildings; your game draws them in the other half as look-only ghosts, and clears trees under
  them in your copy. **Main unknown**: ghosts must not be real entities, or your town manager
  would count their houses and barns as yours. Fallback: minimap markers + a Neighbour panel.
  The instant-build path for real buildings is the `placeStructure` lambda (`starttown.cpp:1425-1790`).
- **Time**: each town keeps its own clock and speed; optional "speed lock" (both at the slower
  player's speed, pause pauses both).
- **Saves**: still single-player saves, plus a class C tail record (room, half, undelivered
  parcels, last neighbour snapshot). `ExtSave_WriteTail` / `ExtSave_ReadTail`
  (`Game/gameupdateloop_serialize.cpp:777, 794`) write and read the tail, but **no extension fills
  a payload yet** (`src/ext/savetail.*` leaves it empty): that plumbing comes first.
- **Hooks**: per frame `Neighbour::BeginFrame()` in `GameUpdateLoop::Update` beside
  PerfX/WalkBatch (`gameupdateloop.cpp:576`); per-game setup next to `Ext::LockForGame` /
  `SelectForGame` (`gameupdateloop_load.cpp:735, 739`). The network runs on its own thread; the
  game only drains a queue, so a slow network never stalls the sim.

### Milestones
- **M0 Plumbing**: game built and running on the PC; relay server up; the game connects, joins
  a room, shows a chat line in the event log; class C payload plumbing.
- **M1 Letters** (works even on two separate maps): trade parcels and migrants.
- **M2 Shared map**: seed from the server, hash check, half-aware start, border rule, ghosts
  (or the minimap fallback).
- **M3 Extras**: speed lock, visible boats/walkers carrying parcels and migrants.

---------------------------------------------------------------------------------------------

## 3. The relay server

- **Transport**: plain TCP, length-prefixed messages; one network thread in
  `src/ext/neighbour/`, plain sockets behind a small `src/platform` seam (Winsock / BSD). No
  third-party library.
- **Server**: ~200 lines of Python 3 standard library (asyncio). A room = two seats + a shared
  password; forwards messages; **keeps parcels for an offline player**; remembers each town's
  last snapshot. Runs as a systemd service (or a one-line container) on one TCP port, e.g. 7777.
- **Safety**: preferably Tailscale for the server and both players (no public port); otherwise
  TLS (`ssl`) + the room password. The game validates every incoming message (sizes, counts,
  names, positions): bad data must never crash a town.
- **Later, if walkers are streamed**: ENet (MIT, ~6 C files in `third_party/`).
  GameNetworkingSockets (BSD) is licence-clean but overkill (protobuf + OpenSSL under MinGW).
- A "fake neighbour" script that plays the other town lets one person test trade and migrants.

---------------------------------------------------------------------------------------------

## 4. Development setup

**PC (primary).** Windows, the user's Banished 1.0.7 install (default `C:/GOG Games/Banished`;
set `BANISHED_DIR` if elsewhere). One-time:
1. Git for Windows, Python 3, `pip install cmake ninja`.
2. MSYS2, then `pacman -S mingw-w64-ucrt-x86_64-gcc` in a fresh install (CLAUDE.md's "don't
   `pacman -S`" is about the original developer's stale install). Put `C:\msys64\ucrt64\bin` on
   PATH. Expect a newer GCC than the reference 13.1.
3. Claude Code for Windows.
4. `git clone -b neighbour-towns https://github.com/dreadylocks/OpenBA.git`, then
   `git submodule update --init`, then `tools/build.sh run` (builds, deploys, starts a new game).
5. The exe needs `FrancophilSans.ttf` and `FrancophilSans-Bold.ttf` beside it or installed: they
   ship in the upstream `OpenBA-v1.0.0.zip` release, never in the tree.

**Mac (secondary).** `brew install mingw-w64 cmake ninja`, then
`tools/build.sh build [flavour] --cross` -> `build/cross-<flavour>/OpenBA.exe`
(`tools/toolchains/mingw-w64-x86_64.cmake`; Homebrew mingw-w64 14.0.0 / GCC 16.2, UCRT). Debug
and release both build (2026-10-19); ~235 extra warnings from GCC 16 (mostly unused variables, 2
`-Warray-bounds` on zero-length arrays in `Work/work_act_consume.cpp:264,298`). A Mac-built exe
had not been run yet at the time of writing. Native macOS is not an option (no D3D11; weeks).

---------------------------------------------------------------------------------------------

## 5. Repo rules that bump into the fork (open, the user decides)

1. `tools/build.sh run` (the gate) needs Windows: fine on the PC.
2. Extension settings are `ext.<id>=0|1` only; server address / room / password need string keys
   in `settings.cfg` or a Join dialog. Env flags stay banned (`Opt()`).
3. Class B/C are off by default and locked per game: fine, the user switches them on.
4. CLAUDE.md's goal is a faithful port: kept; CLAUDE.md now points here.
5. No game content in the tree: no conflict (ghosts use the user's own install; the server
   sees only names and numbers).
6. GPLv3: sharing the exe with the friend = distribution; the public fork covers source access.

---------------------------------------------------------------------------------------------

## 6. Next steps (for the session on the PC)

1. Set up the PC (§4) and get `tools/build.sh run` to a loaded new game. Also confirm the
   upstream release exe runs there, to have a known-good baseline.
2. M0: class C payload plumbing in `src/ext/savetail.*`; `src/ext/neighbour/` skeleton (registry
   row, class C, off by default); the socket seam; the Python relay server (`tools/neighbour_server/`
   or similar) and the fake-neighbour script; a chat line in the event log.
3. Ask the user before: adding string settings keys / a Join dialog, opening any network port,
   and pushing.
