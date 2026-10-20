#!/usr/bin/env node
// An end-to-end check of the relay (no game needed): two pretend towns join a fresh room,
// swap chat lines, one leaves and misses a line, comes back and gets it replayed; a wrong
// password and a third town are refused. Exit 0 = everything held.
//
//   npm run dev            (in one terminal: the local relay on 127.0.0.1:8787)
//   node selftest.mjs ws://127.0.0.1:8787

const server = (process.argv[2] || "ws://127.0.0.1:8787").replace(/\/+$/, "");
const room = `selftest-${Date.now()}`;
let failures = 0;
const check = (ok, what) => { console.log(`${ok ? "ok  " : "FAIL"} ${what}`); if (!ok) failures++; };

function town(name, pass, lastSeq = 0) {
  const ws = new WebSocket(`${server}/room/${encodeURIComponent(room)}`);
  const inbox = [];
  const waiters = [];
  ws.addEventListener("open", () => ws.send(JSON.stringify({ t: "hello", v: 1, room, pass, name, lastSeq })));
  ws.addEventListener("message", (ev) => {
    const m = JSON.parse(ev.data);
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
  ws.addEventListener("open", () => ws.send(JSON.stringify(first)));
  ws.addEventListener("message", (ev) => {
    const m = JSON.parse(ev.data);
    inbox.push(m);
    for (const w of [...waiters]) if (w.pred(m)) { waiters.splice(waiters.indexOf(w), 1); w.resolve(m); }
  });
  return {
    ws, inbox,
    wait: (pred, ms = 5000) => {
      const hit = inbox.find(pred);
      if (hit) return Promise.resolve(hit);
      return new Promise((resolve, reject) => { waiters.push({ pred, resolve }); setTimeout(() => reject(new Error("timed out")), ms); });
    },
  };
}
const creator = opener(lobbyRoom, { t: "create", v: 1, pass: "pw2", name: "Creatorville", ...settings });
const cw = await tryWait(creator.wait((m) => m.t === "welcome"));
check(cw && cw.created === true && cw.seed === settings.seed && cw.size === 1 && cw.terrain === settings.terrain && cw.seat === 0,
      "creating a room keeps its seed and settings");
const dup = opener(lobbyRoom, { t: "create", v: 1, pass: "x", name: "Copycat", ...settings });
const de = await tryWait(dup.wait((m) => m.t === "error"));
check(de && /already exists/.test(de.why), "a taken room name is refused");
const missing = opener(`nosuchroom-${Date.now()}`, { t: "hello", v: 1, pass: "x", name: "Lost", join: true });
const me = await tryWait(missing.wait((m) => m.t === "error"));
check(me && /no room/.test(me.why), "joining a room that does not exist is refused");
const joiner = opener(lobbyRoom, { t: "hello", v: 1, pass: "pw2", name: "Joinham", join: true });
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
joiner.ws.send(JSON.stringify({ t: "hash", hash: "00000001" }));
const h2 = await tryWait(joiner.wait((m) => m.t === "hashCheck" && m.same === false));
check(h2 && h2.theirs === "abcdef12", "a different world hash is reported to both towns");
creator.ws.close(); joiner.ws.close(); dup.ws.close(); missing.ws.close();

const bad = new WebSocket(`${server}/room/${encodeURIComponent(room)}`);
await new Promise((r) => bad.addEventListener("open", r));
bad.send('{"t":"chat","text":"no hello"}');
const badClose = await new Promise((r) => bad.addEventListener("close", (ev) => r(ev)));
check(badClose.code === 4000, "chat before hello closes the line");

a.ws.close(); b4.ws.close();
console.log(failures === 0 ? "\nrelay selftest: all good" : `\nrelay selftest: ${failures} failure(s)`);
process.exit(failures === 0 ? 0 : 1);
