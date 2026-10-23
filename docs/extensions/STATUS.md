# Fixes and extensions -- status (newest first)

## 2026-10-17 -- BETTER TREES: `ext.better_trees` replaces `foliage_aa` + `billboard_shadow` (keys migrated), with the new per-texel shadow dapple; merged (gate 0) and RULED by the user (noise 0.05, scale 32); SESSION CLOSED

- **TS-HASH first** (`handins/TS-CORE.md` "TS-HASH"): the "per-vertex uv hash" follow-up was moot. The user's
  vertex buffer shows every card's four corners carry four DIFFERENT base positions (a 0.055 x 0.090 rectangle,
  0.7..4 hash cycles apart), so the centre hash is already per vertex; a vertex-side term can only cut a card
  along a line or a shallow hyperbola. The user: "combine both into a 'better trees' extension", one row, build
  the noise too.
- **`ext.better_trees`** (`handins/BETTER-TREES.md`): class A, default OFF, Enhanced tab combo Off / On / On +
  distance fill (the tab is five rows again); `ext.foliage_aa_mode` keeps its key (owner better_trees);
  `InitFromSettings` migrates `ext.foliage_aa=1` or `ext.billboard_shadow=1` to the new key (seen live on the
  user's file). The three rewrites: the cutout (FAA), the receiver lookup (TS-DAPPLE, the user's -0.05 / 0.4), and
  the **edge dither**: the billboard VERTEX rewrite hands the card's half extent over in the shadow output's
  unused w (negative), the billboard PIXEL rewrite (`edge_dither.{h,cpp}`, chained after the cutout in the FAA
  pixel stage) adds a hash of the texcoord (cells of 1 / noiseScale uv) times that extent to the compare depth:
  the straight cut inside a card becomes a leaf-cluster-scale jagged edge. Three guards after a broken first arm
  (terrain black): cutout programs only, the ISGN must say the vertex stage writes w, -w taken only in (0, 0.1).
- **The user's ruling, in game with the sliders: noise 0.05, scale 32** (`kDefaultParams`; the block shipped at 0
  until then). Sliders `noise` / `noise scale` in the Alt+F10 "Billboard shadow" panel; the user picked by eye (`rdoc_tree/BT_noise_sheet.png`: 0 vs 0.5 / 64 at yaw 0 / 90; the 8-texel
  cells show as a square grain, a finer scale is the first thing to try). DETAIL v2 variants are not dithered
  (their compare depth goes through a temp; follow-up only if someone runs that level).
- Gate: unit 1241 / 0, acceptance 0, faithful run clean, resave PASS (see the hand-in §5).

## 2026-10-16 night -- TOWN CHATTER + TREE SHADOW: `ext.seamless_loops` and `ext.billboard_shadow` built, merged (gate 0) and RULED by the user (chatter good; tree shading margin -0.05 / jitter 0.4); SESSION CLOSED

- **The user's two complaints** (an opus agent each, the coordinator advising; phase 1 diagnosis, phase 2 build). Both are
  RETAIL behaviour, so both ship as class A extensions (DESIGN.md), default OFF, Enhanced tab rows 5 and 6.
- **Town chatter harsh start / stop** (`handins/CHATTER-DIAG.md`, `handins/SEAMLESS.md`): the chatter is
  `Audio/Effects/CrowdEffect.rsc` (Crowd.wav, 15.4 s loop, slot 1 of every occupied house's emitter, started by
  `ResidenceComponent::AddResident`). Its decoded PCM ends in 24.7 ms of codec padding (2..58 against a body never
  below 312) and a looped buffer replays whole (XAudio2 2.7 identical, XWMA.md), so the murmur drops out and slams
  back at every wrap; every looping ambience carries the same tail (River 21.8 ms, field 43.5 ms, Fire / Rain /
  Tornado / Wind 10..22). No fade exists anywhere in retail's audio logic (nothing lost in the port); the distance
  edge and the SDL backend ruled out. **`ext.seamless_loops`** "Seamless ambient loops": at `DecodeAtLoad`, for
  loop-flagged buffers only (the loop is the buffer's own flag; PlayPlatform loops by it alone), trim at most one
  WMA frame of tail (64-frame windows under 20 % of the sound's 1st-percentile 10 ms RMS), then a 50 ms equal-power
  fold of the end into the start; takes effect at the next start. Seam `Audio::SoundBuffer::_extLoopSeam` (null by
  default; one null test per decode). Row off: `[SoundDecode] pcmHash` unchanged. Row on: 10 `[SeamlessLoops]`
  lines = the ten looping ambiences of 56 buffers; Crowd's wrap `45 19 | 476` -> `647 565 | 542`, seam jump 302 -> 138.
  7 unit tests. Deferred: option B (fade-in / out at voice start / stop: the pause menu cut, a house gaining or
  losing its family) only if the user still hears harshness with A on.
- **Deciduous tree shadows rotate with the camera** (`docs/extensions/TREE_SHADOW.md`, `handins/TS-CORE.md`): the
  ground shadow is right (the shadow pass binds the SUN camera through `group._cullCamera`, so the cards face the
  light; capture eid 655 vs 1061). The straight line that turns is the RECEIVER: the colour pass draws the cards
  viewer-facing and looks the shadow up from them, so each card is cut through its centre by its own sun-facing
  twin; the lit / dark boundary is a plane perpendicular to the sun = one straight screen line per view. Oak,
  birch, the fruit / nut trees; blueberry shares the vertex shader. Light-facing shadow cards (SpeedTree / Unity)
  are what retail already does; Unity carries the same receiver artefact as Won't Fix. **`ext.billboard_shadow`**
  "Stable tree shading": a load-time DXBC rewrite of the user's installed billboard vertex shader (45 instructions
  cloned, the shadow lookup from the card rebuilt sun-facing where its caster was; `receiver_rewrite.{h,cpp}`,
  the stage in `src/render/dx11/better_trees/`, seam `VideoDX11::g_extBillboardShadow`); the sun camera's
  worldToCamera rides in the unused light constant `textureProjection` (cb2 rows 6..9), written by
  `SunLight::DrawLight` through `SunLight::_extBillboardShadow` (null by default; bit-identical to the shadow
  pass's matrix; a mod shader reading that constant would see it, row on only). Evidence: the two-heading
  camera-pin pair (`rdoc_tree/crop_{off,on}_y{0,90}.png`: off = bright cap over a line that runs differently at
  the two headings; on = no line, card-sized dapple); no acne on the top cards; trace_oracle row off == faithful
  (0 on six screens), row on differs only in 8 billboard draws' vertex shader; live toggle, settings round trip,
  device-lost with the render thread, render_pipe on / off; stress clean machine off 911 / on 901 fps (-1.1 %,
  noise), sim 0.16 both. `BANISHED_CAMERA_PIN` takes an optional yaw (`x,y,dist,yaw` / `town,dist,yaw`).
  **Look owed (the user):** with the row on the summer canopies carry more interior shade than retail's lit cap
  (the cards shadowing each other); all seasons, street and overview.
- **The user's check pass:** chatter GOOD. Tree shadows: the first lookup lit the crowns on the side AWAY from the sun
  (the spike carried the viewer-axis card offset onto the sun card's axes, whose up points away from the sun; the
  spike only checked that the line stopped turning, never which side was lit). **TS-FIX** (merge `16c8a205b`,
  `handins/TS-CORE.md` TS-FIX section): the lookup now slides the drawn position along the light direction onto the
  card's caster plane (retail's own light-space position, the caster's depth there: no self-shadow, no turning line,
  the sun-side casters are the front ones). +21 instructions / 3 temps; `rdoc_tree/cropfix_{on,off}_y90.png`: conifers
  and oak crowns lit on the same (sun) side. Rule for this row's look pairs: the sun direction and a conifer in frame.
  Re-gate: faithful run 93 s clean, resave PASS == golden, unit 1239 / 0, acceptance 0, castmetric 2.
- **Second check pass:** still a soft dark band over the sun side of the crowns (`treeshadow2.png`): the plane solve put
  the lookup depth 0.48 units BEHIND the card's own caster at the top of a sun-side crown (RenderDoc pixel debug), a
  softened self-shadow; and the single-heading evidence was never pinned (the camera pin does not run while the window
  is unfocused: the game pauses). **TS-FIX2** (merge `d34e4e4a1`, `handins/TS-CORE.md` TS-FIX2): the lookup keeps the
  fragment's own light-space texel and takes the card CENTRE's depth moved a half extent toward the light
  (`P + (dot(C - P, L) - k|C - P|) L`, k = `BbsRewrite::kMargin` = 1, L = `lc.nearPlane`): no card can shadow itself at
  any heading; 7 instructions, 4260 -> 4468 B; the sun-basis carrier (`textureProjection`, `SunLight::DrawLight`
  write, `_extBillboardShadow`) is GONE, the row is one D3D-side seam and the mod caveat with it. Verified by a
  five-heading sweep (yaw 0 / 90 / 180 / 270 + street, conifers in frame, pinned and checked by the `[CameraPin]`
  line): `rdoc_tree/TSFIX2_contact_sheet.png`. Trade-off for the user: crowns read evenly lit rather than dappled;
  k down to ~0.71 still never self-shadows and brings some inner-crown shade back.
  Re-gate (main `d34e4e4a1`): faithful run clean (crash log 0), resave PASS == golden, unit 1239 / 0, acceptance 0,
  castmetric 2, abi_audit OK. **TS-TWEAK** merged `a727cf7bd` (unit 1239 / 0, acceptance 0): k is a live slider in the
  debug Alt+F10 overlay ("Billboard shadow" panel, 0..1.5 step 0.05, default 1.0, one re-create per release, device-lost
  clean at 0.45; `rdoc_tree/TSTWEAK_k050_k100.png`); no settings key, the user's pick becomes `BbsRewrite::kMargin`.
  (The k slider was superseded by TS-DAPPLE's three sliders the same night; the perf-exe byte check was not run: the rewrite
  body changed anyway.)
- **Third check pass (the user): "nothing actually looks good ... I like the og game's look, it's just the hard
  clipping on the crowns that's off."** The margin approach was wrong for the brief: it removes the self-shadow,
  and the self-shadow IS retail's dapple. **TS-DAPPLE** (the coordinator, by hand; `handins/TS-CORE.md` TS-DAPPLE):
  the lookup keeps retail's own position and depth and adds a per-card pseudo-random depth offset (a hash of the
  card centre, in half extents) so each card's cut lands somewhere else across its card: the halves and the dapple
  stay, the straight edge dissolves. Three immediates basis / margin / jitter (0 / 0 / 0 = retail exactly;
  1 / 1 / 0 = TS-FIX2), shipped 0 / 0 / 0.5, all three live sliders in the Alt+F10 overlay with "default" and
  "retail" buttons. Four-heading sweep retail vs 0/0/0.5, 0/0/1.0, 0/0.25/0.5 (`rdoc_tree/DAPPLE_*_zoom.png`,
  1:1 crops): the edge gone at every heading in all three; 0.5 keeps the crown structure closest to retail.
  **The user's ruling: margin -0.05, jitter 0.4, locked in** (`kDefaultParams{0, -0.05, 0.4}`); residual: a straight
  cut inside a single large card on small crowns (a per-card tilt would need a per-vertex uv hash; not taken). Gate: unit 1239 / 0, acceptance 0, castmetric 2, abi OK,
  faithful run clean, resave PASS.
- **Close (2026-10-16 night):** the two agent worktrees / branches deleted (both merged); row icons for the two rows
  (block EXTICONS2, sonnet, `tools/ext_icons/`); settings.cfg carries both rows ON for the user (backup
  `settings.cfg.pre_check_20261016`). Left for a later session, none urgent: the seamless-loops option B (a fade at
  voice start / stop) only if the user ever hears the pause-menu cut as harsh; a per-card TILT of the billboard cut
  for the residual straight cut on small crowns -- CHECKED 2026-10-17 (TS-CORE.md "TS-HASH"): the centre hash is already per vertex (the card's four base positions differ), a uv seed changes nothing; only a per-texel pixel-shader term would, and that shares the billboard PS with foliage_aa (user's call); the Enhanced tab is six rows (fits).
- **Gate** (main `40926d998`, both merges): faithful in-game 113 s clean (crash log 0), resave PASS == golden
  (23,453,453 bytes), unit 1239 / 0, acceptance 0, castmetric 2, abi_audit OK. Mods parked (0 mounted). The
  two agent worktrees / branches kept until the user says delete. Also: main's `third_party/SDL` submodule was
  found empty (since 18:34) and restored with `git submodule update --init`.

## 2026-10-16 -- FOLIAGE AA + MAX SHADOW RES built and merged (gates 0); AOTUNE part 1 + the GPU cost per tier; the debug ImGui tweak overlay; row 1329 closed (driver heap, not ours)

- **Foliage edge smoothing** (`docs/extensions/FOLIAGE_AA.md`): five blocks in one day, every one merged at gate 0.
  **FAA-SPIKE** (`handins/FAA-SPIKE.md`): GO on S1..S8; three findings changed the plan (§6): the A2C blend clone zeroes
  the alpha through a ONE/ZERO colour + ZERO/ZERO alpha blend (the water reads the reflection target's alpha, an RGB write
  mask leaves the old alpha under the leaves); k is an immediate and the anisotropic lod is used (a mode / k change
  re-creates the 24 shaders); the retail Shadow Resolution combo is THREE steps (High / Medium / Low = 2048 / 1024 / 512),
  so 4096 applies at step 0 only. The pure DXBC reader-writer + matcher merged from the spike. **FAA-CORE**
  (`handins/FAA-CORE.md`): the rewriter over the user's own installed bytecode (a blob must round-trip byte-identical with a
  valid checksum first), 24 transformed copies, the A2C clones, row `ext.foliage_aa` + valued key `ext.foliage_aa_mode`,
  seam `VideoDX11::g_extFoliageAA` (4 acquire-load sites: the program binds before the render state), 7 unit tests on
  synthetic SM4 blobs; row off == faithful on trace_oracle; summer capture: 54 cutout draws transformed, 0 covered samples
  with non-zero alpha. **FAA-SHADOW** (`handins/FAA-SHADOW.md`): `ext.max_shadow_res`, seam `SunLight::_extMaxShadowRes`,
  the map 4096 only at scalar 0, texel 1/4096 in the capture, +58 MB GPU (DX11 builds no colour target beside the map).
  **FAA-MENU** (`handins/FAA-MENU.md`, sonnet): the Enhanced tab moved to `src/ext/enhanced_tab/` (each extension contributes
  its entry), "Foliage edges" combo + "Max shadow resolution" combo, icons for both rows; **two render_pipe deadlocks found
  and fixed**: raising `Device::settingsChanged` from the render thread (the per-frame sun-map sync in `AddDrawItems`) hangs
  the exe -- the rebuild is now requested only from the two main-thread sites (the row toggle, the retail combo's apply) and
  the per-frame sync is log-only. **FAA-TUNE** agent part (`handins/FAA-TUNE.md`): the fill floor 0.15 gates coverage too
  (a fill-only floor left 87% of the speckle samples), ramp unchanged (1.0 / 1.2 px), k 0.25 -> 0.15 (+6.3% canopy over
  the mip-0 reference at the overview; 0.25 = +8.0%, 0.40 = +9.6%), MSAA off keeps the discard; a 38-screenshot
  four-season look set in `C:/GOG Games/Banished/faa_tune/`. **The user's look ruling is owed** (k, floor, defaults).
- **AOTUNE part 1** (`handins/AOTUNE.md`): 7.3 live toggle PASS (six states captured, memory flat over 10 cycles: the DSV
  fix holds), device-lost with the row on PASS, 7.4 eye camera + AO PASS, 7.6 `capture_arm.sh` fixed (in-game capture by
  rank after the `capture armed for 0 frames` line -- the menu loop captures its own frame 10 whenever RenderDoc is
  attached, so every "newest file" pick was the menu backdrop; PID-only kill); the same fix in `dx11_state_diff/capture.sh`.
  **7.2 GPU cost per tier** (coordinator, perf exe, 4x MSAA, fps uncapped, machine CLEAN): faithful 904 fps; AO Low 866
  (-4.2%), Medium 838 (-7.3%), High 785 (-13.2%); foliage mode 1 / 2 within noise; 4096 map -1.5%; sim / draw CPU
  unchanged on every leg (AOTUNE.md §7.2). A first attempt measured the DEBUG exe (`--no-build` skips the deploy) and is
  parked under `perf/results/_invalid_debug_exe_20261016/`.
- **Row 1329 CLOSED, not a port defect** (`handins/DEVLOSTMEM.md`): the post-device-loss memory climb AOTUNE found is the
  NVIDIA UMD's own heap blocks (every growing block carries a pointer into `nvwgf2umx.dll`), a no-loss control run climbs
  the same way, we create 0 GPU objects after the trip and bind 0 device-0 resources on device 1; the climb appears in 4 of
  11 identical runs.
- **TWEAKUI** (`handins/TWEAKUI.md`, `docs/reference/tweak_overlay.md`): Dear ImGui v1.92.9b vendored (core + SDL3 / DX11
  backends, MIT), `BANISHED_TWEAK_OVERLAY` ON for debug on Windows only (perf code / data byte-identical with it OFF),
  Alt+F10 (a `#define`), `src/diag/win/tweak_overlay.cpp`: rows / AO / foliage / shadow panels through the extensions' live
  paths (changes queued, applied on the main thread), drawn before Present on the presenting thread (render thread under
  render_pipe), follows device loss, "Write to settings.cfg" / "Reset defaults", "faithful profile: nothing to tweak".
- **Gates on main**: FAA (`runs/FAA`): routes clean, resave 5/5 == golden, unit 1225 / 0, one problem = five perf-build
  warnings inside the verbatim Intel ASSAO copy (the Oct-11 baselines predate AO) -> silenced per file in CMake. FAA2
  (`runs/FAA2`): routes clean, resave 3/3, unit 1226 / 0, 0 new warnings, one problem = the overlay's Win32 / D3D includes
  under `src/diag/` -> moved to `src/diag/win/` (excluded from that glob, added by the option alone), abi_audit OK after.
  acceptance 0, castmetric 2 (the DXBC writer's seven byte copies marked `CAST-OK: byte_io`).
- **The user's look rulings (evening, live through the overlay)**: AO -- every tier fine, the overview radius
  `AoMath::kRadiusOverview` 4.0 -> 5.0 (AO.md 7.1 RULED); foliage -- k 0.15 and floor 0.15 stay, mode 2 is the look, the
  row ships OFF (FAA-TUNE.md §6); and the shadow entry should offer 4096 AND 8192 -> **FAA-SHADOW2**
  (`handins/FAA-SHADOW2.md`): valued key `ext.max_shadow_size` (4096 | 8192, fixed choices, default 4096), the Options
  combo and the overlay both Off / 4096 / 8192, the size change requested from the main thread like the toggle, a one-time
  8192 probe through the engine's own ImageBuffer that falls back to 4096 with one `[ShadowRes]` line (the key left as set);
  GPU dedicated off ~423 MB / 4096 ~488 / 8192 ~727; live switching under render_pipe, device-lost at 8192, settings
  round-trips all clean; unit 1229 / 0.
- **Gate FAA3** (`runs/FAA3`, main `280e17477`): 0 problems -- routes clean, resave 3/3 == golden, unit 1229 / 0, 0 new
  warnings, abi_audit OK, castmetric 2.
- **Branches cleaned (the user, evening)**: every agent worktree removed and every `worktree-agent-*` branch deleted; the
  unmerged ones pinned as tags `legacy/ao-spike-20261014`, `legacy/faa-spike-20261016`, `legacy/walkbatch-own-tu-unbuilt`
  (XFORM2 was already `legacy/perf-20261013-xform2-unmerged`). Only `main` remains. Every tag was deleted 2026-10-18
  before the repo went public; the unmerged code is kept off the repo in a git bundle.
- **The user's by-hand check of the gated debug exe (evening): "lgtm"** -- the overlay, the shadow combo at Off / 4096 /
  8192, the foliage combo and the AO tiers, crash log 0. Session CLOSED.
- Left: AO 7.5 (Linux, hypervisor off), 7.7 (the next full gate's new-game count / worldcmp with a row on), 7.8 (AO on the
  menu backdrop, the user's call). Nothing in progress.

## 2026-10-14 evening -- AMBIENT OCCLUSION: `ext.ambient_occlusion` (class A, OFF) shipped with the Options "Enhanced" tab (Off / Low / Medium / High + strength 0-100); AOTUNE (the look) open

- Plan `AO.md` (the user's rulings: Intel ASSAO first, MIT; post-multiply the finished opaque colour between the Opaque
  and Blended lists; a fifth Options tab; a spike before any port). Three opus blocks, the coordinator ran the gate.
- **AOSPIKE** (`handins/AOSPIKE.md`): go on all eight questions. Facts: `fxc` blobs offline (`tools/ao_shaders.py`); the
  GCC port of `ASSAODX11.cpp` is +31/-26 lines; the main depth can be `R24G8_TYPELESS` + SRV under MSAA with the faithful
  arm untouched; DX11 depth is 0..1, `viewDepth = 2.00784 / (1.00392 - z)`, ASSAO takes the projection with rows 1 / 2
  swapped; **a loose `.crs` never overrides a packaged object** (`OpenFile` reads the pack index first -- DESIGN §1
  corrected); `TabElement::AddTab` adds a fifth tab at run time; Sky leaves depth 1.0; list 2 (Effect) is always empty;
  the main-menu backdrop passes the main-entry test (so the seam is armed from the game's draw only); the empty hook
  costs nothing measurable.
- **AOCORE** (`handins/AOCORE.md`, merge `4335ca373`): `src/render/dx11/ao/` (the readable main depth chosen per rebuild,
  the MSAA depth resolve to raw R32F -- ASSAO's prepare pass linearises, proven by a unit test on the constants --, the
  ASSAO stage behind `ao_stage.h`, the apply = ASSAO's multiply with the opacity folded into `ShadowMultiplier`), ONE seam
  `Graphics::Renderer::_extAo` (null-default static, read once per frame, tested at k=0 -> 1 of the main entry in both
  `Renderer::Draw` and render_pipe's `Pass2`), armed / disarmed around the one `Renderer::Draw` of `GameUpdateLoop::Draw`.
  trace_oracle: row off == faithful (0 missing on every screen); RenderDoc: resolve, ASSAO passes, apply into the MSAA
  colour, then Blended. Stress (CLEAN, fps RTSS-capped 140): faithful sim 0.15 / draw 0.90 ms; Low / Medium / High within
  -1.7..+2.7% sim, -1.6..+2.6% draw, ~10 MB more memory. D3D11 debug layer 0 messages over 18k frames.
- **AOMENU** (`handins/AOMENU.md`, merge `f333a092d`): the registry's first VALUED keys (`ValueKey` table: `ext.ao_quality`
  1..3 default 2, `ext.ao_opacity` 0..100 default 100; decimal parse, clamp, malformed -> default, profiles leave values
  at defaults; 5 unit tests); `Ext::AO::Current()` / `Set()` (`settings.cpp`); the "Enhanced" tab by `AddTab` before the tab
  restore, shown whenever the build has the extension, hidden under `/profile:faithful`; the `lastTab > 3` clamp was the
  port's (retail 0x1400750db passes it unchecked) -> bounded by `_tabCount - 1`. OK persists, Cancel discards, the
  Extensions checkbox mirrors `quality > 0`, the pause-menu Options shows the tab.
- **Gate on main after both merges** (`e2377de58`): `tools/build.sh run` faithful in-game clean (crash log 0), resave
  5/5 PASS == golden 23,453,453, unit 1216 / 0, acceptance 0 off target, castmetric 2 (ao_pass through
  `Dx11ImageBackend`; the verbatim Intel copy excluded from the metric).
- **The user's look, release exe, the same evening: "overall looks good"** (AO.md §7.0). Rulings and fixes, all on main:
  `ext.ao_opacity` **0..30 default 20** (`5db810549`; 20 is the usable maximum, past 30 "iffy"); **AO fades with the fog**
  (`AoMath::FadeFromFog`: the ASSAO fade takes the frame's `Context::_fogParams` range -- the materials fog in their pixel
  shaders, so the multiply must be gone where the fog is); and a **boot defect the look found: "no window"** -- with the row
  ON in settings.cfg the boot-time readable-depth rebuild left `settingsChanged` set before the first frame,
  `DeviceData_Resize` early-outed on the never-shown window, `BeginCommands` refused every frame, no `Draw`, and the
  deferred reveal (in the splash `Draw`) never fired: every flavour. Fixed `772744560`: the early-out is skipped while
  `BootWindow::IsPending()`. Second look: fog blend good.
- **Close gate** (main `3175ae398` + the fix): run faithful in-game clean (crash log 0), resave PASS == golden, unit
  1217 / 0, acceptance 0, castmetric 2. worldcmp and the new-game 5694 quit-route count not run (the ext is OFF in
  faithful; resave covered load / save). Open: AO.md §7 (AOTUNE: GPU cost per tier with RTSS stopped, the live toggle
  and device-lost with the row on, eye camera, Linux / DXVK, `capture_arm.sh`, trees). Worktrees / branches of the
  three blocks kept until the user says delete.


## 2026-10-14 afternoon -- THE LOADING SESSION: new game 9.86 -> 3.23 s, save load 4.07 -> 3.10 s (1.19 s with ext.quick_screens), boot 4.74 -> 4.46 s (2.89 s); every world byte-identical

- Plan `handins/LOADPLAN.md` (census, the user's four rulings: splash + save-dialog floors = one class A toggle, fades stay; world
  generation byte-identical only; boot left as is after SPLASH), research `handins/LOADRESEARCH.md`, kit `handins/loadkit/`
  (census / `[WorldHash]` scripts + the 9-world baseline). Seven opus blocks, static gate only; the coordinator ran every gate,
  identity check and census. Results table: LOADPLAN §6.
- **LOADSTAMP**: `[LoadTime]` stage stamps (always on, print-only) + the sampler on every registered game thread
  (`sample_report.py --thread / --from / --to`); `[WorldHash]` (height f32 / i16, blend, tiles) after world build.
- **AUDIODEC**: the XWMA float->S16 conversion (79% of the game resource load) through SSE2, exact. **WGPAR**: `System::ParallelFor`
  (pool = physical - 1, cap 7), worldgen base / blend split by rows, the brush falloff pow as an exact table, startTown diag lines
  folded. **SAVELOAD**: cheaper exact `AdjustDelimiters` / `GetLength`, an `ExpandRelativePath` memo (debug shadow-checks every hit).
  **SPLASH**: `ext.quick_screens` (class A, OFF in faithful): splash / loading-screen / save-dialog minimum times end when the work is
  done. **EROSION** (exact single-thread 1.2x) + **EROSIONPAR** (exact speculative-parallel erosion, in-order commit; 3.58 -> 1.20 s).

## 2026-10-14 night -- THE MULTITHREADING SESSION: render thread + parallel cull shipped (gates 0, shadows 0); 4-core heavy speed 10 +67%, speed 50 +35%; the in-game fps work is COMPLETE (the user), next phase = loading speed

- Plan `handins/THREADPLAN.md` (decisions on THREADRESEARCH's eight questions: class A live toggle, one frame of latency accepted,
  counts DERIVED from `src/ext/cpu/roster.h` (now `src/platform/common/cpu/roster.h`, moved 2026-10-20), physical cores inside the affinity mask, path workers BELOW_NORMAL, TSan deferred).
  Four opus worktree blocks, static gate only; the coordinator ran every gate, shadow run, the device-loss route and the A/Bs.
- **CPULIMIT** (`handins/CPULIMIT.md`, merge `c09a54566`, gate 0, unit 1156 / 0): `sys.cpu_limit` (settings.cfg, a CPU mask applied
  before any thread exists, pre-read from the file), `src/ext/cpu/` (now `src/platform/common/cpu/`, moved 2026-10-20: a platform helper, not an extension) (topology: logical / physical cores inside the mask; the 7.4 roster
  as pure functions), path workers = clamp(physical - 2 - render_pipe, 1, 4) at BELOW_NORMAL, the `[Cpu]` line on `[BootState]` and in
  the stress header. This box: P4 = `0x55`, S4 = `0xf`. `stress_run.sh` takes `STRESS_CPU_LIMIT=<mask>`.
- **RENDERPIPE-0** (`handins/RENDERPIPE0.md`, merge `76bdc5718`, gate 0, unit 1164 / 0): `ext.render_pipe` (class A, default OFF) as the
  frame split at the end of pass 1, run single-threaded in the pipelined order through the one `MainLoop::_extFrame` seam (two null tests
  per Frame, none in the step loop); two Renderers, the packet of recorded context writes, the main-owned frame index, `SunLight::Set` /
  `UpdateCamera` only when nothing is held; shadows RenderPipe / RenderPipeFrame / RenderPipeIdem all 0 in four runs; the split neutral;
  the pass-1 share **a = 0.45-0.54** measured at COMPPROF=0 (THREADRESEARCH assumed 0.55).
- **CULLPAR** (`handins/CULLPAR.md`, merge `429cc7e79`, gate 0, unit 1168 / 0): `ext.gather_threads` (class A, default OFF, implies
  draw_gather): the gather walk split at depth 2 into frontier subtree tasks on `Cpu::CullHelpers` spinning helpers (one wake per frame,
  parked at the end of pass 1 through `Scene::_extDrawPass1End`), joined in DFS order; DrawGather / DrawGatherSide shadows 0, side array
  never grew. **Draw -17..-20% heavy / -14..-16% compact with 2 helpers** (8 physical); 0 helpers on 4 cores (the sequential walk).
- **RENDERPIPE-1** (`handins/RENDERPIPE1.md`, merge `bc98c7cd7`, gate 0, unit 1172 / 0): the packet on one event-parked render thread
  (depth 1, the fence before every step, every drain a join, the device lock with the emit; `ContextData::Finish` copied and cut into
  FinishPresent on the thread / FinishRecover on the main thread at the next join). Shadows with the thread on all 0 (RenderPipe pre-hash
  concurrent with the next pass 1); **forced device loss: one round trip, the thread survives**. Hand-off wait 70-230 us per frame,
  packet latency 0.4-0.65 ms.
- **The masked series ab4m** (CLEAN, overlays stopped, interleaved x2): **P4 (4 physical) heavy speed 10 773 -> 1294 fps (+67%), speed 50
  199 -> 268 (+35%), compact speed 10 1182 -> 1684 / 1907**; unrestricted heavy speed 10 835 -> 1272 / 1451, compact 1355 -> 1988 / 2093;
  S4 (2 physical) neutral (`sequential split`); sim ms / iteration unchanged in every pair. THREADRESEARCH 5.1 predicted +73% / +44%.
- **Speed-50 close-out on the final exe** (heavy spread, 1800 frames, fps w1 / w2 / w3): faithful 529 / 364 / 191 and 543 / 471 / 226;
  everything on 387 / 360 / 247; everything + grid 435 / 373 / 266; **everything + grid + render_pipe + gather_threads 752 / 618 / 425**
  (x2: 750 / 631 / 421; was 412 / 375 / 282 at the 2026-10-13 close). **The user (2026-10-14 00:35): the in-game fps improvements are
  complete; the next phase is speeding up the loading.**
- **Defect found by the series and fixed** (`ext: render_pipe: EyeCamera::Active() only inside the built game`, gate row EYEFIX): the hook
  ran `EyeCamera::Active()` from the menu loop at boot (null `m_toolManager`), so every `eye_camera + render_pipe` run crashed at frame 0.
  The same configuration hung ONCE at quit before the fix (`Map::~Map` -> `PathfinderThread::DrainQueue`, row 1328); 7 / 7 clean after it.
- Housekeeping: overlays (Afterburner / RTSS / RTSSHooksLoader64) restarted and the mods moved back at the session close; the hypervisor
  still OFF (the user's call). The four agent worktrees under `.claude/worktrees/` can be removed.

## 2026-10-13 night -- the three next ideas taken: SORTMEMO shipped (draw -14% / -7%), WALKGRID merged as the opt-in class B row, XFORM2 measured neutral and not merged

- Three opus worktree blocks (static gate only; the coordinator ran every shadow, row, A/B and gate; the leg exes deployed by hand
  under the lock -- `stress_run.sh --no-build` runs whatever is deployed -- with the identity verified per run; mods parked;
  overlays stopped; A/B series ab3 interleaved x2, every run `machine load: CLEAN`; `perf/results/*_ab3_*`).
- **SORTMEMO** (`handins/SORTMEMO.md`, merge `ee4f41e9f`, gate 0, unit 1151 / 0): the draw-item sort memo. Per list buffer the
  previous sort's input and output bytes; a masked compare (the two uninitialised pad dwords excluded) decides a hit: byte-equal
  input copies the stored output, a pad-only hit applies the stored permutation; exact because the sort is a pure function of the
  non-pad input bytes (+ MXCSR.DAZ, stored per entry). Census raw 81% / 89%, masked 87% / 92% of lists (heavy / compact);
  compares/frame 66k from 520k. Shadow DrawItemSort 0 in both towns. **Draw 1.12 -> 0.97 ms heavy (-14%), 0.70 -> 0.65 compact
  (-7%)**; sim unchanged. ON.
- **WALKGRID** (`handins/WALKGRID.md`, merge `8ccf44209`, gate 0, unit 1153 / 0): `ext.walk_batch_grid`, the no-Place walk batch
  (CLEANUP removed it as a constexpr variant) as its own class B row, default OFF, formatVersion 1, locked per game with the generic
  pseudo-mod `ext:walk_batch_grid` and manifest; B on implies the batch whether or not the A row is on. Step<kPlace>, three
  EntityList walk instantiations, the pick hoisted to GameUpdateLoop::Update; the faithful and class A walks are the same
  instructions as main (object proof). Shadows with B on: Sim / Span 0, Grid 240,900 / 42M (the bounded N-1-tick grid lag: the
  class B evidence); A only: all 0. **Speed 50 everything on: fps 370 / 257 -> 398 / 286 (w1 / w3, +8% / +12..20%), sim ms per
  iteration -9%**; at speed 10 inside the pair spread. The save with the row on: the lock line, the 47-byte manifest, the
  pseudo-mod in the mod list; the resave golden unchanged with the row off. The user's bar (sub-frame, nothing drawn differs) met.
- **XFORM2** (`handins/XFORM2.md`, NOT merged; the code was at tag `legacy/perf-20261013-xform2-unmerged`, deleted 2026-10-18 and kept off the repo): the world-matrix memo retry
  with only the two repeating sites (world.draw one slot, mesh.draw a 64 KB table) and a switch-off object proof. Exact (shadow
  10.1M / 0) but it keeps 1,800 of the first attempt's 55k requests/frame (ceiling 0.043 ms); A/B neutral (heavy 1.11 / 1.13 vs
  1.11 / 1.12, compact 0.70 / 0.68 vs 0.70 / 0.70). Measured from the worktree exe before any merge, so no cleanup block was needed.
  The world-matrix memo idea is closed.
- **Close-out speed-50 rows on the final exe** (`perf/results/*_s50close_*`, mods mounted, overlays stopped, 1800 frames), fps w1 / w2 / w3:
  faithful **409 / 298 / 165**; everything on **380 / 351 / 259**; everything on + `walk_batch_grid` **412 / 375 / 282** (ms per iteration
  0.17 / 0.19 / 0.21). **IBS pass on that leg** (`handins/IBS_20261013_S50B.md`, tables `perf/uprof/results_20261013_s50close/`): main
  thread 61% of the process (was 86%), the four path workers 36% (`FastPaths::Searcher::Run` 78% of a worker, 31.5% of the process: busy
  now, not idle); on the main thread the snapshot walk ~21%, the fast cull ~14% (the reference bodies gone), the grid walk batch ~13%,
  `CharacterGrid::Place` 1.24% (was 6.54%), the transform lerp ~5.5% (closed); the sort rows seen (5.8%) are the COMPPROF=1 census,
  absent at COMPPROF=0 -- collect at 0 next time. Not memory-bound (miss latency 0.19% of cycles).
- Faithful draw CPU this session: heavy 1.66 (morning) -> 0.97 ms, compact 1.03 -> 0.65 ms. Housekeeping: worktrees removed, the
  three branches kept (xform2's tip tagged); mods restored to WinData; overlays and the hypervisor still off (the user's call).

## 2026-10-13 evening -- the IBS_20261013 ideas taken: CULLROUTE, SORTCMP, GATHERPF shipped; XFORM measured and removed; CLEANUP; faithful draw -25%

- Five opus worktree blocks (static gate only), every merge gated at 0 (resave golden `971aaec2cf95` unchanged, unit 1144 -> 1153 -> 1147
  after the cleanup); shadows at COMPPROF=4 in three runs (heavy faithful, heavy with draw_gather + render_culling, compact) all 0; A/B series
  ab2 / ab2b (CLEAN but one run, overlays stopped, interleaved x2, faithful, speed 10), draw ms per frame heavy / compact:

  | leg | heavy | compact |
  |---|---|---|
  | main before CULLROUTE (`10040dfac`) | 1.65-1.67 | 1.03-1.04 |
  | + CULLROUTE | 1.63 / 1.63 | 0.95 / 0.98 |
  | + SORTCMP | 1.37 / 1.38 | 0.82 / 0.84 |
  | + GATHERPF defaults | 1.25 / 1.26 | 0.76 / 0.77 |

  **Faithful draw CPU -25% in both towns** (heavy 496 -> 660 fps, compact 870 -> 1174 fps, sim columns unchanged).
- **CULLROUTE** (`handins/CULLROUTE.md`): every remaining caller of the reference frustum test routed through the prepared table (the terrain
  chunk grid -- 1024 boxes per call x 18-30 groups per frame was the hidden ~5% --, decals, roads, weather tiles, and ext.draw_gather's walk,
  244k boxes/frame with the extension on); per-site row. Draw -1..-2% heavy, -6..-8% compact.
- **SORTCMP** (`handins/SORTCMP.md`): the plain draw-item sort (hash-group by the 240-bit prefix, sort the groups, sort each by geometry), exact
  by construction: an O(n) tie scan falls back to DRAWSORT's keyed skeleton on any tie with differing `_arg8` (0 fallbacks, 0 ties, output
  byte-identical to retail's on every list in every shadow run). 9.5 compares/item from 13.6; **draw -20% both towns**, far above the
  prediction: the saving is the whole keyed introsort. Census: `same-as-last` 81-91% of lists carry the same key array as the previous frame
  -- a permutation memo (exact with a full key compare) is the next sort idea.
- **GATHERPF** (`handins/GATHERPF.md`): prefetch-ahead hints in the faithful gather (chain stride 1, visible-list dispatch two ahead by vptr,
  materials line); 9 prefetcht0, arms-off object identical. Draw **-8..-9%** both towns. kCellHead neutral; the row's per-object counters cost
  +3..+5% draw by themselves (a serialized RMW chain in the hottest loop) -- both removed.
- **XFORM** (`handins/XFORM.md`): the per-emit-pass world-matrix memo, exact (shadow 128-189M / 0, 2.1 req/key, est. 0.47 ms/frame) but
  **neutral-to-worse** in the A/B: the 1.4 MB direct-mapped table's misses pay back the ALU saved. Shipped OFF, then removed. Retry notes in
  the hand-in (an L2-sized slot layout; memo only the 5.9 / 160 req/key sites).
- **IBS_20261013_S50** (`handins/IBS_20261013_S50.md`): the speed-50 everything-on collect (idea 5): main thread 60% sim / 25% render; the
  draw_gather walk ran the REFERENCE cull (9.6% of ops, now routed); `CharacterGrid::Place` 6.5% (walk_batch class A); not memory-bound.
- **CLEANUP** (`handins/CLEANUP.md`, the user's policy from today): no off-switched perf body stays in main. Removed: XFORM, GATHERPF kCellHead
  and the two per-object counters, the three losing PREFETCHCOV arms, DIRECTTICK, walk_batch's class B variant. Object proof per TU (the
  surviving code byte-identical to the shipped configuration; cull.cpp differs by exactly the two removed adds). The code stays reachable at
  `dc60c0f4e` on main (the tag `legacy/perf-20261013-pre-cleanup` was deleted 2026-10-18).
- **Speed-50 heavy spread on the final exe (`f74378b0d`, mods mounted, overlays stopped, 1800 frames; `perf/results/*_s50final_*`)**, fps per window 1 / 2 / 3: faithful **413 / 284 / 128** (was 312 / 229 / 117 this afternoon, `s50u_faithful_nohooks`); everything on **385 / 346 / 262**. Draw 1.17-1.30 ms; sim ms / iteration unchanged (0.167 / 0.214 / 0.281 faithful).
- Housekeeping: `ibs_thread.py` finds its thread block by name; Afterburner / RTSS / RTSSHooksLoader64 were stopped for the session (restart
  them); the hypervisor is still OFF (the user's call to restore); the mods are PARKED in `_mods_off` (move them back).

## 2026-10-13 late afternoon -- the speed-50 everything-on IBS pass (`handins/IBS_20261013_S50.md`); four perf blocks dispatched

- Idea 5 of IBS_20261013 taken: heavy spread, speed 50, the eight extensions + five fixes on, overlays stopped, CLEAN. Main
  thread 86% of the process; by file: the snapshot walk 18.5%, walk_batch 15%, **the REFERENCE frustum cull 9.6% (ext.draw_gather
  tests every box with `RtVolume_Frustum_Contains`; the FRUSTUMCULL table is never reached with the extension on)**,
  `CharacterGrid::Place` 6.5% (walk_batch's class A Place-every-tick), tick dispatch 5.8%, draw_gather 5.4%, AnimBatchDecide
  3.4%, the keyed sort 4.6%; the per-item transform block ~1.5% at this speed (idea 1 is a speed-10 uncapped target). Misslat
  small everywhere: not memory-bound. `ibs_thread.py` finds its block by name now.
- Dispatched as opus worktree blocks (static gate only; the coordinator runs shadows, rows, gates and A/Bs): XFORM (idea 1),
  CULLROUTE (idea 2, with the draw_gather site added from this pass), SORTCMP (idea 3: plain sort + O(n) tie scan + fallback
  to the keyed skeleton on a tie group with differing `_arg8`, censuses first), GATHERPF (idea 4, hints only, one switch per arm).

## 2026-10-13 afternoon -- session close: the final IBS pass on the merged exe (`handins/IBS_20261013.md`), the next ideas

- Uncapped (all three overlay processes stopped: 545 / 491 / 502 draws per second), heavy spread town, speed 10, faithful,
  mods mounted; tables in `perf/uprof/results_20261013/`. GetBones and StateMachine::Tick are gone from the main-thread
  table (SKINMEMO, SMMEMO); the cull halved per box (34% -> ~23%, ~5% of it still the REFERENCE bodies through an
  unrouted caller); the keyed sort costs more ops at 19 cycles each (~15%) but less wall time; the gather is the top miss
  site (10% of ops, 40.6% L1 miss) in the faithful profile; the per-item transform lerp (GetInstanceTransform /
  RtLerpTransformPair / AsMatrix / Lerp, ~20%) is the largest remaining block with misses. The sim is < 5% of the main
  thread at speed 10. The 2026-10-12 pass ran under the hooks loader's 140 fps cap, so its ops% column is not comparable.
- **Next ideas, ranked** (IBS_20261013 section 3): (1) a per-draw-item transform memo on the SKINMEMO pattern, (2) route the
  remaining reference frustum callers to the fast body, (3) fewer sort compares (a plain sort over the key once a tie census
  in an instanced-batch town allows it), (4) a prefetch-ahead in the faithful gather, (5) the speed-50 everything-on
  collect, (6) housekeeping: restore the hypervisor, make ibs_thread.py find its block by name, log `rtss` beside
  `machine load`.
- The session's shipped blocks: UNSTAMP, PREFETCHCOV arm P, SKINMEMO, DRAWSORT (all gate 0, A/B series ab1); 25 worktrees
  pruned; main pushed. Nothing is in progress.

## 2026-10-13 midday -- the open perf slate cleared: UNSTAMP, PREFETCHCOV (arm P), SKINMEMO, DRAWSORT merged (gates 0)

- Four opus agents in worktrees, static gate only; the coordinator ran every shadow, row, gate and A/B (perf exe, mods
  parked, series ab1: 8 legs x 2 towns x 2 interleaved pairs, 30 of 32 runs `machine load: CLEAN`, RTSS running so the
  fps column is capped at 139; the sim and draw ms columns are what the verdicts rest on).
- **UNSTAMP** (`handins/UNSTAMP.md`, PARTREPEAT doubt 2): the particle un-stamp's "already ran this frame" test is a frame
  stamp in `Component::_pad24` (a retail pad, zeroed by the ctor, never read or serialized) instead of a probe into the
  128 KB `s_compTbl`; the table stays as the kill-switch arm and the shadow's reference. Shadow UnstampStamp 21.8M / 0;
  the /part row: stamp hits = repeats exactly, lowerings 0.00-0.01 per frame (the helper's emitter loop is NOT dead:
  doubt 1 answered with a number, the helper stays). A/B: heavy within noise, compact sim -1.5%. ON.
- **PREFETCHCOV** (`handins/PREFETCHCOV.md`, IDEAS #1 round 2 with SMMEMO on): the four coverage arms built as switches
  (all-off object identical to main). A/B per arm: the particle arm (emitter slots + instances) heavy sim -1.3 / -2.5%,
  EntityList -1.5 / -2.4% in both pairs, compact neutral -> shipped ON (`kPrefetchCovParticle`); the character tail, the
  last-iteration terrain and the state-machine arms are WORSE in compact in both pairs (+2..+4%) and stay off in the
  source with their small-L3 argument. IDEAS #1 is closed: the ideas list is finished.
- **SKINMEMO** (`handins/SKINMEMO.md`, IBS draw-side target 3): the per-Renderer::Draw bone memo, keyed on (pose array,
  bind array, count, instanced, root bits, t bits), at the three GetBones callers; the same instance is posed once per
  pass instead of once per pass x list x light. Shadow SkinBones 461k / 0; 62-65% hits. A/B draw ms heavy -1.2 / -2.1%,
  compact -5.3%. ON. Candidate 2 (still instances) closed not exact: `Lerp(a, a, t)` normalises `a*(1-t) + a*t`.
- **DRAWSORT** (`handins/DRAWSORT.md`, IBS targets 1-2): SetSizeCore's 8.8% of ops was the visible list's Reserve(16384)
  in AddCulledDrawItems, 16,384 EMPTY loop iterations per cull pass (attributed by the sampler's caller view; 0.44% of
  wall samples: IBS's op share was cheap ops); the empty tail is now skipped exactly. The draw-item sort keeps retail's
  skeleton and swap sequence on 320-bit packed keys (`_arg8` IS consumed by DrawItemList, so the algorithm cannot
  change), items moved once; NaN / high-pointer lists fall back to the inlined reference. Shadow DrawItemSort 27,869 / 0;
  the row read 0 ties. A/B draw ms heavy -0.1 / -1.8%, compact -2.5 / -1.9%. ON.
- Faithful profile after this session (heavy, series ab1 base): sim 0.26-0.27 ms/iter, EntityList 2.28-2.33, draw
  1.91-1.93 ms (was 2.1 on 2026-10-12 night); compact sim 0.146, draw 1.27. Resave golden unchanged on every gate.
- **Speed-50 pair after the merges (the user's ask, 2026-10-13 13:00-13:15, heavy spread, 1800 frames)**, main's perf exe,
  `perf/results/*_s50b_*` (mods mounted, RTSS on), `*_s50c_*` (mods mounted, RTSS and Afterburner stopped), `*_s50d_faithful_nomods`
  (mods parked, CLEAN). Sim ms / iteration, windows 1 / 2 (the headline metric, speed- and GPU-independent):

  | run | faithful | everything on |
  |---|---|---|
  | this morning (`*_s50_*`, before UNSTAMP / PREFETCHCOV / SKINMEMO / DRAWSORT) | 0.162 / 0.208 | 0.180 / 0.199 |
  | now, mods mounted (s50c) | 0.167 / 0.179 | 0.185 / 0.199 |
  | now, mods parked (s50d, faithful only) | 0.164 / 0.187 | |

  Window 1 within noise, window 2 faithful -10..-14%; the mods cost nothing measurable in the sim. **The draws/s column is NOT
  comparable today**: every afternoon run reads 103-114 draws per second (frame 8.8-9.7 ms) against 258 / 182 this morning on the
  same town, with the same update ms; the fps column counts draws per wall second (the 600-frame window is the 60 Hz sim step in
  every run, 10.1 s). Ruled out: VideoVSync=false in settings.cfg, RTSS / Afterburner (stopped for s50c, same reading), the mods
  (s50d control), the exe (s50d is main's). Draw-only frames take ~7 ms instead of ~2: a presentation-pacing artifact of the
  afternoon desktop (windowed present / occlusion is the suspect: the overlay restart raised windows). Recorded, not chased; one
  look owed on an idle desktop with the game window in front. Two of the five runs read BUSY: the VS Code C++ indexer re-indexing
  after the 25 worktrees were removed (one core); the sim columns of those runs agree with the CLEAN ones.
- **The speed-50 draws/s gap, SOLVED (2026-10-13 13:15-14:20, sixteen more runs, `perf/results/*_s50e_*` .. `*_s50u_*`)**: it
  was **RTSS's frame limiter, applied by the injected hook of `RTSSHooksLoader64.exe`, which stays alive (and keeps hooking new
  processes) after `RTSS.exe` and Afterburner are killed**. The hook's 140 fps limit spaces the draw-only frames between 60 Hz
  sim steps 7.1 ms apart, so a speed-50 run gets ~2 draws per step instead of ~4. With all THREE processes stopped (Afterburner,
  then RTSS, then the hooks loader, `Stop-Process -Force` from an elevated PowerShell): **faithful 312 / 229 / 117 fps**
  (frame 3.2 / 4.4 ms, `s50u_faithful_nohooks`) against this morning's 258 / 182 / 94 on the pre-merge exe, and the GPU boosts
  to P3 2.1 GHz. Excluded on the way (one run each, all ~115 fps): the exe (the morning commit rebuilt), the mods, focus, a
  topmost cover, minimized (0 draws), the timer resolution held at 1 ms, vsync (true -> 17 fps), the window size, the display
  switched off (the user's theory: no change), the display held awake, a Task Manager pass (nothing stuck). The GPU's P8 state
  seen in the first samples was a consequence (little work per frame), not the cause. **Rule**: before any uncapped fps
  number, `tasklist | grep -i rtss` must be EMPTY, hooks loader included; the memory `reference_rtss_caps_fps_at_140` is
  updated. The sim columns never depended on any of it.
- Housekeeping: the 25 merged agent worktrees and their branches deleted; `walkbatch-own-tu` (unmerged, never built) kept.
- Open, on record and not taken: a plain sort over the packed key (exact only with 0 ties, needs a tie census in an
  instanced-batch town); the visible list's 128 KB alloc / free per cull pass; the un-stamp helper's retirement
  (`behaviour:`, needs the lowering count at 0, which it is not); the uProf `--help` pager blocks a harness (kill by PID).

## 2026-10-13 morning -- IDEAS #3 / #4 / #5 closed: STILLTERRAIN shipped (gate IDEAS345 at 0), DIRECTTICK merged off, PARTREPEAT not exact

- Three opus agents in worktrees, static gate only; the coordinator ran every shadow, row and A/B (perf exe, mods parked,
  machine load CLEAN on all sixteen A/B runs; RTSS left running, it caps only the fps column).
- **STILLTERRAIN** (#4, `handins/STILLTERRAIN.md`): the exact per-character memo of a still character's last-iteration
  `GetTerrainPos` answer, keyed on x / y bits and one global terrain epoch bumped after every heightmap / floorCount write and
  on create / load / teardown (census 1.3); never records a floor cell, so the picking scene is never skipped; the same tick
  takes its trig from STILLPATH's memo. Shadows StillTerrain 6.30M / 0, StillTrig 6.35M / 0; /still-z 95-97% hits,
  epoch bumps 1.3-5.9 per frame; treeWalk -1.8% heavy (both pairs), EntityList -1.9%, compact neutral-or-better. ON.
- **DIRECTTICK** (#5, `handins/DIRECTTICK.md`): qualified direct calls of CharacterComponent / ModelComponent::Tick by the
  vptr the walk already compares; exact by construction (final classes, CHECK_SLOT 8). A/B neutral in both towns
  (heavy -0.5%, compact +0.4%): merged with `kDirectTick = false`, measured and unshipped.
- **PARTREPEAT** (#3, `handins/PARTREPEAT.md`): Step 0 met the time bar (repeats 2.9-3.1% of EntityList, 99.2% early-exit)
  but the skip is not exact: `gameupdateloop.cpp` step 3h advances `ParticleInstance::_frameIndex` once per sim iteration,
  so every iteration re-simulates each shared pool and the early exit copies fresh bounds. Closed; the /part COMPPROF row
  stays. Smaller exact leftover: the un-stamp's s_compTbl probe as a per-component frame stamp (PARTREPEAT.md 8).
- The ideas list is done: #1 demoted by IBS, #2 SMMEMO, #3 closed, #4 shipped, #5 off. Faithful heavy after this merge:
  sim 0.26-0.27, EntityList 2.27, treeWalk 1.88-1.90.

## 2026-10-13 early -- the perf day closed: SMMEMO, FRUSTUMCULL, DRAWGATHER, WALKBATCH merged (gates 0); the everything-on comparison (`handins/PERFDAY_20261012.md`)

- Four more blocks from the IBS pass and the ideas list, each measured CLEAN (hypervisor off, RTSS off) and shadowed at 0:
  SMMEMO (exact, treeWalk -10% both towns), FRUSTUMCULL (exact SSE2 cull, draw -2%), `ext.draw_gather` (class A, draw -6%,
  fps +5..7%), `ext.walk_batch` (class A at the user's call, Place every tick: EntityList -1..-2%; the class B variant -3..-4%
  stays in the source unshipped).
- Faithful profile start -> end of the day (heavy): sim 0.33 -> 0.27 ms/iter, EntityList 2.98 -> 2.34, treeWalk 2.57 -> 1.95;
  resave golden unchanged on every gate. Everything on (five fixes + eight extensions) vs faithful: sim -11%, EntityList -13%,
  draw -15%, fps +20% heavy (the fixes-on town holds 3,700 fewer entities, row 1326: the walk_batch pair inside it is clean).
- IBS (`handins/IBS_20261012.md`): the sim is not memory-bound on this box; DOD struct work in the sim buys nothing here; the
  draw gather was the one miss spot (now draw_gather). Open exact candidates: IDEAS #3 / #4 / #5; draw-side: SetSizeCore,
  the draw-item sort, skinning. The user's hypervisor is still off (restore: `bcdedit /store ... hypervisorlaunchtype auto`).

## 2026-10-12 night -- STILLPATH merged (gate 0): the exact still fast path ships, ~1-2% (`handins/STILLPATH.md`)

- The agent built E1 smaller than OFFSCREEN 4.3 sketched and proved it instead: the executor runs in full; after it, on an
  intermediate iteration with no step, the tail's own yaw / z / rotation results are compared bit for bit with the live model
  and, when equal, the trig and the transform build are skipped (the push and `CharacterGrid::Place` are still made). The only
  memory is a trig memo keyed on the yaw bits, which can only decline. Shadow 56.8M / 0; two scripted unit worlds bit-identical.
- Measured (CLEAN, interleaved): treeWalk -1.4% heavy, -2.0% compact, never worse; 90% of still ticks take it, but a fast tick
  saves only the ~7-10 ns of trig + stores, the executor's misses, `Place` and the model tick stay. Kept on like the sidecar.
- The walk money is still D2: `ext.walk_batch` (class B, 12% of EntityList ceiling by the /char rows). Not started.
- Next, by the user: a hypervisor-off reboot for the AMD uProf IBS pass (Core PMC / IBS = No under Hyper-V).

## 2026-10-12 night -- PREFETCH merged (gate PREFETCH2 at 0): the staged tick prefetch ships; the stride sweep; OFFSCREEN measured (`handins/PREFETCH.md` section 9, `handins/OFFSCREEN.md`)

- The user's two questions of the session, each with an opus agent and the coordinator measuring on a CLEAN machine (RTSS stopped):
  **staged prefetch** -- `kWalkPrefetch = 2` (five stages, one entity apart) ships as faithful perf work: treeWalk -3..-6% over the
  old three-stage walk and -13..-16% over no prefetch, same sign in every interleaved pair, both towns; the agent's "within noise on a
  96 MB L3" prediction was wrong (an L3 hit still stalls the dependent chain) -- the user's rule: never assume the player's L3 is this
  box's. The stride sweep (S = 2 / 4 / 8, stage k at i + k*S) is monotonically worse: one entity apart already covers the chain's
  latency, farther lines die in L1 / L2 before use. **Off-screen agents** -- teleporting off-screen walkers (D1) is rejected (~20
  position readers mid-walk, camera-dependent sim); the ceilings from the new `/char` COMPPROF=2 rows in the heavy town: D1 28.9% of
  EntityList, the walk batch at speed N (D2, class B, no visibility test) 12.0%, the exact still fast path (E1, faithful) 9.2% -> E1
  first, D2 next, D1 never.
- Gate lesson (8 false problems): `gate.sh` / `mergegate.sh` do not park `WinData/*.pkm`; a mounted mod is +1 loader call on newgame and
  tutorial AND flips the resave golden (86 bytes, the Profession bucket order). Park the mods by hand before every gate.
- AMD uProf 5.3.521 installed (driver running); the IBS pass on the perf exe is next, then whether DOD struct work pays.
- OFFSCREEN merged (gate 0 after one CAST-OK marker on the diagnostic's const_cast): the `/char` rows ship in the COMPPROF=2 arm
  (`.claude/skills/stress-test/SKILL.md` points at them). Next block: E1, the exact still fast path (faithful perf work, shadow
  `StillFastPath` under COMPPROF=4), then the D2 walk batch as `ext.walk_batch` (class B).
- uProf is BLOCKED by Hyper-V (WSL2): `AMDuProfCLI info --system` reports Core PMC / IBS = No while a hypervisor runs; the IBS
  pass needs a hypervisor-off boot, the user's call.

## 2026-10-12 evening -- PERFXEXT closed (gate 0): PerfX is `ext.render_culling` (A) + `ext.anim_batch` (B); the true faithful baseline; a stress-harness confound fixed

- Gate PERFXEXT 0 with the debug exe running the snapshot walk, WALKCACHE and the terrain sidecar for the first time. Shadows (both on, heavy): every identity shadow 0; AnimBatchSim 61,913 / 109.5M = the class B evidence. Benchmark (CLEAN, fixes off both sides): TRUE faithful compact 0.18 (the old 0.17 had PerfX on; `perf/baselines/current.csv` re-cut), both on 0.17; heavy 0.33 -> 0.30; speed 50 heavy windows 1-2 fps 172/116 -> 211/144.
- **Confound found and fixed**: STRESS_EXT runs carried the five fixes ON (faithful forces them OFF); `fix.start_barn_height=1` alone removes 3,689 trees/rocks from the large spread stress town (row 1326, mechanism open). `tools/stress_run.sh` now writes `fix.*=0` for STRESS_EXT runs (`STRESS_FIXES=keep|<ids>`), `STRESS_EXT=none` is the per-entry control, `[Census]` prints the entity kinds per window.
- Owed: the user's looks -- the two new Extensions rows; toggling render_culling live above 1x while panning (no pose may stay frozen after switching it off).

## 2026-10-12 late -- PERFXEXT: PerfX becomes `ext.render_culling` (A) and `ext.anim_batch` (B) (`handins/PERFXEXT.md`)

- Classified first: the presentation knobs are class A (`render_culling`, live); the animation batch is class B (`anim_batch`, formatVersion 1: the sim reads the lagging clock through `IsAnimating` -- trees, the felling action, the tornado); the particle freeze was sim-visible as shipped (StopEmitter -> the tornado's life, `active[]` in the save) and now freezes only non-fading systems; the culled-only batch and the ambient throttle are dropped (camera-dependent sim; serialized late slot sounds). The snapshot walk and the terrain sidecar are faithful perf work, now on in every flavour; two terrain writers that never told the mirror (the tombstone, the pasture retype) are fixed. Every `BANISHED_PERF_*` flag, the 17 Options fields and `-DBANISHED_PERF_EXPERIMENTAL` are retired. Shadows `PerfXSim`, `TerrainSidecar` (expect 0), `AnimBatchSim` (class B evidence). Static gate only: the debug gate now runs the snapshot walk and the sidecar for the first time; A/B = `STRESS_EXT=render_culling,anim_batch`.

## 2026-10-12 night -- ELPERF2 closed: BESTTARGET + PERFXTBL merged (gate 0), shadows 0, benchmark within noise

- Shadows in the heavy town at COMPPROF=4: PathDistance 121,940, TickList 155.5M, BestTarget 809,756, PerfXVerdict 166.9M checks, 0 mismatches each. Benchmark vs ELPERF (CLEAN): heavy sim 0.29 = 0.29 (tree walk -1.9%, buckets noise), compact 0.16 = 0.16, speed-50 windows 1-3 Update 9.1/11.4/13.5 -> 9.2/11.4/13.3 ms (fps 225/150/85 -> 217/148/93): within noise. EXECSELF: reasoned stop (the executor's self time is two cache misses on the citizen's action node and array; a staged prefetch in the tick walk is the only lever, A/B only). Gate ELPERF2 read castmetric 4 on the merge (unmarked casts in a shadow's detail line), 2 after the comment-only marker. **User decisions**: layout / DOD work parked (flat ns per node across towns); PerfX is to become an EXTENSION (next block) rather than a perf-build-only one-off; the PerfX anim-cull key bug (stack-address cache key) is fixed in `2cec1554e`.

## 2026-10-12 evening -- PERFXTBL: PerfX's per-model / per-particle bookkeeping becomes one load (`handins/PERFXTBL.md`)

- Fix first (`fix(perfx)`): `AnimCullSkip` / the `_ANIM_BATCH_CULLED` arm keyed the visibility cache on a stack copy of the position (perf exe 0x1401e6e88), so every model got the frame's first model's verdict (estimated: in ~70% of frames every on-screen citizen left root-only). Now keyed on the model's own position; invisible to the gate, verified by the shadow and by eye. Then the batch skip count and the particle verdict move to a 40 B slot per TREE_SNAPSHOT entity reached through the walk index; counts spill to the old hash when an entity loses its slot, so the FIXED decisions are unchanged; `NeedsPerTickRootUpdate` stays per iteration (it is not frame-invariant). 8 unit tests (0 mismatches against the per-call `BatchEntry`; mutations caught); COMPPROF=4 shadow `PerfXVerdict` (expect 0). Not yet run in the game.

## 2026-10-12 night -- BESTTARGET: QueryNearestCharacter on a lean walk (`handins/BESTTARGET.md`)

- The profile corrects the brief: `QueryNearestCharacter` (~21.7% of the bucket phase incl. inlined leaves) is called by `MoveTo::Enter` (0.5-radius neighbour probe), not by `FindBestTarget`, whose 13.7% is 81% `Map::PathDistance` (PATHDIST's). The query: 56% candidate loop (two dependent misses per candidate over 4-9 loose 16-unit cells), 28% walk, 15% walker Init's dead 6 KB clear + Subdivide. Byte-identical replacement `CharGridNearest::Query` (no clear, children tested at push, candidates prefetched per chunk, retail order and float expressions); retail body kept as `QueryNearestCharacterRetail` (fallback + COMPPROF=4 shadow `BestTarget`). Unit: 58,000 queries 0 mismatches (2,354+ order-decided ties), tie-rule / child-order mutations caught (1,359 / 88); 1078 / 0. Not measured (estimate 3-7% of the bucket phase); COMPPROF=4 in the heavy town: 701,613 live queries, 0 mismatches (`20261012_155050_besttarget_verify`); the A/B is folded into the ELPERF2 benchmark. Tight cell set (1-4 cells) described, not built: needs an unproven "cell == current position" invariant.

## 2026-10-12 night -- EXECSELF: `ActionStack::Execute`'s self time is two cache misses, nothing to restructure (`handins/EXECSELF.md`)

- Leaf histogram of the ELPROF sampler run (perf exe `7f77a506a`, 1,401 of 12,486 tree-phase samples = 11.2%): 75.6% on the first touch of the Action node (`_status` test, line 158), 14.5% on the `actions[current]` load; the precheck loop has 0 samples, the steady path is 22 instructions with no `_status` store. The pre-precheck store is retail-visible (`ActionStackPush` -> `ActionStackWalkClear` tests it to decide `Exit`), so ELPROF 6.4 is closed with no code change. The lever is a staged software prefetch in the WALKCACHE tick walk (behaviour-free, needs only a CLEAN A/B): the user's call.

## 2026-10-12 evening -- WALKCACHE: the slot-8 walk visits only the components that tick (`handins/WALKCACHE.md`)

- Per-entity side table (16 B, parallel to the TREE_SNAPSHOT array) of the components with flag bit 2, staled by `RefreshEntityFlags` (every post-ctor flag writer reaches it) and keyed on the component array; retail's loop takes over mid-entity on a stale, 0x400 still breaks after the same tick; 11 unit tests (retail vs cached logs identical under mutation), COMPPROF=4 shadow `TickList`: 155.7M live checks in the heavy town, 0 mismatches. Gate ELPERF 0 (both merged, `aea9b6834`). **Benchmark (CLEAN)**: heavy town sim 0.32 -> 0.29 ms/iter (-9.7%; tree walk -4.7%, PathDistance 0.44 -> 0.17 ms/frame, CitizenList 0.21 -> 0.08), compact 0.17 -> 0.16 (-5.5%); speed 50 heavy windows 1-3 (RTSS stopped): Update 10.5/12.7/14.9 -> 9.1/11.4/13.5 ms, fps 186/128/57 -> 225/150/85 (the step-boundary effect amplifies the third). Both are faithful replacements (same results), not extensions -- the user's question answered that way.

## 2026-10-12 evening -- PATHDIST: the region search on a bucket open set (`handins/PATHDIST.md`)

- `RegionGraph::FindPath` (Map::PathDistance's region A*) pops from a LIFO bucket set in the AVL's exact (f asc, seq desc) order, faithful and default: unit 0 mismatches over 12,805 queries (1063 / 0); the COMPPROF=4 shadow (`StressHarness_Verify "PathDistance"`, retail's AVL body kept as `FindPathAvl`): 127,594 live region searches in the heavy town, 0 mismatches (`20261012_143402_pathdist_verify`).

## 2026-10-12 afternoon -- ELPROF: the EntityList profile (`handins/ELPROF.md`)

- Heavy town, speed 10, COMPPROF=2 + the sampler (two runs, CLEAN). EntityList = tree walk 85%
  (2,630 entities per iteration at 78 ns each, no hot spot) + bucket phase 15% (4x that in the
  first two windows). Tree walk by type: CharacterComponent move tick + action executor 49%,
  ModelComponent 10% (PerfX bookkeeping), particles 9%, state machines 6%, the walk itself ~13%.
  Bucket phase: `Map::PathDistance` 46% (the region search on the same AVL open set as retail's
  cell search), `FindBestTarget` 26%.
- Threading data (Amdahl, upper bounds): the entity-local ticks are 45% of EntityList -> Update
  -29%, speed-50 heavy windows ~60 -> ~85 fps; everything beyond that threads the citizen AI's
  shared-state transitions (class C, non-deterministic sim). Serial candidates with faithful
  results: PathDistance on the fast_paths bucket queue, PerfX dense side table, cached slot-8
  component lists (~-20% of EntityList together). The user's call on what to build.

## 2026-10-12 afternoon -- E2E split: the 15 ms tail is the 60 Hz Update step, not pathfinding

- Four pf_* columns (`pf_ready_us_*`, `pf_poll_us_*`, `pf_frames{0,1,2,3p}_pct`; `PfProf_Ready` at
  both publish sites). Heavy town, both extensions: enqueue->ready p95 745 us, ready->taken p95
  14.3 ms, 12.6% of requests cross one Update, none two. `MainLoop::Frame` runs Update on a fixed
  1/60 s accumulator with the speed iterations back to back; a late request waits for the next
  Update. Wall latency only (one sim iteration in sim time); no bearing on fps.
- fps at speed 50 (heavy town, rows 1-3): Update 12.7-15.5 ms = the whole step, EntityList ~88% of
  it, fps 48-67 in the windows where Updates pile up before a Draw. The limiter is the main-thread
  component tick; pathfinding's main-thread share is PathDistance ~0.3 ms/Update.
- Facts recorded (memory too): the user's RTSS limiter capped every earlier fps column at ~140
  (uncapped: 390-430 fps at speed 10; stop Afterburner first, it respawns RTSS); the heavy stress
  town starves to 0 citizens at ~110k-200k iterations at any speed (speed-invariant sim), so speed
  50 steady-state averages are only valid for rows 1-3. Hand-in: `handins/PFBENCH.md` (E2E split).

## 2026-10-12 -- `ext.path_workers` + the extension load notice merged (gates at 0); the heavy benchmark

- The user LOOKED at `ext.fast_paths`: "looks fine in game". Decisions: build the worker pool; DROP the GPU
  replay prototype for now; PAUSE the gameplay items (road costs, cost-aware PathDistance); the `ext:` load
  notice must say "extension".
- EXTNOTICE (`4c1c9bbe4`, gate EXTNOTICE 0): three MessageBox dialogs inside `Extensions.rsc` (not in build /
  newer / turned off) with their strings, `src/ext/load_notice` picks one in `SaveLoadDialog::DoLoad`; real
  mods untouched; the kit compiled them with 0 problems. Owed: the user's look (save with fast_paths on,
  turn it off, load).
- PATHWORKERS (merge + `f4bf88986`, gate PATHWORKERS 0): `ext.path_workers` (class B, formatVersion 1, off)
  -- retail's worker thread becomes a dispatcher; N = hardware threads - 2 in [1, 4] pool workers
  (`System::Thread`), each with its own search scratch (0x18 bytes per cell per worker: 24 MB at N = 4 on
  a large map), commands 2 / 3 as a barrier, results published under the lock; composes with
  `ext.fast_paths` (per-worker fast scratch). 118k randomised requests identical per slot vs the serial
  path. Not built: a priority lane for the road tool / boats; shadow mode does not run under the pool.
  `pf_worker_busy_pct` is the SUM over workers under the pool.
- **HEAVY BENCHMARK** (every run `machine load: CLEAN`; the 800-citizen towns never back the queue up, this
  one does: retail's single worker is 59% busy and a citizen waits 0.8 s at the median for a path):

| run (large map, 1600 citizens, spread, speed 10) | search us p50 / p95 / max | wait p50 / p95 | e2e p95 | worker busy | req / sim s | sim ms/iter |
|---|---|---|---|---|---|---|
| faithful (`20261012_105902_pfh_faithful`) | 1223 / 8887 / 30304 | 826 ms / 947 ms | 961 ms | 58.9% | 25.0 | 0.32 |
| ext.path_workers, 4 workers (`20261012_110144_pfh_workers`) | 36 / 3699 / 18071 | 18 us / 1.0 ms | 18.8 ms | 22.9% (sum of 4) | 41.0 | 0.31 |
| ext.fast_paths (`20261012_110422_pfh_fast`) | 4.1 / 674 / 4118 | 6.8 us / 1.8 ms | 15.1 ms | 4.6% | 40.5 | 0.31 |
| both (`20261012_110655_pfh_both`) | 4.8 / 780 / 4865 | 15 us / 39 us | 14.7 ms | 5.5% | 41.2 | 0.31 |

  The pool removes the queue (wait p95 947 ms -> 1 ms); the fast search removes most of the work (search
  p95 8.9 -> 0.7 ms, busy 59% -> 4.6%); together the wait p95 is 39 us. Requests per sim second rise 60%+
  with either, because citizens are no longer standing still waiting (path len mean 46 -> 30 for the same
  reason: the sims diverge). Sim ms / iteration is unchanged within noise in all four.
- Also merged earlier today (see the entry below): PATHRESEARCH, EXTSAVE, PFBENCH, `ext.fast_paths`.
- Open: the user's looks (the notice; `ext.path_workers` in play), one run per cell (the plan wanted three
  per side; the paired shadow and the two-order-of-magnitude deltas make repeats a formality), the two
  EXTSAVE doubts (old saves keep the requested set; Yes on a fatal notice still leads to the error box).

## 2026-10-12 -- PATHFINDING: research, the class B machinery, the benchmark, `ext.fast_paths` (gates at 0)

- The user opened the pathfinding block with an opus team ("research first, then implement; benchmark GPU A*
  in the stress town"). Rule recorded: **benchmarks only on an idle machine**, between agent waves, and a
  stress report counts only with `machine load: CLEAN`.
- PATHRESEARCH (`d62630584`, `handins/PATHRESEARCH.md`): retail's heuristic prices a step at 10 while the
  cheapest real step costs 30 (road) / 40 (ground), so A* expands a blob (verified in `action_pf_astar.cpp`
  :340 / :375); an AVL open set and ~48 divisions per expansion. GPU A*: searches are 10^3-10^4 nodes with a
  citizen waiting on each, a dispatch + readback costs more than the CPU search, vendor tie-breaking is
  non-deterministic -- a benchmark-only prototype later. The user's decisions: equal-cost paths that differ in
  shape are acceptable in a locked game; GPU = benchmark-only prototype; class B.
- EXTSAVE (`defef548a`, gate EXTSAVE 0): the class B / C save machinery -- pseudo-mods `ext:<id>` through
  `Save::SaveLoad::_extExtraMods` (null under faithful), the block-1 tail manifest (zero bytes when no B / C
  extension is active) locking the set on load (`Ext::LockForGameWithSet`), the `ext:` load notice, 7 tests.
  Open: an old save without a manifest keeps the requested set; the notice wording says "mod".
- PFBENCH (`dab117dc1`, gate PFBENCH 0): pathfinding columns in the stress CSV (worker search time,
  expansions, queue wait / depth, e2e, PathDistance / FillZones / road spin; shadow columns), the spread town
  (`--spread` = negative sets), the PATHFINDING report section; harness cost within noise (sim 0.17 = 0.17).
  Shadow switch = `BANISHED_STRESS_COMPPROF=3` (level 1 + shadow). `STRESS_EXT=<id>` runs a single-extension
  A/B through settings.cfg.
- FASTPATHS (`b0b308590` + `44b14ff4b`, gate FASTPATHS 0): `ext.fast_paths` (class B, formatVersion 1,
  default off) -- heuristic scaled to the cheapest step type present on the map (type 11 is passable and costs
  1, so the minimum is over the types the grid holds, counted at the case-2 seam), LIFO bucket queue that
  reproduces retail's (f, LIFO) pop order, division-free neighbours; selection read in `GameUpdateLoop::Load`
  after the lock is final; 4000-grid unit tests: identical cells at scale 1, equal cost at the real scale.
- **BENCHMARK (spread town, 800 citizens, speed 10, medium map; all runs CLEAN):** faithful vs
  `ext.fast_paths` alone: search p95 2008 -> 717 us, p50 153 -> 16 us, max 6.5 -> 3.6 ms, worker busy 4.9% ->
  1.6%, queue wait p95 4.3 -> 0.7 ms, end-to-end p95 21.3 -> 15.8 ms, sim 0.19 = 0.19 ms / iteration.
  **Shadow (paired, 13,160 requests): equal cost 100%, worse 0, same cells 46.7%, speedup 8.25x.** One run
  per side so far (the paired shadow is the decisive measure); the compact town's worker is under 6% busy
  either way, so the player-visible gain is per-request latency. Results in `perf/results/20261012_09*_pf_*`.
- PATHWORKERS (`ad3369633` + `34e2fbad9`, `handins/PATHWORKERS.md`; not yet gated or benchmarked):
  `ext.path_workers` (class B, formatVersion 1, default off). The pathfinder thread becomes a dispatcher:
  commands 0 / 1 / 4 apply in order, 5 becomes a job with a parameter snapshot, 2 / 3 are a barrier. N =
  hardware threads - 2, clamped to [1, 4], `System::Thread` pool workers, each with its own GridPathSearch
  (retail's Run) or FastPaths::Searcher; results published under the lock; drained and joined in
  `~PathfinderThread`. Unit tests: 3000 streams (118k requests, 47k edits) at N = 1..4 equal the serial path
  per slot. Faithful cost: one byte load per command. Expect a gain only where the queue backs up
  (`--citizens 1600 --map large --spread`); `pf_worker_busy_pct` is the sum over workers.
- Next candidates (PATHRESEARCH §4): `ext.path_workers` (a worker pool, retail's cells, 3-4 agent-days), the
  GPU batch prototype as an offline replay (5-8 agent-days, benchmark only), the gameplay items in §6 Q6
  (speed-proportional road costs, cost-aware PathDistance) only if the user wants them. Owed: the user's look
  at `ext.fast_paths` in play (toggle it in Extensions before a new game; the save then carries `ext:fast_paths`).

## 2026-10-12 -- SHARPUI2 LOOKED (good); the fifth fix `fix.ui_scale_layout`

- SHARPUI2 (`6e22720b1`) + the loose-variant check fix (`252a6c01c`: `PlatformExists` on
  `GetCompiledPath`'s full path with `:resource`; `FileSystem::Exists` is gated off during play).
  **LOOKED (the user, 2026-10-12)**: "overall looks good at all ui scales and in game the tooltips and
  render look good". One defect reported: on Apply of a UI-scale change the dialogs jump to a corner for an
  instant (upper left going 75 -> 150, lower right going 150 -> 75).
- Traced FIRST (EYECAM §11 lesson): a frame-timing line in `SharpUI::Draw` for the change frame and the three
  after it. The variant load is 1-20 ms, every frame around the change is 2-12 ms, and the user reproduced
  the jump at the menu, in game, and with every extension OFF. Not the extension's: it is retail's
  `UserInterface::Update` reading `_uiScale` for the layout size BEFORE the events that Apply runs in
  (`retail_bugs.md` §5, row 1325). The trace was removed again (numbers in `handins/SHARPUI.md` §12.2).
- Fifth fix `fix.ui_scale_layout` (default on): the layout loop uses `backBuffer * _uiScale` when the events
  changed the scale. First ENGINE-side fix seam: `UI::UserInterface::_extFixScaleLayout`, a null-by-default
  `const bool*` that `Ext::InitFromSettings` points at `Fix().ui_scale_layout` (hook rule 5; the menu toggle
  stays live through the pointer). Unit 1032/0. Gate UISCALE in `phase_d/GATE_TABLE.md`.
- Still owed: the next extension (class B pathfinding, `INVENTORY.md` §5.2), route 2 (port the compiler),
  the hard-link hazard in `ext_resources.sh`, row 1324, the 18 RESTORE items.
- `ext.fast_paths` (class B, default off, formatVersion 1; block FASTPATHS, `handins/FASTPATHS.md`): PATHRESEARCH
  §4.1 (a)-(d) as one search (`src/ext/fast_paths/`), selected once per game in `GameUpdateLoop::Load` (after the
  lock and again after step 31), dispatched in `ApplyCommand` case 5 (retail's `grid.Run` on the faithful arm),
  heuristic scaled by the cheapest PRESENT passable type (type 11 sits in 0xe1f at cost 1, so case 2 keeps type
  counts). Unit: scale 1 identical to retail on 4000 grids; real scale cost-exact, expansions 0.625 overall,
  0.35 with roads, 0.01 on open ground. Not run in game; the first live exercise of EXTSAVE §4.3.

## 2026-10-12 -- SHARPUI + EXTRSC merged (gate SHARPUI at 0); the stress false alarm

- `ext.sharp_ui` (class A, default off, `dee1fd743`): when on, the effective UI scale snaps to an integer
  device-pixel ratio -- `ceil(100 / slider%)`, so 75-99% = 2x, 100-150% = 1x, capped at the largest ratio
  that leaves an 800x600 layout (1080p stays 1x, `clamped=1` in the trace; 1440p and 4K get 2x); the slider
  value is never written. Pixel-snapped quads and per-batch point sampling through
  `UI::SpriteContext::_extSharp` / `Video::ImageBufferData::_extPointSampling` (rule 5 shape); retail's
  `UserInterface::Draw` runs unchanged inside the wrapper. NOTE the slider's sense: 75% is the BIGGER UI.
  Sharp text (c) is a costed plan in `handins/SHARPUI.md` §11, not code. `[SharpUI] on ...` trace line.
- EXTRSC (`404aede4d`): the Extensions dialog, button, 19 strings and 5 placeholder sprites are our own
  resources under `src/ext/ui/`, compiled by `ToolKit/bin/x64/Tools-x64.exe` through `tools/ext_resources.sh`
  (`/build <T> /pathres <res> /pathdat <WinData> /pathpkg <WinData>`, the kit's `resource/` overlaid with ours),
  28 `.crs` deployed loose into WinData/Dialog by `tools/build.sh deploy`. Deploy now NEEDS the kit. The
  script hard-links the game's `.pkg` into the build dir (mtimes intact, but a hazard: make it a copy or a
  read-only reference). A failed compile pops a modal Assert box (unattended hazard; 900 s timeout per call).
- **False alarm, two hours**: the first stress pair read 0.01 ms per iteration. Not the loose files (the
  correlation was coincidence): the game window came up UNFOCUSED while the user typed elsewhere,
  `Application::IsPaused` + `GameAutoPause=true` made `PlayerToolManager::Update` (retail 0x1401026a0) open
  the pause menu at frame 1, and a pause menu pauses the sim. `tools/stress_run.sh` now switches
  GameAutoPause off for the run and restores settings.cfg at exit, as `linux_run.sh` already did
  (`8f15c5936`). Lesson: before bisecting a stress stall, check for `[MainMenu] OnOpened ... game=<world>` at
  frame 1 in the error log.
- **LOOKED (the user, 2026-10-12)**: the Extensions dialog on compiled resources works, glyphs show. The
  integer snap at 75% = 2x: crisp but "blows it up to the point of being unusable" (screenshot 100 vs 75).
  VERDICT: no scale snap; the slider means what it always meant. The kit's Font/*.rsc name an installed
  typeface + pixel height and the compiler rasterizes the sheet, so sharp text at a non-integer scale =
  scale-matched font atlases with metrics divided back to layout units. Block SHARPUI2 dispatched: snap
  removed, compiled font variants under src/ext/ui/Font/, a 2x sprite-sheet lookup convention (no art).

## 2026-10-11 night -- decisions: resources for extensions, the toolkit compiler

- **Now (route 1)**: the official mod kit's `Tools-x64.exe` compiles the sources under `src/ext/<name>/ui/`
  offline; a build step deploys the compiled `.crs` loose into WinData (loose loads first); the Extensions
  dialog moves from its Mod.rsc clone to its own `Dialog/Extensions.rsc` + icon sprite. The compiler is a
  developer-machine dependency (not redistributable, not in the repo); Linux consumes the compiled outputs.
  NOT blocked after all: the kit is in the repo at `ToolKit/` (git-ignored; `bin/x64/Tools-x64.exe`, the
  profile binaries, `resource/`, the example mods) -- EXTSCAFFOLD searched only outside the repo. Block EXTRSC
  dispatched: the compile step + the dialog swap.
- **Later (route 2)**: port the compiler -- the text Encoding reader (stripped from the shipped DLL, present
  in Runtime-x64-profile.dll) + the Visitor read path over the existing Serialize bodies + the source-tree
  driver -- as `tools/rsc_compile` in CMake, cross-platform. The start of our own modding toolkit. A planned
  block once two or three extensions need resources.
- SHARPUI in flight with the user's snap decision: when on, the effective UI scale snaps to an integer
  device-pixel ratio (slider untouched), fit-checked for the largest dialogs, clamped to 1x below the fit.

## 2026-10-11 night -- ext.eye_camera DONE: the user's look clean

- The idle drop's OBSERVED cause (`handins/EYECAM.md` §11): EditTool::Update's retail off-display rule
  (0x1400fcebc) closed the citizen panel, and with it the FollowDialog, because the followed citizen
  projects to no screen rect at eye level. One seam in `edittool.cpp` exempts the eye's own citizen
  (`EyeCamera::FollowedEntity()`), `a39611200`. The user: "lgtm no issues" (follow through stops and
  buildings, the four fixes, the starting town on the ground).
- Lesson for the next extension: when a drop or close is intermittent, put the trace in FIRST and let the
  user's run name the cause; EYECAM2 and EYECAM3 fixed real defects but inferred the wrong one twice.
- Next in the user's order: class B pathfinding (`INVENTORY.md` §5.2), then class A sharp UI at high scale
  (§5.3).

## 2026-10-11 night -- EYECAM3 + the fourth fix merged (gate EYECAM3 at 0)

- BARNZ: the floating starter barn is RETAIL's (0x1400a1fc0 samples the corners before the flatten; the
  player path 0x1400fda00 stores z = 0): retail_bugs §4, row 1323, and the fourth fix `fix.start_barn_height`
  (`e696a0633`, default on; applies to every start-town structure).
- EYECAM3 (`fe72a743e`): the followed ENTITY is the identity, read each step from the frontmost
  FollowDialog; hand-back only on no dialog / no citizen / entity removed; `[EyeCam] engage / disengage
  reason=` lines in standalone_error.log. Cause inferred, not reproduced: if the view drops again the
  trace names the test. Row 1324 (CitizenUI::OnClosed skips the FollowDialog close on a marked citizen).
- **Owed**: the user's look (idle follow in a busy town for minutes, then grep `[EyeCam]`; the starting
  town sitting on the ground, no building sunk).

## 2026-10-11 late -- EYECAM2 merged and LOOKED; EYECAM3 + BARNZ dispatched

- EYECAM2 (`10aeeb57c`, gate row EYECAM2 at 0): child eye height from the age scale (confirmed good), shadows
  at eye level through `SunLight::_extShadowFitView` (confirmed good; DESIGN hook rule 5 names that shape),
  the slot-reuse weak ref. The idle drop is only MOSTLY fixed: standing still SOMETIMES still hands the view
  back to retail's follow -- block EYECAM3 enumerates every exit condition, adds an `[EyeCam] disengage
  reason=` stderr trace and fixes the live one.
- The user also saw the STARTER STORAGE BARN floating above uneven terrain on a new game: a faithful-port bug,
  not the extension's -- block BARNZ (root cause against retail's placement order; known_port_issues row).

## 2026-10-11 late -- EYECAM merged: the first extension

- `ext.eye_camera` (class A, default off) merged `a67c8b445`: `src/ext/eye_camera/` (eye_math pure + the
  game glue), three seams (Update step 6, Draw step 4 at the renderer's alpha + near 0.1, the zoom in
  camera_input.cpp), six unit tests; PlayerController's saved fields never written. Gate EYECAM at 0;
  stress faithful vs `/profile:all` identical (0.17 ms / iteration, 140 fps). `tools/stress_run.sh` takes
  `STRESS_PROFILE` (default faithful).
- **LOOKED (the user, 2026-10-11 late)**: engage, the three exit paths, the return to retail's camera and the
  zoom all good. Three defects, block EYECAM2 dispatched: children get the adult eye height; shadows do not
  render at the default distance 0.9 (back at ~3 units); an idle citizen kicks the view back to retail's
  follow until they walk again.
- Was owed: the user's look (`handins/EYECAM.md` §7: 13 checks -- eye height 0.88 units, the follow
  engage / disengage, first person, hidden / removed citizen, faithful load of a save made while following).

The design is `DESIGN.md`; the classified deviation list is `INVENTORY.md`; every block's hand-in is
`handins/<BLOCK>.md`. The gate table for this phase is `docs/runtime_port/phase_d/GATE_TABLE.md`
(rows EXT1 / EXT2).

## 2026-10-11 night -- the phase opened; scaffold, three fix pilots, verification, inventory

- **Merged on main**: EXTSCAFFOLD (`src/ext/registry.*`, `Ext::Fix()` / `Ext::Ext()`, settings keys
  `fix.<id>` / `ext.<id>` / `profile=faithful|all`, the class lock, the pseudo-mod provider type, the
  Extensions dialog + button beside Mods in the front end and the pause menu, 11 unit tests),
  FIXPILOT (`fix.icon_slide` at slot 11 AND `SetFlag`, `fix.residence_goto`, `fix.tavern_counter`;
  default on), SAVETAIL (`handins/SAVETAIL.md`, five verdicts VERIFIED), EXTINVENTORY
  (`INVENTORY.md`: RESTORE 18, FIX 7, EXT-A 1, KEEP 7).
- **Gate EXT2 at 0 problems** (faithful profile forced by `/profile:faithful` on the exe's command
  line: `tools/build.sh run`, `resave_gate.sh`, `phase_d/gate.sh`): new game 5694 / 0, tutorial
  5773 / 12, loadgame 3436 / 0, typetable `965b527a`, resave 3/3 == golden (23,453,453 bytes), unit
  1013 / 0, castmetric 2, acceptance 0 off target, 0 new warnings. EXT1 (one problem, castmetric 6)
  was the dialog's three casts and the residence faithful arm's; fixed in `e2d8be2f4`.
- **Design corrections from SAVETAIL**: a class-C chunk lives at the tail of save block 1, not after
  the file (retail checks the exact file size); every B/C extension writes a manifest there; the
  pseudo-mod's format version goes in ModInfo+0x18 (+0x10 is the package CRC); both write seams emit
  zero bytes under the faithful profile.
- **Facts learned**: a loose text `.rsc` is NOT loaded by the shipped loader and `Tools-x64.exe` is
  not on this machine, so the Extensions dialog reuses `Dialog/Mod.rsc` with its texts replaced and
  draws its icon in code (EXTSCAFFOLD hand-in). The button's tooltip still reads the Mods text.
- **LOOKED at by the user (2026-10-11 night)**: the Extensions dialog's name / author / description
  rows and the enable / disable toggles work in the front end and in game; the row icon column is empty
  (the user will author programmer art later; the slot stays). The three fixes seen working: icon_slide,
  residence_goto, tavern_counter. Fixes are global and live, NOT recorded in the save (a disable + save +
  re-enable + load shows the fix enabled): confirmed as intended by the user.
- **Next**: the first extension, class A eye-level camera (seam in `INVENTORY.md` §2).
