# The neighbour-towns relay

The small server two OpenBA towns talk through (`docs/fork/NEIGHBOUR_TOWNS.md` §3). It is a
Cloudflare Worker with one Durable Object per room. The game's side is `src/ext/neighbour/`.

What a room does:
- It has **two seats and a password**. The first town in sets the password; a third town is refused.
- It **passes chat lines on** and keeps the last 200 lines. A town that was away gets up to the
  20 newest lines it missed when it comes back, because the game remembers (in its save) the last
  line number it saw.
- It tells each town when the other one **arrives or leaves**.

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

The self-test checks two towns chatting, a missed line being replayed, a wrong password and a
third town being refused.

## Pretend to be the neighbour

```bash
node fake_neighbour.mjs --room pc-test --pass test --name "Fake Town" --say "Hello from next door!"
```

It joins the room like a game does, prints what it receives, and sends each line you type.
`--every 20 "text"` repeats a line, and `--quit-after 60` leaves after a minute.

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
neighbour's lines. The gate (`tools/build.sh run`) forces every extension off, so it is not
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
