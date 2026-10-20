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

const bad = new WebSocket(`${server}/room/${encodeURIComponent(room)}`);
await new Promise((r) => bad.addEventListener("open", r));
bad.send('{"t":"chat","text":"no hello"}');
const badClose = await new Promise((r) => bad.addEventListener("close", (ev) => r(ev)));
check(badClose.code === 4000, "chat before hello closes the line");

a.ws.close(); b2.ws.close();
console.log(failures === 0 ? "\nrelay selftest: all good" : `\nrelay selftest: ${failures} failure(s)`);
process.exit(failures === 0 ? 0 : 1);
