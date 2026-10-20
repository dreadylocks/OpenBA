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
// order gets an {"t":"error"} and, for a bad hello, the line is closed.

const kProtocolVersion = 1;
const kMaxFrameBytes = 16 * 1024;
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
const kCreatesPerDay = 5;
const kDayMs = 24 * 60 * 60 * 1000;
// The new-game settings a room carries, besides the seed: the map size row and the disasters
// switch as small whole numbers, and the start condition, terrain and climate as the game's
// resource paths (the same data on both PCs resolves them to the same generators).
const kIntSettings = ["size", "disasters"];
const kPathSettings = ["start", "terrain", "climate"];
const kSettingKeys = [...kIntSettings, ...kPathSettings];
const kMaxPathChars = 160;
// Flood guard: at most this many messages per socket in kWindowMs.
const kBurst = 30;
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
      if (request.headers.get("Upgrade") !== "websocket") return new Response("expected a WebSocket", { status: 426 });
      const stub = env.ROOMS.get(env.ROOMS.idFromName(room));
      const fwd = new Request(request);
      fwd.headers.set("X-Neighbour-Room", room);
      fwd.headers.set("X-Neighbour-Ip", request.headers.get("CF-Connecting-IP") || "local");
      return stub.fetch(fwd);
    }
    if (url.pathname === "/lobby" || url.pathname === "/lobby/") {
      if (request.headers.get("Upgrade") !== "websocket") return new Response("expected a WebSocket", { status: 426 });
      return env.DIRECTORY.get(env.DIRECTORY.idFromName("directory")).fetch(request);
    }
    if (url.pathname === "/") return new Response("OpenBA neighbour relay: connect to /room/<name>\n");
    return new Response("not found", { status: 404 });
  },
};

function clean(text, max) {
  if (typeof text !== "string") return "";
  return text.replace(/[\u0000-\u001f\u007f]/g, " ").replace(/\s+/g, " ").trim().slice(0, max);
}

async function passwordHash(room, pass) {
  const bytes = new TextEncoder().encode(`openba-neighbour:${room}:${pass}`);
  const digest = await crypto.subtle.digest("SHA-256", bytes);
  return [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

export class Room {
  constructor(ctx, env) {
    this.ctx = ctx;
    this.env = env;
    // Pings are answered without waking the object (the hibernation API's auto-response).
    this.ctx.setWebSocketAutoResponse(new WebSocketRequestResponsePair('{"t":"ping"}', '{"t":"pong"}'));
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
    return (await this.ctx.storage.get("meta")) || { passHash: null, seats: new Array(kSeats).fill(null), seq: 0 };
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

  socketsFor(seat) {
    return this.ctx.getWebSockets().filter((ws) => {
      const a = ws.deserializeAttachment();
      return a && a.seat === seat;
    });
  }

  send(ws, msg) {
    try { ws.send(JSON.stringify(msg)); } catch { /* the line is gone */ }
  }

  fail(ws, why, close) {
    this.send(ws, { t: "error", why });
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
    if (a.seat < 0) return this.fail(ws, "say hello first", true);
    if (msg.t === "chat") return this.onChat(ws, a, msg);
    if (msg.t === "parcel") return this.onParcel(ws, a, msg);
    if (msg.t === "ack") return this.onAck(ws, a, msg);
    if (msg.t === "hash") return this.onHash(ws, a, msg);
    if (msg.t === "ping") return this.send(ws, { t: "pong" });
    // Unknown types from a newer game: ignored.
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
      if (typeof msg[k] !== "string" || msg[k].length < 1 || msg[k].length > kMaxPathChars || /[ -]/.test(msg[k])) {
        return this.fail(ws, `bad setting ${k}`, true);
      }
      settings[k] = msg[k];
    }
    // The creation allowance (per internet address, per day).
    let allowed = true;
    try {
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
    await this.ctx.storage.put("meta", meta);
    // Now an ordinary join as the room's first town.
    return this.onHello(ws, a, { ...msg, t: "hello", lastSeq: 0 });
  }

  async onHash(ws, a, msg) {
    if (typeof msg.hash !== "string" || !/^[0-9a-fA-F]{1,32}$/.test(msg.hash)) return this.fail(ws, "bad world hash", false);
    const meta = await this.meta();
    meta.hashes = meta.hashes || [null, null];
    meta.hashes[a.seat] = msg.hash.toLowerCase();
    await this.ctx.storage.put("meta", meta);
    const other = meta.hashes[1 - a.seat];
    if (!other) return;   // the neighbour has not built its map yet; it will be told when it does
    const same = other === meta.hashes[a.seat];
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
    if (msg.join === true && !meta.created) return this.fail(ws, `there is no room called "${a.room}"`, true);
    const hash = await passwordHash(a.room, pass);
    if (meta.passHash === null) meta.passHash = hash;              // the first town sets it
    else if (meta.passHash !== hash) return this.fail(ws, "wrong room password", true);

    let seat = meta.seats.indexOf(name);                            // a town coming back
    if (seat < 0) seat = meta.seats.indexOf(null);                  // a new town
    if (seat < 0) return this.fail(ws, `room "${a.room}" already has two towns`, true);
    meta.seats[seat] = name;
    await this.ctx.storage.put("meta", meta);

    // The same town connecting again (a reconnect): the older line goes.
    for (const old of this.socketsFor(seat)) {
      if (old !== ws) { try { old.close(4001, "replaced by a newer connection"); } catch { /* gone */ } }
    }
    a.seat = seat;
    a.name = name;
    ws.serializeAttachment(a);

    const other = 1 - seat;
    const peerName = meta.seats[other] || "";
    const peerOnline = this.socketsFor(other).length > 0;
    const welcome = { t: "welcome", seat, peer: peerName, online: peerOnline, created: !!meta.created };
    if (meta.created) {
      welcome.seed = meta.seed;
      welcome.creator = meta.creator;
      for (const k of kSettingKeys) welcome[k] = meta.settings[k];
    }
    this.send(ws, welcome);
    await this.publish(a.room, meta);

    // What it missed: the neighbour's lines newer than the last one it saw.
    const lastSeq = Number.isInteger(msg.lastSeq) && msg.lastSeq > 0 ? msg.lastSeq : 0;
    // At most kReplayChats of them: a long absence must not flood the event log.
    const chats = (await this.ctx.storage.get("chats")) || [];
    const missed = chats.filter((c) => c.seq > lastSeq && c.seat !== seat).slice(-kReplayChats);
    for (const c of missed) this.send(ws, { t: "chat", from: c.from, text: c.text, seq: c.seq });
    for (const peer of this.socketsFor(other)) this.send(peer, { t: "peer", name, online: true });

    // Parcels that waited for this town.
    for (const p of await this.parcels()) {
      if (p.to === seat) this.send(ws, { t: "parcel", id: p.id, kind: p.kind, body: p.body, from: p.from });
    }
  }

  // ---- parcels: goods and families. Kept until the receiving town acknowledges them. ----------

  async parcels() {
    return (await this.ctx.storage.get("parcels")) || [];
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
        return this.fail(ws, "too many parcels are waiting for your neighbour", false);
      }
      const entry = { id, kind: msg.kind, body: msg.body, from: a.name, fromSeat: a.seat, to };
      list.push(entry);
      await this.ctx.storage.put("parcels", list);
      for (const peer of this.socketsFor(to)) this.send(peer, { t: "parcel", id, kind: entry.kind, body: entry.body, from: a.name });
    }
    this.send(ws, { t: "stored", id });
  }

  async onAck(ws, a, msg) {
    const id = typeof msg.id === "string" ? msg.id : "";
    const list = await this.parcels();
    const keep = list.filter((p) => !(p.id === id && p.to === a.seat));
    if (keep.length !== list.length) await this.ctx.storage.put("parcels", keep);
  }

  async onChat(ws, a, msg) {
    const text = clean(msg.text, kMaxChatChars);
    if (!text) return;
    const meta = await this.meta();
    meta.seq += 1;
    const entry = { seq: meta.seq, from: a.name, seat: a.seat, text };
    const chats = (await this.ctx.storage.get("chats")) || [];
    chats.push(entry);
    while (chats.length > kKeepChats) chats.shift();
    await this.ctx.storage.put({ meta, chats });
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
    if (!a || a.seat < 0) return;
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
    if (request.headers.get("Upgrade") !== "websocket") return new Response("not found", { status: 404 });
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
