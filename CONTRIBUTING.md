# Contributing

Thanks for working on OpenBA. Two things before you open a pull request:

1. **Read `AGENTS.md`, and have your coding agent read it too.** It is the short list of rules a PR
   is reviewed against: no game content in the tree, faithful code with fixes and extensions kept
   apart, the flag inventory, the platform and renderer seams, the style rule, and what a PR
   description must state. If you use Claude Code, Codex, Cursor, Copilot or a similar tool, it will
   pick the file up from the repo root; if it does not, paste it into the session.
2. **Build and gate.** `CLAUDE.md` has the build commands (`tools/build.sh`) and the logs to read;
   `docs/reference/environment.md` has the toolchain. The Windows gate and the serializer check are
   run by the maintainer; say in the PR which platforms you could and could not run.

Base your branch on current `main` and rebase before review, so the diff shows only your change.
Large work is easier to review as a stack of small PRs, one concern each, with the seam or
interface change first.

Licensing: the project licence is in `LICENSE`; third-party code and its linkage are listed in
`THIRD_PARTY_NOTICES.md`. A new dependency adds a row there.
