# Two-player testing: two machines, two agents, one shared map

How the Mac and the PC test neighbor towns against each other, unattended, by looking at the real
game. Each machine runs its own town; a coding agent on each machine plays it, takes screenshots of
its own window, and tells the other agent what it did and what it saw. Findings go to
`docs/known_port_issues.md` once confirmed; this file is the method and the checklist.

## The three tools

- **The dev loop** (`docs/extensions/DEVLOOP.md`, `tools/devloop.sh watch`): every push to the shared
  branch rebuilds and restarts the game on both machines into their own autosave, so a fix reaches
  both towns without anyone touching them. A neighbor-towns save reconnects to its room by itself.
- **The drive mailbox** (`BANISHED_DRIVE=<dir>`, `src/diag/drive_harness.cpp`): the agent's hands.
  The game polls the real mouse and keyboard and reads keys only while its window is in front, so
  input sent to a window in the background never arrives. With the flag set, a script written to
  `<dir>/cmd` plays as the mouse and keyboard through the real UI, in menus and in game. The
  language is in `src/diag/drive_script.h`: `move`, `click`, `dclick`, `drag`, `wheel`, `key`,
  `hold` / `unhold`, `type`, `wait`, with coordinates in client-area points (a screenshot of the
  window minus its title bar) or as fractions (`clickf 0.5 0.5`). `<dir>/info` gives the client
  size, `<dir>/status` says idle / playing line N / error, `<dir>/out` is the log. Write the script
  to `cmd.tmp` and rename it, so a half-written file is never played.
- **Screenshots**: the drive script's `shot` makes the game write its own screenshot, UI included
  (`<save folder>/Screenshots/ScreenshotN.bmp`), in game and on the menus and lobby alike, with no
  OS permission. It is the one to use: macOS stops showing a restarted game's window to computer use,
  and a Windows agent may not be granted the window at all. On the Mac `BANISHED_BACKGROUND=<W>x<H>`
  gives an unfocused window of that size (in points), so the owner of the screen keeps working while
  the game runs behind. The drive folder's `camera` file says where the camera is (`camera X Y D YAW`
  in a script moves it there).

A typical Mac `devloop.cfg` for a test town (kept out of the tree, here `build/val/devloop.cfg`;
the user dir keeps the test town's saves, settings and relay seat away from the player's own):

```
GAME_DIR="/Volumes/DevSSD/dev/games/Banished"
SAVE_DIR="<checkout>/build/val/user/Save"
DEVLOOP_DIR="<checkout>/build/val/devloop"
EXTRA_ENV="BANISHED_USER_DIR=<checkout>/build/val/user BANISHED_BACKGROUND=1280x800 BANISHED_DRIVE=<checkout>/build/val/drive"
```

### One machine alone

- A second town on the same Mac: `OpenBA.app/Contents/MacOS/OpenBA --run-dir <dir> --game-dir <Banished>`
  with its own `BANISHED_USER_DIR` and `BANISHED_DRIVE` keeps its saves, settings and logs
  (`<dir>/support/Run/`) apart from the first.
- A local relay: `npx wrangler dev --port 8799 --ip 127.0.0.1` in `tools/neighbour_server`, then
  `neighbour.server=ws://127.0.0.1:8799` in that town's settings.cfg.
- `node fake_neighbour.mjs --server ws://127.0.0.1:8799 --room R --pass P --echo-walkers 3` joins as a
  pretend neighbour that sends the town's own villagers back 3 m to the east: each villager on screen
  gets its twin beside it, which checks the drawing of the neighbour's villagers with one game.

### The test relay

Testing a relay change against the friend's live relay would change it for every room. The test relay
is the same Worker under another name (`npx wrangler deploy --name openba-neighbour-test`, then
`node selftest.mjs wss://openba-neighbour-test.spellpress.workers.dev`); both towns pick it with the
lobby's Change relay. The live relay is deployed only once the user says so.

## Rules of the game

- Same build on both machines (the dev loop makes it so), same game data, the same mods in the same
  order (vanilla is the baseline).
- Every finding names: what this town did, what it expected, what it saw (a screenshot), the log
  lines (`[Neighbour]` in `standalone_error.log`), and what the other town saw at the same moment.
- Pause when a step is done (the pause is shared): a town left running unattended with no houses
  starves and freezes, and a dead town sends no villagers (the first "no villagers" report was that).
- A Colonial Charter start has no builders: set them first (Tools and Reports, F2, the profession list),
  or nothing placed rises past its frame.
- Adversarial: try to break it. Do the same thing in both towns at once, do things while the other
  town is offline or loading, quit at awkward moments, load old saves.

## The checklist

Each row is done when both towns have seen the right thing on screen.

| # | Area | What to do | What both towns must see |
|---|---|---|---|
| 1 | Lobby | Mac makes a room (One land); PC finds it in the list (search too) and joins | Same land ("the same land" in the log), towns on opposite sides, each sees the other's start town |
| 2 | Start towns | Look at the neighbor's start town | Its buildings see-through, their ground (stockpile yards, foundations) painted, no trees inside them |
| 3 | Villagers | Watch the neighbor's town for a minute | Their villagers walk, work, are see-through, at the right places |
| 4 | Buildings | Each places a house and a storage barn | Appears in the other town as a foundation, then each construction stage, then finished |
| 5 | Claims | Both try to build on the same tiles at once; then on the other's land | One wins, the other is refused (red placement, or "taken" in the log); no overlap anywhere |
| 6 | Solid | Route your villagers past the neighbor's buildings | They walk around, never through |
| 7 | Roads | Each lays dirt and stone roads, including into the other's land | Real roads in both, speed bonus; cannot remove the neighbor's road |
| 8 | Trees / stone | Fell trees, quarry, gather; both target the same tree | Gone in both towns; a double harvest at most, no crash |
| 9 | Foresters | Plant saplings | Appear in both |
| 10 | Demolish | Demolish a finished and a half-built building | Gone in both, land free again, no crash |
| 11 | Fields / pastures / orchards | Lay out each | The neighbor sees the outline and soil |
| 12 | Pause / speed | Each pauses, unpauses, changes speed | The other follows; no echo loop |
| 13 | Chat | N panel, send lines both ways, punctuation, capitals | Arrive in the event log, in order |
| 14 | Goods | Send goods both ways, more than fits | Leave storage at once, arrive, overflow waits |
| 15 | Families | Send a family both ways | Arrive alive, find homes |
| 16 | Quit / rejoin | One town quits and comes back (dev loop restart, plain quit, crash) | Changes made while away arrive; the left town's mirrors stay |
| 17 | Old save | Load a save older than what the relay holds | The neighbor's view returns to that save's state (reload) |
| 18 | Fresh start | Start over in the same seat | The neighbor's old buildings, roads and land vanish |
| 19 | Long game | Play an hour at top speed, both towns | No drift: counts of buildings / roads match; no crash or hang |
| 20 | Continents | Repeat 1-8 on a Continents room | Sea between, both on their own land, traders reach both |

## Results, 2026-11-22 (Mac against Windows, rooms val-1 to val-3)

| # | Result | Notes |
|---|---|---|
| 1 | pass | Lobby on both machines through the real UI (front-end shots); the same scatter fingerprint and "the same land" both ways. |
| 2 | pass | No trees inside the neighbour's start buildings; their ground painted. They draw solid, not see-through: the model material ignores the mirror's opacity (a shader change, not done). |
| 3 | pass | Both ways once both towns had people (the first failure was a town that had starved: "sending 0 of 0"). Villagers' positions now never wait behind the shared map's queue. |
| 4 | pass | Each town's Log Cabin and storage barn rose to finished on the other's screen. |
| 5 | pass | Placing on the neighbour's land is red and refused. Both towns clicking the same free spot in the same second (20:33:00, 6 ms apart): the relay keeps the first, refuses the second's claim and its mirror, and the second game takes its fresh site back with an event line; one cabin on both maps. |
| 13 | pass | Lines arrive in order, quotes, brackets, braces, %, &, backslash intact. Found on the way: the event log drew every other entry (a retail-divergent row index in EventLogUI), the chat line went out on any click away (Cancel included), the box widened and moved Send, and only the keypad's Enter sent. |
| 12 | pass | Pause and unpause both ways; a town joining a paused game comes up paused. |
| 7 | pass (Mac to PC) | A Colonial Charter dirt road reaches the neighbour (it never did: only road types 0 and 1 were sent, and CC's roads use more). Removing the neighbour's road is refused. Not yet: PC to Mac, stone roads, the speed bonus. |
| 8 | pass (one machine) | A town's villagers felled a marked forest while a pretend neighbour (fake_neighbour.mjs --fell-around) removed the trees around each felled one 0.5-3 s later, the trees they were walking to or chopping: 200 s, 97 removals sent, 52 of the neighbour's applied, no crash, no hang, no villager stuck. (The two test towns' forests were not reachable from both.) |
| 10 | pass | A half-built cabin demolished while the neighbour was away is gone from its map on return. A demolition done while paused is sent when the game runs on (the entity goes then). |
| 16 | pass | The PC quit; the Mac built a road and a cabin, felled trees and demolished a site; the PC loaded its older save and saw all of it (after the fixes below); the PC's structures stayed drawn on the Mac while it was away. |
| 17 | pass | The PC placed a cabin, was killed (taskkill /F) and loaded a save older than it: the cabin vanished from the Mac; the Mac's changes since that save all showed on the PC. |

Found and fixed during the run: a dead town sends nothing (pause between steps); villagers starved behind
a long queue; a reconnect kept structures taken down while away and the neighbour's released land; a tree
felled or a road taken up after a town's last save came back when that save was loaded (the relay now
keeps both records and replays them to a town that loads a save or starts again); a reload reported the
removal tool's marks and the goods lying on the ground as buildings (61 for 3); the hang watchdog killed a
dev loop game sitting in a Load's question.

### Open after this run

- The neighbour's buildings and villagers draw solid, not see-through: the model material ignores the
  mirror's opacity (a shader change).
- Goods a receiving game does not know (another mod list) are dropped with an event line, not returned.
- Saves written on the Windows PC list a mod with an empty display name that DoLoad reports as
  `modsOutOfDate` (CheckMod 2) when the same PC loads them. Traced (2026-11-25): it is
  RKEditorChoiceEdition.pkm, and the package file is incomplete (docs/retail_bugs.md section 10).
- The removal tool's preview over the neighbour's road is green (the removal itself skips those tiles).
- Row 20 (Continents) not run; row 6 checked by solid-tile counts only.
- Shared spreading (6263d4f8f): seat 0 spreads plants on the west half of the map, seat 1 on the east,
  each sending its new plants to the other. So a shared-map game's forests differ from the same seed
  played alone, since its random numbers are drawn differently after the first skipped spread. A
  town's half spreads only while that town plays. The shared map sends at most 40 changes a second and
  holds 20,000 waiting, so a huge burst of spreading can drop plants; the map fingerprint line shows it.

## Results, 2026-11-24 (Mac against Windows, rooms val-4 and val-5, test relay)

Row 19 (no drift) was the subject of this run. The map fingerprint line is the check: two paused games
that print the same nature hash hold the same natural resources, tile and template, on both halves.

- **val-4 had drifted.** Rejoined after the Cloudflare quota reset, both towns paused: each owner's half
  held 1.2-1.6k more plants than the neighbour's copy of it (west Mac 19916 / PC 18357, east PC 15062 /
  Mac 13838). Cause: the night before, the free tier's daily row writes ran out during a 50x run, and the
  relay of that time wrote each change to storage before passing it on, so a failed write lost the change
  for good. Nothing could mend it: the replay on rejoin only resends what the relay recorded.
- **val-5, a fresh room, 25 minutes at 1x-5x, did not drift.** Both towns started with the same
  fingerprint, played (the Mac's villagers felled a forest across the middle line, the PC's foresters
  reached onto the Mac's half), paused, and printed the same `34164 (abd729ef1eb69ff7)`. 0 dropped.
- **The nature sync** (`nature_sync.{h,cpp}`, NEIGHBOUR_TOWNS.md): each town checks its copy of the
  neighbour's half against the owner's every 45 s and mends the blocks that differ. On val-4's rejoin the
  first round mended 1858 plants on the PC's copy of the west half and 1322 on the Mac's copy of the east,
  the second round said "matches" on both, and both printed `34879 (19830fdb187beadb)`. Then 5 minutes at
  5x: every round matched.
- **Found and fixed: the replay on rejoin brought felled plants back.** The relay kept one record a tile a
  seat and replayed seat 0's records, then seat 1's; a plant one town spread and the other felled came
  back from the older record (the PC's half grew by 489 on a reload with nothing simulated), and the sync
  then copied the owner's swollen half to the neighbour, as it should. The relay now stamps each record and
  replays only the newest change per tile and template (a removal wins a tie between unstamped records);
  the game ignores a replayed plant that already stands on its tile (a herb does not hold its tile, so it
  grew twice). After both towns reloaded: both printed `34176 (9bf374eca15cb582)`, the sync matched.
- **Found and fixed: plants died faster on a shared map.** In val-4 (no villagers on either side) both
  copies kept ending plants on both halves and sending the removals: a wild plant's life ends when its
  growth passes a threshold each copy rolls at random, or when it freezes under that game's own weather,
  so every plant died at the earlier of two times. Now a plant's end is decided by one game, as its
  spreading: the half's owner, or the town whose structure (field, orchard) holds the tile
  (`SharedMap::DecidesNature`, an EXT SEAM in `GrowthComponent::Update`). 5 minutes at 5x afterwards:
  each town sent 0 removals for the neighbour's half (the nature line now counts them), the winter
  die-off hit each half from its owner's game, and both towns ended on `33594 (17f1c5465f382d34)`.
- Reviewed by a Sonnet subagent before it reached the two machines: nothing blocking; its notes (rocks
  that cannot be put back asked for every round, a request stuck behind a long queue given up on, a plant
  a gatherer is picking put back, request floods) are in the second sync commit.
- Pause and speed: the speed steps do not unpause a paused game; a save broadcasts its stored speed to
  the neighbour on arrival (val-4's 50x from the night before came back while both were paused).

### Open after this run

- Loading MacTown's own val-4 save from the lobby asked "The following mod is out of date ...
  [RKEditorChoiceEdition.pkm] Saved with version 0 (0.0.0)" (`modsOutOfDate`, CheckMod 2): the save
  records that mod's header version (`Mod::i18`, the package's `_version`) as 0. The PC's nameless mod
  is the same package: its DoLoad lines list the save's mods as "CC: 1.76 Journey" (0), "American City
  Living V3.0" (0), "" (CheckMod 2) and the neighbour pseudo-mod, and RKEditorChoiceEdition.pkm is the
  third enabled mod. So the package's compressed header gives an empty name and version 0 on both
  platforms. Traced (2026-11-25, docs/retail_bugs.md section 10): not a misread. The Mac's copy of the
  package is incomplete: its last 209,349,628 bytes are zeros, from 961,544,192 (917 MiB) to the end,
  and the directory blob its header points at lies in them. `ReadPackfile`, ours and retail's alike,
  then mounts it with no name, version 0 and no files, and every save stamps version 0. Retail asks the
  same. Fix: replace the package on both machines (the PC's copy is probably the same incomplete
  file; `tools/build.sh unit` prints each installed package's header in `package_header_installed_pkm`).
  Old saves then ask `modsChanged` on each load until they are saved again. A load on the PC also asks
  `extensionTurnedOff` for ext.neighbour (off in its settings, turned on by the save).
- The drive mailbox's front-end `shot` does not show a modal question opened inside a dialog pump (the
  question above stayed invisible in shots; the real window showed it), and the boot watchdog reports
  HUNG while such a question waits on the front end (attended runs are not killed).
- The old build hung once on quitting a paused game for the dev loop (killed after 60 s, its save was
  written); the next build quit a paused game cleanly twice. Not reproduced.
- Unstamped records from before this run decide a tie for the removal, so a tile where one town removed
  a plant and the other planted the same kind again later loses that plant once on a reload (about 10 of
  15,000 in val-5).

## Results, 2026-11-25 (Mac against Windows, room val-6, test relay)

val-6: seed 9465646, Medium, CC Lake Waters, Mediterranean (No Snow), disasters off, CC Medium+ start; Mac
seat 0 at 160,198, PC seat 1 at 240,192. Both games on the dev loop, so every pushed fix relaunched both
mid-game (four times), which doubled as a quit-and-rejoin check each time.

| # | Result | Notes |
|---|---|---|
| 7 | pass | Dirt roads both ways (Mac 168..200 east, PC 233..188 west, overlapping 188..200) and a planned stone road. Each town's finished tiles arrive as real roads (the fingerprints' road hashes matched at the same moment, roads 109 `95ec1381010a7d58`). The neighbour's road is refused by the removal tool, silently. Found the planned-road bug below. Not checked: the speed bonus. |
| 9, 11 | pass | Crop fields, orchards, a pasture, a wool pen and forester lodges on both sides: the neighbour sees the field's soil, the orchard's ground, the lodge's foundation and the pasture's fence going up. An unbuilt pasture or pen draws nothing on its owner's map (both platforms); see below. |
| 14 | pass | Goods both ways while paused: they leave storage at once and arrive at once. An adversarial double send (two clicks 0.1 s apart) arrived as exactly two parcels (+50). Three parcels (500 + 250 + 50) and a chat line sent while the Mac was out of the room arrived once each on its rejoin (+800 exactly). |
| 15 | pass | A family of 3 Mac to PC (homeless until houses), a family of 4 PC to Mac sent during a 10x run: each arrived once, alive. |
| 16 | pass | Quit to the menu and rejoin from the lobby, and four dev-loop relaunches: mirrors stayed drawn on the other map while a town was away; claims made while the other was away (two cabins) showed on its return; no refusals. |
| 18 | pass | The Mac's town froze to death (see below), so it started over in its seat: on the PC its old cabins, field, pasture, lodge, 24 road tiles and land all vanished (roads 113 to 89); the PC then laid a road on that land with no refusal; the next sync round mended 37 blocks once and then matched. |
| 8 | pass | Both towns marked the same 14 x 10 forest on the middle line; both felled into it at 5x and 10x: no crash, 0 "found nothing here", 0 "off the tile's slots", and the sync matched afterwards. |
| 19 | pass | One hour at 10x, 17:34:43 to 18:35 UTC, after both towns had started over: no crash or hang on either machine, 0 dropped (queue full). Neither town survived it (both froze or starved in their first winter, unattended), so most of the hour was nature: the Mac sent 11,789 plants and 12,378 removals, the PC 12,702 and 12,360, 0 of them on the neighbour's half. Every sync round for the other's half matched, except the first round after a reconnect: each machine lost the relay once (PC about 18:12, Mac 18:34), reconnected by itself, and that round mended 17 and 1 blocks; the next matched. At rest after the hour both sides' last rounds said "matches" (218 and 223 blocks). Roads stayed equal (0; earlier 109 at the same moment). The relay wrote about 390 storage rows a minute (fixed below). |

Found and fixed during the run:
- **A planned road did not show** until some other road tile finished somewhere: `RoadTool::CalcPath` placed
  the tiles (type and the queued marker 0x80, which the road mesh draws see-through) but never rebuilt the
  mesh. A road laid while paused, or with nothing under construction, was invisible (the user's "when you
  lay a road you can't tell"). It now rebuilds after its placement loop; both machines then showed a road
  laid while paused at once, and kept it across a reload.
- **The cost/size label of a dragged tool sat at half the cursor's position on the Mac**: retail scales the
  label's rectangle by the UI scale twice (`docs/retail_bugs.md` section 11), which shows at any scale but
  100%, and a Retina Mac defaults to 50%. `fix.createbar_scale`.
- **Diagnostics called the cold meter thirst**: the Mac's town froze to death (half its firewood sent to the
  PC, no woodcutter) and `[Burial]` said "thirst". Now `cold` / `freezing`.
- **The relay wrote about 350 storage rows a minute at 10x** (counted by a new `[rows]` line in
  `wrangler tail`): every nature shard touched in 5 s was a row. Cloudflare's free plan allows 100,000 a day
  for the whole account, the live relay included, so a few hours of 10x play would have stopped every room.
  Nature records now reach storage once a minute; roads, mirrors and the rest keep 5 s. A local relay with two
  pretend towns sending 80 changes a second (ten times the hour's pace) then wrote 73 to 75 rows a minute.
  A deploy does not reach a room already running: its object keeps the old code until it restarts.

### Open after this run

- An unbuilt pasture or animal pen draws nothing on its owner's own map (no outline, no fence ghosts), on
  both platforms; the neighbour's mirror draws its ground. Unbuilt houses show only the dark foundation.
  Whether retail shows more (a see-through model, fence ghosts) is not known: needs a look at retail.
- Removing a planned road with "Remove Structures" (Colonial Charter) left its see-through strip drawn. The
  removal tool's own path rebuilds the mesh when it takes a planned tile up, so the removal itself probably
  did not apply to roads; not followed up.
- A town on a shared map gets the room's last 20 chat lines again when it starts over (it has no saved chat
  position). By design, but it reads like new messages.
- The drive mailbox ends a press with its script (`Runner::Start` clears the held buttons), so a road drag
  must be click-start, click-end, or press, moves and release in one script; a separate `shot` script sees
  the button already up.
- Speed steps are 1, 2, 5 and 10x, and play resumes at the last speed: "play" then one "faster" from a 5x
  pause gives 10x, not 2x.
- (Fixed 2026-11-26, NEIGHBOUR_TOWNS.md §14: the game drops a line quiet for 60 s, and the restate puts right what went down it.) A town whose connection dies without a close stays "here" on the other side until it reconnects (the PC's
  18:12 drop showed on the Mac only as "PcTown has arrived" later). Changes sent to it meanwhile are lost to it,
  and the sync mended them after the reconnect. The relay could notice a stale socket from its pings.
- `wrangler tail` samples a busy room's events, so its `[rows]` and alarm lines are not a count; measure a rate
  on a local relay (`npm run dev`), whose log keeps every line.
- Neither town survives an unattended hour at 10x from a CC Medium+ start (both froze or starved in their
  first winters); the hour run's second half was nature only on the Mac's side.

## Results, 2026-11-27 (Mac against Windows, room val-7, test relay): the restate

The subject was PR 15 (NEIGHBOUR_TOWNS.md §14): a change sent down a line that died without closing is put
right on the next join and by the town's minute list. Both machines ran 3ab9b1360 (the tree merged as
116e8295e), and the test relay ran the same relay code. The Mac's game reached the relay through a proxy on
the Mac that can silence a line both ways without closing it. While a line is silent, the proxy keeps the
relay's side open, so the relay goes on treating the town as present and sending to it. It also silences
every new line the game opens until the silence ends.

- **Step (a), both lines up.** Each town laid a dirt road and a house site and ran at 5x until the road was
  finished, then paused. At rest the fingerprints were the same on both machines: nature 25317
  `3c4259923cd2d016`, roads 14 `6e98bbd9a2780919`. Each town drew the other's structures, and the Mac's lists
  matched the relay's record every minute (no correction).
- **Step (b), the Mac's line silent from 22:55:02 to 23:02:16 UTC.**
  - The Mac removed its second house site (190,205). Its `unbuild` and `release` went down the silent line
    and were dropped.
  - The PC removed its Log Cabin site (242,189). The relay passed `unbuild` and `released` to the Mac's
    silent line, and they were dropped.
  - The Mac's game said "the relay went quiet" 60 s after it last heard anything, then retried every 62 s;
    each new line was silenced until the end.
- **After the Mac rejoined (23:02:16).**
  - The relay replaced the old line (4001), which it had treated as present all along.
  - The Mac dropped the PC's cabin site at `mirrorEnd` ("1 of the neighbor's structure(s) taken down while
    away removed").
  - The Mac's first list, 15 s later, had the relay take down the Mac's site and give back its 8 tiles
    (`[restate] room val-7, seat 0: 1 building(s) taken down, 0 road tile(s) taken up, 0 tile(s) claimed,
    8 given back; asked again for 0`).
  - On the PC the Mac's empty foundation frame was gone: still drawn in its 14:58:29 (PC time) shot, gone
    by 15:05:06, after the relay's correction at 15:02:31. No line said so (see below).
  - After the next nature sync both machines printed the same fingerprint: nature 25317
    `e84c2cfa80ec6d84`, roads 14 `6e98bbd9a2780919`.
  - The lists after that matched: no further correction.
  - The Mac's screenshot of PcTown shows the barn, the field's ground and the finished road, and no cabin
    site.
- **Not covered: taking up a road.** Remove Roads did not take up a finished Colonial Charter dirt road on
  either machine: the area showed red ("Remove Roads 4x11"), and the road was still there after 25 s at 1x.
  So the `roadsEnd` sweep was not exercised between two machines. It was on the Mac, against a scripted
  neighbour (§14).

### Found during this run

- **On the PC, clicks on the map do nothing while the game is paused.** The toolbar and menus work; the road
  tool's green hover tile follows the mouse, but a click starts nothing, and a right click does not cancel.
  Unpaused at 1x, the same clicks work at once. On the Mac, placing a road and a house and Remove Structures
  all worked while paused. This first looked like dead input after quitting val-6 to the menu and starting
  val-7 in the same process, but the restart only happened to unpause.
- **Remove Roads does not take up a finished road** (Colonial Charter dirt road, both machines). This is
  probably the same as the Remove Structures item from 2026-11-25, and not followed up.
- **The Mac's debug build was stopped by the 20 s hang watchdog** during the menu-to-game switch while the
  machine was heavily loaded (load average 9 to 14, the game at 1.3 fps). The main thread was in
  `Ext::DrawGather` `ExtClean`, under `Scene::Clean` and `MenuDialogLoopT::TickParticles`. The release build
  then started at once. Probably one very slow frame rather than a deadlock; not followed up.
- **A structure the neighbour took down was not logged** (seen on the PC): an `unbuild` changed no
  counter, so the "from the neighbor so far" line still said "now 4 of their structure(s) drawn".
  Fixed here: that line now counts the neighbour's structures taken down ("N structure(s) taken down")
  and is printed again when one is. Built on the Mac; not run in a game.
- **A player sees only the first list of each join in the log** ("this town's list to the relay"); later lists
  are not logged, by design. The relay logs a `[restate]` line only when it corrects something, so its log,
  or the game's "record lacked" line, is where a correction shows.

## Results, 2026-11-27 evening (two Windows PCs, test relay): the first game between two players

Two players, each on their own Windows PC, played the v1.1.2 build from main 905171f25 (the cross-built
zip, renamed beside an older OpenBA.exe) on the test relay. This is from the players' account, not from
their logs. Nothing blocked play. What looked different in the neighbour's town than in its owner's:

- **Stockpiles showed no goods**: the neighbour's yard was drawn, but not the piles on it.
- **Animals were not drawn**: livestock of the kind kept in pastures.
- **Walkways and bridges from a docks mod were not drawn**, while that mod's buildings were.
- **A placed building nothing had been delivered to drew its first foundation stage.** Its owner sees only
  a shadow until every material is in.
- **One player could not remove their own road.** The other did not confirm it. This matches "Remove Roads
  does not take up a finished road" from val-7.

The first four are fixed in NEIGHBOUR_TOWNS.md §15 and were checked between two Mac games. The road removal
is not fixed: the removal tool's road branch (`Map::RemovePendingRoad`, `keystone_a_removalcalcpath.cpp`)
needs comparing with retail.
