"""gpu_timing.py <capture.rdc> [out.json]

Full-precision GPU duration per draw from a RenderDoc capture (EventGPUDuration
counter, replayed), grouped by pass (clear / output-set boundaries) and, inside a
pass, by the (vertex shader, pixel shader) pair. Prints a report and writes JSON.
"""
import os, sys, json, time
mp = os.environ.get("RENDERDOC_MODULE_PATH", "")
if mp:
    sys.path.insert(0, mp)
    os.add_dll_directory(mp)
import renderdoc as rd

cap_path = sys.argv[1]
out_path = sys.argv[2] if len(sys.argv) > 2 else cap_path + ".timing.json"

cap = rd.OpenCaptureFile()
assert cap.OpenFile(cap_path, "", None) == rd.ResultCode.Succeeded
assert cap.LocalReplaySupport()
res, ctrl = cap.OpenCapture(rd.ReplayOptions(), None)
assert res == rd.ResultCode.Succeeded, res
sf = ctrl.GetStructuredFile()

# ---- counters: run the GPU duration counter 3 times, keep the per-event minimum
counters = ctrl.EnumerateCounters()
dur = [c for c in counters if c == rd.GPUCounter.EventGPUDuration]
assert dur, [ctrl.DescribeCounter(c).name for c in counters]
desc = ctrl.DescribeCounter(dur[0])
print("counter:", desc.name, desc.unit, file=sys.stderr)
timing = {}
for rep in range(3):
    for r in ctrl.FetchCounters([dur[0]]):
        v = r.value.d
        if r.eventId not in timing or v < timing[r.eventId]:
            timing[r.eventId] = v
scale = 1e3 if desc.unit == rd.CounterUnit.Seconds else 1.0  # -> ms

# ---- flatten actions
actions = []
def walk(acts):
    for a in acts:
        actions.append(a)
        if a.children:
            walk(a.children)
walk(ctrl.GetRootActions())

textures = {int(t.resourceId): t for t in ctrl.GetTextures()}
resnames = {int(r.resourceId): r.name for r in ctrl.GetResources()}
def tex_desc(rid):
    t = textures.get(int(rid))
    if not t:
        return "?"
    return f"{t.width}x{t.height} {str(t.format.Name()) if hasattr(t.format,'Name') else t.format.type} x{t.arraysize} m{t.mips}"

# ---- per draw: state (shaders, depth target)
t0 = time.time()
rows = []
for a in actions:
    is_clear = bool(a.flags & rd.ActionFlags.Clear)
    is_draw = bool(a.flags & rd.ActionFlags.Drawcall)
    is_disp = bool(a.flags & rd.ActionFlags.Dispatch)
    if not (is_clear or is_draw or is_disp):
        continue
    row = {"eid": a.eventId, "name": a.GetName(sf), "clear": is_clear, "draw": is_draw,
           "indices": a.numIndices, "instances": a.numInstances,
           "ms": timing.get(a.eventId, 0.0) * scale,
           "outputs": [int(o) for o in a.outputs if int(o) != 0],
           "depth": int(a.depthOut) if int(a.depthOut) != 0 else 0}
    if is_draw:
        ctrl.SetFrameEvent(a.eventId, False)
        st = ctrl.GetPipelineState()
        vs = st.GetShader(rd.ShaderStage.Vertex)
        ps = st.GetShader(rd.ShaderStage.Pixel)
        gs = st.GetShader(rd.ShaderStage.Geometry)
        row["vs"] = int(vs); row["ps"] = int(ps); row["gs"] = int(gs)
        row["vs_name"] = resnames.get(int(vs), str(vs))
        row["ps_name"] = resnames.get(int(ps), str(ps))
        vp = st.GetViewport(0)
        row["viewport"] = [vp.width, vp.height]
        # sampled textures (count + largest)
        try:
            ro = st.GetReadOnlyResources(rd.ShaderStage.Pixel)
            srv = [int(r.descriptor.resource) for r in ro if int(r.descriptor.resource) != 0]
            row["ps_srvs"] = srv[:8]
        except Exception:
            pass
    rows.append(row)
print(f"state walk: {len(rows)} rows in {time.time()-t0:.1f}s", file=sys.stderr)

# ---- passes
passes = []
cur = None
last_key = None
for r in rows:
    key = (tuple(r["outputs"]), r["depth"])
    if r["clear"] or cur is None or key != last_key:
        cur = {"idx": len(passes), "start": r["eid"], "end": r["eid"], "outputs": r["outputs"],
               "depth": r["depth"], "draws": 0, "clears": 0, "ms": 0.0, "indices": 0, "rows": []}
        passes.append(cur)
        last_key = key
    cur["end"] = r["eid"]
    cur["ms"] += r["ms"]
    cur["indices"] += r["indices"] * max(1, r["instances"])
    cur["draws" if r["draw"] else "clears"] += 1
    cur["rows"].append(r)

total = sum(r["ms"] for r in rows)
print(f"\n== {os.path.basename(cap_path)}: {len(rows)} events, GPU total {total:.3f} ms (sum of per-event durations)\n")
print(f"{'pass':>4} {'ms':>8} {'%':>5} {'draws':>5} {'tris':>9}  targets")
for p in passes:
    tg = ", ".join(tex_desc(o) for o in p["outputs"]) or "-"
    dp = (" depth " + tex_desc(p["depth"])) if p["depth"] else ""
    print(f"{p['idx']:>4} {p['ms']:>8.3f} {100*p['ms']/total:>5.1f} {p['draws']:>5} {p['indices']//3:>9}  {tg}{dp}")
    # inside: by shader pair
    by = {}
    for r in p["rows"]:
        if not r["draw"]:
            continue
        k = (r["vs_name"], r["ps_name"])
        b = by.setdefault(k, [0, 0.0, 0])
        b[0] += 1; b[1] += r["ms"]; b[2] += r["indices"] * max(1, r["instances"])
    for k, b in sorted(by.items(), key=lambda kv: -kv[1][1])[:6]:
        if b[1] < 0.02:
            continue
        print(f"         {b[1]:>8.3f} {100*b[1]/total:>5.1f} {b[0]:>5} {b[2]//3:>9}    vs={k[0]} ps={k[1]}")

print("\n== top 25 draws")
for r in sorted((r for r in rows if r["draw"]), key=lambda r: -r["ms"])[:25]:
    print(f"  eid {r['eid']:>5} {r['ms']:>7.3f} ms  tris={r['indices']//3:>7} inst={r['instances']:>3}  "
          f"vp={int(r['viewport'][0])}x{int(r['viewport'][1])}  vs={r['vs_name']} ps={r['ps_name']}")

json.dump({"capture": cap_path, "unit": "ms", "total_ms": total, "rows": rows,
           "passes": [{k: v for k, v in p.items() if k != "rows"} for p in passes]},
          open(out_path, "w"), indent=1)
ctrl.Shutdown(); cap.Shutdown()
