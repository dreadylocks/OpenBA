#!/usr/bin/env python3
"""Unit tests for analyze.py on hand-written sample traces (no game, no SDL). Run directly."""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze  # noqa: E402
import corrupt_trace  # noqa: E402


def good_trace(density=1):
    """A correct full test, as text lines. density 2 = Retina (px = 2 x points)."""
    d = density
    W, H = 640, 360

    def st(t, w, h, fs=0, tag="S", extra=""):
        return "%s t=%.3f win=%dx%d px=%dx%d density=%.2f fs=%d focus=1 %s" % (tag, t, w, h, w * d, h * d, d, fs, extra)

    L = ["H t=0.000 version=1 platform=macOS video=cocoa sdl=3002000 win=%dx%d px=%dx%d density=%.2f" % (W, H, W * d, H * d, d),
         "E t=0.100 name=focus_gained d1=0 d2=0 win=%dx%d px=%dx%d density=%.2f fs=0 focus=1" % (W, H, W * d, H * d, d)]
    L.append(st(1, W, H, extra="mouse_px=1,1 btn=0 ev=3 mn_x=0.0 mx_x=%.1f mn_y=0.0 mx_y=%.1f" % (W * d - 1, H * d - 1)))
    L.append("M t=1.5 x=100.0 y=100.0 px=%.1f py=%.1f rel=1,1 btn=0" % (100 * d, 100 * d))
    L.append("B t=2.0 btn=1 down=1 clicks=1 x=100.0 y=20.0 px=%.1f py=%.1f mod=0x0" % (100 * d, 20 * d))
    L.append("B t=2.1 btn=1 down=0 clicks=1 x=100.0 y=20.0 px=%.1f py=%.1f mod=0x0" % (100 * d, 20 * d))
    L.append("B t=3.0 btn=2 down=1 clicks=1 x=300.0 y=180.0 px=%.1f py=%.1f mod=0x0" % (300 * d, 180 * d))
    L.append("B t=3.5 btn=2 down=0 clicks=1 x=300.0 y=180.0 px=%.1f py=%.1f mod=0x0" % (300 * d, 180 * d))
    L.append("B t=4.0 btn=1 down=1 clicks=1 x=300.0 y=180.0 px=%.1f py=%.1f mod=0x100" % (300 * d, 180 * d))
    L.append("B t=4.5 btn=1 down=0 clicks=1 x=300.0 y=180.0 px=%.1f py=%.1f mod=0x100" % (300 * d, 180 * d))
    L.append("W t=5.0 y=1.00 x=0.00 flipped=0 mod=0x0")
    L.append("W t=5.1 y=-1.00 x=0.00 flipped=0 mod=0x0")
    t = 6.0
    for label, sc, dik in analyze.KEYS:
        L.append("K t=%.1f down=1 rep=0 sc=%d name=%s dik=0x%02X mod=0x0 eff_dn=0x%02X eff_up=-" % (t, sc, label, dik, dik))
        L.append("K t=%.1f down=0 rep=0 sc=%d name=%s dik=0x%02X mod=0x0 eff_dn=- eff_up=0x%02X" % (t + .05, sc, label, dik, dik))
        t += 0.2
    L.append("K t=8.0 down=1 rep=0 sc=230 name=Right_Option dik=0xB8 mod=0x200 eff_dn=0xB8 eff_up=-")
    L.append("K t=8.1 down=1 rep=0 sc=82 name=Up dik=0xC8 mod=0x200 eff_dn=0xC9 eff_up=0xB8")
    L.append("K t=8.2 down=0 rep=0 sc=82 name=Up dik=0xC8 mod=0x200 eff_dn=- eff_up=0xC9")
    L.append("K t=8.3 down=0 rep=0 sc=230 name=Right_Option dik=0xB8 mod=0x0 eff_dn=- eff_up=-")
    L.append("E t=9.0 name=enter_fullscreen d1=0 d2=0 win=1440x900 px=%dx%d density=%.2f fs=1 focus=1" % (1440 * d, 900 * d, d))
    L.append(st(10, 1440, 900, fs=1, extra="mouse_px=1,1 btn=0 ev=9 mn_x=-1.0 mx_x=-1.0 mn_y=-1.0 mx_y=-1.0"))
    L.append("E t=11.0 name=leave_fullscreen d1=0 d2=0 win=%dx%d px=%dx%d density=%.2f fs=0 focus=1" % (W, H, W * d, H * d, d))
    L.append(st(12, 800, 450, extra="mouse_px=1,1 btn=0 ev=12 mn_x=-1.0 mx_x=-1.0 mn_y=-1.0 mx_y=-1.0"))
    L.append("K t=13.0 down=1 rep=0 sc=227 name=Left_GUI dik=0xDB mod=0x400 eff_dn=0x1D eff_up=-")
    L.append("Q t=13.5 kind=quit")
    L.append("K t=13.6 down=0 rep=0 sc=227 name=Left_GUI dik=0xDB mod=0x0 eff_dn=- eff_up=0x1D")
    L.append("X t=14.0 events=40")
    return L


def run(lines, meta=None, frames=None):
    recs = [analyze.parse_line(l) for l in lines]
    return analyze.analyze([r for r in recs if r], meta or {"exit_code": "0"}, frames, True)


def status(res, key):
    return next(r.status for r in res if r.key == key)


class AnalyzeTests(unittest.TestCase):
    def test_good_trace_passes_everything(self):
        for d in (1, 2):
            res = run(good_trace(d))
            bad = [(r.key, r.status, r.detail) for r in res if r.status == analyze.FAIL]
            self.assertEqual(bad, [], "density %d" % d)
            self.assertEqual(status(res, "edges"), analyze.PASS)
            self.assertEqual(status(res, "key_pgup"), analyze.PASS)
            self.assertEqual(status(res, "fullscreen"), analyze.PASS)
            self.assertEqual(status(res, "quit"), analyze.PASS)

    def test_pixel_space_check_only_on_retina(self):
        self.assertEqual(status(run(good_trace(1)), "pixels"), analyze.SKIP)
        self.assertEqual(status(run(good_trace(2)), "pixels"), analyze.PASS)

    def test_every_corruption_is_detected(self):
        for name, lines in corrupt_trace.mutate(good_trace(1)).items():
            res = run(lines)
            self.assertTrue(any(r.status == analyze.FAIL for r in res), "defect %r went unnoticed" % name)

    def test_specific_defects_name_the_right_item(self):
        m = corrupt_trace.mutate(good_trace(1))
        self.assertEqual(status(run(m["right_edge_unreached"]), "edges"), analyze.FAIL)
        self.assertEqual(status(run(m["wrong_dik_for_W"]), "key_W"), analyze.FAIL)
        self.assertEqual(status(run(m["pageup_layer_missing"]), "key_pgup"), analyze.FAIL)
        self.assertEqual(status(run(m["fullscreen_never_entered"]), "fullscreen"), analyze.FAIL)
        self.assertEqual(status(run(m["fullscreen_second_press_lost"]), "fullscreen"), analyze.FAIL)
        self.assertEqual(status(run(m["fullscreen_drawable_unchanged"]), "fullscreen"), analyze.FAIL)
        self.assertEqual(status(run(m["quit_never_clean"]), "quit"), analyze.FAIL)
        self.assertEqual(status(run(m["no_quit_request"]), "quit"), analyze.FAIL)
        self.assertEqual(status(run(m["no_header"]), "trace"), analyze.FAIL)
        self.assertEqual(status(run(m["no_wheel"]), "wheel"), analyze.FAIL)
        self.assertEqual(status(run(m["no_click"]), "click"), analyze.FAIL)
        self.assertEqual(status(run(m["window_never_resized"]), "resize"), analyze.FAIL)
        self.assertEqual(status(run(m["size_mismatch"]), "sizes"), analyze.FAIL)

    def test_other_keys_other_items_unaffected(self):
        res = run(corrupt_trace.mutate(good_trace(1))["wrong_dik_for_W"])
        self.assertEqual(status(res, "key_A"), analyze.PASS)
        self.assertEqual(status(res, "quit"), analyze.PASS)

    def test_crash_exit_code_fails_quit(self):
        self.assertEqual(status(run(good_trace(1), {"exit_code": "139"}), "quit"), analyze.FAIL)

    def test_retina_half_coordinates_fail(self):
        # density 2 but the engine-side px never goes past the point width -> mapping bug
        lines = [l.replace("px=", "px=") for l in good_trace(2)]
        def halve(l):
            if l.startswith(("M ", "B ")):
                import re
                return re.sub(r"\bpx=([\d.]+)", lambda m: "px=%.1f" % (float(m.group(1)) / 2), l)
            if l.startswith("S "):
                import re
                return re.sub(r"\bmx_x=([\d.]+)", lambda m: "mx_x=%.1f" % (float(m.group(1)) / 2), l)
            return l
        self.assertEqual(status(run([halve(l) for l in lines]), "pixels"), analyze.FAIL)

    def test_stuck_key_warns(self):
        lines = [l for l in good_trace(1) if not ("sc=4 " in l and "down=0" in l)]
        self.assertEqual(status(run(lines), "stuck"), analyze.WARN)

    def test_fps_from_frame_log(self):
        import tempfile
        with tempfile.NamedTemporaryFile("w", suffix=".csv", delete=False) as f:
            f.write("frame,seconds,frame_ms\n")
            for i in range(200):
                f.write("%d,%.3f,%.3f\n" % (i, i * 0.016, 16.0))
            p = f.name
        self.assertEqual(status(run(good_trace(1), frames=p), "fps"), analyze.PASS)
        os.unlink(p)
        self.assertEqual(status(run(good_trace(1), frames="/nonexistent.csv"), "fps"), analyze.SKIP)

    def test_manual_answers(self):
        res = run(good_trace(1), {"exit_code": "0", "manual.ui_scale": "yes", "manual.toolbar": "no", "manual.zoom_view": "skip"})
        self.assertEqual(status(res, "manual_ui_scale"), analyze.PASS)
        self.assertEqual(status(res, "manual_toolbar"), analyze.FAIL)
        self.assertEqual(status(res, "manual_zoom_view"), analyze.SKIP)

    def test_summary_text_and_exit_code(self):
        res = run(corrupt_trace.mutate(good_trace(1))["wrong_dik_for_W"])
        s = analyze.summary(res)
        self.assertIn("did not work", s)
        self.assertIn("Key W", s)
        self.assertIn("Everything", analyze.summary(run(good_trace(1))))

    def test_nan_density_does_not_crash_consistency(self):
        lines = [l.replace("density=1.00", "density=nan") if l.startswith("S ") else l for l in good_trace()]
        self.assertIn(status(run(lines), "sizes"), ("PASS", "FAIL", "SKIP"))

    def test_garbage_lines_ignored(self):
        recs = [analyze.parse_line(l) for l in ["", "garbage", "standalone_debug.log: x=1", "K nonsense"]]
        self.assertEqual([r for r in recs if r], [])


if __name__ == "__main__":
    unittest.main(verbosity=1)
