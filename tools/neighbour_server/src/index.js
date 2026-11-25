// The neighbour-towns relay: a Cloudflare Worker with one Durable Object per room.
// docs/fork/NEIGHBOUR_TOWNS.md §3. The game's side is src/ext/neighbour/ (protocol.h has the
// message list). A room has two seats and a password; the first town in sets the password.
// Chat lines are numbered and kept (the last kKeepChats), so a town that was away gets what
// it missed when it comes back (its hello carries the last number it saw).
//
//   GET /room/<name>   (WebSocket upgrade)   -> that room's Durable Object
//   GET /lobby         (WebSocket upgrade)   -> the Directory: the public list of rooms
//   GET /              -> a one-line health text
//
// ROOMS AND THE LOBBY (M2). A room made from the game's Multiplayer lobby is CREATED: its
// creator sends {"t":"create"} with the password, its town name, the seed and the new-game
// settings; the room keeps them and tells the Directory. Joining sends a hello as before, and
// the welcome then carries the room's seed and settings so the joiner's game builds the same
// map. A room never created (the settings.cfg route of M0 / M1) still works as before.
// The Directory lists rooms that were active in the last kRoomIdleDays (name, creator town,
// seats taken, settings; never a password), at most kMaxListedRooms, and limits each internet
// address to kCreatesPerDay new rooms. Each town reports its world hash; the room says whether
// the two towns built the same map.
//
// Every message is a JSON object with a string "t". Anything malformed, oversized or out of
// order gets an {"t":"error"} and, for a bad hello, the line is closed ("fatal": the game stops
// retrying). The messages are listed once in docs/fork/NEIGHBOUR_PROTOCOL.md.
//
// PROTOCOL 2 (2026-10-23, the interop spec fran-cap/Banished-Interop-Spec #1):
//   * SEATS BY TOKEN (layer 0). A town's first join gets a random seat token in its welcome; every
//     later hello sends it back, and the room seats the town by it, never by its name. The room
//     keeps only a hash of each token. A room from before tokens hands each named seat its token
//     once, to the town with that name.
//   * THE HANDSHAKE (layer 1). A hello / create carries engine, engineVer, simBuild, dataHash,
//     mods, exts and maxPlayers. The separate-town rules: a different retail data hash REFUSES the
//     join (both towns need the same land), and so does a different protocol version; a different
//     sim build, engine or mod list only WARNS ({"t":"compat"} to both towns).
//   * THE LAND. A town whose world hash differs from the room's is refused (fatal): the two games
//     built different worlds, and nothing mirrored would line up.

const kProtocolVersion = 2;
const kTokenHex = /^[0-9a-f]{16,64}$/;
const kHash64 = /^[0-9a-f]{16}$/;
const kMaxMods = 256;
const kMaxModsChars = 12 * 1024;
const kMaxFrameBytes = 16 * 1024;
const kMaxSyncChars = 15000;   // a nature sync message's list (the game keeps them under 11,000)
const kSyncWindowMs = 5000;    // at most kSyncPerWindow sync requests from a town in this long
const kSyncPerWindow = 8;
const kMaxNameChars = 24;
const kMaxRoomChars = 64;
const kMaxPassChars = 128;
const kMaxChatChars = 200;
const kKeepChats = 200;
const kReplayChats = 20;
const kMaxParcels = 64;          // waiting for one town
const kMaxParcelBody = 8 * 1024;
const kParcelId = /^[0-9a-z_-]{1,40}$/;
const kSeats = 2;
const kRoomIdleDays = 60;
const kMaxListedRooms = 500;
const kCreatesPerDay = 50;   // new lobby rooms a day from one address (a spam guard; friends share a home address)
const kDayMs = 24 * 60 * 60 * 1000;
// The new-game settings a room carries, besides the seed: the map size row and the disasters
// switch as small whole numbers, and the start condition, terrain and climate as the game's
// resource paths (the same data on both PCs resolves them to the same generators).
const kIntSettings = ["size", "disasters"];
const kPathSettings = ["start", "terrain", "climate"];
const kSettingKeys = [...kIntSettings, ...kPathSettings];
const kMaxPathChars = 160;
// Land claims and mirrored buildings (M2). A claim is a list of tile rectangles "x,y,w,h;...";
// the room keeps who owns each tile in kClaimBlock x kClaimBlock blocks (one string of seat
// digits per block, "." = free). A town's buildings are kept as the latest record per building id.
const kMaxMapDim = 1024;
const kClaimBlock = 32;
const kMaxClaimRects = 64;
const kMaxClaimTiles = 4096;   // per message
const kMaxBuildings = 4000;    // per town
const kBuildingId = /^[0-9a-z_-]{1,40}$/;
// M3: shared roads and resources. A town's finished road tiles are kept (cell -> road) and handed
// to a joining town; natural-resource changes (a tree felled, a deposit gone, a sapling planted)
// go straight to the neighbour, or wait in a queue (the newest kMaxNatQueue) while it is away.
const kMaxRoadCells = 200_000;
const kMaxNatQueue = 20_000;
// Every resource change is also kept for good, the newest per tile (natlog:<seat>:<shard>, kNatShard
// tiles a shard, its template paths numbered within it), stamped with when it came. A town that starts
// again in its seat or loads a save gets the newest change per tile and template across both towns'
// records: the changes after that save reached the neighbour live and sit in no queue, a tree felled in
// one copy of the land must be gone in the other, and a plant one town spread and the other felled must
// not come back from the older record.
const kNatShardBits = 12;
const kMaxCell = kMaxMapDim * kMaxMapDim;
// Flood guard: at most this many messages per socket in kWindowMs.
// The game paces its shared-map messages to 40 a second (shared_map.cpp kSendRate), a burst of 120.
const kBurst = 600;
const kWindowMs = 10_000;

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const m = url.pathname.match(/^\/room\/([^/]+)\/?$/);
    if (m) {
      let room;
      try { room = decodeURIComponent(m[1]); } catch { return new Response("bad room name", { status: 400 }); }
      room = room.trim();
      if (room.length === 0 || room.length > kMaxRoomChars) return new Response("bad room name", { status: 400 });
      if (!isWebSocketUpgrade(request)) return new Response("expected a WebSocket", { status: 426 });
      const stub = env.ROOMS.get(env.ROOMS.idFromName(room));
      const fwd = new Request(request);
      fwd.headers.set("X-Neighbour-Room", room);
      fwd.headers.set("X-Neighbour-Ip", request.headers.get("CF-Connecting-IP") || "local");
      return stub.fetch(fwd);
    }
    if (url.pathname === "/lobby" || url.pathname === "/lobby/") {
      if (!isWebSocketUpgrade(request)) return new Response("expected a WebSocket", { status: 426 });
      return env.DIRECTORY.get(env.DIRECTORY.idFromName("directory")).fetch(request);
    }
    if (url.pathname === "/") return new Response("OpenBA neighbor relay: connect to /room/<name>\n");
    return new Response("not found", { status: 404 });
  },
};

// RFC 6455 4.2.1: the Upgrade value is case-insensitive. macOS's Network.framework sends "WebSocket"
// (WinHTTP and node send "websocket"); an exact match turned every Mac away with a 426.
function isWebSocketUpgrade(request) {
  return (request.headers.get("Upgrade") || "").toLowerCase() === "websocket";
}

function clean(text, max) {
  if (typeof text !== "string") return "";
  return text.replace(/[\u0000-\u001f\u007f]/g, " ").replace(/\s+/g, " ").trim().slice(0, max);
}

async function sha256Hex(text) {
  const digest = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(text));
  return [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

function randomToken() {
  const bytes = new Uint8Array(16);
  crypto.getRandomValues(bytes);
  return [...bytes].map((b) => b.toString(16).padStart(2, "0")).join("");
}

function tokenHash(room, token) {
  return sha256Hex(`openba-neighbour-seat:${room}:${token}`);
}

// The mod list, as src/ext/neighbour/handshake.cpp EncodeMods writes it: "name|size|hash;..." with
// '%', '|' and ';' in a name as %25, %7C, %3B and an empty hash for none. null when malformed.
function decodeMods(text) {
  if (typeof text !== "string" || text.length > kMaxModsChars) return null;
  if (text === "") return [];
  const entries = text.split(";");
  if (entries.length > kMaxMods) return null;
  const out = [];
  for (const e of entries) {
    const f = e.split("|");
    if (f.length !== 3) return null;
    if (/%(?!25|7C|3B)/.test(f[0])) return null;
    const name = f[0].replace(/%7C/g, "|").replace(/%3B/g, ";").replace(/%25/g, "%");
    if (!name || name.length > 260 || !/^[0-9]{1,20}$/.test(f[1])) return null;
    if (f[2] !== "" && !kHash64.test(f[2])) return null;
    out.push({ name, size: f[1], hash: f[2] });
  }
  return out;
}

// A hello's layer-1 fields -> { compat } or { why } (a refusal).
function readCompat(msg) {
  const hex = (v) => (v === "" || (typeof v === "string" && kHash64.test(v)) ? v : null);
  const c = {
    engine: clean(msg.engine, 32),
    engineVer: clean(msg.engineVer, 32),
    simBuild: hex(msg.simBuild),
    dataHash: hex(msg.dataHash),
    mods: typeof msg.mods === "string" ? msg.mods : "",
    exts: clean(msg.exts, 512),
  };
  if (!c.engine) return { why: "the game did not say what it is (update the game)" };
  if (c.simBuild === null || c.dataHash === null) return { why: "bad build or data hash" };
  if (decodeMods(c.mods) === null) return { why: "bad mod list" };
  if (!Number.isInteger(msg.maxPlayers) || msg.maxPlayers < kSeats) {
    return { why: `this game cannot share a room of ${kSeats} towns` };
  }
  return { compat: c };
}

// The separate-town profile's rules: what refuses `mine` (one string, or null) and what it is warned about.
function compareCompat(mine, theirs, peer) {
  if (mine.dataHash && theirs.dataHash && mine.dataHash !== theirs.dataHash) {
    return {
      refuse: `your Banished game data is not the same as ${peer}'s, so your maps would not match. ` +
        "Both games need the same Banished 1.0.7 files (copy the WinData folder from one computer to the other).",
      warn: [],
    };
  }
  const warn = [];
  if (mine.engine !== theirs.engine || mine.engineVer !== theirs.engineVer) {
    warn.push(`${peer} plays ${theirs.engine} ${theirs.engineVer}; you play ${mine.engine} ${mine.engineVer}.`);
  }
  if (mine.simBuild && theirs.simBuild && mine.simBuild !== theirs.simBuild) {
    warn.push(`${peer}'s game is another build (for example Windows and Mac). The towns can still play; ` +
      "say so if something looks out of place.");
  }
  if (mine.mods !== theirs.mods) {
    const a = decodeMods(mine.mods) || [];
    const b = decodeMods(theirs.mods) || [];
    const key = (m) => m.name.toLowerCase();
    const inB = new Map(b.map((m) => [key(m), m]));
    const inA = new Map(a.map((m) => [key(m), m]));
    const onlyMine = a.filter((m) => !inB.has(key(m))).map((m) => m.name);
    const onlyTheirs = b.filter((m) => !inA.has(key(m))).map((m) => m.name);
    const other = a.filter((m) => {
      const t = inB.get(key(m));
      return t && (t.size !== m.size || (t.hash && m.hash && t.hash !== m.hash));
    }).map((m) => m.name);
    const parts = [];
    if (onlyMine.length) parts.push(`only yours: ${onlyMine.slice(0, 5).join(", ")}`);
    if (onlyTheirs.length) parts.push(`only ${peer}'s: ${onlyTheirs.slice(0, 5).join(", ")}`);
    if (other.length) parts.push(`another version: ${other.slice(0, 5).join(", ")}`);
    if (!parts.length) parts.push("the same mods in another order");
    warn.push(`Your mods and ${peer}'s differ (${parts.join("; ")}).`);
  }
  return { refuse: null, warn };
}

async function passwordHash(room, pass) {
  const bytes = new TextEncoder().encode(`openba-neighbour:${room}:${pass}`);
  const digest = await crypto.subtle.digest("SHA-256", bytes);
  return [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

// Write-back for a room's busy keys. Storage writes are what a room costs: Cloudflare's free plan allows
// 100,000 rows written a day for the whole account, and a busy town (a run at 50x spreads and fells
// thousands of plants a minute) rewrote these keys several times a second, which used the day's allowance
// up and left every room unable to save a hello. Writes to these keys wait in memory and go together,
// at most once every kFlushMs (an alarm), one row a key; reads see them at once. Claims, the room's meta,
// chats and parcels still write through. A room evicted before its alarm loses those last seconds of the
// buffered record (the live messages were already passed on; a town's structures are sent again on join),
// a buffered delete included (a fresh start's removal of the old town's mirror and roads could come back).
// The nature keys wait longer (kNatFlushMs): at 10x two towns change thousands of plants a minute, and
// with a 5 s flush every touched shard was a row every 5 s, about 350 rows a minute, a fifth of the day's
// allowance an hour (val-6, 2026-11-25). Their record only serves a town that loads or starts again
// (natlog) or one that comes back (natq); a minute lost to an eviction is mended by the nature sync.
const kBufferedPrefixes = ["natlog:", "natq:", "roads:", "roadoff:", "roadq:", "mirror:"];
const kSlowPrefixes = ["natlog:", "natq:"];
const kFlushMs = 5000;
const kNatFlushMs = 60_000;

class BufferedStore {
  constructor(ctx) {
    this.ctx = ctx;
    this.puts = new Map();
    this.dels = new Set();
    this.version = new Map();   // key -> a count bumped on every buffered put or delete (flush's "unchanged since")
    this.armed = false;
    this.alarmAt = 0;
    this.rowsWritten = 0;
    this.loggedAt = Date.now();
    this.loggedRows = 0;
  }

  // Counts rows written and logs the rate at most once a minute (`wrangler tail` shows it): what a room
  // costs of the account's daily allowance.
  count(n) {
    this.rowsWritten += n;
    const now = Date.now();
    if (now - this.loggedAt < 60_000) return;
    const id = this.ctx.id.toString().slice(0, 8);
    console.log(`[rows] room ${id}: ${this.rowsWritten - this.loggedRows} row(s) written in ` +
                `${Math.round((now - this.loggedAt) / 1000)} s, ${this.rowsWritten} since it woke`);
    this.loggedAt = now;
    this.loggedRows = this.rowsWritten;
  }

  buffered(key) {
    return kBufferedPrefixes.some((p) => key.startsWith(p));
  }

  // How soon a staged key must reach storage: a minute for the nature keys, 5 s for the rest.
  delayFor(key) {
    return kSlowPrefixes.some((p) => key.startsWith(p)) ? kNatFlushMs : kFlushMs;
  }

  // The delay the waiting entries need: the shortest of theirs.
  waitingDelay() {
    for (const k of this.puts.keys()) if (this.delayFor(k) === kFlushMs) return kFlushMs;
    for (const k of this.dels) if (this.delayFor(k) === kFlushMs) return kFlushMs;
    return kNatFlushMs;
  }

  async get(key) {
    if (this.puts.has(key)) return this.puts.get(key);
    if (this.dels.has(key)) return undefined;
    return this.ctx.storage.get(key);
  }

  async put(keyOrEntries, value) {
    if (typeof keyOrEntries === "string") {
      if (this.buffered(keyOrEntries)) return this.stage(keyOrEntries, value);
      this.count(1);
      return this.ctx.storage.put(keyOrEntries, value);
    }
    const direct = {};
    for (const [k, v] of Object.entries(keyOrEntries)) {
      if (this.buffered(k)) this.stage(k, v);
      else direct[k] = v;
    }
    const n = Object.keys(direct).length;
    if (n > 0) {
      this.count(n);
      await this.ctx.storage.put(direct);
    }
  }

  stage(key, value) {
    this.puts.set(key, value);
    this.dels.delete(key);
    this.version.set(key, (this.version.get(key) || 0) + 1);
    this.arm(this.delayFor(key));
  }

  async delete(keys) {
    const direct = [];
    for (const k of Array.isArray(keys) ? keys : [keys]) {
      if (this.buffered(k)) {
        this.puts.delete(k);
        this.dels.add(k);
        this.version.set(k, (this.version.get(k) || 0) + 1);
        this.arm(this.delayFor(k));
      } else {
        direct.push(k);
      }
    }
    if (direct.length > 0) {
      this.count(direct.length);
      await this.ctx.storage.delete(direct);
    }
  }

  async list(options) {
    const out = await this.ctx.storage.list(options);
    const prefix = (options && options.prefix) || "";
    for (const k of this.dels) if (k.startsWith(prefix)) out.delete(k);
    for (const [k, v] of this.puts) if (k.startsWith(prefix)) out.set(k, v);
    return out;
  }

  // Sets the alarm, or brings it forward when a key needs storage sooner than the one set.
  arm(delayMs = kFlushMs) {
    const at = Date.now() + delayMs;
    if (this.armed && this.alarmAt <= at) return;
    this.armed = true;
    this.alarmAt = at;
    this.count(1);   // the alarm is a row too
    // A refused alarm must not leave the buffer unflushable: the next write arms again.
    this.ctx.storage.setAlarm(at).catch(() => { this.armed = false; });
  }

  // Writes what waits, chunk by chunk; an entry leaves the buffer only once written and only if it was
  // not put or deleted again meanwhile (its version unchanged: callers change a cached object in place and
  // put the same object back, so the value itself cannot tell), so a failed write (a refused quota) keeps
  // the rest for the next alarm.
  async flush() {
    this.armed = false;
    const puts = [...this.puts].map(([k, v]) => [k, v, this.version.get(k)]);
    const dels = [...this.dels].map((k) => [k, this.version.get(k)]);
    const done = (k, ver) => {
      if (this.version.get(k) !== ver) return false;
      this.version.delete(k);
      return true;
    };
    let failed = false;
    try {
      for (let i = 0; i < puts.length; i += 100) {
        const chunk = puts.slice(i, i + 100);
        await this.ctx.storage.put(Object.fromEntries(chunk.map(([k, v]) => [k, v])));
        for (const [k, , ver] of chunk) if (done(k, ver)) this.puts.delete(k);
        this.count(chunk.length);
      }
      for (let i = 0; i < dels.length; i += 100) {
        const chunk = dels.slice(i, i + 100);
        await this.ctx.storage.delete(chunk.map(([k]) => k));
        for (const [k, ver] of chunk) if (done(k, ver)) this.dels.delete(k);
        this.count(chunk.length);
      }
    } catch {
      failed = true;   // a write refused (the day's allowance gone, say): try again in a minute, not 5 s
    } finally {
      if (this.puts.size > 0 || this.dels.size > 0) this.arm(failed ? 60_000 : this.waitingDelay());
    }
  }
}

export class Room {
  constructor(ctx, env) {
    this.ctx = ctx;
    this.env = env;
    this.store = new BufferedStore(ctx);
    // Pings are answered without waking the object (the hibernation API's auto-response).
    this.ctx.setWebSocketAutoResponse(new WebSocketRequestResponsePair('{"t":"ping"}', '{"t":"pong"}'));
  }

  // The write-back flush (BufferedStore.arm).
  async alarm() {
    await this.store.flush();
  }

  async fetch(request) {
    const pair = new WebSocketPair();
    const [client, server] = Object.values(pair);
    this.ctx.acceptWebSocket(server);
    server.serializeAttachment({
      room: request.headers.get("X-Neighbour-Room") || "",
      ip: request.headers.get("X-Neighbour-Ip") || "local",
      seat: -1, name: "", window: 0, count: 0,
    });
    return new Response(null, { status: 101, webSocket: client });
  }

  // ---- state -----------------------------------------------------------------
  // meta: { passHash, seats: [name|null, name|null], seq }   chats: [{seq, from, seat, text}]

  async meta() {
    return (await this.store.get("meta")) || { passHash: null, seats: new Array(kSeats).fill(null), seq: 0 };
  }

  directory() {
    return this.env.DIRECTORY.get(this.env.DIRECTORY.idFromName("directory"));
  }

  // Tell the Directory how this room stands (created rooms only; the Directory is the list).
  async publish(room, meta) {
    if (!meta.created) return;
    const entry = {
      name: room, creator: meta.creator || "", seats: meta.seats.filter((x) => x !== null).length,
      lastActive: Date.now(), seed: meta.seed, ...meta.settings,
    };
    try {
      await this.directory().fetch("https://directory/update", { method: "POST", body: JSON.stringify(entry) });
    } catch { /* the list is best effort; the room itself is unaffected */ }
  }

  // A seat's game lines. A lobby probe (hello `probe`: the lobby checking the room and getting its
  // seat, just before the game connects) is not one: it is never announced, never replaces the
  // game's line and is not sent the neighbour's messages.
  socketsFor(seat) {
    return this.ctx.getWebSockets().filter((ws) => {
      const a = ws.deserializeAttachment();
      return a && a.seat === seat && !a.probe;
    });
  }

  send(ws, msg) {
    try { ws.send(JSON.stringify(msg)); } catch { /* the line is gone */ }
  }

  // close = a refusal: the line is closed and the game told not to retry ("fatal").
  fail(ws, why, close) {
    this.send(ws, close ? { t: "error", why, fatal: true } : { t: "error", why });
    if (close) { try { ws.close(4000, why.slice(0, 120)); } catch { /* already closed */ } }
  }

  // ---- the hibernation API's handlers ---------------------------------------------

  async webSocketMessage(ws, raw) {
    const a = ws.deserializeAttachment();
    const now = Date.now();
    if (now - a.window > kWindowMs) { a.window = now; a.count = 0; }
    if (++a.count > kBurst) { ws.serializeAttachment(a); return this.fail(ws, "too many messages, slow down", false); }
    ws.serializeAttachment(a);

    if (typeof raw !== "string" || raw.length > kMaxFrameBytes) return this.fail(ws, "message too large", false);
    let msg;
    try { msg = JSON.parse(raw); } catch { return this.fail(ws, "not JSON", false); }
    if (!msg || typeof msg !== "object" || typeof msg.t !== "string") return this.fail(ws, "no message type", false);

    if (msg.t === "hello") return this.onHello(ws, a, msg);
    if (msg.t === "create") return this.onCreate(ws, a, msg);
    // Not fatal: the line closes and the town's game connects again with its hello.
    if (a.seat < 0) {
      this.send(ws, { t: "error", why: "say hello first" });
      try { ws.close(4000, "say hello first"); } catch { /* already closed */ }
      return;
    }
    if (msg.t === "chat") return this.onChat(ws, a, msg);
    if (msg.t === "parcel") return this.onParcel(ws, a, msg);
    if (msg.t === "ack") return this.onAck(ws, a, msg);
    if (msg.t === "hash") return this.onHash(ws, a, msg);
    if (msg.t === "claim") return this.onClaim(ws, a, msg);
    if (msg.t === "release") return this.onRelease(ws, a, msg);
    if (msg.t === "build") return this.onBuild(ws, a, msg);
    if (msg.t === "unbuild") return this.onUnbuild(ws, a, msg);
    if (msg.t === "road") return this.onRoad(ws, a, msg);
    if (msg.t === "nat") return this.onNat(ws, a, msg);
    if (msg.t === "pause") return this.onPause(ws, a, msg);
    if (msg.t === "speed") return this.onSpeed(ws, a, msg);
    if (msg.t === "walkers") return this.onWalkers(ws, a, msg);
    if (msg.t === "natSyncReq" || msg.t === "natDigest" || msg.t === "natBlockReq" || msg.t === "natBlock") {
      return this.onNatSync(ws, a, msg);
    }
    if (msg.t === "ping") return this.send(ws, { t: "pong" });
    // Unknown types from a newer game: ignored.
  }

  // Shared pause: either town pausing pauses both. Passed straight to the neighbour, kept nowhere.
  onPause(ws, a, msg) {
    if (typeof msg.on !== "boolean") return this.fail(ws, "bad pause", false);
    for (const peer of this.socketsFor(1 - a.seat)) this.send(peer, { t: "pause", on: msg.on, from: a.name || "" });
  }

  // Shared speed: either town changing the game speed changes both. Passed straight on, kept nowhere.
  onSpeed(ws, a, msg) {
    if (!Number.isInteger(msg.x) || msg.x < 1 || msg.x > 100) return this.fail(ws, "bad speed", false);
    for (const peer of this.socketsFor(1 - a.seat)) this.send(peer, { t: "speed", x: msg.x, from: a.name || "" });
  }

  // The town's villagers, once a second: passed straight to the neighbour if it is online, kept nowhere.
  onWalkers(ws, a, msg) {
    if (typeof msg.tmpl !== "string" || msg.tmpl.length < 1 || msg.tmpl.length > kMaxPathChars ||
        typeof msg.list !== "string" || msg.list.length > 14000 || !/^[0-9,;-]*$/.test(msg.list)) {
      return this.fail(ws, "bad walkers", false);
    }
    for (const peer of this.socketsFor(1 - a.seat)) this.send(peer, { t: "walkers", tmpl: msg.tmpl, list: msg.list });
  }

  // The nature sync (src/ext/neighbour/nature_sync.h): the town that does not own a half of the map asks
  // its owner how the half stands (natSyncReq, natBlockReq) and the owner answers (natDigest, natBlock).
  // Passed straight to the neighbour if it is online, kept nowhere: a round nobody answered is asked
  // again by the game.
  onNatSync(ws, a, msg) {
    if (!Number.isInteger(msg.id) || msg.id < 0 || (msg.half !== 0 && msg.half !== 1)) {
      return this.fail(ws, "bad nature sync", false);
    }
    // Each request makes the neighbour's game walk its whole map: a few a round, a round every ~45 s.
    if (msg.t === "natSyncReq" || msg.t === "natBlockReq") {
      const now = Date.now();
      if (!a.syncWindow || now - a.syncWindow > kSyncWindowMs) { a.syncWindow = now; a.syncCount = 0; }
      a.syncCount = (a.syncCount || 0) + 1;
      ws.serializeAttachment(a);
      if (a.syncCount > kSyncPerWindow) return;   // dropped: the game asks again later
    }
    const ev = { t: msg.t, id: msg.id, half: msg.half };
    for (const k of ["list", "blocks", "tmpls", "done"]) {
      if (msg[k] === undefined) continue;
      if (typeof msg[k] !== "string" || msg[k].length > kMaxSyncChars) return this.fail(ws, "bad nature sync", false);
      ev[k] = msg[k];
    }
    if (msg.end === true) ev.end = true;
    for (const peer of this.socketsFor(1 - a.seat)) this.send(peer, ev);
  }

  async onCreate(ws, a, msg) {
    if (a.seat >= 0) return this.fail(ws, "already joined", false);
    if (msg.v !== kProtocolVersion) return this.fail(ws, `this relay speaks protocol ${kProtocolVersion}; update the game or the relay`, true);
    const meta = await this.meta();
    if (meta.created || meta.seats.some((x) => x !== null)) return this.fail(ws, `a room called "${a.room}" already exists; pick another name`, true);
    const name = clean(msg.name, kMaxNameChars);
    if (!name) return this.fail(ws, "give your town a name", true);
    if (typeof msg.pass !== "string" || msg.pass.length < 1) return this.fail(ws, "a room needs a password", true);
    if (!Number.isInteger(msg.seed)) return this.fail(ws, "the room needs a seed", true);
    const settings = {};
    for (const k of kIntSettings) {
      if (!Number.isInteger(msg[k]) || msg[k] < 0 || msg[k] > 64) return this.fail(ws, `bad setting ${k}`, true);
      settings[k] = msg[k];
    }
    for (const k of kPathSettings) {
      if (typeof msg[k] !== "string" || msg[k].length < 1 || msg[k].length > kMaxPathChars || /[\x00-\x1f]/.test(msg[k])) {
        return this.fail(ws, `bad setting ${k}`, true);
      }
      settings[k] = msg[k];
    }
    // The creation allowance (per internet address, per day).
    // The local test relay (wrangler dev) sees no outside address: no allowance there.
    const localTest = a.ip === "local" || a.ip === "127.0.0.1" || a.ip === "::1";
    let allowed = true;
    if (!localTest) try {
      const r = await this.directory().fetch("https://directory/allow", { method: "POST", body: JSON.stringify({ ip: a.ip }) });
      allowed = r.ok;
    } catch { allowed = true; }
    if (!allowed) return this.fail(ws, `at most ${kCreatesPerDay} new rooms a day from one place; try again tomorrow`, true);
    meta.created = true;
    meta.creator = name;
    meta.seed = msg.seed;
    meta.settings = settings;
    meta.createdAt = Date.now();
    meta.passHash = await passwordHash(a.room, msg.pass.slice(0, kMaxPassChars));
    await this.store.put("meta", meta);
    // Now an ordinary join as the room's first town.
    return this.onHello(ws, a, { ...msg, t: "hello", lastSeq: 0 });
  }

  // ---- land claims ----------------------------------------------------------------------------

  parseRects(text) {
    if (typeof text !== "string" || text.length === 0 || text.length > 4096) return null;
    const rects = [];
    let tiles = 0;
    for (const part of text.split(";")) {
      const n = part.split(",").map((x) => Number(x));
      if (n.length !== 4 || n.some((v) => !Number.isInteger(v) || v < 0 || v > kMaxMapDim)) return null;
      const [x, y, w, h] = n;
      if (w < 1 || h < 1 || x + w > kMaxMapDim || y + h > kMaxMapDim) return null;
      tiles += w * h;
      if (rects.length >= kMaxClaimRects || tiles > kMaxClaimTiles) return null;
      rects.push({ x, y, w, h });
    }
    return rects;
  }

  // Visit every tile of `rects`, block by block; `fn(block, offset)` reads or writes one char.
  async eachTile(rects, write, fn) {
    const blocks = new Map();
    for (const r of rects) {
      for (let y = r.y; y < r.y + r.h; y++) {
        for (let x = r.x; x < r.x + r.w; x++) {
          const key = `claims:${Math.floor(x / kClaimBlock)}:${Math.floor(y / kClaimBlock)}`;
          if (!blocks.has(key)) {
            const v = (await this.store.get(key)) || ".".repeat(kClaimBlock * kClaimBlock);
            blocks.set(key, v.split(""));
          }
          const off = (y % kClaimBlock) * kClaimBlock + (x % kClaimBlock);
          if (fn(blocks.get(key), off) === false) return false;
        }
      }
    }
    if (write) {
      const out = {};
      for (const [k, v] of blocks) out[k] = v.join("");
      await this.store.put(out);
    }
    return true;
  }

  async onClaim(ws, a, msg) {
    const id = typeof msg.id === "string" && kParcelId.test(msg.id) ? msg.id : "";
    const rects = this.parseRects(msg.rects);
    if (!id || !rects) return this.fail(ws, "bad claim", false);
    const mine = String(a.seat);
    // Rect by rect: a message carries many structures' land, and one that overlaps the neighbour's
    // must not cost the others theirs (it used to refuse the whole message, again on every join).
    const granted = [];
    const refused = [];
    for (const r of rects) {
      let free = true;
      await this.eachTile([r], false, (b, off) => {
        if (b[off] !== "." && b[off] !== mine) { free = false; return false; }
        return true;
      });
      (free ? granted : refused).push(r);
    }
    if (granted.length > 0) await this.eachTile(granted, true, (b, off) => { b[off] = mine; return true; });
    const enc = (list) => list.map((r) => `${r.x},${r.y},${r.w},${r.h}`).join(";");
    if (refused.length === 0) this.send(ws, { t: "claimed", id, ok: true });
    else this.send(ws, { t: "claimed", id, ok: false, why: "your neighbor already holds that land", refused: enc(refused) });
    if (granted.length > 0) {
      for (const peer of this.socketsFor(1 - a.seat)) this.send(peer, { t: "claims", seat: a.seat, rects: enc(granted) });
    }
  }

  async onRelease(ws, a, msg) {
    const rects = this.parseRects(msg.rects);
    if (!rects) return this.fail(ws, "bad release", false);
    const mine = String(a.seat);
    await this.eachTile(rects, true, (b, off) => { if (b[off] === mine) b[off] = "."; return true; });
    for (const peer of this.socketsFor(1 - a.seat)) this.send(peer, { t: "released", seat: a.seat, rects: msg.rects });
  }

  // Everything the neighbour holds, for a town that just joined: one message per block.
  async sendClaims(ws, seat) {
    const other = String(1 - seat);
    const all = await this.store.list({ prefix: "claims:" });
    for (const [key, v] of all) {
      if (!v.includes(other)) continue;
      const [, bx, by] = key.split(":");
      this.send(ws, { t: "claimBlock", bx: Number(bx), by: Number(by), size: kClaimBlock, owner: v.replace(new RegExp(`[^${other}]`, "g"), ".") });
    }
  }

  // ---- mirrored buildings ----------------------------------------------------------------------

  async onBuild(ws, a, msg) {
    const id = typeof msg.id === "string" && kBuildingId.test(msg.id) ? msg.id : "";
    if (!id || typeof msg.tmpl !== "string" || msg.tmpl.length < 1 || msg.tmpl.length > kMaxPathChars) {
      return this.fail(ws, "bad building", false);
    }
    const rec = { id, tmpl: msg.tmpl };
    for (const k of ["x", "y", "z", "rot", "tx", "ty", "tw", "th", "built"]) {
      if (!Number.isInteger(msg[k]) || Math.abs(msg[k]) > 10_000_000) return this.fail(ws, `bad building ${k}`, false);
      rec[k] = msg[k];
    }
    rec.grp = Number.isInteger(msg.grp) && msg.grp >= 0 && msg.grp < 64 ? msg.grp : 0;   // the model variant
    // M3: which footprint tiles are solid (row by row, "1" solid, "0" walkable); citizens walk round them.
    if (typeof msg.bm === "string" && msg.bm.length === rec.tw * rec.th && msg.bm.length <= 4096 && /^[01]*$/.test(msg.bm)) rec.bm = msg.bm;
    // Its ground (a stockpile's yard, a field's soil): the footprint as the game holds it, rotation, decal state.
    if (typeof msg.dec === "string" && /^-?\d{1,7}(,-?\d{1,7}){5}$/.test(msg.dec)) rec.dec = msg.dec;
    // Not on the neighbour's land: the town's claim for it (sent just before) was refused, so both towns
    // placed there at once and the neighbour won. Mirrored, it would stand inside the neighbour's own.
    if (rec.tw > 0 && rec.th > 0 && rec.tw * rec.th <= 4096) {
      const theirs = String(1 - a.seat);
      let clear = true;
      await this.eachTile([{ x: rec.tx, y: rec.ty, w: rec.tw, h: rec.th }], false, (b, off) => {
        if (b[off] === theirs) { clear = false; return false; }
        return true;
      });
      if (!clear) {
        this.send(ws, { t: "buildRefused", id, why: "your neighbor holds that land" });
        return;
      }
    }
    const key = `mirror:${a.seat}`;
    const all = (await this.store.get(key)) || {};
    if (!(id in all) && Object.keys(all).length >= kMaxBuildings) return this.fail(ws, "too many buildings to mirror", false);
    all[id] = rec;
    await this.store.put(key, all);
    for (const peer of this.socketsFor(1 - a.seat)) this.send(peer, { t: "build", ...rec });
  }

  async onUnbuild(ws, a, msg) {
    const id = typeof msg.id === "string" && kBuildingId.test(msg.id) ? msg.id : "";
    if (!id) return this.fail(ws, "bad building", false);
    const key = `mirror:${a.seat}`;
    const all = (await this.store.get(key)) || {};
    if (id in all) { delete all[id]; await this.store.put(key, all); }
    for (const peer of this.socketsFor(1 - a.seat)) this.send(peer, { t: "unbuild", id });
  }

  // ---- shared roads (M3) ------------------------------------------------------------------------

  async onRoad(ws, a, msg) {
    const cell = msg.cell, type = msg.type, tt = msg.tt;
    if (!Number.isInteger(cell) || cell < 0 || cell >= kMaxCell) return this.fail(ws, "bad road", false);
    const removed = msg.off === true;
    if (!removed && (!Number.isInteger(type) || type < 0 || type > 254 || !Number.isInteger(tt) || tt < 0 || tt > 31 ||
        typeof msg.tmpl !== "string" || msg.tmpl.length > kMaxPathChars ||
        (msg.mat !== undefined && (typeof msg.mat !== "string" || msg.mat.length > kMaxPathChars)))) {
      return this.fail(ws, "bad road", false);
    }
    // The road's material (a mod's road type the neighbour's game may not know the template of).
    const mat = typeof msg.mat === "string" ? msg.mat : "";
    const key = `roads:${a.seat}`;
    const all = (await this.store.get(key)) || {};
    if (removed) {
      // Only a town's own road comes up: a town that took up the neighbour's road in its copy (the
      // Remove tool over it) must not take it up in the neighbour's game too.
      if (!(cell in all)) return;
      delete all[cell];
    } else {
      if (!(cell in all) && Object.keys(all).length >= kMaxRoadCells) return this.fail(ws, "too many road tiles", false);
      all[cell] = mat ? [type, tt, msg.tmpl, mat] : [type, tt, msg.tmpl];
    }
    await this.store.put(key, all);
    await this.noteRoadsOff(a.seat, [cell], removed);
    const out = removed ? { t: "road", cell, off: true } : { t: "road", cell, type, tt, tmpl: msg.tmpl };
    if (!removed && mat) out.mat = mat;
    const peers = this.socketsFor(1 - a.seat);
    for (const peer of peers) this.send(peer, out);
    // An absent neighbour keeps our roads in its save as real roads: one taken up while it is away must
    // reach it when it comes back (a road laid again meanwhile cancels that).
    if (peers.length === 0) await this.queueRoadOffs(1 - a.seat, [cell], removed);
  }

  // Every road tile a town has taken up (and not laid again), kept for good (roadoff:<seat>): a town that
  // loads a save older than the removal still has that road in its copy, and gets the removal again.
  async noteRoadsOff(seat, cells, off) {
    const key = `roadoff:${seat}`;
    const set = new Set((await this.store.get(key)) || []);
    const before = set.size;
    for (const c of cells) {
      if (off) set.add(c);
      else set.delete(c);
    }
    if (set.size === before) return;
    const list = [...set];
    while (list.length > kMaxRoadCells) list.shift();
    await this.store.put(key, list);
  }

  // For a town joining with a new game or a loaded save: the neighbour's removed roads (replay: the
  // town's copy may not have them any more), those it has not laid again since.
  async sendRoadsOff(ws, seat) {
    const removed = (await this.store.get(`roadoff:${1 - seat}`)) || [];
    const standing = (await this.store.get(`roads:${1 - seat}`)) || {};
    // In batches of cells (one message a tile could run past one call's sends).
    const off = removed.filter((cell) => !(cell in standing));
    for (let i = 0; i < off.length; i += 1800) this.send(ws, { t: "roadsOff", cells: off.slice(i, i + 1800).join(","), replay: true });
  }

  // The road tiles taken up while `seat` was away, sent to it on joining after the roads that stand.
  // `add` true queues the cells, false takes them out of the queue (the road is back).
  async queueRoadOffs(seat, cells, add = true) {
    const key = `roadq:${seat}`;
    const q = new Set((await this.store.get(key)) || []);
    const before = q.size;
    for (const c of cells) {
      if (add) q.add(c);
      else q.delete(c);
    }
    if (!add && q.size === before) return;
    const list = [...q];
    while (list.length > kMaxRoadCells) list.shift();
    await this.store.put(key, list);
  }

  async sendRoadQueue(ws, seat) {
    const key = `roadq:${seat}`;
    const q = (await this.store.get(key)) || [];
    if (q.length === 0) return;
    // A road the neighbour holds now (laid again) is not taken up.
    const standing = (await this.store.get(`roads:${1 - seat}`)) || {};
    const off = q.filter((cell) => !(cell in standing));
    for (let i = 0; i < off.length; i += 1800) this.send(ws, { t: "roadsOff", cells: off.slice(i, i + 1800).join(",") });
    await this.store.delete(key);
  }

  async sendRoads(ws, seat) {
    const all = (await this.store.get(`roads:${1 - seat}`)) || {};
    // In batches ("roadBatch"): the templates and materials once each, then "cell,type,tt,template,material"
    // a tile (material -1: none). 500 tiles keep a batch well under the game's 16 KB frame.
    const rows = Object.entries(all);
    for (let i = 0; i < rows.length; i += 500) {
      const tmpls = new Map();
      const mats = new Map();
      const idx = (m, v) => {
        let k = m.get(v);
        if (k === undefined) {
          k = m.size;
          m.set(v, k);
        }
        return k;
      };
      const parts = rows.slice(i, i + 500).map(([cell, [type, tt, tmpl, mat]]) =>
        `${cell},${type},${tt},${idx(tmpls, tmpl)},${mat ? idx(mats, mat) : -1}`);
      this.send(ws, { t: "roadBatch", tmpls: [...tmpls.keys()].join("\n"), mats: [...mats.keys()].join("\n"), list: parts.join(";") });
    }
  }

  // ---- shared natural resources (M3) -------------------------------------------------------------

  async onNat(ws, a, msg) {
    const cell = msg.cell;
    if (!Number.isInteger(cell) || cell < 0 || cell >= kMaxCell || (msg.op !== "rm" && msg.op !== "add") ||
        typeof msg.tmpl !== "string" || msg.tmpl.length > kMaxPathChars) {
      return this.fail(ws, "bad resource change", false);
    }
    const ev = { t: "nat", op: msg.op, cell, tmpl: msg.tmpl };
    // A new plant's look (its model variant), so both copies draw the same tree: kept and passed on.
    const look = msg.op === "add" && Number.isInteger(msg.grp) && Number.isInteger(msg.var) &&
                 msg.grp >= 0 && msg.grp < 64 && msg.var >= 0 && msg.var < 64 ? [msg.grp, msg.var] : null;
    if (look) { ev.grp = look[0]; ev.var = look[1]; }
    await this.logNat(a.seat, msg.op, cell, msg.tmpl, look);
    const peers = this.socketsFor(1 - a.seat);
    if (peers.length > 0) { for (const peer of peers) this.send(peer, ev); return; }
    const key = `natq:${1 - a.seat}`;
    const q = (await this.store.get(key)) || [];
    q.push(look ? [ev.op === "rm" ? 0 : 1, cell, ev.tmpl, look[0], look[1]] : [ev.op === "rm" ? 0 : 1, cell, ev.tmpl]);
    while (q.length > kMaxNatQueue) q.shift();
    await this.store.put(key, q);
  }

  // A tile's record: [code, grp, var, stamp]: the op and template as one number (op + 2 * template
  // index), a new plant's look (-1 none) and when the change came (natStamp). Records from before stamps
  // are the code alone or [code, grp, var] and count as oldest.
  async logNat(seat, op, cell, tmpl, look = null) {
    const key = `natlog:${seat}:${cell >> kNatShardBits}`;
    const shard = (await this.store.get(key)) || { t: [], c: {} };
    let i = shard.t.indexOf(tmpl);
    if (i < 0) {
      shard.t.push(tmpl);
      i = shard.t.length - 1;
    }
    const code = (op === "rm" ? 0 : 1) + 2 * i;
    shard.c[cell] = [code, look ? look[0] : -1, look ? look[1] : -1, this.natStamp()];
    await this.store.put(key, shard);
  }

  // When a change came, in microseconds: increasing within this object's life (a Worker's clock stands
  // still between I/O, so changes in one millisecond count up from it) and past every stamp of an earlier
  // life (its clock has moved on).
  natStamp() {
    const now = Date.now() * 1000;
    this.lastNatStamp = now > (this.lastNatStamp || 0) ? now : this.lastNatStamp + 1;
    return this.lastNatStamp;
  }

  // (natBatch, roadBatch and roadsOff are newer than protocol 2's one-a-tile messages: a game older than
  // them misses what they carry, so both towns and the relay are updated together.)
  // Resource changes in few messages ("natBatch"): a long record sent one message a tile ran a joining
  // town's hello past what one Durable Object call can do (tens of thousands of sends) and the line lost
  // its seat. A batch: its templates once ("tmpls", one per line), then "cell,op,template[,grp,var]" a
  // change, op 0 removed / 1 planted. kNatBatch changes keep a batch well under the game's 16 KB frame.
  sendNatBatches(ws, entries, replay) {
    const kBatchChars = 12000;   // the encoded batch, well under the game's 16 KB frame
    let tmpls = new Map();
    let parts = [];
    let chars = 0;
    const out = () => {
      if (parts.length === 0) return;
      const ev = { t: "natBatch", tmpls: [...tmpls.keys()].join("\n"), list: parts.join(";") };
      if (replay) ev.replay = true;
      this.send(ws, ev);
      tmpls = new Map();
      parts = [];
      chars = 0;
    };
    for (const [cell, op, tmpl, grp, vr] of entries) {
      const newTmpl = !tmpls.has(tmpl);
      const entryChars = 24 + (newTmpl ? tmpl.length + 1 : 0);
      if (chars + entryChars > kBatchChars) out();
      let k = tmpls.get(tmpl);
      if (k === undefined) {
        k = tmpls.size;
        tmpls.set(tmpl, k);
        chars += tmpl.length + 1;
      }
      parts.push(Number.isInteger(grp) && Number.isInteger(vr) ? `${cell},${op},${k},${grp},${vr}` : `${cell},${op},${k}`);
      chars += 24;
    }
    out();
  }

  // Both towns' whole record of resource changes (replay: the game counts no refusal for a tile that is
  // already as the record says). Returns how many.
  async sendNatLog(ws) {
    // The newest change per tile and template, whichever town made it. On a tie (records from before
    // stamps) the removal: a plant is spread or planted before it is felled, far more often than felled
    // and planted again on the same tile.
    const newest = new Map();
    for (const [, shard] of await this.store.list({ prefix: "natlog:" })) {
      for (const [cell, rec] of Object.entries(shard.c)) {
        const r = Array.isArray(rec) ? rec : [rec];
        const tmpl = shard.t[r[0] >> 1];
        if (typeof tmpl !== "string") continue;
        const stamp = Number.isFinite(r[3]) ? r[3] : 0;
        const key = `${cell}\n${tmpl}`;
        const had = newest.get(key);
        if (had && (had.stamp > stamp || (had.stamp === stamp && had.op === 0))) continue;
        newest.set(key, { cell: Number(cell), op: r[0] & 1, tmpl, grp: r[1], vr: r[2], stamp });
      }
    }
    const entries = [];
    for (const e of newest.values()) {
      const look = Number.isInteger(e.grp) && e.grp >= 0 && Number.isInteger(e.vr) && e.vr >= 0;
      entries.push(look ? [e.cell, e.op, e.tmpl, e.grp, e.vr] : [e.cell, e.op, e.tmpl]);
    }
    this.sendNatBatches(ws, entries, true);
    return entries.length;
  }

  async sendNatQueue(ws, seat) {
    const key = `natq:${seat}`;
    const q = (await this.store.get(key)) || [];
    this.sendNatBatches(ws, q.map(([op, cell, tmpl, grp, vr]) => [cell, op, tmpl, grp, vr]), false);
    if (q.length > 0) await this.store.delete(key);
  }

  // A town starting a new game in its seat (hello `fresh`): what its earlier game left goes -- its
  // buildings, roads and land, and the resource changes still waiting for the neighbour -- and an
  // online neighbour is told, so nothing of the old town stays on the neighbour's map. A town that
  // loads a save (hello `reload`) is reset the same way except the resource queue (keepNatQueue), and
  // sends its buildings, roads and land again on joining: the save may be older than the seat.
  async resetSeat(seat, keepNatQueue = false) {
    const peers = this.socketsFor(1 - seat);
    const mine = String(seat);
    const mirror = (await this.store.get(`mirror:${seat}`)) || {};
    for (const id of Object.keys(mirror)) for (const p of peers) this.send(p, { t: "unbuild", id });
    const roads = (await this.store.get(`roads:${seat}`)) || {};
    for (const cell of Object.keys(roads)) for (const p of peers) this.send(p, { t: "road", cell: Number(cell), off: true });
    await this.noteRoadsOff(seat, Object.keys(roads).map(Number), true);
    if (peers.length === 0 && Object.keys(roads).length > 0) await this.queueRoadOffs(1 - seat, Object.keys(roads).map(Number));
    const blocks = await this.store.list({ prefix: "claims:" });
    let batch = {};
    for (const [key, v] of blocks) {
      if (!v.includes(mine)) continue;
      batch[key] = v.split(mine).join(".");
      const [, bx, by] = key.split(":");
      const rects = `${Number(bx) * kClaimBlock},${Number(by) * kClaimBlock},${kClaimBlock},${kClaimBlock}`;
      for (const p of peers) this.send(p, { t: "released", seat, rects });
      if (Object.keys(batch).length === 100) {   // storage.put takes at most 128 keys at once
        await this.store.put(batch);
        batch = {};
      }
    }
    if (Object.keys(batch).length > 0) await this.store.put(batch);
    await this.store.delete(keepNatQueue ? [`mirror:${seat}`, `roads:${seat}`]
                                               : [`mirror:${seat}`, `roads:${seat}`, `natq:${1 - seat}`]);
  }

  async sendMirror(ws, seat) {
    const all = (await this.store.get(`mirror:${1 - seat}`)) || {};
    for (const rec of Object.values(all)) this.send(ws, { t: "build", ...rec });
    this.send(ws, { t: "mirrorEnd", count: Object.keys(all).length });
  }

  async onHash(ws, a, msg) {
    if (typeof msg.hash !== "string" || !/^[0-9a-fA-F]{1,32}$/.test(msg.hash)) return this.fail(ws, "bad world hash", false);
    const meta = await this.meta();
    meta.hashes = meta.hashes || [null, null];
    const mine = msg.hash.toLowerCase();
    const other = meta.hashes[1 - a.seat];
    if (other && other !== mine) {
      // Another land: refused (the room's land is the one the other town already built).
      const peer = meta.seats[1 - a.seat] || "your neighbor";
      this.send(ws, { t: "hashCheck", same: false, mine, theirs: other });
      for (const p of this.socketsFor(1 - a.seat)) this.send(p, { t: "hashCheck", same: false, mine: other, theirs: mine });
      return this.fail(ws, `your map is not the same land as ${peer}'s: the two games built different worlds ` +
        "from the same seed, so nothing shared would line up. This town cannot play in this room.", true);
    }
    meta.hashes[a.seat] = mine;
    await this.store.put("meta", meta);
    if (!other) return;   // the neighbour has not built its map yet; it will be told when it does
    const same = true;
    const report = (seat) => ({ t: "hashCheck", same, mine: meta.hashes[seat], theirs: meta.hashes[1 - seat] });
    this.send(ws, report(a.seat));
    for (const peer of this.socketsFor(1 - a.seat)) this.send(peer, report(1 - a.seat));
  }

  async onHello(ws, a, msg) {
    if (a.seat >= 0) return this.fail(ws, "already joined", false);
    if (msg.v !== kProtocolVersion) return this.fail(ws, `this relay speaks protocol ${kProtocolVersion}; update the game or the relay`, true);
    const name = clean(msg.name, kMaxNameChars);
    const pass = typeof msg.pass === "string" ? msg.pass.slice(0, kMaxPassChars) : "";
    if (!name) return this.fail(ws, "set neighbour.name in settings.cfg", true);

    const meta = await this.meta();
    meta.tokens = meta.tokens || new Array(kSeats).fill(null);
    meta.compat = meta.compat || new Array(kSeats).fill(null);
    if (msg.join === true && !meta.created) return this.fail(ws, `there is no room called "${a.room}"`, true);
    const hash = await passwordHash(a.room, pass);
    if (meta.passHash === null) meta.passHash = hash;              // the first town sets it
    else if (meta.passHash !== hash) return this.fail(ws, "wrong room password", true);
    const read = readCompat(msg);
    if (read.why) return this.fail(ws, read.why, true);

    // The seat: by the town's token, never by its name (interop spec layer 0).
    let seat = -1;
    if (typeof msg.token === "string" && kTokenHex.test(msg.token)) {
      seat = meta.tokens.indexOf(await tokenHash(a.room, msg.token));   // a town coming back
    }
    let issue = false;
    if (seat < 0) {
      // A room from before seat tokens knows its towns by name: each named seat is handed its token once.
      seat = meta.seats.findIndex((n, i) => n === name && meta.tokens[i] === null);
      if (seat < 0) seat = meta.seats.findIndex((n, i) => n === null && meta.tokens[i] === null);   // a new town
      if (seat < 0) return this.fail(ws, `room "${a.room}" already has two towns`, true);
      issue = true;
    }
    const other = 1 - seat;
    const otherName = meta.seats[other] || "your neighbor";
    const theirs = meta.compat[other];
    const verdict = theirs ? compareCompat(read.compat, theirs, otherName) : { refuse: null, warn: [] };
    if (verdict.refuse) return this.fail(ws, verdict.refuse, true);
    let token = null;
    if (issue) {
      token = randomToken();
      meta.tokens[seat] = await tokenHash(a.room, token);
    }
    meta.seats[seat] = name;
    meta.compat[seat] = read.compat;
    await this.store.put("meta", meta);

    // The same town connecting again (a reconnect): the older line goes (a probe leaves the game's be).
    const probe = msg.probe === true;
    if (!probe) {
      for (const old of this.socketsFor(seat)) {
        if (old !== ws) { try { old.close(4001, "replaced by a newer connection"); } catch { /* gone */ } }
      }
    }
    a.seat = seat;
    a.name = name;
    a.probe = probe;
    ws.serializeAttachment(a);
    if (msg.fresh === true) await this.resetSeat(seat);   // a new game in this seat: the old town goes
    // A loaded game (perhaps an older save than what the seat holds): the same, but the neighbour's
    // resource changes waiting for this town stay; the town sends its buildings, roads and land again.
    else if (msg.reload === true) await this.resetSeat(seat, true);

    const peerName = meta.seats[other] || "";
    const peerOnline = this.socketsFor(other).length > 0;
    const welcome = { t: "welcome", seat, peer: peerName, online: peerOnline, created: !!meta.created };
    if (token) welcome.token = token;   // keep it: the next hello sends it back
    if (meta.created) {
      welcome.seed = meta.seed;
      welcome.creator = meta.creator;
      for (const k of kSettingKeys) welcome[k] = meta.settings[k];
    }
    this.send(ws, welcome);
    await this.publish(a.room, meta);
    // What only warns (another build, other mods): told to both towns.
    for (const w of verdict.warn) this.send(ws, { t: "compat", warn: w });
    if (theirs && verdict.warn.length) {
      const back = compareCompat(theirs, read.compat, name);
      for (const peer of this.socketsFor(other)) for (const w of back.warn) this.send(peer, { t: "compat", warn: w });
    }

    // What it missed: the neighbour's lines newer than the last one it saw.
    const lastSeq = Number.isInteger(msg.lastSeq) && msg.lastSeq > 0 ? msg.lastSeq : 0;
    // At most kReplayChats of them: a long absence must not flood the event log.
    const chats = (await this.store.get("chats")) || [];
    const missed = chats.filter((c) => c.seq > lastSeq && c.seat !== seat).slice(-kReplayChats);
    for (const c of missed) this.send(ws, { t: "chat", from: c.from, text: c.text, seq: c.seq });
    if (!probe) for (const peer of this.socketsFor(other)) this.send(peer, { t: "peer", name, online: true });

    // Parcels that waited for this town.
    for (const p of await this.parcels()) {
      if (p.to === seat) this.send(ws, { t: "parcel", id: p.id, kind: p.kind, body: p.body, from: p.from });
    }
    // The neighbour's land and buildings (a shared-map room).
    if (meta.created) {
      await this.sendClaims(ws, seat);
      await this.sendMirror(ws, seat);
      await this.sendRoads(ws, seat);
      await this.sendRoadQueue(ws, seat);
      await this.sendNatQueue(ws, seat);
      // A new game or a loaded save in this seat: its copy of the land may lack changes it once had.
      if (msg.fresh === true || msg.reload === true) {
        await this.sendNatLog(ws);
        await this.sendRoadsOff(ws, seat);
      }
    }
  }

  // ---- parcels: goods and families. Kept until the receiving town acknowledges them. ----------

  async parcels() {
    return (await this.store.get("parcels")) || [];
  }

  async onParcel(ws, a, msg) {
    const id = typeof msg.id === "string" ? msg.id : "";
    if (!kParcelId.test(id)) return this.fail(ws, "bad parcel id", false);
    if (msg.kind !== "goods" && msg.kind !== "family") return this.fail(ws, "unknown parcel kind", false);
    if (typeof msg.body !== "string" || msg.body.length === 0 || msg.body.length > kMaxParcelBody) {
      return this.fail(ws, "bad parcel contents", false);
    }
    const list = await this.parcels();
    const to = 1 - a.seat;
    // A parcel sent again (the sender reconnected before it heard "stored"): stored already.
    if (!list.some((p) => p.id === id && p.fromSeat === a.seat)) {
      if (list.filter((p) => p.to === to).length >= kMaxParcels) {
        return this.fail(ws, "too many parcels are waiting for your neighbor", false);
      }
      const entry = { id, kind: msg.kind, body: msg.body, from: a.name, fromSeat: a.seat, to };
      list.push(entry);
      await this.store.put("parcels", list);
      for (const peer of this.socketsFor(to)) this.send(peer, { t: "parcel", id, kind: entry.kind, body: entry.body, from: a.name });
    }
    this.send(ws, { t: "stored", id });
  }

  async onAck(ws, a, msg) {
    const id = typeof msg.id === "string" ? msg.id : "";
    const list = await this.parcels();
    const keep = list.filter((p) => !(p.id === id && p.to === a.seat));
    if (keep.length !== list.length) await this.store.put("parcels", keep);
  }

  async onChat(ws, a, msg) {
    const text = clean(msg.text, kMaxChatChars);
    if (!text) return;
    const meta = await this.meta();
    meta.seq += 1;
    const entry = { seq: meta.seq, from: a.name, seat: a.seat, text };
    const chats = (await this.store.get("chats")) || [];
    chats.push(entry);
    while (chats.length > kKeepChats) chats.shift();
    await this.store.put({ meta, chats });
    for (const peer of this.socketsFor(1 - a.seat)) this.send(peer, { t: "chat", from: a.name, text, seq: entry.seq });
  }

  async webSocketClose(ws, code, reason) {
    await this.left(ws);
    try { ws.close(code, reason); } catch { /* already closed */ }
  }

  async webSocketError(ws) {
    await this.left(ws);
  }

  async left(ws) {
    const a = ws.deserializeAttachment();
    if (!a || a.seat < 0 || a.probe) return;
    // Still connected on another line (a reconnect replaced this one): not a departure.
    if (this.socketsFor(a.seat).some((s) => s !== ws)) return;
    for (const peer of this.socketsFor(1 - a.seat)) this.send(peer, { t: "peer", name: a.name, online: false });
    await this.publish(a.room, await this.meta());
  }
}

// ---------------------------------------------------------------------------------------------
// The Directory: one Durable Object holding the public list of created rooms.
//   internal (from Room objects): POST /update {room entry}, POST /allow {ip}
//   public (the game's lobby, WebSocket /lobby): {"t":"list"} -> {"t":"room", ...} per room,
//   newest activity first, then {"t":"roomsEnd", "count": n}
// ---------------------------------------------------------------------------------------------
export class Directory {
  constructor(ctx, env) {
    this.ctx = ctx;
    this.env = env;
    this.ctx.setWebSocketAutoResponse(new WebSocketRequestResponsePair('{"t":"ping"}', '{"t":"pong"}'));
  }

  async rooms() {
    const all = (await this.ctx.storage.get("rooms")) || {};
    const cutoff = Date.now() - kRoomIdleDays * kDayMs;
    let changed = false;
    for (const [name, r] of Object.entries(all)) {
      if (!r || r.lastActive < cutoff) { delete all[name]; changed = true; }
    }
    if (changed) await this.ctx.storage.put("rooms", all);
    return all;
  }

  async fetch(request) {
    const url = new URL(request.url);
    if (request.method === "POST" && url.pathname === "/update") {
      const e = await request.json();
      if (!e || typeof e.name !== "string") return new Response("bad", { status: 400 });
      const all = await this.rooms();
      all[e.name] = e;
      const names = Object.keys(all);
      if (names.length > kMaxListedRooms) {   // drop the longest idle
        names.sort((x, y) => all[x].lastActive - all[y].lastActive);
        for (const n of names.slice(0, names.length - kMaxListedRooms)) delete all[n];
      }
      await this.ctx.storage.put("rooms", all);
      return new Response("ok");
    }
    if (request.method === "POST" && url.pathname === "/allow") {
      const { ip } = await request.json();
      const key = `creates:${ip || "?"}`;
      const now = Date.now();
      const recent = ((await this.ctx.storage.get(key)) || []).filter((t) => t > now - kDayMs);
      if (recent.length >= kCreatesPerDay) return new Response("limit", { status: 429 });
      recent.push(now);
      await this.ctx.storage.put(key, recent);
      return new Response("ok");
    }
    if (!isWebSocketUpgrade(request)) return new Response("not found", { status: 404 });
    const pair = new WebSocketPair();
    const [client, server] = Object.values(pair);
    this.ctx.acceptWebSocket(server);
    server.serializeAttachment({ window: 0, count: 0 });
    return new Response(null, { status: 101, webSocket: client });
  }

  async webSocketMessage(ws, raw) {
    const a = ws.deserializeAttachment() || { window: 0, count: 0 };
    const now = Date.now();
    if (now - a.window > kWindowMs) { a.window = now; a.count = 0; }
    if (++a.count > kBurst) { ws.serializeAttachment(a); return; }
    ws.serializeAttachment(a);
    let msg;
    try { msg = JSON.parse(raw); } catch { return; }
    if (!msg || msg.t !== "list") return;
    const all = Object.values(await this.rooms()).sort((x, y) => y.lastActive - x.lastActive);
    for (const r of all) {
      const out = { t: "room", name: r.name, creator: r.creator, seats: r.seats, lastActive: Math.floor(r.lastActive / 1000) };
      for (const k of kSettingKeys) out[k] = r[k];
      try { ws.send(JSON.stringify(out)); } catch { return; }
    }
    try { ws.send(JSON.stringify({ t: "roomsEnd", count: all.length })); } catch { /* gone */ }
  }

  async webSocketClose(ws, code, reason) {
    try { ws.close(code, reason); } catch { /* closed */ }
  }
}
