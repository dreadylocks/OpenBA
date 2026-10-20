// The neighbour-towns relay: a Cloudflare Worker with one Durable Object per room.
// docs/fork/NEIGHBOUR_TOWNS.md §3. The game's side is src/ext/neighbour/ (protocol.h has the
// message list). A room has two seats and a password; the first town in sets the password.
// Chat lines are numbered and kept (the last kKeepChats), so a town that was away gets what
// it missed when it comes back (its hello carries the last number it saw).
//
//   GET /room/<name>   (WebSocket upgrade)   -> that room's Durable Object
//   GET /              -> a one-line health text
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
      return stub.fetch(fwd);
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
    server.serializeAttachment({ room: request.headers.get("X-Neighbour-Room") || "", seat: -1, name: "", window: 0, count: 0 });
    return new Response(null, { status: 101, webSocket: client });
  }

  // ---- state -----------------------------------------------------------------
  // meta: { passHash, seats: [name|null, name|null], seq }   chats: [{seq, from, seat, text}]

  async meta() {
    return (await this.ctx.storage.get("meta")) || { passHash: null, seats: new Array(kSeats).fill(null), seq: 0 };
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
    if (a.seat < 0) return this.fail(ws, "say hello first", true);
    if (msg.t === "chat") return this.onChat(ws, a, msg);
    if (msg.t === "parcel") return this.onParcel(ws, a, msg);
    if (msg.t === "ack") return this.onAck(ws, a, msg);
    if (msg.t === "ping") return this.send(ws, { t: "pong" });
    // Unknown types from a newer game: ignored.
  }

  async onHello(ws, a, msg) {
    if (a.seat >= 0) return this.fail(ws, "already joined", false);
    if (msg.v !== kProtocolVersion) return this.fail(ws, `this relay speaks protocol ${kProtocolVersion}; update the game or the relay`, true);
    const name = clean(msg.name, kMaxNameChars);
    const pass = typeof msg.pass === "string" ? msg.pass.slice(0, kMaxPassChars) : "";
    if (!name) return this.fail(ws, "set neighbour.name in settings.cfg", true);

    const meta = await this.meta();
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
    this.send(ws, { t: "welcome", seat, peer: peerName, online: peerOnline });

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
  }
}
