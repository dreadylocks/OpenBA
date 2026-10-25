#!/usr/bin/env node
// An end-to-end check of the relay (no game needed): two pretend towns join a fresh room,
// swap chat lines, one leaves and misses a line, comes back and gets it replayed; a wrong
// password and a third town are refused. Protocol 2: seats by token, the layer-1 handshake (a
// different retail data hash or land refused, a different build or mod list warned). Exit 0 =
// everything held.
//
//   npm run dev            (in one terminal: the local relay on 127.0.0.1:8787)
//   node selftest.mjs ws://127.0.0.1:8787

import http from "node:http";
import https from "node:https";

const server = (process.argv[2] || "ws://127.0.0.1:8787").replace(/\/+$/, "");
const room = `selftest-${Date.now()}`;
let failures = 0;
// Every hello / create carries the layer-1 fields (docs/fork/NEIGHBOUR_PROTOCOL.md "hello").
const HS = { v: 2, engine: "openba", engineVer: "1.0.0", simBuild: "1111111111111111", dataHash: "2222222222222222",
             mods: "", exts: "neighbour:4", maxPlayers: 2 };
// The seat tokens the relay hands out, by room and town name, sent back on the next hello like a game does.
const tokens = new Map();
const keep = (roomName, name, m) => { if (m.t === "welcome" && m.token) tokens.set(`${roomName}/${name}`, m.token); };
const withToken = (roomName, first) => {
  const t = tokens.get(`${roomName}/${first.name}`);
  return t && first.t === "hello" && !("token" in first) ? { ...first, token: t } : first;   // an explicit token stays
};
const check = (ok, what) => { console.log(`${ok ? "ok  " : "FAIL"} ${what}`); if (!ok) failures++; };

function town(name, pass, lastSeq = 0) {
  const ws = new WebSocket(`${server}/room/${encodeURIComponent(room)}`);
  const inbox = [];
  const waiters = [];
  ws.addEventListener("open", () => ws.send(JSON.stringify(withToken(room, { ...HS, t: "hello", room, pass, name, lastSeq }))));
  ws.addEventListener("message", (ev) => {
    const m = JSON.parse(ev.data);
    keep(room, name, m);
    inbox.push(m);
    for (const w of [...waiters]) if (w.pred(m)) { waiters.splice(waiters.indexOf(w), 1); w.resolve(m); }
  });
  const closed = new Promise((r) => ws.addEventListener("close", (ev) => r(ev)));
  return {
    ws, inbox, closed,
    say: (text) => ws.send(JSON.stringify({ t: "chat", text })),
    wait: (pred, ms = 5000) => {
      const hit = inbox.find(pred);
      if (hit) return Promise.resolve(hit);
      return new Promise((resolve, reject) => {
        const w = { pred, resolve };
        waiters.push(w);
        setTimeout(() => reject(new Error("timed out")), ms);
      });
    },
    leave: () => { ws.close(1000, "bye"); return closed; },
  };
}

const tryWait = async (p) => { try { return await p; } catch { return null; } };

const a = town("Ashford", "pw");
const wa = await tryWait(a.wait((m) => m.t === "welcome"));
check(wa && wa.seat === 0 && wa.peer === "", "the first town gets seat 0 and an empty room");
check(wa && /^[0-9a-f]{32}$/.test(wa.token || ""), "the first join hands the town a seat token");

const b = town("Brookvale", "pw");
const wb = await tryWait(b.wait((m) => m.t === "welcome"));
check(wb && wb.seat === 1 && wb.peer === "Ashford" && wb.online === true, "the second town gets seat 1 and sees the first");
check(!!(await tryWait(a.wait((m) => m.t === "peer" && m.name === "Brookvale" && m.online))), "the first town hears the second arrive");

a.say("hello from Ashford");
const c1 = await tryWait(b.wait((m) => m.t === "chat" && m.text === "hello from Ashford"));
check(c1 && c1.from === "Ashford" && c1.seq === 1, "a chat line reaches the neighbour, numbered 1");

await b.leave();
check(!!(await tryWait(a.wait((m) => m.t === "peer" && m.name === "Brookvale" && !m.online))), "the first town hears the second leave");
a.say("while you were away");
await new Promise((r) => setTimeout(r, 300));

const b2 = town("Brookvale", "pw", 1);   // back, having seen line 1
const missed = await tryWait(b2.wait((m) => m.t === "chat" && m.text === "while you were away"));
check(missed && missed.seq === 2, "a town coming back gets the line it missed (and only that one)");
await new Promise((r) => setTimeout(r, 300));
check(!b2.inbox.some((m) => m.t === "chat" && m.seq === 1), "the line it had already seen is not replayed");

const wrong = town("Intruder", "nope");
const e1 = await tryWait(wrong.wait((m) => m.t === "error"));
check(e1 && /password/.test(e1.why), "a wrong password is refused");
const third = town("Thirdton", "pw");
const e2 = await tryWait(third.wait((m) => m.t === "error"));
check(e2 && /two towns/.test(e2.why), "a third town is refused");

// Parcels: sent while the neighbour is away, stored, delivered when it joins, gone once acked,
// and a resend of the same id is not a second parcel.
await b2.leave();
a.ws.send(JSON.stringify({ t: "parcel", id: "p-1", kind: "goods", body: "Template/Fish.rsc:25" }));
const st1 = await tryWait(a.wait((m) => m.t === "stored" && m.id === "p-1"));
check(!!st1, "the relay confirms it holds a parcel");
a.ws.send(JSON.stringify({ t: "parcel", id: "p-1", kind: "goods", body: "Template/Fish.rsc:25" }));
await tryWait(a.wait((m) => m.t === "stored" && m.id === "p-1" && m !== st1));
const b3 = town("Brookvale", "pw", 2);
const p1 = await tryWait(b3.wait((m) => m.t === "parcel" && m.id === "p-1"));
check(p1 && p1.kind === "goods" && p1.body === "Template/Fish.rsc:25" && p1.from === "Ashford",
      "a parcel sent while the neighbour was away arrives when it joins");
await new Promise((r) => setTimeout(r, 300));
check(b3.inbox.filter((m) => m.t === "parcel" && m.id === "p-1").length === 1, "a resent parcel arrives once");
b3.ws.send(JSON.stringify({ t: "ack", id: "p-1" }));
await new Promise((r) => setTimeout(r, 300));
await b3.leave();
const b4 = town("Brookvale", "pw", 2);
await tryWait(b4.wait((m) => m.t === "welcome"));
await new Promise((r) => setTimeout(r, 400));
check(!b4.inbox.some((m) => m.t === "parcel"), "an acknowledged parcel is not delivered again");
a.ws.send(JSON.stringify({ t: "parcel", id: "p-2", kind: "family", body: "30.0/1/0;28.5/0/0;6.2/0/1" }));
const p2 = await tryWait(b4.wait((m) => m.t === "parcel" && m.id === "p-2"));
check(p2 && p2.kind === "family", "a parcel to a town that is online is passed on at once");
a.ws.send(JSON.stringify({ t: "parcel", id: "BAD ID", kind: "goods", body: "x:1" }));
const e3 = await tryWait(a.wait((m) => m.t === "error" && /parcel id/.test(m.why)));
check(!!e3, "a malformed parcel is refused");

// M2: a CREATED room (the lobby's route): settings and seed kept and handed to the joiner, the
// room on the public list, a taken name refused, a join to a missing room refused, the world
// hash compared.
const lobbyRoom = `lobbytest-${Date.now()}`;
const settings = { seed: 123456789, size: 1, terrain: "Game/Terrain/Valleys.rsc", climate: "Game/Climate/Mild.rsc", start: "Game/StartCondition/Easy.rsc", disasters: 1 };
function opener(roomName, first) {
  const ws = new WebSocket(`${server}/room/${encodeURIComponent(roomName)}`);
  const inbox = [];
  const waiters = [];
  ws.addEventListener("open", () => ws.send(JSON.stringify(withToken(roomName, { ...HS, ...first }))));
  const closed = new Promise((r) => ws.addEventListener("close", (ev) => r(ev)));
  ws.addEventListener("message", (ev) => {
    const m = JSON.parse(ev.data);
    keep(roomName, first.name, m);
    inbox.push(m);
    for (const w of [...waiters]) if (w.pred(m)) { waiters.splice(waiters.indexOf(w), 1); w.resolve(m); }
  });
  return {
    ws, inbox, closed,
    wait: (pred, ms = 5000) => {
      const hit = inbox.find(pred);
      if (hit) return Promise.resolve(hit);
      return new Promise((resolve, reject) => { waiters.push({ pred, resolve }); setTimeout(() => reject(new Error("timed out")), ms); });
    },
  };
}
const creator = opener(lobbyRoom, { t: "create", pass: "pw2", name: "Creatorville", ...settings });
const cw = await tryWait(creator.wait((m) => m.t === "welcome"));
check(cw && cw.created === true && cw.seed === settings.seed && cw.size === 1 && cw.terrain === settings.terrain && cw.seat === 0,
      "creating a room keeps its seed and settings");
const dup = opener(lobbyRoom, { t: "create", pass: "x", name: "Copycat", ...settings });
const de = await tryWait(dup.wait((m) => m.t === "error"));
check(de && /already exists/.test(de.why), "a taken room name is refused");
const missing = opener(`nosuchroom-${Date.now()}`, { t: "hello", pass: "x", name: "Lost", join: true });
const me = await tryWait(missing.wait((m) => m.t === "error"));
check(me && /no room/.test(me.why), "joining a room that does not exist is refused");
const joiner = opener(lobbyRoom, { t: "hello", pass: "pw2", name: "Joinham", join: true });
const jw = await tryWait(joiner.wait((m) => m.t === "welcome"));
check(jw && jw.seed === settings.seed && jw.climate === settings.climate && jw.start === settings.start && jw.disasters === 1 && jw.creator === "Creatorville",
      "the joiner gets the room's seed and settings");

const lobby = new WebSocket(`${server}/lobby`);
const listed = [];
const listDone = new Promise((resolve) => {
  lobby.addEventListener("message", (ev) => {
    const m = JSON.parse(ev.data);
    if (m.t === "room") listed.push(m);
    if (m.t === "roomsEnd") resolve(m);
  });
  setTimeout(() => resolve(null), 5000);
});
lobby.addEventListener("open", () => lobby.send(JSON.stringify({ t: "list" })));
const end = await listDone;
const mine = listed.find((r) => r.name === lobbyRoom);
check(!!end && mine && mine.creator === "Creatorville" && mine.seats === 2 && mine.size === 1 && !("pass" in mine) && !("passHash" in mine),
      "the lobby lists the room (seats, settings) and no password");
lobby.close();

creator.ws.send(JSON.stringify({ t: "hash", hash: "abcdef12" }));
joiner.ws.send(JSON.stringify({ t: "hash", hash: "ABCDEF12" }));
const h1 = await tryWait(creator.wait((m) => m.t === "hashCheck"));
check(h1 && h1.same === true, "two towns with the same world hash are told the maps match");
// Land claims: first come first served, the neighbour told, released land free again.
creator.ws.send(JSON.stringify({ t: "claim", id: "cl1", rects: "10,10,3,3" }));
const cl1 = await tryWait(creator.wait((m) => m.t === "claimed" && m.id === "cl1"));
check(cl1 && cl1.ok === true, "a claim on free land is granted");
const told = await tryWait(joiner.wait((m) => m.t === "claims" && m.rects === "10,10,3,3"));
check(told && told.seat === 0, "the neighbour is told about the claim");
joiner.ws.send(JSON.stringify({ t: "claim", id: "cl2", rects: "11,11,2,2" }));
const cl2 = await tryWait(joiner.wait((m) => m.t === "claimed" && m.id === "cl2"));
check(cl2 && cl2.ok === false, "a claim overlapping the neighbour's land is refused");
joiner.ws.send(JSON.stringify({ t: "claim", id: "cl3", rects: "40,40,2,2;50,50,1,1" }));
const cl3 = await tryWait(joiner.wait((m) => m.t === "claimed" && m.id === "cl3"));
check(cl3 && cl3.ok === true, "a claim elsewhere is granted");
joiner.ws.send(JSON.stringify({ t: "claim", id: "cl4", rects: "1,2,3" }));
check(!!(await tryWait(joiner.wait((m) => m.t === "error" && /claim/.test(m.why)))), "a malformed claim is refused");

// Mirrored buildings: passed on, kept, and handed to a town that joins later.
creator.ws.send(JSON.stringify({ t: "build", id: "b1", tmpl: "Template\\WoodHouse.rsc", x: 1250, y: -300, z: 40, rot: 90, tx: 10, ty: 10, tw: 3, th: 3, built: 25 }));
const b1 = await tryWait(joiner.wait((m) => m.t === "build" && m.id === "b1"));
check(b1 && b1.tmpl === "Template\\WoodHouse.rsc" && b1.built === 25 && b1.tw === 3, "a building is mirrored to the neighbour");
creator.ws.send(JSON.stringify({ t: "build", id: "b1", tmpl: "Template\\WoodHouse.rsc", x: 1250, y: -300, z: 40, rot: 90, tx: 10, ty: 10, tw: 3, th: 3, built: 100 }));
await tryWait(joiner.wait((m) => m.t === "build" && m.id === "b1" && m.built === 100));
joiner.ws.close();
await new Promise((r) => setTimeout(r, 300));
const rejoin = opener(lobbyRoom, { t: "hello", pass: "pw2", name: "Joinham", join: true, lastSeq: 99 });
const blk = await tryWait(rejoin.wait((m) => m.t === "claimBlock"));
check(blk && blk.bx === 0 && blk.by === 0 && blk.owner[10 * 32 + 10] === "0" && blk.owner[40 * 32 + 40] !== "1",
      "a town joining gets the neighbour's land (and only the neighbour's)");
const mEnd = await tryWait(rejoin.wait((m) => m.t === "mirrorEnd"));
const b1again = rejoin.inbox.find((m) => m.t === "build" && m.id === "b1");
check(mEnd && mEnd.count === 1 && b1again && b1again.built === 100, "a town joining gets the neighbour's buildings, up to date");
creator.ws.send(JSON.stringify({ t: "unbuild", id: "b1" }));
check(!!(await tryWait(rejoin.wait((m) => m.t === "unbuild" && m.id === "b1"))), "a removed building is removed from the mirror");
creator.ws.send(JSON.stringify({ t: "release", rects: "10,10,3,3" }));
await tryWait(rejoin.wait((m) => m.t === "released"));
rejoin.ws.send(JSON.stringify({ t: "claim", id: "cl5", rects: "11,11,2,2" }));
const cl5 = await tryWait(rejoin.wait((m) => m.t === "claimed" && m.id === "cl5"));
check(cl5 && cl5.ok === true, "released land can be claimed by the neighbour");

// M3: solid tiles, shared roads and resource changes.
creator.ws.send(JSON.stringify({ t: "build", id: "b2", tmpl: "Template\Barn.rsc", x: 0, y: 0, z: 0, rot: 0, tx: 50, ty: 50, tw: 2, th: 2, built: 100, bm: "1101", dec: "50,50,2,2,90,0" }));
const bb2 = await tryWait(rejoin.wait((m) => m.t === "build" && m.id === "b2"));
check(bb2 && bb2.bm === "1101", "a building's solid tiles are passed on");
check(bb2 && bb2.dec === "50,50,2,2,90,0", "and its ground (footprint, rotation, decal state)");
creator.ws.send(JSON.stringify({ t: "road", cell: 70000, type: 0, tt: 6, tmpl: "Template\RoadDirt.rsc" }));
const rd = await tryWait(rejoin.wait((m) => m.t === "road" && m.cell === 70000));
check(rd && rd.type === 0 && rd.tt === 6 && rd.tmpl === "Template\RoadDirt.rsc", "a finished road tile is passed on");
creator.ws.send(JSON.stringify({ t: "road", cell: 70001, type: 1, tt: 7, tmpl: "Template\RoadStone.rsc" }));
await tryWait(rejoin.wait((m) => m.t === "road" && m.cell === 70001));
creator.ws.send(JSON.stringify({ t: "road", cell: 70001, off: true }));
check(!!(await tryWait(rejoin.wait((m) => m.t === "road" && m.cell === 70001 && m.off === true))), "a removed road tile is passed on");
creator.ws.send(JSON.stringify({ t: "road", cell: -4, type: 0, tt: 6, tmpl: "" }));
check(!!(await tryWait(creator.wait((m) => m.t === "error" && /road/.test(m.why)))), "a malformed road is refused");
creator.ws.send(JSON.stringify({ t: "nat", op: "rm", cell: 1234, tmpl: "Template\Tree.rsc" }));
const nr = await tryWait(rejoin.wait((m) => m.t === "nat" && m.cell === 1234));
check(nr && nr.op === "rm", "a felled tree is passed on");
rejoin.ws.close();
await new Promise((r) => setTimeout(r, 300));
creator.ws.send(JSON.stringify({ t: "nat", op: "add", cell: 999, tmpl: "Template\Tree.rsc" }));
await new Promise((r) => setTimeout(r, 300));
const rejoin2 = opener(lobbyRoom, { t: "hello", pass: "pw2", name: "Joinham", join: true, lastSeq: 99 });
const queued = await tryWait(rejoin2.wait((m) => m.t === "nat" && m.cell === 999));
check(queued && queued.op === "add", "a resource change waits for a town that is away");
const roads = await tryWait(rejoin2.wait((m) => m.t === "road" && m.cell === 70000));
check(roads && !rejoin2.inbox.some((m) => m.t === "road" && m.cell === 70001), "a town joining gets the neighbour's roads (not removed ones)");
rejoin2.ws.close();

creator.ws.close(); joiner.ws.close(); dup.ws.close(); missing.ws.close();

// Protocol 2: seats by token, the handshake's refusals and warnings, another land refused.
const name2 = town("Brookvale", "pw", 2);   // b4 holds Brookvale's seat with its token; this one has it too
await tryWait(name2.wait((m) => m.t === "welcome"));
const tokenless = opener(room, { t: "hello", pass: "pw", name: "Brookvale", token: "" });   // the name, no token
const tl = await tryWait(tokenless.wait((m) => m.t === "error"));
check(tl && tl.fatal === true && /two towns/.test(tl.why), "a town with another town's name but not its token gets no seat");
name2.ws.close();
const v1 = opener(`v1-${Date.now()}`, { t: "hello", v: 1, pass: "x", name: "Oldtown" });
const ev1 = await tryWait(v1.wait((m) => m.t === "error"));
check(ev1 && ev1.fatal === true && /protocol 2/.test(ev1.why), "a protocol 1 game is refused, and told not to retry");
const hsRoom = `hstest-${Date.now()}`;
const hsA = opener(hsRoom, { t: "create", pass: "pw3", name: "Alpha", ...settings, mods: "Fields.pkm|10|" });
await tryWait(hsA.wait((m) => m.t === "welcome"));
const hsData = opener(hsRoom, { t: "hello", pass: "pw3", name: "Delta", join: true, dataHash: "3333333333333333" });
const ed = await tryWait(hsData.wait((m) => m.t === "error"));
check(ed && ed.fatal === true && /game data/.test(ed.why), "a town with other retail data is refused");
const hsB = opener(hsRoom, { t: "hello", pass: "pw3", name: "Beta", join: true, simBuild: "4444444444444444",
                             mods: "Fields.pkm|10|;Roads.pkm|5|" });
const bw = await tryWait(hsB.wait((m) => m.t === "welcome"));
const bwarn = await tryWait(hsB.wait((m) => m.t === "compat" && /mods/.test(m.warn)));
const bbuild = await tryWait(hsB.wait((m) => m.t === "compat" && /build/.test(m.warn)));
check(bw && bw.seat === 1 && !!bbuild && bwarn && /Roads\.pkm/.test(bwarn.warn), "another build and other mods only warn the joiner");
check(!!(await tryWait(hsA.wait((m) => m.t === "compat" && /Roads\.pkm/.test(m.warn)))), "and the neighbour is warned too");
hsA.ws.send(JSON.stringify({ t: "hash", hash: "1234abcd" }));
hsB.ws.send(JSON.stringify({ t: "hash", hash: "9999aaaa" }));
const el = await tryWait(hsB.wait((m) => m.t === "error"));
check(el && el.fatal === true && /same land/.test(el.why), "a town that built other land is refused");
check(!!(await tryWait(hsA.wait((m) => m.t === "hashCheck" && m.same === false))), "and the neighbour is told the lands differ");
for (const x of [tokenless, v1, hsA, hsData, hsB]) x.ws.close();

// Shared pause: one town's pause reaches the other, with its name.
const pauseRoom = `pausetest-${Date.now()}`;
const pa = opener(pauseRoom, { t: "hello", pass: "pw5", name: "Pauser" });
await tryWait(pa.wait((m) => m.t === "welcome"));
const pb = opener(pauseRoom, { t: "hello", pass: "pw5", name: "Waiter" });
await tryWait(pb.wait((m) => m.t === "welcome"));
pa.ws.send(JSON.stringify({ t: "pause", on: true }));
const paused = await tryWait(pb.wait((m) => m.t === "pause" && m.on === true));
pa.ws.send(JSON.stringify({ t: "pause", on: false }));
const resumed = await tryWait(pb.wait((m) => m.t === "pause" && m.on === false));
check(paused && paused.from === "Pauser" && !!resumed, "a pause and its end reach the neighbour, with the town's name");
pa.ws.send(JSON.stringify({ t: "speed", x: 5 }));
const sped = await tryWait(pb.wait((m) => m.t === "speed"));
pa.ws.send(JSON.stringify({ t: "speed", x: 0 }));
const badSpeed = await tryWait(pa.wait((m) => m.t === "error" && /speed/.test(m.why)));
check(sped && sped.x === 5 && sped.from === "Pauser" && !!badSpeed, "a speed change reaches the neighbour; a bad one is refused");
pa.ws.send(JSON.stringify({ t: "walkers", tmpl: "Template\\Citizen.rsc", list: "1,100,-200,300,450,1,0,100,3;2,0,0,0,0,0,1,66,5" }));
const walkers = await tryWait(pb.wait((m) => m.t === "walkers"));
pa.ws.send(JSON.stringify({ t: "walkers", tmpl: "Template\\Citizen.rsc", list: "1,a,b" }));
const badWalkers = await tryWait(pa.wait((m) => m.t === "error" && /walkers/.test(m.why)));
check(walkers && walkers.list.startsWith("1,100,-200") && !!badWalkers, "villagers reach the neighbour; a bad list is refused");
for (const x of [pa, pb]) x.ws.close();

// Starting over: a town back in its seat with a new game (hello `fresh`) takes its old buildings,
// roads and land with it, and the neighbour is told.
const freshRoom = `freshtest-${Date.now()}`;
const fa = opener(freshRoom, { t: "create", pass: "pw4", name: "Firstly", ...settings });
await tryWait(fa.wait((m) => m.t === "welcome"));
const fb = opener(freshRoom, { t: "hello", pass: "pw4", name: "Secondly", join: true });
await tryWait(fb.wait((m) => m.t === "welcome"));
fa.ws.send(JSON.stringify({ t: "claim", id: "fc1", rects: "5,5,2,2" }));
await tryWait(fa.wait((m) => m.t === "claimed" && m.id === "fc1"));
fa.ws.send(JSON.stringify({ t: "build", id: "fb1", tmpl: "Template\\WoodHouse.rsc", x: 0, y: 0, z: 0, rot: 0, tx: 5, ty: 5, tw: 2, th: 2, built: 100 }));
fa.ws.send(JSON.stringify({ t: "road", cell: 1234, type: 1, tt: 0, tmpl: "Template\\Road.rsc" }));
await tryWait(fb.wait((m) => m.t === "road" && m.cell === 1234 && !m.off));
fa.ws.close(1000, "bye");
await fa.closed;
const fa2 = opener(freshRoom, { t: "hello", pass: "pw4", name: "Firstly", fresh: true });
const fw = await tryWait(fa2.wait((m) => m.t === "welcome"));
const gone = await tryWait(fb.wait((m) => m.t === "unbuild" && m.id === "fb1"));
const unpaved = await tryWait(fb.wait((m) => m.t === "road" && m.cell === 1234 && m.off === true));
const freed = await tryWait(fb.wait((m) => m.t === "released" && m.seat === 0 && m.rects === "0,0,32,32"));
check(fw && fw.seat === 0 && !!gone && !!unpaved && !!freed, "a town starting over in its seat takes its old buildings, roads and land");
fb.ws.close(1000, "bye");
await fb.closed;
const fb2 = opener(freshRoom, { t: "hello", pass: "pw4", name: "Secondly", join: true });
const fend = await tryWait(fb2.wait((m) => m.t === "mirrorEnd"));
check(fend && fend.count === 0 && !fb2.inbox.some((m) => m.t === "claimBlock"), "and a neighbour joining later sees none of them");
// A loaded save (hello `reload`): the seat's buildings and roads go the same way (the town sends what
// its save holds again), but the neighbour's resource changes waiting for it stay.
fa2.ws.send(JSON.stringify({ t: "build", id: "rb1", tmpl: "Template\\WoodHouse.rsc", x: 0, y: 0, z: 0, rot: 0, tx: 8, ty: 8, tw: 2, th: 2, built: 100 }));
fa2.ws.send(JSON.stringify({ t: "road", cell: 4321, type: 1, tt: 0, tmpl: "Template\\Road.rsc" }));
await tryWait(fb2.wait((m) => m.t === "road" && m.cell === 4321 && !m.off));
fa2.ws.close(1000, "bye");
await fa2.closed;
fb2.ws.send(JSON.stringify({ t: "nat", op: "add", cell: 777, tmpl: "Template\\Tree.rsc" }));
await new Promise((r) => setTimeout(r, 300));
const fa3 = opener(freshRoom, { t: "hello", pass: "pw4", name: "Firstly", reload: true });
const rgone = await tryWait(fb2.wait((m) => m.t === "unbuild" && m.id === "rb1"));
const runpaved = await tryWait(fb2.wait((m) => m.t === "road" && m.cell === 4321 && m.off === true));
const kept = await tryWait(fa3.wait((m) => m.t === "nat" && m.cell === 777));
check(!!rgone && !!runpaved && !!kept, "a town loading a save drops what its seat held, but keeps the changes waiting for it");
for (const x of [fa3, fb2]) x.ws.close();

// The Upgrade value is case-insensitive (RFC 6455 4.2.1): macOS's Network.framework sends "WebSocket".
const upgradeStatus = await new Promise((resolve) => {
  const u = new URL(`${server}/lobby`.replace(/^ws/, "http"));
  const req = (u.protocol === "https:" ? https : http).request(u, { headers: {
    Connection: "Upgrade", Upgrade: "WebSocket", "Sec-WebSocket-Version": "13", "Sec-WebSocket-Key": "dGhlIHNhbXBsZSBub25jZQ==" } });
  req.on("upgrade", (res, sock) => { sock.destroy(); resolve(res.statusCode); });
  req.on("response", (res) => { res.resume(); resolve(res.statusCode); });
  req.on("error", () => resolve(0));
  req.setTimeout(5000, () => { req.destroy(); resolve(0); });
  req.end();
});
check(upgradeStatus === 101, `a Mac's "Upgrade: WebSocket" is accepted (got ${upgradeStatus})`);

const bad = new WebSocket(`${server}/room/${encodeURIComponent(room)}`);
await new Promise((r) => bad.addEventListener("open", r));
bad.send('{"t":"chat","text":"no hello"}');
const badClose = await new Promise((r) => bad.addEventListener("close", (ev) => r(ev)));
check(badClose.code === 4000, "chat before hello closes the line");

a.ws.close(); b4.ws.close();
console.log(failures === 0 ? "\nrelay selftest: all good" : `\nrelay selftest: ${failures} failure(s)`);
process.exit(failures === 0 ? 0 : 1);
