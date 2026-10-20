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

Status (2026-10-20): M0, M1 and most of M2 done. A Multiplayer lobby on the main menu creates and
joins rooms; both towns build the same map; the neighbour's land is claimed and their buildings
are drawn in your game. In game, N opens the Neighbour panel (chat, goods, a family). What was
built and how it was tested: §7 (M0), §8 (M1), §9 (M2). Next steps: §6.

---------------------------------------------------------------------------------------------

## Decisions so far (2026-10-19)

- **Split world** (option B in §2) for neighbour towns: each PC still simulates only its own town.
- **Full mixing, no border** (the user, 2026-10-19, superseding the border line): both towns build
  anywhere on one shared map. Land is claimed through the server (first come, first served),
  resources are shared (a tree felled in one town is gone in the other), the neighbour's roads
  are real roads in your copy, the neighbour's buildings block your citizens' paths, and the
  neighbour's buildings and roads draw with a colour tint. Design: "Mixed world design" in §2.
  The border line is no longer an intermediate step.
- **No islands needed for it.** `island_maps` is a separate, optional feature for later.
- **Machines**: the user's Windows PC (their normal Banished PC) is where the game is built,
  tested and played; development moves there (clone of this fork, Claude Code on the PC). The
  user's Apple Silicon Mac can cross-build the Windows exe (`tools/build.sh build --cross`) and
  will host / test the relay server work if convenient.
- **Server: Cloudflare Workers + Durable Objects** (the user, 2026-10-19), not a Python relay
  behind Tailscale: always on, free tier, TLS included, no port on anyone's PC. See §3.
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

### Mixed world design (chosen 2026-10-19: full mixing)
Each PC still runs only its own town; what is shared is the LAND. Your copy holds your real
town plus a mirror of the neighbour's buildings, roads and resource changes.
- **Map**: same seed and settings from the server; the joining client checks the world hash.
- **Land claims (the server is the referee)**: placing a building, road, bridge or fence first
  marks its tiles "pending" locally and sends a claim; the room's Durable Object (one request at
  a time, so it can never grant the same tile twice) answers granted / refused and tells the
  other town. Refused: the plan is cancelled and its materials refunded. Your game rejects
  placement on tiles the neighbour holds, in the same checks a border would have used
  (`Map::TestPlacement`, `Player/tool_base_path.cpp:101`, the road tool `roadtool.cpp:~150`,
  bridge and fence tools; the on-map check is `tool_base_ray.cpp:357`). Claims persist in the
  Durable Object, so they hold while a player is offline. **Open**: building while offline
  (proposal: only on tiles you already hold).
- **Shared resources**: trees, stone, iron, and later fish and game. **Prerequisite**: both copies
  must start with the SAME scatter, but the tree/stone scatter draws from the gameplay RNG after
  the founding spawns (`starttown.cpp:4619, 4640-4652`), so today it differs between screens.
  Fix: one town generates the scatter and the other receives it (or the scatter gets its own
  seeded RNG, behind the extension). After that, every change is an event (tree felled / planted /
  grown, deposit mined out); a resource removed by the neighbour must leave your copy through the
  game's own removal path so a citizen mid-job cancels cleanly. Two towns taking the same tree at
  once: a resource claim through the server, or accept a rare double harvest (open).
- **Neighbour roads are real**: in your copy they are actual road tiles (speed bonus, pathing),
  owned by the neighbour (you cannot remove them). **Unknown**: how roads are stored (tile flags
  or entities) and whether a road can exist without your town manager owning it.
- **Neighbour buildings block paths**: their footprints are written into your map's occupancy /
  pathing so your citizens walk around them and zones (`FillZones`,
  `Action/action_pf_roots_road.cpp:56-93`) stay correct. They are still NOT your entities: your
  town manager must not count their houses, storage or jobs. This is the main unknown (it was
  already, for look-only ghosts, and now the ghosts must also be solid). Fields, orchards and
  pastures the same way.
- **Tint**: the neighbour's buildings and roads draw with a colour tint (one colour per player),
  a per-instance colour multiplier in our own DX11 renderer (`src/render/dx11/`, our code, so no
  retail shader content). Models come from the user's own install at run time.
- **Start towns**: two start spots, far enough apart, found by the start spiral
  (`StartTown_FindSpawnTile`, `starttown.cpp:368`) from two different points; the second town's
  founding land is claimed like any other.
- **Not shared** (each copy is separate): citizens, livestock, fire, disease, weather events.
- **Trade**: you pack goods into an export at your Trading Post (removed from storage at once,
  so nothing is duplicated). The neighbour's post gets a merchant boat carrying exactly those
  goods (write its `manifest` / `tradeId` directly, `Component/tradecomponent_dock.cpp:245`) and
  barters with it in the normal Trade window (`Dialog/tradeui*.cpp`). Payment sails back;
  unsold goods come home.
- **Migrants**: "send a family" from the Town Hall: they walk to the map edge and leave. They
  arrive next door as an ordinary nomad group spawned at the map edge, accepted or
  refused in the usual window (`ImmigrationComponent`, Accept :459, `NomadComponent::ConvertToCitizen`
  :406). Adding citizens directly: `Game_SpawnCitizens_Impl` (`Dialog/debugui.cpp:409`).
- **Messages**: a chat line and neighbour news in the event log.
- **Mirror updates**: claims, placements, build progress, demolitions and resource changes are
  sent as events when they happen, plus a full snapshot {template name, position, rotation,
  built %} every ~10 s to repair anything missed. The instant-build path for real buildings is
  the `placeStructure` lambda (`starttown.cpp:1425-1790`).
- **Time**: each town keeps its own clock and speed; optional "speed lock" (both at the slower
  player's speed, pause pauses both).
- **Saves**: still single-player saves, plus a class C tail record (room, cached claims, the neighbour mirror, undelivered
  parcels, last neighbour snapshot). `ExtSave_WriteTail` / `ExtSave_ReadTail`
  (`Game/gameupdateloop_serialize.cpp:777, 794`) write and read the tail, but **no extension fills
  a payload yet** (`src/ext/savetail.*` leaves it empty): that plumbing comes first.
- **Hooks**: per frame `Neighbour::BeginFrame()` in `GameUpdateLoop::Update` beside
  PerfX/WalkBatch (`gameupdateloop.cpp:576`); per-game setup next to `Ext::LockForGame` /
  `SelectForGame` (`gameupdateloop_load.cpp:735, 739`). The network runs on its own thread; the
  game only drains a queue, so a slow network never stalls the sim.

### Milestones
- **M0 Plumbing** (DONE 2026-10-19, §7): game built and running on the PC; relay server up; the
  game connects, joins a room, shows a chat line in the event log; class C payload plumbing.
- **M1 Letters** (DONE 2026-10-19, §8; works on two separate maps): goods parcels, families,
  chat from the game (the Neighbour panel).
- **M2 Shared map** (MOSTLY DONE 2026-10-20, §9): the Multiplayer lobby, seed and settings from the
  room, hash check, two start towns, land claims, the neighbour's buildings mirrored. Open: mirrors
  blocking paths, a real colour tint, fence / bridge claims.
- **M3 Shared roads and resources**: real neighbour roads; the same resource scatter on both
  screens; resource change events.
- **M4 Extras**: speed lock, visible boats/walkers carrying parcels and migrants.

---------------------------------------------------------------------------------------------

## 3. The relay server

Chosen 2026-10-19: **Cloudflare Workers + Durable Objects** (free plan; check its current
limits before relying on them). The earlier plan (plain TCP to a Python asyncio relay behind
Tailscale) is the fallback.
- **Transport**: WebSocket over TLS (`wss://`), because Workers accept HTTP/WebSocket only, not
  raw TCP. One network thread in `src/ext/neighbour/`, behind a small `src/platform` seam: on
  Windows the OS's own WinHTTP WebSocket client (`WinHttpWebSocketCompleteUpgrade`, Windows 8+),
  so still no third-party library; the Linux build gets a stub (or a later client).
  Keep the message format separate from the transport, so the Tailscale/TCP fallback is a swap.
- **Server**: a small Worker (plain JavaScript, `tools/neighbour_server/src/index.js`) plus one Durable Object per room. A room = two seats
  + a shared password; forwards messages; **keeps parcels for an offline player** in the
  object's storage; remembers each town's last snapshot. Lives in `tools/neighbour_server/`.
- **Safety**: TLS from Cloudflare + the room password. The game validates every incoming
  message (sizes, counts, names, positions): bad data must never crash a town.
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

State on the PC (2026-10-19): Banished from Steam at
`C:/Program Files (x86)/Steam/steamapps/common/Banished` (`BANISHED_DIR`); Python 3.13 (python.org,
`python` only, no `python3`), pip cmake 4.4.4 + ninja 1.13.2, MSYS2 UCRT64 GCC 16.2.0 first on the
user PATH. Debug build: 0 errors, 156 warnings; `tools/build.sh unit` 1174 tests, 0 failures,
5 skipped (ToolKit, fonts); the gate passes after `4c9b37a99` (boot load ~25 s before frame 1
there, which the old 20 s watchdog budget killed). The upstream v1.0.0 exe also starts a new game.

**Mac (secondary).** `brew install mingw-w64 cmake ninja`, then
`tools/build.sh build [flavour] --cross` -> `build/cross-<flavour>/OpenBA.exe`
(`tools/toolchains/mingw-w64-x86_64.cmake`; Homebrew mingw-w64 14.0.0 / GCC 16.2, UCRT). Debug
and release both build (2026-10-19); ~235 extra warnings from GCC 16 (mostly unused variables, 2
`-Warray-bounds` on zero-length arrays in `Work/work_act_consume.cpp:264,298`). A Mac-built exe
had not been run yet at the time of writing. Native macOS is not an option (no D3D11; weeks).

---------------------------------------------------------------------------------------------

## 5. Repo rules that bump into the fork (open, the user decides)

1. `tools/build.sh run` (the gate) needs Windows: fine on the PC.
2. Extension settings are `ext.<id>=0|1` only; server address / room / password need string keys.
   DECIDED (2026-10-19, the user said not to wait on them): `neighbour.server`, `neighbour.room`,
   `neighbour.password`, `neighbour.name` in `settings.cfg`, read-only, when a game starts
   (`src/ext/neighbour/neighbour.cpp` ReadConfig). A Join dialog can come later. Env flags stay
   banned (`Opt()`).
3. Class B/C are off by default and locked per game: fine, the user switches them on.
4. CLAUDE.md's goal is a faithful port: kept; CLAUDE.md now points here.
5. No game content in the tree: no conflict (the mirrored, tinted neighbour buildings use the user's own install; the server
   sees only names and numbers).
6. GPLv3: sharing the exe with the friend = distribution; the public fork covers source access.

---------------------------------------------------------------------------------------------

## 6. Next steps

1. DONE: the PC set up (§4); M0 (§7).
2. The user deploys the relay to their Cloudflare account (`tools/neighbour_server/README.md`:
   `npx wrangler login`, `npm run deploy`); both players point `neighbour.server` at it.
3. DONE (M1): the Neighbour panel, goods, families.
4. M2 leftovers (§9 "Not done"): mirrors that block paths (needs a save-safe way to mark tiles),
   a real colour tint (needs a tinted material copy), claims for fences and bridges, mirrors of
   structures that exist when a save is loaded, construction stages on mirrors. Then M3.
5. M1 follow-ups, when wanted: barter through a merchant boat at the trading post (the plan's
   original trade), an accept / refuse window for an arriving family, families walking in from
   the map edge rather than appearing among the townspeople.
6. Still ask the user before: pushing, opening a network port beyond 127.0.0.1, deploying.

---------------------------------------------------------------------------------------------

## 7. M0 as built (2026-10-19)

- **Save plumbing** (`src/ext/savetail.*`): `RegisterPayload(id, writer, reader)`. An active class C
  extension's writer fills its manifest record's payload; `ExtSave_ReadTail` hands each payload to
  its reader (`DeliverPayloads`). The writer may run on the background-save worker. Still zero
  bytes with no B / C extension on. Test: `ext_savetail_payload_hooks`.
- **The extension** (`src/ext/neighbour/`, registry row `neighbour`, class C, off by default,
  formatVersion 1):
  - `protocol.*`: the JSON messages (a bounded parser that fails closed), `Sanitize` (printable
    ASCII for the game's fonts), `RoomUrl`, the save payload (room + last chat number seen).
  - `link.*`: the connection. One worker thread connects, sends the hello, writes the queue,
    pings every 25 s and reconnects (2 s doubling to 30 s); a reader thread fills the incoming
    queue. The main thread only queues and polls.
  - `link_platform.cpp` + `src/platform/{common/platform_websocket.h, win/websocket_win.cpp,
    posix/websocket_posix.cpp}`: WinHTTP's WebSocket client (`winhttp` linked); POSIX is a stub
    that reports "not available".
  - `neighbour.*`: settings, the four seams (`ResetForGame(this)` / `SelectForGame()` in Load,
    `BeginFrame()` in Update, `EndGame(this)` in ~GameUpdateLoop, owner-checked like the class
    lock), and the event log rows.
- **Event log rows**: appended straight to `EventLog::entries` with our own text, colour (blue =
  chat, green = joins / leaves, amber = problems), icon `EventIcon0` and eventType 0x7E (no retail
  writer uses it; the pop-up files it under "other", on by default; retail's log window reads only
  colour / text / tile, so such a save still loads in retail). Not through `AddEvent`: that needs
  a string-table id.
- **The relay** (`tools/neighbour_server/`): see its README. Rooms of two seats, a password set by
  the first town, the last 200 lines kept, at most 20 replayed to a returning town, a flood guard,
  pings answered without waking the object. `selftest.mjs`: 10 checks. `fake_neighbour.mjs`: a
  pretend town.
- **Verified on the PC**: unit 1184 tests, 0 failures, 5 skipped (10 new); the gate
  (`/profile:faithful`) loads a new game with the extension off and no `[Neighbour]` line; with
  `ext.neighbour=1` against the local relay + the fake town, a new game joins the room, gets the
  lines, and a clean quit tells the fake town "has left" (exit in ~4 s, no hang log); an autosave
  holds the 19-byte payload and a load replays only lines newer than the save. Not checked by
  eye: the rows in the event log window (the logs show each one posted).
- **Not done**: sending chat from the game (no UI yet); the Cloudflare deploy (needs the user's
  account); a resave-gate golden under the extension (no `Gate.sav` on this PC).

---------------------------------------------------------------------------------------------

## 8. M1 as built (2026-10-19)

Simpler than §2 on purpose, so the two towns can exchange things now; the richer versions are
follow-ups (§6).
- **The Neighbour panel** (`src/ext/neighbour/neighbour_panel.cpp`, `src/ext/ui/Dialog/ExtensionsNeighbour.rsc`):
  N opens / closes it in a game with `ext.neighbour` on (not while a text box has the keyboard).
  A status line (room, the neighbour, parcels not yet sent / waiting), a chat box (Enter or Send;
  Esc keeps the text), goods (`<` `>` pick a material the town holds, `-` `+` the amount
  1..500, capped at the stock; Send goods) and the family that would move (Send this family).
  Modal, does not pause. The chat box is the New Game dialog's name box with an empty filter;
  `text_reader.cpp` learned `EditDescription` for it. Buttons are the shared Ok button's frame
  with our caption.
- **Goods** (`src/ext/neighbour/town.cpp`): sending takes the amount out of the town's storages at
  once (count minus reserved). Receiving puts it into storages that accept it and have room
  (largest fit per storage); what does not fit WAITS (saved) and is retried every ~10 s. Goods are
  named on the wire by their template path (`Template\RawMaterialPotato.rsc:resource`); an unknown
  path is dropped with a line in the log. No barter, no boat yet.
- **Families**: "send" picks the town's smallest household that has an adult, never the town's
  last family, and removes it in one frame, children first (`EntityList::QueueDestroy`; a child
  whose last parent is removed first is buried by `CitizenComponent::OnRemove`). An arriving
  family becomes new citizens the way a birth makes one (a copy of a townsperson's template,
  `CitizenComponent::Initialize`, `RegisterEntity`), standing where a townsperson stands, the two
  adults married; they find homes and work like accepted nomads. Ages travel in the game's own
  age unit. No accept / refuse window, no walk from the edge yet.
- **Delivery that loses nothing**: every parcel has an id. The sender keeps it in its OUTBOX (saved)
  and sends it again on every join until the relay answers `stored`; the relay keeps it until the
  receiver's `ack`, and a resend with the same id is not a second parcel; the receiver remembers
  the last 128 ids it applied, so a repeat is acknowledged and dropped. One gap, as in any single
  player game: a parcel received and then not saved (the game quit without a save) is gone.
- **Save payload v2** (`protocol.h` SavedState, registry formatVersion 2): + outbox, received ids,
  waiting parcels. v1 saves (M0) still load.
- **Relay** (`tools/neighbour_server/src/index.js`): `parcel` -> `stored`, `ack`, parcels kept for an
  absent town (at most 64, 8 KB each) and delivered on its join. `fake_neighbour.mjs`:
  `--send-goods path:count`, `--send-family "age/gender/child;..."`, `/goods` and `/family` typed,
  and it acknowledges what it receives.
- **Tested on the PC**: unit 1186 tests, 0 failures (`ext_neighbour_parcel_bodies`, the v2 payload,
  the panel resource); relay selftest 16 checks; the faithful gate loads a new game with no
  `[Neighbour]` line. In game against the local relay and the fake town: a family of 3 arrived in
  a new game; 30 Potato arrived; an unknown material was dropped safely; the panel opened (N), its
  goods list matched the storage, Send goods sent 25 Firewood, typed chat with punctuation arrived;
  on the user's Save0 (36 citizens) Send this family sent a family of 1 and the town ran on with no
  crash. Not checked: capital letters typed (the test script could not send Shift for letters; the
  game's key table has them); a save / load holding a v2 payload in game (covered by the unit
  test only). `Save3.sav` crashes the port while loading, with or without the extension:
  `docs/known_port_issues.md` row 1331.

---------------------------------------------------------------------------------------------

## 9. M2 as built (2026-10-20)

- **The Multiplayer lobby** (`src/ext/neighbour/lobby*.{h,cpp}`, `src/ext/ui/Dialog/ExtensionsLobby.rsc`):
  a Multiplayer button on the front-end menu (not the pause menu, not under `/profile:faithful`)
  opens a list of every room on the relay, with Room name / Password / Your town fields and Refresh,
  Create room, Join, Cancel. Asking the relay waits a few seconds at most, on the menu.
  - **Create room** remembers the room for the next new game and opens the game's own New Game
    dialog; pressing Ok there reaches `MainMenuDialog::StartNewGame`, whose first line (EXT SEAM,
    `Neighbour::BeforeNewGame`) creates the room with exactly the chosen seed and settings
    (generators by resource path). A refusal (a taken name, no network) goes back to the lobby with
    the reason; New Game's Cancel forgets the room (`ClearLobbyStart`).
  - **Join** asks the room for its seed and settings and calls `StartNewGame` with them.
  - Either way ext.neighbour is on for that one game (`Ext::SetRequested`, restored once the game
    has its lock): no settings.cfg editing. The relay defaults to the user's Worker
    (`lobby.cpp` DefaultServer); `neighbour.server` overrides it. A lobby game's save (payload v3)
    remembers its town name, password, relay and world hash and reconnects by itself.
- **The shared map**: the two towns' start spirals begin at 3/8 and 5/8 of the map
  (`StartTown_FindSpawnTile` EXT SEAM, `Neighbour::StartCentre`). The land's height hash
  (`[WorldHash] height f32`, worldgen EXT SEAM `Neighbour::NoteWorldHash`; it depends on seed, size
  and terrain only) is sent on join; the relay compares the two towns' and the event log says
  "the same land" or warns (different exe, mods or seed).
- **Land claims** (`src/ext/neighbour/shared_map.*`): every structure this town makes
  (`PlacementTool::CreateAt`, `DragPlacementTool` fields, the start town's `placeStructure`) claims
  its footprint, a road tile its tile (`Map::PlaceRoadCell`); the relay grants first come first
  served and tells the neighbour. This town keeps a byte per tile of what the neighbour holds:
  `Map::TestPlacement` (buildings, fields, the red / green overlay) and `Map::PlaceRoadCell` refuse
  those tiles. A structure's claim is released when it is gone (checked every ~5 s). A claim refused
  by the relay (two towns placing on the same tile in the same moment) is reported, not undone.
- **Mirrors**: every structure is reported (template path, position, rotation, model variant,
  footprint); the relay keeps the latest per id (`s<x>_<y>`, the footprint origin) and hands the
  whole set to a town that joins. The neighbour's are drawn as bare `Graphics::MeshInstance`s in the
  scene, the way the placement preview is drawn: not entities, so this town's manager never counts
  them and nothing can work on them. Trees and rocks (`NaturalResourceComponent`) under a mirror are
  removed in this copy (`QueueDestroy`).
- **Relay**: `create`, the Directory (`/lobby`), `hash` / `hashCheck`, `claim` / `claimed` /
  `claims` / `release` / `released` / `claimBlock`, `build` / `unbuild` / `mirrorEnd`. Local test
  connections (no outside address) are exempt from the 5-rooms-a-day limit. selftest: 33 checks.
- **Tested on the PC** against the local relay: the lobby listed rooms; Create made pclobby1 /
  pclobby2 with the New Game choices; Join as a second town built the same land (the same height
  hash on both, "the same land" in the log) on the other side of the map; the creator's start town
  (barn, stockyard, six houses) arrived as claims + 8 mirrors and was drawn, trees cleared, in the
  joiner's game (screenshot). Unit 1189 tests, 0 failures; the faithful gate unchanged (no
  Multiplayer button, no `[Neighbour]` line, the start town at the map centre).
- **Not done / not verified**:
  - Mirrors do NOT block paths yet: the save-safe way to mark tiles (the pathfinder's own blocked
    bytes, or terrain types restored before a save) is open.
  - No colour tint: `MeshInstance::_color` is the model scale, not a colour; the opacity written is
    not visibly honoured for buildings. A tint needs a tinted copy of the material.
  - Fences and bridges do their own checks: not yet claim-aware.
  - Structures already standing when a lobby save is loaded are not re-reported (the relay still
    holds their last state); mirrors always show the finished building, not its construction stage.
  - Placement refusal on the neighbour's land was not tried by hand (the code path is the game's
    own `TestPlacement`).
