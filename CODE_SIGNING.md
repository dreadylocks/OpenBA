# Code signing policy

> **Status: not active yet.** OpenBA has not applied to SignPath Foundation's program, and every
> release so far is unsigned. Windows will show "Windows protected your PC" for those; choose
> **More info → Run anyway**. This page is the policy from the first signed release on, and that
> release's notes will say so.

Free code signing provided by [SignPath.io](https://about.signpath.io), certificate by
[SignPath Foundation](https://signpath.org).

## What is signed

- `OpenBA.exe` in the Windows release zip (`OpenBA-v<version>.zip`). The Release workflow
  (`.github/workflows/release.yml`) builds it from the version's tag on a GitHub-hosted runner,
  sends it to SignPath, and packages the signed copy. Nobody builds or signs a release by hand.
- Nothing else. The fonts and text files beside the exe are not programs, the Linux and macOS
  packages are not signed through SignPath, and the "latest" builds CI makes from `main` are
  never signed.

A signed `OpenBA.exe` shows **SignPath Foundation** as the signer under Properties → Digital
Signatures. Each release also lists a `.sha256` checksum for every package.

## Team roles

- **Committers and reviewers:** [fran-cap](https://github.com/fran-cap). `main` takes changes only
  through pull requests, and every pull request, from a team member or anyone else, is reviewed
  before it is merged.
- **Approvers:** [fran-cap](https://github.com/fran-cap) approves each signing request in SignPath
  before a release is signed.

Every team member must use multi-factor authentication on GitHub and on SignPath.

## Privacy policy

This program will not transfer any information to other networked systems unless specifically
requested by the user or the person installing or operating it.

OpenBA makes no network connections: no telemetry, no update check, no online features. It reads
the game's data from the user's own Banished installation and writes saves, settings and log files
on the local machine only. The third-party libraries built into it
([THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)) make no network connections either.
