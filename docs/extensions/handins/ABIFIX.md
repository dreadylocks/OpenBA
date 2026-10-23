# ABIFIX -- `tools/abi_audit.py` passes again

Block ABIFIX, 2026-10-19, on `perf-unbanished` (5bc928d9e). Build + unit only; the game was not run
(nothing here changes the exe's behaviour on Windows).

## What was wrong

Section (ii) of the audit (plans/ABI.md C.1, now `docs/runtime_port/phase_d/plans/ABI.md`) counts the
§A pattern lines per directory outside `src/platform/win`, `src/render/dx11` and `src/diag/win` and
fails on growth over `tools/abi_audit.baseline`. It failed with 12 GROWTH rows, on main too. Two causes:

1. **The audit could not see `#if defined(_WIN32)`.** Since the Linux port, Windows code lives beside its
   POSIX twin under a guard (`src/app/main.cpp`, `src/diag/*`, `tests/unit/platform_win_test.cpp`,
   the `__MINGW32__`-only `one_write_stdio.h` and its test). Those lines are Windows backend code, the
   same thing the excluded directories hold, but they were counted. The baseline (written at the
   Linux-port commits) had absorbed the `src/app` / `src/diag` ones; later guarded additions (tests,
   `one_write_stdio.h`, one more guarded `<windows.h>` in `src/diag`) showed up as growth.
2. **New unguarded code after the baseline was cut** (`src/ext`, `tests`, `src/diag/err_line.h`): three
   real `long` uses and three printf-style `va_list` forwarders.

## The audit change

`count_patterns()` now tracks the `#if` / `#ifdef` / `#ifndef` / `#elif` / `#else` / `#endif` stack (on
the comment-stripped text, continued directives joined) and does not count a line inside a branch that
can only be taken on Windows: a condition that is a conjunction (no `||`) with an un-negated term on
`_WIN32`, `_WIN64`, `__MINGW32__`, `__MINGW64__` or `_MSC_VER`, or the `#else` of exactly one negated
such test (`#ifndef _WIN32`, `#if !defined(_WIN32)`). An `||` condition and the `#else` after an `#elif`
are never Windows-only (conservative). The skipped lines are printed as a per-directory total, and
`--list PATTERN:guarded` lists them. No directory exclusion was widened. Section (i) is unchanged.

## Per-row disposition

| row (tree / old baseline) | lines | disposition |
|---|---|---|
| `long` src/ext 1/0 | `text_reader.cpp` `strtol` into an `int32_t` field | **fixed**: `strtoll` clamped to `[INT32_MIN, INT32_MAX]`, which is exactly what `strtol` gives on Win64 (32-bit `long`); on Linux the old code truncated a 64-bit value instead. Windows-identical |
| `long` tests 9/0 | 2 `platform_win_test.cpp` (guarded); `ext_sheet_builder_test.cpp` `ftell` + an iterator offset; `xwma_decode_test.cpp` 5 file-offset / index lines | guarded 2 **recognised**; 7 **fixed**: `int64_t` for the `ftell` result, `std::ptrdiff_t` for iterator offsets and the scan index (`%lx` -> `%tx`), `int32_t` for the known 0x2d802 offset passed to `fseek` |
| `wchar_t` tests 4/0, `wide_literal` tests 2/0, `wide_crt` tests 2/0, `win32_typedef` tests 6/0 | `platform_win_test.cpp` (whole file under `#if defined(_WIN32)`) | **guard recognised** |
| `win_include` tests 2/0 | `platform_win_test.cpp` `<windows.h>`; `one_write_stdio_test.cpp` `<io.h>` under `__MINGW32__` | **guard recognised** |
| `win_include` src/diag 7/6 | all 7 under `#if defined(_WIN32)` / `#ifdef _WIN32` | **guard recognised** (row now 0, dropped) |
| `va_list` src/app 4/0 | `one_write_stdio.h`, under `__MINGW32__ && !BANISHED_THIRD_PARTY_SOURCE` | **guard recognised** |
| `va_list` tests 2/0 | `one_write_stdio_test.cpp`, under `__MINGW32__` | **guard recognised** |
| `va_list` src/diag 1/0 | `err_line.h` `ErrLine(fmt, ...)` -> `vsnprintf` / `vfprintf` | **baseline raised to 1**: a printf-style forwarder; `va_list` is the correct type on every host and never crosses a layout or a file (plans/ABI.md §A.2 classes `va_list` forwarding as (b), "none") |
| `va_list` src/ext 2/0 | `text_reader.cpp` `Log(fmt, ...)` and `Fail(fmt, ...)` -> `vfprintf` / `vsnprintf` | **baseline raised to 2**: same reason |

Ratcheted down by the guard recognition (rows dropped, every line still listed by `--list X:guarded`):
`long` src/app 4, `wchar_t` src/app 1, `callconv` src/app / src/diag / src/engine 1 each, `declspec`
src/app 2 / src/engine 1, `win_include` src/app 6 / src/diag 6, `win32_typedef` src/app 14 / src/diag 7.
69 lines in all are now "not counted" (src/app 32, src/diag 15, src/engine 2, tests 20).

## Keeping it green

- Windows-only code outside the backend directories goes under `#if defined(_WIN32)` (or `__MINGW32__`
  for MinGW-CRT specifics); the audit then ignores it. Do not use `||` in such a guard if you want it
  recognised.
- In portable code: no bare `long` (use `int32_t` / `int64_t` / `std::ptrdiff_t`; `strtoll` / `ftell`
  results go into a fixed-width type), no `wchar_t` (`char16_t` / `Platform::PathChar`).
- A `va_list` in a new printf-style forwarder is legitimate: raise its row by one and say so in the commit.
- `python -I tools/abi_audit.py` prints `OK` (exit 0) when clean; when the tree falls below a row, run
  `--write-baseline` in the same commit (down only).
