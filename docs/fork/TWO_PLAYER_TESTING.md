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
| 12 | pass | Pause and unpause both ways; a town joining a paused game comes up paused. |
| 16 | in progress | See below. |

Found and fixed during the run: a dead town sends nothing (pause between steps); villagers starved behind
a long queue; a reconnect kept structures taken down while away and the neighbour's released land; a tree
felled or a road taken up after a town's last save came back when that save was loaded (the relay now
keeps both records and replays them to a town that loads a save or starts again).
