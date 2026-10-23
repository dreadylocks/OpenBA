# Playing your Mac against your PC

Two towns, one map: one on the Mac, one on the PC, talking through a small relay (the "postbox").
No Steam account or login is involved. Neighbour towns is still young; nobody has played a real
two-machine game yet, so expect to find things.

## 1. Same game data on both machines

Both games must read the **same Banished 1.0.7 files**, or the relay refuses the second town ("your
Banished game data is not the same"). Simplest: copy them from the PC.

1. On the PC, find the Banished folder (yours is
   `C:/Program Files (x86)/Steam/steamapps/common/Banished`). Copy its `WinData` folder (the four
   files `data0.pkg`, `data1.pkg`, `patch0.pkg`, `patch1.pkg`, plus any `.pkm` mods you use) to a USB
   stick or a network share.
2. On the Mac, put it on the external SSD, for example `/Volumes/DevSSD/dev/games/Banished/WinData`.
3. The first time `OpenBA.app` starts, it asks for the game folder: pick
   `/Volumes/DevSSD/dev/games/Banished` (the folder that holds `WinData`).

Use the same mods on both, in the same order. Different mods only warn, but they can make the two maps
different.

## 2. Same version of OpenBA on both

Build both from the same commit of the `neighbour-towns` branch:
- **Mac**: `tools/macos/make_app.sh`, which makes `dist/OpenBA.app`.
- **PC**: pull the branch and run `tools/build.sh deploy`. Or copy the Mac's cross-built
  `build/cross-release/OpenBA.exe` (`tools/build.sh build release --cross`) into the PC's Banished
  folder.

The two builds differ (Mac and Windows), so the event log shows a warning that the neighbour's game is
"another build". That's expected and only a warning.

## 3. A relay both machines can reach

Pick one:
- **A relay on your home network** (quickest to try). On one machine, in `tools/neighbour_server`:
  `npm install`, then `npx wrangler dev --ip 0.0.0.0 --port 8791`. The other machine uses
  `ws://<that machine's home address>:8791`; the machine running the relay uses `ws://127.0.0.1:8791`.
  This opens port 8791 to your home network (your Mac's firewall may ask), so only do it at home.
  Port 8787 is already used by another tool on your Mac, so this guide uses 8791.
- **Your Cloudflare Worker** (works from anywhere; secure `wss://`). A Worker deployed before
  2026-10-23 speaks the previous protocol and refuses the new games, so deploy this branch's relay
  (`npx wrangler login`, then `npm run deploy` in `tools/neighbour_server`). Then both machines use
  `wss://openba-neighbour.<your-subdomain>.workers.dev`.

You can also put the address in each machine's `settings.cfg` as `neighbour.server=...`, so the
lobby's Relay box starts with it. On the Mac that file is `~/Documents/Banished/settings.cfg`; on the
PC it's `Documents/Banished/settings.cfg`.

## 4. Start the game

1. **Mac**: main menu, then **Multiplayer**. Type the relay address in **Relay**, then press
   **Refresh**. Type a room name, a password and your town's name, then press **Create room**. Choose
   the map in New Game and press Ok.
2. **PC**: main menu, then **Multiplayer**. Same relay, press **Refresh**, pick the room, type the
   password and your town's name, then press **Join**.
3. The PC builds the same map from the same seed and starts on the other side of it.

## 5. What to look for

- In the event log: **"the same land"** means it works.
- **"not the same land"** and *Neighbour towns stopped* means the Mac and the PC built slightly
  different worlds from the same seed. This is the one big unknown: the two operating systems' maths
  libraries may round some numbers differently. If it happens, send both logs (below); there is a
  planned fix.
- Press **N** in game for the Neighbour panel: chat, send goods, send a family.

Logs to send if something goes wrong:
- Mac: `~/Library/Application Support/OpenBA/Run/standalone_error.log`
- PC: `standalone_error.log` in the Banished folder.

The lines that matter start with `[Neighbour]` and `[WorldHash]`.
