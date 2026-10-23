#!/usr/bin/env python3
"""Makes deliberately broken copies of a good trace, one defect each, for testing analyze.py.
usage: corrupt_trace.py <good-trace> <out-dir>   writes <out-dir>/bad_<defect>.log"""
import os
import re
import sys


def mutate(lines):
    """Returns {defect: [lines]}."""
    def sub(pred, fn):
        return [fn(l) if pred(l) else l for l in lines]

    def drop(pred):
        return [l for l in lines if not pred(l)]

    out = {}
    # the pointer never reaches the right edge: halve every x pixel and summary maximum
    def halve(l):
        l = re.sub(r"\bpx=([\d.]+)", lambda m: "px=%.1f" % (float(m.group(1)) / 2), l) if l.startswith(("M ", "B ")) else l
        return re.sub(r"\bmx_x=([\d.]+)", lambda m: "mx_x=%.1f" % (float(m.group(1)) / 2), l)
    out["right_edge_unreached"] = [halve(l) for l in lines]
    # W produces the wrong DIK (the A key's)
    out["wrong_dik_for_W"] = sub(lambda l: l.startswith("K ") and " sc=26 " in l and "down=1" in l,
                                 lambda l: l.replace("eff_dn=0x11", "eff_dn=0x1E"))
    out["pageup_layer_missing"] = sub(lambda l: l.startswith("K ") and " sc=82 " in l, lambda l: l.replace("0xC9", "0xC8"))
    out["fullscreen_never_entered"] = drop(lambda l: l.startswith("E ") and "fullscreen" in l)
    out["fullscreen_second_press_lost"] = drop(lambda l: l.startswith("E ") and "leave_fullscreen" in l)
    # entered, but the drawable did not follow the window
    def keep_size(l):
        if (l.startswith("E ") or l.startswith("S ") or l.startswith("H ")) and "fs=1" in l:
            return re.sub(r"\bpx=\d+x\d+", "px=640x360", re.sub(r"\bwin=\d+x\d+", "win=640x360", l))
        return l
    out["fullscreen_drawable_unchanged"] = [keep_size(l) for l in lines]
    out["quit_never_clean"] = drop(lambda l: l.startswith("X "))
    out["no_quit_request"] = drop(lambda l: l.startswith(("Q ", "X ")))
    out["no_header"] = drop(lambda l: l.startswith("H "))
    out["no_wheel"] = drop(lambda l: l.startswith("W "))
    out["no_click"] = drop(lambda l: l.startswith("B ") and "btn=1" in l)
    out["window_never_resized"] = [re.sub(r"\bpx=800x450", "px=640x360", re.sub(r"\bwin=800x450", "win=640x360", l))
                                   for l in lines if not (l.startswith("E ") and "d1=800" in l)]
    out["size_mismatch"] = [re.sub(r"\bpx=640x360", "px=1280x720", l) if l.startswith("S ") else l for l in lines]
    return out


def main():
    src, dst = sys.argv[1], sys.argv[2]
    with open(src) as f:
        lines = f.read().splitlines()
    os.makedirs(dst, exist_ok=True)
    for name, ls in mutate(lines).items():
        with open(os.path.join(dst, "bad_%s.log" % name), "w") as f:
            f.write("\n".join(ls) + "\n")
    print("wrote %d corrupted traces to %s" % (len(mutate(lines)), dst))


if __name__ == "__main__":
    main()
