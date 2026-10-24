# VISITPASS -- the resource-reference visit reaches its own row from the object, exact (issue #13)

Block VISITPASS, 2026-10-23, branch `issue13/visit` (based on `issue13/lazy` 6d5bb9f43). Faithful perf work in the
PATHINDEX / SAVELOAD style: no registry row, no settings key, no `BANISHED_*` flag, no `Opt()` field. The debug exe
checks every shortcut it would take; the perf and release exes trust it.

## 1. What was slow

Sampled boot profile (perf exe, MegaMod 7-part, warm derived cache; scratchpad `bootprof/s1_*`): after the big
load, every loader pass -- `GameLoadDialogLoopT` / `MenuDialogLoopT` (loads 2-4, ~0.9 s each) and the new game's
load (~0.9 s) -- makes ZERO `External::Load` calls. The time is the clean-generation visit: `ExternalFactory::Add`
on a HIT whose `usageMask` lacks the loop's bit serializes the object into a `VisitorStream`, every
`ExternalPtr<T>` it meets runs mode 1 of `ExternalPtr<T>::Serialize` (`FindOrLoad(cur->GetPath())`, result
discarded), and FindOrLoad is `ExpandRelativePath` (the memo's hashing, 26 %), the private `Find` (20 %) and `Add`
(17.5 %, of which 71 % is `GetPath` plus a Filename copy), around the mesh visitor bodies (9 %) and the allocator
(14 %). 2.6 s to the menu plus ~0.7 s per new game / save load on a 376k-row resource set, for visits that end on the
object's own row every time.

## 2. As built

**The pointer index** (`src/engine/collection/ptr_index.h`). A hash table beside `ExternalFactory::_resources`
mapping an `External*` to the row (node) holding it, with one flag per row. Kept in step where the fold index is:
`rtresset::InsertCore` (the three link points), `rtresset::Erase`, `rtresset::IndexClear` (~ExternalFactory). A
row's `external` is written once, at insert (`PairCtor`), so the pairs change only there. The index declines
(the caller walks) when its count disagrees with the tree's, for a null pointer, for a second tree, and for good
when one pointer is held by two rows.

**The visit** (`src/engine/object/external_visit.cpp`, called from external_ptr_serialize.cpp mode 1). For a
non-null `cur` the shortcut runs Add's HIT arm (`RtObject_ExternalFactory_AddHit`, factored out of
external_add.cpp unchanged) on the row holding `cur`, under the walk's own `_loadEnabled` test, then the same
discarded `IsDerivedFrom`. It is taken only when (a) the index answers and names a row N for `cur`; (b) N carries
the VERIFIED flag; (c) the path stack is empty; (d) N's key is `cur->_path` character for character. Otherwise
retail's walk runs, and when it returns `found == cur` under (c) and (d), N is flagged.

**Exactness.** Under an empty stack `ExpandRelativePath(P)` is a function of P's characters (expand_memo.h, point
4). The private `Find`'s answer is a function of the string and the key set, and the tree never holds two
fold-equal keys. So `Find(Expand(P))` names one row for a given P and key set; the verifying walk showed it is N
(P being N's key, by (d)). A later visit under (d) asks the same question of the same characters; its answer moves
only if the key set moves "the row fold-equal to E": N erased (the index then has no row for `cur`, (a) fails) or
a fold-equal key inserted beside N (impossible: an insert meeting a fold-equal key inserts nothing). So
`found == cur` again, the walk's one effect is `if (_loadEnabled) Add(cur)`, whose descent for `cur->_path` lands
on N by (d), and the shortcut is that arm on N. Identical rows, masks and visits; no load either way; the save's
reference table (numbered from these rows) cannot see the difference. What is skipped: the walk's Filename copies
and refcount traffic, the memo store, the fold-index reads, counters.

**Replacement paths** (external_visit.cpp's banner, each against (a)-(d)): ForceUnload zeroes a mask and
changes nothing above; Clean deletes and erases (the index drops the row; a dangling pointer walks as it does in
retail); a remount is those two; OnVideoRestart reloads IN PLACE (same pointer, row, key); a `_path` rewrite fails
(d) and walks (FindOrLoad's and the providers' writes precede their Add; FindOrLoad's re-assignment after a
provider's Pin writes the same characters); ~ExternalFactory unbinds the index; a pushed path stack fails (c).

## 3. Exactness evidence

- Unit (`tests/unit/ptr_index_test.cpp`; 1353 tests, 0 failures): the index against a std::map under 60k random
  inserts / erases / probes / flag sets, the flag following its row, the decline rules.
- Shadow runs (TEMP: the debug check forced on in the perf exe, removed before the final commit): every shortcut
  the exe would take is walked instead, and `found == cur` plus the row's mask after the walk are compared with
  what the hit arm would have left. MegaMod 7-part boot + new game (pause-menu quit, so the exit line printed):
  `visits=5034493 shortcuts=0 walks=5034493 verified=375504 checked=4658989 mismatches=0 index: rows=376420
  broken=0 duplicates=0` -- **4,658,989 compared, 0 mismatches**, twice (a first run with the exit line lost to the
  perf exe's printf strip showed `checked 4194304 shortcuts, 0 differ` as its last progress line). Mods parked,
  boot + `Gate.sav` load: `visits=37867 ... verified=9932 checked=27935 mismatches=0 index: rows=5490`.
- Gate (debug exe, which checks every shortcut): `tools/build.sh run` faithful, mods parked, in game (frame
  49061), crash log 0 bytes, 0 `MISMATCH` lines of any kind. `tools/resave_gate/resave_gate.sh Gate` (debug exe,
  mods parked): NEW-GOLDEN in a fresh worktree with md5 `971aaec2cf95f5f730bb1cd1a4fa8428`, the known golden, so
  the save and its reference table are byte-identical; its run's exit line `visits=8290 ... verified=3397
  checked=4893 mismatches=0 index: rows=3429`.
- Unit (`tools/build.sh unit perf`): 1353 tests, 0 failures.

## 4. Timings

Perf exe, MegaMod 7-part, warm derived cache, new game autostarted, two runs each interleaved (before = the branch
point 6d5bb9f43, after = this branch). **Machine load: NOT clean** -- other agents were compiling (ninja, cc1plus,
ld) through every run; the big load ran 24 s against the quiet profile's 10 s, so the wall numbers are indicative
and only the per-pass loader stamps (`[LoadTime] loader.resources` .. `loader.done`) are compared.

| loader pass | before (2 runs) | after (2 runs) | |
|---|---|---|---|
| loads 2-4 to the menu, summed | 2.67 s / 2.35 s | 1.03 s / 0.91 s | -1.5 s |
| new game: the loop's resource pass | 0.91 s / 0.87 s | 0.25 s / 0.25 s | -0.6 s |
| new game: the following load pass | 1.45 s / 1.41 s | 0.84 s / 0.84 s | -0.6 s |
| boot to the menu (wall) | 35.9 s / 34.5 s | 35.8 s / 34.6 s | the big load, under machine load |

The after runs' exit line: `shortcuts=4658989 walks=375504` of 5,034,493 visits -- every visit after a row's first
is the shortcut. The remaining ~0.25 s of a pass is the first-touch bookkeeping (Add's own descent for the loop's
roots, the mesh / material visitor bodies, the allocator).

## 5. Open

- The first visit of each row still walks (that walk is the verification); under MegaMod that is the big load's
  own first-touch visits, which were already paying it.
- The mesh / material visitor bodies and the allocator (the other ~23 % of a pass) are untouched.
