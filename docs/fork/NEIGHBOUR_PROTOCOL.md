# Neighbour towns: the wire protocol (version 2)

Every message the game (`src/ext/neighbour/`) and the relay (`tools/neighbour_server/src/index.js`)
exchange, defined once. Code comments point here instead of repeating the list.

It follows the interop spec being drafted in
[fran-cap/Banished-Interop-Spec](https://github.com/fran-cap/Banished-Interop-Spec) (issue #1, PRs #2
and #4): layer 0 (relay, envelope, seats), layer 1 (handshake), layer 2 (chat, presence) and layer 3b
(the separate-town profile). Where the spec is still open, this file says what we do today.

## Envelope

- A WebSocket (`wss://` to an internet relay, `ws://` to a test relay), one JSON object per **text**
  frame, at most 16 KB. Every object has a string `"t"`, its type; the other fields are flat (string,
  integer, boolean). Binary frames are not used: nothing here is bulk data.
- `GET /room/<name>` is a room; `GET /lobby` is the public list of lobby rooms; `GET /` a health line.
- Unknown types are ignored by both sides (a newer peer may send more). A receiver checks every field
  before using it; a bad message is dropped or answered with `error`, never trusted.
- There is **no default relay**: the game uses `neighbour.server` in `settings.cfg` or what the player
  types in the lobby's Relay box.

## Refuse or warn (layer 1, separate-town profile, settled in issue #1)

| Field | Different between the two towns | Where it is checked |
|---|---|---|
| `v` (protocol version) | **refuse** | relay, at `hello` / `create` |
| `dataHash` (retail data) | **refuse** (both non-empty and different) | relay, at `hello` |
| world hash (`hash`, the land the game built) | **refuse** the town that reports it second | relay, at `hash` |
| `simBuild`, `engine`, `engineVer` | warn (`compat`) | relay, at `hello`, to both towns |
| `mods` | warn (`compat`), naming what differs | relay, at `hello`, to both towns |
| `maxPlayers` | refuse when below the room's 2 seats | relay, at `hello` |

A refusal is an `error` with `"fatal": true`, and the relay closes the line; the game stops retrying
for that game and says why in the event log (or the lobby).

## Game -> relay

### `hello` (join a room; sent first on every connect)
| field | type | meaning |
|---|---|---|
| `v` | int | protocol version, 2 |
| `room` | string | the room name (also in the URL) |
| `pass` | string | the room password (the first town in sets it) |
| `name` | string | the town's display name (1..24 chars); never used to pick a seat |
| `token` | string, optional | the seat token from this town's first `welcome` (16..64 lowercase hex) |
| `lastSeq` | int | the last chat number this town has shown (newer lines are replayed) |
| `join` | bool, optional | true from the lobby's Join: refused if the room was not created in the lobby |
| `fresh` | bool, optional | true on a new game's hellos until its first `welcome` (never a loaded save, never a reconnect): the relay drops what this seat's earlier game left -- its buildings, roads, land and the resource changes still queued for the neighbour -- and tells an online neighbour (`unbuild`, `road` `off`, `released`) |
| `engine` | string | `openba` |
| `engineVer` | string | OpenBA's version, e.g. `1.0.0` |
| `simBuild` | string | 16 hex: xxHash64 of version, git commit, compiler and target; `""` = unknown |
| `dataHash` | string | 16 hex: xxHash64 over WinData's `data0.pkg`, `data1.pkg`, `patch0.pkg`, `patch1.pkg` (each as name, size, xxHash64 of its bytes); `""` = unreadable |
| `mods` | string | the enabled mods in load order, `name|size|xxh64;...` (hash may be empty; `%`, `|`, `;` in a name as `%25`, `%7C`, `%3B`). Follows fran-cap's open proposal (name + size required, xxHash64 optional); only `handshake.cpp` and `decodeMods` in the relay spell it |
| `exts` | string | the active class B / C extensions, `id:formatVersion,...` |
| `maxPlayers` | int | the most towns this game takes in a room: 2 |

```json
{"t":"hello","v":2,"room":"river","pass":"secret","name":"Ashford","token":"ac24a0aa785d279c20581e821c702bf0",
 "lastSeq":12,"engine":"openba","engineVer":"1.0.0","simBuild":"5b1d0c3e9a77f210","dataHash":"8f2e44a1c09b3d6e",
 "mods":"BetterFields.pkm|48213|0c1f9e2d7a3b4c55","exts":"neighbour:5","maxPlayers":2}
```

### `create` (the lobby's Create room)
Every `hello` field except `room`, `token`, `lastSeq` and `join`, plus the new game's settings:
`seed` (int), `size` (int 0..2: 256 / 384 / 512), `disasters` (int 0/1), `start`, `terrain`,
`climate` (resource paths). Refused if the room exists. Then handled as the creator's `hello`.

The map type rides in `terrain`: a Continents room (a land each, sea between and all round,
`NEIGHBOUR_TOWNS.md` §12) sends `continents:` before the TerrainGenerator's path, e.g.
`continents:Game\Terrain\Valleys.rsc`; a one-land room sends the path alone. The relay stores and
hands it back as any path. A game that does not know a prefix cannot load the path and refuses to
join, so it never builds other land.

### `chat`
`text` (string, at most 200 chars after cleaning). Example: `{"t":"chat","text":"Hello from Ashford"}`

### `parcel` (goods or a family, delivered exactly once)
`id` (string `[0-9a-z_-]{1,40}`, unique per sender), `kind` (`goods` | `family`), `body` (string, at most
8 KB): goods `Template\Path.rsc:resource:count,...`; a family `age/gender/child;...`, adults first.
The sender resends it on every join until it gets `stored`.

### `ack`
`id`: the receiver applied the parcel; the relay deletes it.

### `hash`
`hash` (hex, 1..32): the world (land) hash the town's map was built with. See the refuse table.

### Shared map (layer 3b)
| type | fields | meaning |
|---|---|---|
| `claim` | `id`, `rects` = `"x,y,w,h;..."` (at most 64 rects, 4096 tiles) | claim land; answered `claimed` |
| `release` | `rects` | give land back |
| `build` | `id` (`[0-9a-z_-]{1,40}`), `tmpl` (path), `x`, `y`, `z`, `rot`, `tx`, `ty`, `tw`, `th`, `built` (ints), `grp` (int 0..63), `bm` (`0`/`1` per footprint tile, row by row, optional) | a structure, its place and construction stage |
| `unbuild` | `id` | it is gone |
| `road` | `cell`, `type`, `tt` (ints), `tmpl` (path); or `cell`, `off: true` | a finished road tile, or one taken up |
| `nat` | `op` (`rm` \| `add`), `cell` (int), `tmpl` (path) | a natural resource felled / mined out, or a sapling planted |

### `pause`
`on` (bool): this town paused (`true`) or carried on (`false`): the toolbar's pause / play or the pause
key. The relay passes it to the neighbour, whose game does the same (shared pause: either town pausing
pauses both). Sent again on arriving or joining while paused. Nothing is kept.

### `walkers`
This town's villagers, once a second while the neighbour is online (`walkers.h`): `tmpl` (the citizen
template's path) and `list` = `id,x,y,z,yaw,clip,grp,scale,cloth;...`, integers: a small id per villager
while it lives, the render position x 100, the yaw in degrees x 10, the animation clip it plays (0
standing, 1-4 walking, others working), the mesh group (0 man, 1 woman), the size x 100 (100 adult, 83
student, 66 child) and the clothes index. One message per template, its list at most about 12,000
characters: a town of more than about 300 villagers sends its first ~300 (walkers.cpp kMaxListChars). The relay passes it on and keeps nothing; the neighbour draws each villager see-through and
lets one no message names for 4 s go.

### `ping`
Every 25 s of silence; answered `pong` (without waking the room).

## Relay -> game

| type | fields | meaning |
|---|---|---|
| `welcome` | `seat` (0/1), `peer` (name or `""`), `online` (bool), `created` (bool), `token` (only on a first join: keep it), and for a lobby room `seed`, `creator`, `size`, `disasters`, `start`, `terrain`, `climate` | joined |
| `peer` | `name`, `online` | the neighbour arrived or left |
| `chat` | `from`, `text`, `seq` | a line (also replayed on join: the newest 20 after `lastSeq`) |
| `parcel` | `id`, `kind`, `body`, `from` | a parcel for this town |
| `stored` | `id` | the relay holds this town's parcel |
| `hashCheck` | `same` (bool), `mine`, `theirs` | the two towns' land compared |
| `compat` | `warn` (a sentence for the event log) | a difference that only warns (another build, other mods) |
| `pause` | `on`, `from` | the neighbour paused (`true`) or carried on (`false`): do the same |
| `walkers` | `tmpl`, `list` | the neighbour's villagers, as above |
| `claimed` | `id`, `ok` (bool), `why` | the answer to a `claim` |
| `claims` / `released` | `seat`, `rects` | the neighbour claimed / released land |
| `claimBlock` | `bx`, `by`, `size`, `owner` (one char per tile: the neighbour's seat digit or `.`) | the neighbour's land, on join |
| `build` / `unbuild` / `road` / `nat` | as above | the neighbour's; on join: every building, then `mirrorEnd` `count`, every road, the queued `nat`s |
| `error` | `why`, `fatal` (bool, optional) | a problem; `fatal` = refused, the line closes, do not retry |
| `pong` | | |

Example of a refusal: `{"t":"error","why":"your Banished game data is not the same as Ashford's, ...","fatal":true}`

## Seats (layer 0)

A room has two seats. A town's first `hello` (no `token`) takes a free seat, and its `welcome` carries
a random 128-bit `token`; the room keeps only a SHA-256 of it. Every later `hello` sends the token back
and gets the same seat, whatever its `name`. A town with no token, or one the room does not know, takes
a free seat or is refused (`already has two towns`). A room made before tokens existed knows its towns
by name only; each such seat is handed its token once, to the town with that name. The game keeps its
token in its save (neighbour save payload v4 and later, `protocol.h` `SavedState`) and, the moment it arrives,
in `<Documents>/Banished/neighbour_seats.txt` beside `settings.cfg` (one line per relay, room and town;
`seat_store.h`), so a town that quits or crashes before saving keeps its seat. A hello with no token in
the save, and the lobby's Join, send the one from that file. The file never leaves the machine.

## Lobby (`/lobby`)

`{"t":"list"}` -> one `room` per lobby room (`name`, `creator`, `seats`, `size`, `disasters`, `start`,
`terrain`, `climate`, `lastActive`; never a password or seed), then `roomsEnd` `count`.

## Not yet (agreed in issue #1, planned after this version works between two machines)

- A **harvest lease**: a town asks the relay before felling a tree, so two towns cannot fell the same one.
- A periodic **repair snapshot**: each town's full building, road and resource state now and then, so
  drift (a missed message, saplings growing apart) is put right.
- The reference relay the working group proposes (`unbanished-server`, a self-hostable native server) is
  not used yet; its spec is not written. The Worker relay stays.
