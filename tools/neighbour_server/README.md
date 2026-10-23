# The neighbour-towns relay

The small server two OpenBA towns talk through (`docs/fork/NEIGHBOUR_TOWNS.md` §3). It is a
Cloudflare Worker with one Durable Object per room. The game's side is `src/ext/neighbour/`.

It speaks **protocol 2**; every message is listed in `docs/fork/NEIGHBOUR_PROTOCOL.md`. There is no
built-in relay address in the game: each player points the game at this relay (`neighbour.server`
in `settings.cfg`, or the lobby's Relay box).

What a room does:
- It has **two seats and a password**. The first town in sets the password; a third town is refused.
- **Seats by token**: a town's first join is handed a seat token, which the game keeps in its save and
  sends back on every later join. A town is never seated by its name.
- **The handshake**: each town says which game it runs (engine, build, retail data hash, mods,
  extensions). Other retail data (or another protocol version) is refused; another build or other
  mods only warn both towns. A town that built other land from the room's seed is refused too.
- It **carries parcels** (goods and families): it answers `stored` to the sender, keeps the parcel
  while the other town is away (up to 64), and deletes it once the receiving town says `ack`.
- It **passes chat lines on** and keeps the last 200 lines. A town that was away gets up to the
  20 newest lines it missed when it comes back, because the game remembers (in its save) the last
  line number it saw.
- It tells each town when the other one **arrives or leaves**.
- **Lobby rooms** (M2): a room created from the game's Multiplayer lobby keeps its seed and map
  settings and hands them to the town that joins; `/lobby` lists such rooms (never a password;
  rooms idle 60 days drop off; 5 new rooms a day per address, not counted on the local relay).
- **The shared map**: it compares the two towns' world hash, referees land claims (first come
  first served) and keeps each town's buildings so the other town can draw them.
- **Shared roads and resources** (M3): it keeps each town's finished road tiles for the other town,
  and passes on trees felled and saplings planted (queued, the newest 20 000, while a town is away).

## Run it on this PC (no Cloudflare account needed)

```bash
cd tools/neighbour_server
npm install
npm run dev
```

The relay is then at `ws://127.0.0.1:8787` (this PC only). In another terminal:

```bash
node selftest.mjs ws://127.0.0.1:8787
```

The self-test (47 checks) covers chat and missed lines, passwords and full rooms, parcels,
lobby rooms and their list, the world-hash check, land claims, mirrored buildings, shared roads and
resource changes, and protocol 2: seat tokens, a protocol 1 game refused, other retail data or land
refused, another build or other mods warned. If port 8787 is taken, run
`npx wrangler dev --ip 127.0.0.1 --port 8791` and pass `ws://127.0.0.1:8791` instead.

## Pretend to be the neighbour

```bash
node fake_neighbour.mjs --room pc-test --pass test --name "Fake Town" --say "Hello from next door!"
```

It joins the room like a game does, prints what it receives (and acknowledges parcels), and
sends each line you type. Its first join prints the seat token it was given: pass `--token <it>` to
come back to the same seat (the relay never seats a town by name). It sends an empty retail data hash,
so the relay cannot refuse it for other game data; `--data-hash <16 hex>` (the game's log prints its
own) tests that refusal. `--every 20 "text"` repeats a line, and `--quit-after 60` leaves after a
minute. Parcels:

```bash
node fake_neighbour.mjs --room pc-test --pass test --send-goods "Template\RawMaterialPotato.rsc:resource:30"
node fake_neighbour.mjs --room pc-test --pass test --send-family "31.0/1/0;29.5/0/0;7.0/1/1"
```

or type `/goods <path> <count>` / `/family <members>` while it runs. A family is
`age/gender/child` per person, separated by `;`, adults first.

## Point the game at it

In `Documents/Banished/settings.cfg` (the game reads it when a game starts):

```
ext.neighbour=1
neighbour.server=ws://127.0.0.1:8787
neighbour.room=pc-test
neighbour.password=test
neighbour.name=My Town
```

Then start a new game or load a save. The event log shows "Joined the room ..." and the
neighbour's lines. Press **N** in game for the Neighbour panel: chat, send goods, send a family. The gate (`tools/build.sh run`) forces every extension off, so it is not
affected.

## Put it on Cloudflare (always on, free plan)

```bash
cd tools/neighbour_server
npx wrangler login
npm run deploy
```

`wrangler login` opens a browser to sign in to your Cloudflare account; `deploy` prints the
address, like `https://openba-neighbour.<your-subdomain>.workers.dev`. Both players then set:

```
neighbour.server=wss://openba-neighbour.<your-subdomain>.workers.dev
```

Use `wss://` (secure) for the Cloudflare address and `ws://` only for the local relay. Pick a room
name and password only the two of you know.
