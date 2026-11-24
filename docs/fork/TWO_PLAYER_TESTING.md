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
  `modsOutOfDate` (CheckMod 2) when the same PC loads them; not yet traced (needs one such save).
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
  records that mod's header version (`Mod::i18`, the package's `_version`) as 0. Not traced: whether the
  package header really says 0 (then retail asks the same) or the port reads it wrong. Probably the same
  family as the PC's nameless mod entry above.
- The drive mailbox's front-end `shot` does not show a modal question opened inside a dialog pump (the
  question above stayed invisible in shots; the real window showed it), and the boot watchdog reports
  HUNG while such a question waits on the front end (attended runs are not killed).
- The old build hung once on quitting a paused game for the dev loop (killed after 60 s, its save was
  written); the next build quit a paused game cleanly twice. Not reproduced.
- Unstamped records from before this run decide a tie for the removal, so a tile where one town removed
  a plant and the other planted the same kind again later loses that plant once on a reload (about 10 of
  15,000 in val-5).
