# FAA-CORE -- ext.foliage_aa: the rewriter, the stage, the seam, the row (hand-in, 2026-10-16)

> (moved to the DESIGN.md §3 names 2026-10-20, block ext-standard: `src/render/dx11/billboard_shadow/` and `src/render/dx11/foliage_aa/` moved to `src/render/dx11/better_trees/` (file names kept); `tests/unit/ext_billboard_shadow_test.cpp` and `ext_foliage_aa_test.cpp` merged into `ext_better_trees_test.cpp`, cases `ext_bbs_*` / `ext_faa_*` renamed `ext_better_trees_*`.)

Block FAA-CORE of `docs/extensions/FOLIAGE_AA.md` §4 (the plan §3 as amended by the spike's §6).
Branch `worktree-agent-acf2c742cd06ca3c2`, from main `40983144a`, main merged twice (the last time
with FAA-SHADOW, `b80d6006e`: both registry rows kept). Not merged to main. The menu entry is
FAA-MENU's; the sun map is FAA-SHADOW's (untouched here).

## 1. What was built

| Piece | Files | What |
|---|---|---|
| The rewriter (pure) | `src/render/dx11/foliage_aa/dxbc.{h,cpp}`, `cutout_rewrite.{h,cpp}` | The spike's reader-writer kept as is (header comment only). The matcher is now the §6.1 grammar, exactly: a plain `sample` (opcode 69, four operands) writing rA.c, at most one season `mad`, `add rT.c, rA.c, l(-cut)` (cut in (0, 1)), `lt rT.c, rT.c, l(0)` and `discard_nz rT.c` back to back, one discard, one `mov o0.w, l(0)`, no flow control, `ret` once and last, ps_4_x, no extended opcode tokens on the three, and the blob must round-trip byte-identical with a valid checksum before it is touched. The spike's modes 2 / 5 (drop only, lod debug) and the isotropic lod are gone. `Transform(match, Options{coverage, fillK})` emits: fill (aniso lod, §6.5; k an immediate) before the add, the alpha saved to a new temp before the add, the discard replaced by the sharpened alpha, `mov o0.w, P.x`; `coverage = false` (MSAA off) keeps the discard and o0.w = 0 and applies only the fill (§6.6). `OptionsFor(mode, k, msaa)` is the mode table. Every emit re-parses itself (identity + checksum) or is dropped. |
| The stage (D3D) | `foliage_aa_stage.{h,cpp}`, `foliage_aa_backend.h` | Per `PixelProgramData` the transformed copy of each matched variant (all four DETAIL variants), made from the user's own `_owner->_byteCode`. Per retail blend state an A2C clone (§6.3: AlphaToCoverage + BlendEnable, colour ONE / ZERO / ADD, alpha ZERO / ZERO / ADD, full mask); a retail state that blends gets no clone and its cutout draws stay retail. The "bound pair" (the retail PS / blend the engine asked for, and whether the copy / clone is bound in their place). A reset (restore the retail pair, release everything, re-make lazily) on a mode / k request, a device change (device lost) and an MSAA change (`Video::Configuration::multisample` per bind). The copies are made at program load (`PixelCreated`) once the request is applied, else at the first bind. One mutex (programs load on loader threads, bind on the main or the render thread). Unmatched blobs with a discard are logged once each (`[FoliageAA] untouched <hash> v<i>: <why>`). `FaaBackend::Request(mode, k)` is the D3D-free call the extension makes. |
| The extension | `src/ext/foliage_aa/foliage_aa.{h,cpp}`, `src/ext/registry.{h,cpp}`, `registry_boot.cpp` | Row `ext.foliage_aa` (class A, default OFF, "Smooth foliage edges" / "Soft leaf and needle edges (MSAA), filled in at a distance."). Valued key `ext.foliage_aa_mode` 1..2, default 2, through AOMENU's `ValueKey` code (clamp / malformed -> default). `Ext::FoliageAA::Current()` (mode = row ? key : 0), `Set()` (the AO pattern: mode <= 0 turns the row off and keeps the stored mode), `Sync()` (from `InitFromSettings`, every `SetAndRecord`, and `Set`): `Request(mode, mode == 2 ? kDistanceFillK : 0)`. `kDistanceFillK = 0.25f` is the named constant FAA-TUNE moves. |
| Tests | `tests/unit/ext_foliage_aa_test.cpp`, `ext_registry_values_test.cpp` | Seven tests on SYNTHETIC ps_4_0 containers built token by token in the test (section 4). |

## 2. The seam

`VideoDX11::g_extFoliageAA` (`std::atomic<FaaStage::Stage*>`, null by default; an acquire load is a
plain `mov` on x64). It is tested in five retail bodies, each `if (Stage* s = load) { ... }`:

| Body | Per | With the seam set |
|---|---|---|
| `PixelProgramData::Bind` | pixel-program bind | the stage binds the retail variant or its copy (+ clone / retail blend), then returns |
| `RenderStateData::Bind` | render-state bind | after retail's three calls: the clone over the retail blend state when the bound program is a copy |
| `PixelProgramData::PlatformCreate` | program load | make the copies |
| `~PixelProgramData`, `~RenderStateData` | destruction | forget the copies / the clone |

**Why two bind sites, not one.** `Video::Context::BindResources` binds the pixel program (dirty 0x80)
before the render state (0x20), so a clone bound with the program alone is overwritten by the retail
blend state a moment later; and the alpha-test props share `OpaqueMaterial.rsc:rs` with every solid
mesh, so a program change under an unchanged render state must restore the retail blend state from the
program bind. The faithful path pays one null test per program bind and one per render-state bind.
`src/engine` does not include `src/ext`; `src/ext/foliage_aa` includes only the D3D-free backend header.

**Set and cleared.** `Request(mode > 0)` stores the seam (main thread). `Request(0)` only records the
request: the binding thread's next pixel bind restores the retail blend state if a clone is bound,
releases everything and nulls the seam (under the mutex, so a racing turn-on re-sets it). A clear from
the main thread could leave the clone bound under a retail program, which is an invisible draw.

## 3. Evidence

Machine: Windows 10, NVIDIA, FL 11_0, 2560x1377, `VideoMultisample=3` (4x), `VideoShaderDetail=3`.
Every run held `game_dir_lock.sh` as `faacore` (`assert-held` with the exe before each log read), no
`*.pkm` mounted, `settings.cfg` backed up at the grant (md5 `f7fad13a...`) and restored byte-identical
after each session (`cmp` clean, md5 checked at both releases). Captures and logs:
`C:/GOG Games/Banished/rdoc_foliage/faa_*` and `rdoc_foliage/runs/`. Final exe md5 `e69cab95...`
(debug); the base exe for the faithful comparison was built from `40983144a` in this worktree
(`5c504953...`).

| Item | Result |
|---|---|
| `tools/build.sh run`, row off | Clean twice (the second on the final exe): autostart new game, ~37k frames in game, crash log 0, 0 `[FoliageAA]` lines, `ext.foliage_aa=0`. |
| Row on, mode 2 (user's settings + `ext.foliage_aa=1`: render thread, AO, everything on) | Clean, crash log 0 (the capture runs and the device-lost run below). |
| Row OFF == faithful (`trace_oracle --test`, base exe vs ours, both `profile=faithful`, seed 12345, `town,25`, frame 2000) | **0 findings on every screen**: S1 69 / 69 shared 69, S2 30 / 30, S4 0, S5 64 / 64, S6 5 / 5; S3 every verdict field 0.0%. |
| SUMMER in-game capture, row on (only FAA on, every other row and fix 0; `BANISHED_FORCE_SPEED=10`, in-game frame 20000; `faa_only_on2_f20k.rdc`) | Summer: the deciduous canopy program `a437d0d2` passes 10.57 M samples in the faithful twin `faa_off_ours_f20k.rdc` (FAA-SPIKE summer street 10.6 M; the frame-2000 captures were still in the spring leaf-in ramp, 3.6 M). |
| ... every §6.1 colour program transformed | `[FoliageAA] transformed` for all 24 blobs (6 programs x 4 variants, G5 / G6 r1.x included) at load (the device-lost run's `24 transformed (6 programs), 0 create failures`); in the summer frame 54 A2C draws, each with a transformed PS; the depth programs (`f7d47395`, `17cb63f8`, `9f58bc58`) and the water-clip programs logged `untouched`. |
| ... A2C on, the clone bound | `A2C clone of blend state ...: hr=0`, `first transformed bind of gen 1 (A2C clone bound)`; 0 A2C draws with a retail PS, 0 transformed PS without A2C. |
| ... RT alpha 0 under the leaves (`a2c_check.py`, all 54 A2C draws, main ms4 + reflection ms1) | covered 14,495,238 samples, **post-draw alpha non-zero 0**, alpha changed 491,751 = exactly the covered samples whose old alpha was non-zero (retail writes 0 there); partial pixels (1-3 of 4 samples) 280,708. Mode 1: the same, post-alpha non-zero 0, partial 290,189. |
| ... the water identical / trace_oracle differs only in those draws | Raw oracle on2 vs off: S4 0, S6 0, S1 / S2 one-sided 11-12 / 7-8. With each transformed PS mapped back to its retail program and the clone back to the retail blend state (`build/faa/only_diff.py`, not in the tree): **mode 1: 32 (off) vs 31 (on) signatures, 0 on-only** (1 off-only: a draw the on frame did not have); mode 2: one one-sided signature per side (the on side's is a 772 B instanced program absent from the off frame) (the measured S2 noise floor between identical runs is 0-3). Every non-A2C draw's PS, the water's included, is in the faithful trace. |
| Mode 1 vs mode 2 | Both captured (`faa_only_on1_f20k`, `faa_only_on2_f20k`): mode 1 logs `mode 1 (smooth edges) ... fill k 0.000` and its copies have no `resinfo`; mode 2 `fill k 0.250` (+580 B per blob). |
| Live toggle, one session | Main menu (the summer backdrop), row off at boot, `BANISHED_AUTOMENU_BUTTON=13` opens Extensions, then real mouse input (AOTUNE's `ui.ps1`, a copy in git-ignored `build/faa/`): **off -> on** (`request mode 2 ... seam set`, `stage gen 1`, clone, first transformed bind), **on -> off** (`stage reset (mode / k request) ... 142471 of 569884 pixel binds transformed`, `stage off: retail state restored, seam cleared`), **off -> on** again (gen 3). Crash log 0. `rdoc_foliage/runs/faa_live_toggle.error.log`, screenshots `toggle_*.png`. |
| Device lost, row on | `BANISHED_FORCE_DEVICE_LOST=12000` on the newgame route (render thread on): injected, `recreate OK`, `stage reset (program load)` on the new device, 24 copies re-made, clone re-made, `device-lost round trip COMPLETE`; ran on to frame 209k, crash log 0. `runs/faa_devlost_on2.error.log`. |
| Unit tests | `tools/build.sh unit`: 1225 / 0 (7 new). |
| Not run | `tools/build.sh stress` (the brief: the coordinator's, machine not clean); resave gate, acceptance, castmetric (the coordinator's gate). |

## 4. The unit tests (`tests/unit/ext_foliage_aa_test.cpp`)

All blobs are assembled in the test from the instruction forms of §6.1 (no shader's content):
the container round trip and the checksum over both final-block layouts, a corrupted byte caught, one
frozen checksum; the grammar's two rT variants (r0.w over the alpha, r1.x separate) with and without
the season mad; eight refused near misses (the depth shape without `mov o0.w`, discard_z, flow
control, the water clip on an interpolant, a mov between add and lt, a cut of 1.5, a vertex program,
ps_5_0), each left byte-identical in every mode; the coverage emit over the four DETAIL temp counts
(3 / 3 / 4 / 4) with the other chunks and everything before the add token-identical; the fill with and
without coverage (k an immediate, a k change re-emits a different blob, MSAA off keeps the discard and
o0.w = 0, mode 1 without MSAA emits nothing); the registry row and the mode key.

## 5. Open items

- **GPU cost** row on vs off (FOLIAGE_AA §6.6 / S7): the coordinator's stress pair. The on path adds
  per pixel-program bind one uncontended mutex + one hash lookup, per render-state bind one mutex.
- **The Extensions dialog row has no icon** for `foliage_aa` (nor for FAA-SHADOW's row): the sprite
  sheet `src/ext/ui/Dialog/ExtensionsSprites.rsc` has none for them (screenshot `toggle_ext4.png`).
  FAA-MENU's or the ext-resources owner's.
- **Look / tuning** (FAA-TUNE): k (0.25), the fill floor for near-zero-alpha texels (§6.5), the
  MSAA-off fallback, AO's sample-0 resolve at leaf edges (§6.4).
- `cfg->multisample > 0` decides coverage per bind; the reflection pass is 1x and gets A2C too (as the
  spike measured; its alpha is 0 on every covered sample).
- The menu: FAA-MENU needs only `Ext::FoliageAA::Current()/Set()` and the key.

## 6. Nothing retail entered the tree

`git diff --stat main...HEAD` reviewed: C++ sources and headers, one unit test, `sources.cmake` and
this markdown. No DXBC blob or dump, no `.srsl` / `.rsc` copy, no capture, no texture, no screenshot.
The test blobs are synthetic. Every transformed shader exists only in memory, made from the user's
installed bytecode at run time.
