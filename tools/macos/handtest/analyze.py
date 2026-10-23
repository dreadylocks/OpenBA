#!/usr/bin/env python3
"""Reads input_trace.log (written by src/diag/darwin/input_trace_darwin.cpp) and prints a pass/fail table.

usage: analyze.py <input_trace.log> [--meta handtest_meta.txt] [--frames standalone_metal_frames.csv]
                  [--json] [--no-manual]

Exit code: 0 when no item FAILed, 1 otherwise, 2 when the trace is missing or empty.
Trace line format: see the banner of src/diag/darwin/input_trace_darwin.cpp ("TAG t=<s> key=value ...").
Pure standard library; analyze() is importable (test_analyze.py).
"""
import argparse
import json
import os
import re
import sys

PASS, FAIL, WARN, SKIP = "PASS", "FAIL", "WARN", "SKIP"

KMOD_ALT = 0x0300
KMOD_RALT = 0x0200
KMOD_GUI = 0x0C00

# (label, SDL scancode, expected DIK after the Mac layer)
KEYS = [
    ("W", 26, 0x11), ("A", 4, 0x1E), ("S", 22, 0x1F), ("D", 7, 0x20), ("Q", 20, 0x10), ("E", 8, 0x12),
    ("F1", 58, 0x3B), ("Space", 44, 0x39),
]
PAGEUP_DIK = 0xC9
SC_UP = 82


def parse_line(line):
    parts = line.split()
    if len(parts) < 2 or not re.fullmatch(r"[A-Z]", parts[0]):
        return None
    rec = {"tag": parts[0]}
    for p in parts[1:]:
        if "=" in p:
            k, v = p.split("=", 1)
            rec[k] = v
    try:
        rec["t"] = float(rec["t"])
    except (KeyError, ValueError):
        return None
    return rec


def load(path):
    out = []
    with open(path, errors="replace") as f:
        for line in f:
            r = parse_line(line.rstrip("\n"))
            if r:
                out.append(r)
    return out


def num(rec, key, default=None):
    v = rec.get(key)
    if v is None:
        return default
    try:
        return int(v, 0) if re.fullmatch(r"-?0[xX][0-9a-fA-F]+|-?\d+", v) else float(v)
    except ValueError:
        return default


def pair(rec, key):
    v = rec.get(key, "")
    m = re.fullmatch(r"(-?\d+)x(-?\d+)", v)
    return (int(m.group(1)), int(m.group(2))) if m else None


def dik_list(v):
    if not v or v == "-":
        return []
    return [int(x, 16) for x in v.split("+") if x]


class Result:
    def __init__(self, key, name, status, detail, hint=""):
        self.key, self.name, self.status, self.detail, self.hint = key, name, status, detail, hint

    def as_dict(self):
        return {"key": self.key, "name": self.name, "status": self.status, "detail": self.detail, "hint": self.hint}


def window_states(recs):
    """Every (t, pxw, pxh, ptw, pth, density, fs) the trace reports, oldest first."""
    out = []
    for r in recs:
        if r["tag"] in ("H", "S", "E"):
            px, pt = pair(r, "px"), pair(r, "win")
            if px and px[0] > 0:
                out.append(dict(t=r["t"], px=px, pt=pt, d=num(r, "density", 1.0) or 1.0,
                                fs=int(num(r, "fs", 0) or 0), tag=r["tag"], name=r.get("name", "")))
    return out


def check_edges(recs):
    reached = {"left": False, "right": False, "top": False, "bottom": False}
    cur = None
    over = None   # largest (x pixels - window width in points) seen
    cur_pt_w = 0
    density = 1.0
    seen_mouse = 0
    for r in recs:
        if r["tag"] in ("H", "S", "E"):
            px = pair(r, "px")
            if px and px[0] > 0:
                cur = px
                density = num(r, "density", 1.0) or 1.0
                pt = pair(r, "win")
                if pt:
                    cur_pt_w = pt[0]
        if cur is None:
            continue
        tol = max(2.0, density)
        w, h = cur
        pts = []
        if r["tag"] in ("M", "B") and "px" in r:
            x, y = num(r, "px"), num(r, "py")
            if x is not None and y is not None:
                seen_mouse += 1
                pts.append((x, x, y, y))
        elif r["tag"] == "S" and num(r, "mn_x", -1) >= 0:
            pts.append((num(r, "mn_x"), num(r, "mx_x"), num(r, "mn_y"), num(r, "mx_y")))
        for mnx, mxx, mny, mxy in pts:
            if cur_pt_w:
                over = (mxx - cur_pt_w) if over is None else max(over, mxx - cur_pt_w)
            if mnx <= tol: reached["left"] = True
            if mxx >= w - 1 - tol: reached["right"] = True
            if mny <= tol: reached["top"] = True
            if mxy >= h - 1 - tol: reached["bottom"] = True
    if seen_mouse == 0 and not any(r["tag"] == "S" and num(r, "mn_x", -1) >= 0 for r in recs):
        return [Result("edges", "Mouse reaches every screen edge", FAIL, "no mouse movement recorded at all",
                       "Was the mouse over the game window? Or the window never got mouse events.")]
    missing = [k for k, v in reached.items() if not v]
    out = []
    if missing:
        hint = ""
        if set(missing) <= {"right", "bottom"}:
            hint = "Only right/bottom missing: the edge snap (last pixel of the last point) may be broken, or the pointer never touched them."
        out.append(Result("edges", "Mouse reaches every screen edge", FAIL,
                          "not reached: " + ", ".join(missing) + " (reached: " + ", ".join(k for k, v in reached.items() if v) + ")", hint))
    else:
        out.append(Result("edges", "Mouse reaches every screen edge", PASS, "left, right, top and bottom all touched (pixel space)"))
    if density > 1.0 and over is not None:
        if over > 1:
            out.append(Result("pixels", "Mouse coordinates are in pixels (Retina)", PASS,
                              "density %.1f: x reached %.0f px beyond the window's width in points" % (density, over)))
        else:
            out.append(Result("pixels", "Mouse coordinates are in pixels (Retina)", FAIL,
                              "density %.1f but x never went beyond the window width in points" % density,
                              "The engine would see half-size coordinates: the pointer would not line up with the drawing."))
    else:
        out.append(Result("pixels", "Mouse coordinates are in pixels (Retina)", SKIP,
                          "pixel density is 1.0 in this run (not a Retina window), nothing to check"))
    return out


def check_consistency(recs):
    bad = []
    for s in window_states(recs):
        if s["pt"] and s["px"] and s["fs"] == 0 and s["d"] == s["d"]:   # NaN density (density=nan in a damaged trace): skip
            ex = (round(s["pt"][0] * s["d"]), round(s["pt"][1] * s["d"]))
            if abs(ex[0] - s["px"][0]) > 1 or abs(ex[1] - s["px"][1]) > 1:
                bad.append("t=%.1f %s points %s x %.1f != pixels %s" % (s["t"], s["tag"], s["pt"], s["d"], s["px"]))
    if bad:
        return Result("sizes", "Window points x density = drawable pixels", FAIL, bad[0] + (" (+%d more)" % (len(bad) - 1) if len(bad) > 1 else ""),
                      "The drawable size and the window size disagree: a resize may not have reached the renderer.")
    return Result("sizes", "Window points x density = drawable pixels", PASS, "all %d size reports agree" % len(window_states(recs)))


def check_click(recs):
    n = [r for r in recs if r["tag"] == "B" and r.get("btn") == "1" and r.get("down") == "1" and not (num(r, "mod", 0) & KMOD_ALT)]
    up = [r for r in recs if r["tag"] == "B" and r.get("btn") == "1" and r.get("down") == "0"]
    if n and up:
        return Result("click", "Left click on a toolbar button", PASS, "%d click(s) at px %s" % (len(n), ", ".join("(%.0f,%.0f)" % (num(r, "px"), num(r, "py")) for r in n[:3])))
    return Result("click", "Left click on a toolbar button", FAIL, "no plain left click (down + up) recorded", "Click once without holding Option.")


def check_rotate(recs):
    mid = [r for r in recs if r["tag"] == "B" and r.get("btn") == "2" and r.get("down") == "1"]
    opt = [r for r in recs if r["tag"] == "B" and r.get("btn") == "1" and r.get("down") == "1" and (num(r, "mod", 0) & KMOD_ALT)]
    if mid and opt:
        return Result("rotate", "Middle-drag and Option+drag", PASS, "middle button and Option+left both seen")
    if mid or opt:
        what = "middle button" if mid else "Option+left"
        miss = "Option+left-drag" if mid else "middle-drag"
        return Result("rotate", "Middle-drag and Option+drag", WARN, "only %s seen, %s not recorded" % (what, miss), "Do both if you can; a trackpad only has Option+drag.")
    return Result("rotate", "Middle-drag and Option+drag", FAIL, "no middle button and no Option+left press recorded")


def check_wheel(recs):
    w = [num(r, "y", 0.0) for r in recs if r["tag"] == "W"]
    if not w:
        return Result("wheel", "Scroll wheel (zoom)", FAIL, "no wheel event recorded")
    ups, downs = sum(1 for y in w if y > 0), sum(1 for y in w if y < 0)
    if ups and downs:
        return Result("wheel", "Scroll wheel (zoom)", PASS, "%d events: %d up, %d down" % (len(w), ups, downs))
    return Result("wheel", "Scroll wheel (zoom)", WARN, "%d events but all one direction" % len(w), "Scroll both ways to prove zoom in and out.")


def key_downs(recs):
    return [r for r in recs if r["tag"] == "K" and r.get("down") == "1" and r.get("rep") != "1"]


def check_keys(recs):
    kd = key_downs(recs)
    out = []
    for label, sc, dik in KEYS:
        hits = [r for r in kd if num(r, "sc") == sc]
        if not hits:
            out.append(Result("key_" + label, "Key %s -> DIK 0x%02X" % (label, dik), FAIL, "key not recorded", "Press %s with the game window focused." % label))
            continue
        good = [r for r in hits if dik in dik_list(r.get("eff_dn"))]
        if good:
            out.append(Result("key_" + label, "Key %s -> DIK 0x%02X" % (label, dik), PASS, "scancode %d reached the game as 0x%02X" % (sc, dik)))
        else:
            got = hits[0].get("eff_dn", "-")
            out.append(Result("key_" + label, "Key %s -> DIK 0x%02X" % (label, dik), FAIL, "scancode %d arrived but the game would read %s" % (sc, got),
                              "A modifier held down, or the Mac key layer mapped it elsewhere."))
    hits = [r for r in kd if num(r, "sc") == SC_UP and (num(r, "mod", 0) & KMOD_RALT)]
    name = "Right-Option+Up -> Page Up (DIK 0xC9)"
    if not hits:
        up_plain = [r for r in kd if num(r, "sc") == SC_UP]
        out.append(Result("key_pgup", name, FAIL, "Up with Right Option held not recorded" + (" (plain Up was)" if up_plain else ""),
                          "Hold the RIGHT Option key (not the left) and tap the Up arrow."))
    elif any(PAGEUP_DIK in dik_list(r.get("eff_dn")) for r in hits):
        out.append(Result("key_pgup", name, PASS, "Up + Right Option reached the game as Page Up"))
    else:
        out.append(Result("key_pgup", name, FAIL, "Up + Right Option arrived but the game would read %s" % hits[0].get("eff_dn", "-"),
                          "The Mac key layer (mac_keymap.h) did not turn it into Page Up."))
    return out


def check_fullscreen(recs):
    enters = [r for r in recs if r["tag"] == "E" and r.get("name") == "enter_fullscreen"]
    leaves = [r for r in recs if r["tag"] == "E" and r.get("name") == "leave_fullscreen"]
    name = "Ctrl+Cmd+F toggles full screen (twice)"
    if not enters:
        return Result("fullscreen", name, FAIL, "no enter_fullscreen event", "Did Ctrl+Cmd+F run while the game window was focused?")
    states = window_states(recs)
    first, leave = enters[0], (leaves[0] if leaves else None)
    before = [s for s in states if s["t"] < first["t"] and s["fs"] == 0]
    # SDL reports the resize right AFTER the enter event (same instant, or an animation later):
    # the full-screen size is the last full-screen report before the first leave.
    in_fs = [s for s in states if s["t"] >= first["t"] and s["fs"] == 1 and (leave is None or s["t"] <= leave["t"])]
    fs_size = in_fs[-1]["px"] if in_fs else None
    if before and fs_size and fs_size == before[-1]["px"]:
        return Result("fullscreen", name, FAIL, "entered full screen but the drawable stayed %dx%d" % before[-1]["px"],
                      "The renderer did not follow the window into full screen (resize event missing).")
    if not leaves:
        return Result("fullscreen", name, FAIL, "entered full screen but never left it", "Press Ctrl+Cmd+F a second time.")
    note = ""
    if before and fs_size:
        note = "windowed %dx%d -> full screen %dx%d" % (before[-1]["px"] + fs_size)
    after = [s for s in states if s["t"] >= leave["t"] and s["fs"] == 0 and s["px"] != fs_size][:1]
    if before and after and after[0]["px"] != before[0]["px"] and after[0]["px"] != before[-1]["px"]:
        return Result("fullscreen", name, WARN, "%s, but back in a window it is %dx%d, not the original %dx%d" % ((note,) + after[0]["px"] + before[-1]["px"]))
    return Result("fullscreen", name, PASS, "%d enter / %d leave; %s" % (len(enters), len(leaves), note or "size not reported"))


def check_resize(recs):
    sizes = []
    states = window_states(recs)
    start = states[0]["px"] if states else None
    # the leave event can still carry the full-screen size; the enter event still the windowed one
    fs_sizes = {s["px"] for s in states if s["fs"] == 1 and s["px"] != start}
    for s in states:
        if s["fs"] == 0 and s["px"] not in sizes and s["px"] not in fs_sizes:
            sizes.append(s["px"])
    if len(sizes) >= 2:
        return Result("resize", "Resize the window", PASS, "windowed sizes seen: " + ", ".join("%dx%d" % s for s in sizes[:5]))
    return Result("resize", "Resize the window", FAIL, "the window size never changed (only %s seen)" % ("%dx%d" % sizes[0] if sizes else "none"),
                  "Drag a window corner or edge. If you did, the resize never reached the drawable.")


def check_focus(recs):
    g = [r for r in recs if r["tag"] == "E" and r.get("name") == "focus_gained"]
    f = any(r["tag"] == "S" and r.get("focus") == "1" for r in recs) or any(r["tag"] == "E" and r.get("focus") == "1" for r in recs)
    if g or f:
        return Result("focus", "Game window had focus", PASS, "focus seen")
    return Result("focus", "Game window had focus", WARN, "focus never reported", "Click into the game window so it gets keyboard input.")


def check_stuck(recs):
    state = {}
    q = next((r["t"] for r in recs if r["tag"] == "Q"), None)
    for r in recs:
        if r["tag"] == "K" and r.get("rep") != "1":
            sc = num(r, "sc")
            # the Cmd of Cmd+Q is legitimately still down when the game quits
            if q is not None and r["t"] >= q - 5 and sc is not None and sc >= 224:
                continue
            state[sc] = (r.get("down") == "1", r.get("name"))
    stuck = [n for d, n in state.values() if d]
    if stuck:
        return Result("stuck", "No key left pressed", WARN, "still down at the end: " + ", ".join(stuck),
                      "A key-up event was lost (e.g. after Cmd or a focus change).")
    return Result("stuck", "No key left pressed", PASS, "every key press has a matching release")


def check_quit(recs, meta):
    name = "Cmd+Q quits cleanly"
    q = [r for r in recs if r["tag"] == "Q"]
    x = [r for r in recs if r["tag"] == "X"]
    if not q:
        return Result("quit", name, FAIL, "no quit request recorded", "Quit with Cmd+Q at the end of the test.")
    if not x:
        return Result("quit", name, FAIL, "quit requested but the process never wrote its final line", "The game hung or crashed while quitting; see standalone_error.log.")
    code = meta.get("exit_code")
    after = [r for r in recs if r["t"] > q[0]["t"] + 5 and r["tag"] in ("K", "B", "M")]
    if code is not None and code != "0":
        return Result("quit", name, FAIL, "quit seen, but the process exit code was %s" % code, "A crash while shutting down.")
    if after:
        return Result("quit", name, WARN, "quit seen, but the game kept taking input for more than 5 s afterwards")
    return Result("quit", name, PASS, "quit at t=%.1f s, final line written%s" % (q[0]["t"], ", exit code 0" if code == "0" else " (exit code not recorded)"))


def check_fps(frames_path):
    name = "Frame rate during the test"
    if not frames_path or not os.path.exists(frames_path):
        return Result("fps", name, SKIP, "no frame log (run with BANISHED_METAL_STATS=1)")
    ms = []
    try:
        with open(frames_path) as f:
            next(f, None)
            for line in f:
                p = line.strip().split(",")
                if len(p) >= 3:
                    ms.append(float(p[2]))
    except (OSError, ValueError):
        return Result("fps", name, SKIP, "frame log unreadable")
    ms = ms[10:]
    if len(ms) < 30:
        return Result("fps", name, SKIP, "too few frames (%d)" % len(ms))
    avg = 1000.0 * len(ms) / sum(ms)
    s = sorted(ms)
    p99 = s[int(len(s) * 0.99) - 1]
    detail = "%.0f fps average, slowest 1%% of frames take %.0f ms (%d frames)" % (avg, p99, len(ms))
    if avg < 10:
        return Result("fps", name, FAIL, detail, "Very slow; input timing results may be unreliable.")
    if avg < 20 or p99 > 120:
        return Result("fps", name, WARN, detail)
    return Result("fps", name, PASS, detail)


MANUAL = [
    ("ui_scale", "UI scale changed the size of buttons/text (Options > UI scale)"),
    ("edge_scroll", "Edge scrolling moved the view when the pointer touched an edge"),
    ("toolbar", "The toolbar button you clicked reacted"),
    ("rotate_view", "Middle-drag / Option+drag rotated the camera"),
    ("zoom_view", "The scroll wheel zoomed in and out"),
]


def check_manual(meta):
    out = []
    for key, text in MANUAL:
        a = meta.get("manual." + key)
        if a is None:
            continue
        st = {"y": PASS, "yes": PASS, "n": FAIL, "no": FAIL}.get(a.lower(), SKIP)
        out.append(Result("manual_" + key, "You said: " + text, st, "answer: " + a))
    return out


def analyze(recs, meta=None, frames=None, manual=True):
    meta = meta or {}
    res = []
    if not recs or not any(r["tag"] == "H" for r in recs):
        res.append(Result("trace", "Trace file is valid", FAIL, "no header line: the logger never started", "Was the game started with BANISHED_INPUT_TRACE=1 and did it get past boot?"))
        return res
    res.append(Result("trace", "Trace file is valid", PASS, "%d lines, %.1f s" % (len(recs), recs[-1]["t"])))
    res += check_edges(recs)
    res.append(check_consistency(recs))
    res.append(check_click(recs))
    res.append(check_rotate(recs))
    res.append(check_wheel(recs))
    res += check_keys(recs)
    res.append(check_fullscreen(recs))
    res.append(check_resize(recs))
    res.append(check_focus(recs))
    res.append(check_stuck(recs))
    res.append(check_quit(recs, meta))
    res.append(check_fps(frames))
    if manual:
        res += check_manual(meta)
    return res


def summary(res):
    fails = [r for r in res if r.status == FAIL]
    warns = [r for r in res if r.status == WARN]
    ok = sum(1 for r in res if r.status == PASS)
    lines = []
    if not fails:
        lines.append("Everything the logger can measure worked (%d passed%s)." % (ok, ", %d to look at" % len(warns) if warns else ""))
    else:
        lines.append("%d thing(s) did not work out of %d checked:" % (len(fails), len(res)))
        for r in fails:
            lines.append("  - %s: %s" % (r.name, r.detail))
            if r.hint:
                lines.append("      (%s)" % r.hint)
    for r in warns:
        lines.append("  ? %s: %s" % (r.name, r.detail))
    if any(r.key == "trace" and r.status == FAIL for r in res):
        lines.append("Send the folder with input_trace.log and standalone_error.log back for diagnosis.")
    elif fails:
        lines.append("Send the whole result folder (input_trace.log, handtest_result.txt, standalone_*.log) back: it has all that is needed to diagnose this without screenshots.")
    return "\n".join(lines)


def table(res):
    w = max(len(r.name) for r in res)
    rows = ["%-4s  %-*s  %s" % (r.status, w, r.name, r.detail) for r in res]
    return "\n".join(rows)


def read_meta(path):
    meta = {}
    if path and os.path.exists(path):
        with open(path, errors="replace") as f:
            for line in f:
                if "=" in line:
                    k, v = line.strip().split("=", 1)
                    meta[k.strip()] = v.strip()
    return meta


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("trace")
    ap.add_argument("--meta")
    ap.add_argument("--frames")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--no-manual", action="store_true")
    a = ap.parse_args(argv)
    if not os.path.exists(a.trace):
        print("analyze: %s does not exist (the game never wrote a trace)" % a.trace)
        return 2
    recs = load(a.trace)
    if not recs:
        print("analyze: %s is empty" % a.trace)
        return 2
    res = analyze(recs, read_meta(a.meta), a.frames, not a.no_manual)
    if a.json:
        print(json.dumps([r.as_dict() for r in res], indent=1))
    else:
        print(table(res))
        print()
        print(summary(res))
    return 1 if any(r.status == FAIL for r in res) else 0


if __name__ == "__main__":
    sys.exit(main())
