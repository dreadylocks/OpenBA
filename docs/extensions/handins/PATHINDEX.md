# PATHINDEX -- resource path resolution during loads, exact (issue #13)

Block PATHINDEX, 2026-10-23, branch `issue13/pathindex` (based on `issue13/bc4` 6acdb7902). Faithful perf
work in the SAVELOAD / SORTMEMO style: no registry row, no settings key, no `BANISHED_*` flag, no `Opt()`
field. The debug exe checks every answer it skips work for; the perf and release exes trust it.

## 1. What was slow

Research sampling (perf exe, MegaMod 7-part, warm derived cache): the loader thread spends ~20% of the
12.8 s big load and ~71% of loads 2-4 in path / string work, the main thread ~50% of remount 2. Per
resource reference `FindOrLoad` runs `ExpandRelativePath` (four splits, two six-term candidate
concatenations, the package probes, ~25 allocations), the private `Find` (an AVL descent of
`ExternalFactory::_resources`, ~17 `_stricmp` over paths sharing their first 20-40 characters), then `Add`
(the same descent again). The remount re-finds every resource by path (`FindNext`) and probes the package
directory (`GetPackfileId`, `ExistsInPackfile`); LoadPackages descends the package directory once per
package entry before inserting it (401,167 entries under MegaMod).

## 2. As built

**The fold index** (`src/engine/collection/fold_index.h`). A hash table of node pointers beside a retail
path tree, keyed on the key folded exactly as retail's `_stricmp` folds ("C" locale: unsigned bytes,
'A'..'Z' lowered). The trees are untouched: every insert, rotation, erase and in-order walk is retail's,
so the save's reference table (numbered in in-order key order) cannot move. Exactness: the comparator is
a total preorder whose classes are "equal after folding"; each tree is a BST under it; an insert that
meets a fold-equal key inserts nothing; so a descent for K returns the one node fold-equal to K, or
null -- which is what the index returns. It declines (the caller walks) for a null search key, while
any node key is null (OpenFile's walk treats those differently), for a tree it is not bound to, and when
the tree's own count differs from the index's (a mutation that went round the hooks).

- `ExternalFactory::_resources` (`src/engine/object/external_set.cpp`): `InsertCore` adds each fresh
  node, `Erase` removes the node before unlinking it, `~ExternalFactory` clears. Answers
  `rtresset::Find` (private `Find`, `Clean`, `ForceUnload`), `Add`'s inlined descent and external.cpp's
  `ResourceSetFind` (`FindNext`, `GetReferenceId`, the public `Find`).
- `FileSystem::_packfileIndex` (`src/engine/io/packfile_fold_index.{h,cpp}`): LoadPackages' insert adds
  the node, ClosePackages and `~FileSystem` clear before freeing. Answers `ExistsInPackfile`,
  `GetPackfileId`, `OpenFile` and LoadPackages' `IndexFind`. The override arm rewrites a value, never a key.
- The hash folds eight bytes at a time (`FoldWord`, SWAR); `FoldEqual` compares byte by byte.

**The ExpandRelativePath memo** (`src/engine/object/expand_memo.{h,cpp}`, block SAVELOAD's). With an empty
path stack -- always, in this tree: `PushPath` has no caller -- the body's two answer arms build the same
characters (`stackDir` is a null Filename and `operator+` adopts the other side), so the answer is a
function of the request alone. So: every answer is kept, with no context (the filesystem /
data-directory / package-generation context and `io/packfile_generation.h`, its only reader, are gone);
a first request builds the answer without the candidate and its three probes (`ExpandEmptyStack` in
`external_path.cpp`); the bound is 2^20 entries (the old 2^16 was below MegaMod's 262k-524k distinct
requests, so the memo emptied itself over and over); an answer equal to its request (all but 4 of 262,144
measured) is stored as an empty value and handed back as the request's own characters.

**AdjustDelimiters** (`src/engine/system/filename.cpp`): both scans find their byte with `memchr`. Same
writes, same window.

## 3. Exactness evidence

- Unit (`tests/unit/path_index_test.cpp`, `expand_memo_test.cpp`; 1331 tests, 0 failures): the fold
  against `_stricmp` on 300k random byte-string pairs (bytes >= 0x80, the codes between 'Z' and 'a', case
  variants) and hash agreement; `FoldWord` against `Fold` on every byte value in every lane and 2M random
  words; the index against an ordered tree under retail's comparator under 60k random inserts / erases /
  probes; the game's resource names; all 401,167 entry names of the seven MegaMod packages (371,674
  distinct keys, normalised by the real `Filename`), probed as stored, case-flipped, with '/' and with one
  byte changed (1.6M lookups, 0 differ; skips without the install); the decline rules; `null + X` adopts X.
  `filename_adjust_test.cpp` (exhaustive over short strings, against the pre-SAVELOAD body) covers the
  `memchr` scans.
- Shadow runs (TEMP, removed: the checks forced on in a perf exe), MegaMod boot + new game: every index
  answer walked and compared (> 16.7M), every memo hit and every empty-stack answer compared with the
  body (> 2M each): **0 mismatches**, three times (index + memo, + empty-stack, final code).
- Gate (debug exe, which checks every answer): `tools/build.sh run` faithful, mods parked, in game, crash
  log 0 bytes, 0 `MISMATCH` lines. `tools/resave_gate/resave_gate.sh Gate`: **PASS** (md5 971aaec2...,
  the golden), so the save and its reference table are byte-identical.

Found on the way, not changed: `AdjustDelimiters` never returns for some arrangements of a delimiter
before "..", e.g. `..\..\B/\../\:resource` (retail's body and ours alike; the random test avoids dots).

## 4. Timings

Perf exe, MegaMod 7-part, warm derived cache. The machine was quiet before every run (no cc1plus,
ninja, ld, as or other game process).

| | before (bc4 tip, 3 runs) | after (3 runs) | |
|---|---|---|---|
| boot to menu | 22.93 s | 18.86 s | -4.07 s (-17.7%) |
| big load (load 1) | 11.70 s | 10.50 s | -1.20 s |
| loads 2-4 | 4.73 s | 2.26 s | -2.47 s |
| remount 2 | 2.21 s | 1.88 s | -0.33 s |
| new game, click -> world load | 3.72 s | 1.97 s | -1.75 s |
| working set in game | 2.49 GB | 2.56 GB | +68 MB (memo + two indexes) |

The last step (memchr + SWAR hash) alone, A/B 3 x 3 interleaved: loads 2-4 -5%, new game 2.09 -> 1.97 s.

Sampler (2000 Hz, index + memo build, before the last step), path / string work on the critical path:
load 1 2.55 -> 1.50 s (20.7% -> 13.5%), loads 2-4 3.81 -> 1.84 s, remount 2 1.17 -> 0.88 s: 7.5 -> 4.2 s.

## 5. Open

- What remains in loads 2-4 is the Filename copies of `Find` / `Add` / `GetPath` (each copy constructor
  normalises twice), the memo's own hashing and the index hashing.
- The remount's `SnapshotFind` (a case-SENSITIVE signed-strcmp tree local to `SetupFileSystem`) is not
  indexed. (Done by block PKGCACHE: `SnapshotTable`, handins/PKGCACHE.md.)
