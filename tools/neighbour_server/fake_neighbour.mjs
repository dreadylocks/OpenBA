#!/usr/bin/env node
// A pretend neighbour town, so one person can test neighbour towns alone
// (docs/fork/NEIGHBOUR_TOWNS.md). It joins a room on the relay like the game does, prints
// everything it receives, and sends each line you type as a chat message. Node 22+ (it uses
// Node's built-in WebSocket; nothing to install).
//
//   node fake_neighbour.mjs [--server ws://127.0.0.1:8787] [--room neighbours]
//                           [--pass secret] [--name "Fake Town"]
//                           [--say "hello"]... [--every 30 "a line repeated every 30 s"]
//                           [--quit-after 10] [--send-goods "path:count"] [--send-family "3.5/1/0;3.2/0/0;0.8/1/1"]
//                           [--token <seat token>] [--data-hash <16 hex>] [--mods "name|size|;..."]
//
// Protocol 2 (docs/fork/NEIGHBOUR_PROTOCOL.md): the relay seats a town by its token, not its name.
// The first join prints the token it was given; pass it with --token to come back to the same seat.
// --data-hash defaults to empty ("unknown"), so the relay cannot refuse this pretend town for other
// retail data; give the game's (its log prints it) to test that refusal.
//
// --say lines are sent right after joining. --quit-after leaves after that many seconds
// (handy in scripts). Ctrl+C leaves. Parcels it receives (goods, families) are printed and
// acknowledged, as the game does. Typed lines starting with "/goods <path> <count>" or
// "/family <members>" send a parcel; anything else is chat.

import readline from "node:readline";

function parseArgs(argv) {
  const o = { server: "ws://127.0.0.1:8787", room: "neighbours", pass: "", name: "Fake Town", say: [], every: null, quitAfter: 0, lastSeq: 0, parcels: [],
              token: "", dataHash: "", mods: "" };
  for (let i = 0; i < argv.length; i++) {
    const k = argv[i];
    const next = () => { if (i + 1 >= argv.length) throw new Error(`${k} needs a value`); return argv[++i]; };
    if (k === "--server") o.server = next();
    else if (k === "--room") o.room = next();
    else if (k === "--pass") o.pass = next();
    else if (k === "--name") o.name = next();
    else if (k === "--say") o.say.push(next());
    else if (k === "--every") o.every = { seconds: Number(next()), text: next() };
    else if (k === "--quit-after") o.quitAfter = Number(next());
    else if (k === "--last-seq") o.lastSeq = Number(next());
    else if (k === "--send-goods") o.parcels.push({ kind: "goods", body: next() });
    else if (k === "--send-family") o.parcels.push({ kind: "family", body: next() });
    else if (k === "--token") o.token = next();
    else if (k === "--data-hash") o.dataHash = next();
    else if (k === "--mods") o.mods = next();
    else if (k === "--help" || k === "-h") { console.log("see the comment at the top of fake_neighbour.mjs"); process.exit(0); }
    else throw new Error(`unknown argument ${k}`);
  }
  return o;
}

const opt = parseArgs(process.argv.slice(2));
const url = `${opt.server.replace(/\/+$/, "")}/room/${encodeURIComponent(opt.room)}`;
const stamp = () => new Date().toLocaleTimeString();
const log = (s) => console.log(`[${stamp()}] ${s}`);

if (typeof WebSocket === "undefined") {
  console.error("This needs Node 22 or newer (a built-in WebSocket).");
  process.exit(2);
}

log(`connecting to ${url} as "${opt.name}"`);
const ws = new WebSocket(url);
const send = (msg) => ws.send(JSON.stringify(msg));
let joined = false;
let parcelCounter = 0;
const sendParcel = (kind, body) => {
  const id = `fake-${Date.now().toString(36)}-${(++parcelCounter).toString(36)}`;
  send({ t: "parcel", id, kind, body });
  log(`me: parcel ${id} (${kind}) ${body}`);
};

ws.addEventListener("open", () => {
  const hello = { t: "hello", v: 2, room: opt.room, pass: opt.pass, name: opt.name, lastSeq: opt.lastSeq,
                  engine: "fake_neighbour", engineVer: "2", simBuild: "", dataHash: opt.dataHash, mods: opt.mods,
                  exts: "", maxPlayers: 2 };
  if (opt.token) hello.token = opt.token;
  send(hello);
});

ws.addEventListener("message", (ev) => {
  let m;
  try { m = JSON.parse(ev.data); } catch { log(`(not JSON) ${ev.data}`); return; }
  switch (m.t) {
    case "welcome":
      joined = true;
      log(`joined room "${opt.room}" in seat ${m.seat}; neighbour: ${m.peer ? `${m.peer} (${m.online ? "here" : "away"})` : "nobody yet"}`);
      if (m.token) log(`seat token: ${m.token}  (come back to this seat with --token ${m.token})`);
      for (const text of opt.say) { send({ t: "chat", text }); log(`me: ${text}`); }
      for (const p of opt.parcels) sendParcel(p.kind, p.body);
      if (opt.every && opt.every.seconds > 0) {
        setInterval(() => { send({ t: "chat", text: opt.every.text }); log(`me: ${opt.every.text}`); }, opt.every.seconds * 1000);
      }
      break;
    case "chat": log(`${m.from} (#${m.seq}): ${m.text}`); break;
    case "peer": log(`${m.name} has ${m.online ? "arrived" : "left"}`); break;
    case "error": log(`relay error: ${m.why}${m.fatal ? " (refused)" : ""}`); break;
    case "compat": log(`warning: ${m.warn}`); break;
    case "parcel":
      log(`${m.from} sent a parcel ${m.id} (${m.kind}): ${m.body}`);
      send({ t: "ack", id: m.id });
      break;
    case "stored": log(`the relay holds parcel ${m.id}`); break;
    case "pong": break;
    default: log(`(${m.t}) ${ev.data}`);
  }
});

ws.addEventListener("close", (ev) => {
  log(`line closed (${ev.code}${ev.reason ? `: ${ev.reason}` : ""})`);
  process.exit(joined ? 0 : 1);
});
ws.addEventListener("error", () => log("connection error (is the relay running? `npm run dev`)"));

setInterval(() => { if (ws.readyState === WebSocket.OPEN) send({ t: "ping" }); }, 25_000);

if (process.stdin.isTTY) {
  const rl = readline.createInterface({ input: process.stdin });
  rl.on("line", (line) => {
    const text = line.trim();
    if (!text) return;
    if (!joined) { log("not joined yet"); return; }
    const g = text.match(/^\/goods\s+(\S+)\s+(\d+)$/);
    const f = text.match(/^\/family\s+(\S+)$/);
    if (g) sendParcel("goods", `${g[1]}:${g[2]}`);
    else if (f) sendParcel("family", f[1]);
    else send({ t: "chat", text });
  });
}

if (opt.quitAfter > 0) setTimeout(() => { log("leaving"); ws.close(1000, "bye"); }, opt.quitAfter * 1000);
process.on("SIGINT", () => { ws.close(1000, "bye"); setTimeout(() => process.exit(0), 300); });
