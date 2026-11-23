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
- **Screenshots of the window**: computer use, in the background (`app_screenshot` on the Mac).
  On the Mac `BANISHED_BACKGROUND=<W>x<H>` gives an unfocused window of that size (in points), so the
  owner of the screen keeps working while the game runs behind.

A typical Mac `devloop.cfg` for a test town (kept out of the tree, here `build/val/devloop.cfg`;
the user dir keeps the test town's saves, settings and relay seat away from the player's own):

```
GAME_DIR="/Volumes/DevSSD/dev/games/Banished"
SAVE_DIR="<checkout>/build/val/user/Save"
DEVLOOP_DIR="<checkout>/build/val/devloop"
EXTRA_ENV="BANISHED_USER_DIR=<checkout>/build/val/user BANISHED_BACKGROUND=1280x800 BANISHED_DRIVE=<checkout>/build/val/drive"
```

## Rules of the game

- Same build on both machines (the dev loop makes it so), same game data, the same mods in the same
  order (vanilla is the baseline).
- Every finding names: what this town did, what it expected, what it saw (a screenshot), the log
  lines (`[Neighbour]` in `standalone_error.log`), and what the other town saw at the same moment.
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
