# Playing your Mac against your PC

Two towns, one map: one on the Mac, one on the PC, talking through a small relay (the "postbox").
No Steam account or login is involved.

What is already proven (on the Mac, 2026-10-23): the Mac game boots and plays; it trades chat, goods
and families with a pretend town; and two Mac games on one shared map build the same land, see each
other's buildings and share felled trees. A shared map is built with the same maths code on Windows
and Mac, and GitHub's Windows, Linux and Mac test runs all compute it to the same bits, so the PC
should build the same land too. The first real Mac-against-PC game is what is left to test.

Your machines (home network): Mac `192.168.0.168`, PC `DESKTOP-3I0OFRD` = `192.168.0.177`.

## 1. Same game data on both machines (done)

The Mac has a copy of the PC's Banished folder at `/Volumes/DevSSD/dev/games/Banished` (copied from
`\\DESKTOP-3I0OFRD\Media\Banished`). The first time `OpenBA.app` starts, it asks for the game folder:
pick `/Volumes/DevSSD/dev/games/Banished`.

**Mods:** the Mac has your four mods but none switched on. For the first test, switch mods off on the
PC too (or switch the same ones on, in the same order, on both). Different mods only warn, but a mod
that changes the terrain would make the two maps differ.

## 2. Same version of OpenBA on both

- **Mac**: already built, `dist/OpenBA.app` in the worktree (rebuild: `tools/macos/make_app.sh`).
- **PC**: pull the `neighbour-towns` branch, then `tools/build.sh unit` and `tools/build.sh deploy`.
  The unit test `ext_neighbour_world_maths_same_bits_everywhere` must pass on the PC: it proves the PC
  computes the shared map's maths exactly as the Mac does.

## 3. Start the relay on the Mac

In Terminal on the Mac (leave the window open while you play):

```bash
cd /Volumes/DevSSD/dev/repos/OpenBA/.claude/worktrees/neighbour-towns-0cd35d/tools/neighbour_server
npx wrangler dev --ip 0.0.0.0 --port 8791
```

When it says `Ready on`, the relay is at `ws://192.168.0.168:8791` for the PC and
`ws://127.0.0.1:8791` for the Mac. Only devices on your home network can reach it, and it stops when
you close the window. (Port 8787 is used by another tool on your Mac, so this uses 8791.)

## 4. Start the game

1. **Mac**: open `OpenBA.app`, then **Multiplayer**. In **Relay** type `ws://127.0.0.1:8791`, then
   press **Refresh**. Type a room name, a password and your town's name, then press **Create room**.
   Choose the map in New Game and press Ok.
2. **PC**: start OpenBA, then **Multiplayer**. In **Relay** type `ws://192.168.0.168:8791`, then press
   **Refresh**. Pick the room, type the password and your town's name, then press **Join**.
3. The PC builds the same map from the same seed and starts on the other side of it.

## 5. What to look for

- In the event log: **"the same land"** means it works. You should then see each other's buildings
  appear, roads become real roads, and trees one town fells disappear in the other.
- **"not the same land"** and *Neighbour towns stopped* means the two built different worlds. Check
  the mods (step 1) first; otherwise send both logs (below).
- **"game data is not the same"**: the two have different Banished files (or different mods inside
  `WinData`).
- A warning that the neighbour's game is "another build" is expected (Mac and Windows) and harmless.
- Press **N** in game for the Neighbour panel: chat, send goods, send a family.

Logs to send if something goes wrong (the lines that matter start with `[Neighbour]` and
`[WorldHash]`):
- Mac: `~/Library/Application Support/OpenBA/Run/standalone_error.log`
- PC: `standalone_error.log` in the Banished folder.

## Unattended runs (for testing)

With `BANISHED_AUTOSTART=1`, a game can create or join the room on its own: in its `settings.cfg` put
`neighbour.autolobby=create` (or `join`) with `neighbour.server`, `neighbour.room`,
`neighbour.password` and `neighbour.name`. A joining game takes the room's seed and settings itself.
This is how the two-Mac test ran (`docs/fork/NEIGHBOUR_TOWNS.md` §11). The gate's faithful run
(`tools/build.sh run`) switches every extension off, so test with a plain autostart instead.

## Playing from anywhere (later)

The Cloudflare Worker relay works across the internet with secure `wss://`. Any Worker deployed before
2026-10-23 speaks the previous protocol and refuses these games, so deploy this branch's relay first
(`npx wrangler login`, then `npm run deploy` in `tools/neighbour_server`). Both machines then use
`wss://openba-neighbour.<your-subdomain>.workers.dev`.
