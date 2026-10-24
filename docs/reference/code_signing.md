# Code signing (Windows exe, SignPath Foundation)

The public policy is [`CODE_SIGNING.md`](../../CODE_SIGNING.md). This page is the maintainer's side:
what is in the repository, and the steps that switch signing on. Until those steps are done every
build is unsigned, exactly as before.

## What is in the repository

- **`.github/workflows/release.yml`, job `windows`:** after the release build and its unit tests, the
  exe is uploaded as the artifact `unsigned-exe` and sent to SignPath with
  `signpath/github-action-submit-signing-request` (pinned to v3.0's commit). The job waits up to an
  hour for the signing request to be approved, copies the signed exe over
  `build/cmake-release/OpenBA.exe`, checks it with `Get-AuthenticodeSignature`, and only then
  packages it (`tools/release/package.sh`), so the zip and its `.sha256` hold the signed exe.
  These steps run only when the repository variable `SIGNPATH_ORGANIZATION_ID` is set, and only for
  a `v*` tag (policy `release-signing`) or a manual dispatch with `test_sign` (policy `test-signing`).
  A pull request's dry run never signs.
- **`.github/workflows/release.yml`, job `release`:** with signing on, the release notes get a
  "Code signing policy" link and the SignPath attribution, which SignPath Foundation's terms ask for
  on every download page.
- **`tools/release/signpath/artifact-configuration.xml`:** the artifact configuration to paste into
  SignPath. It signs `OpenBA.exe` at the root of the `unsigned-exe` artifact, and only if its
  ProductName is `OpenBA` and its ProductVersion is the version the workflow passes as the parameter
  `version`.
- **`src/app/version.rc.in`:** the exe's VERSIONINFO resource (ProductName `OpenBA`, the CMake
  `VERSION` as FileVersion / ProductVersion), configured into `<build>/gen/version.rc`. The terms
  ask every signed binary to carry the project name and version.
- **`.github/workflows/ci.yml`, job `windows-latest`:** the unsigned latest build from every push
  to `main`. It never goes to SignPath.

## Switching it on

SignPath Foundation's terms say the signing team must be the project's own team and own the
repository, so the repository owner does every step below.

1. **Apply** at [signpath.org](https://signpath.org) (the application form is linked there). Give the
   repository URL and the policy URL (`CODE_SIGNING.md` on `main`). Before you apply, read the open
   points below.
2. **Set up the SignPath project** once you are accepted:
   - project slug `OpenBA` (if you pick another, set the variable `SIGNPATH_PROJECT_SLUG`);
   - link the predefined **GitHub.com** trusted build system to the project. This is the origin
     verification: SignPath fetches the exe from the workflow run's own artifact and checks that the
     run was on a GitHub-hosted runner from this repository;
   - paste `tools/release/signpath/artifact-configuration.xml` in as the default artifact configuration;
   - signing policies `test-signing` and `release-signing`, with yourself as the approver on
     `release-signing` (every release needs a manual approval: that is a term of the program);
   - a CI user with submitter rights on both policies, and an API token for it.
3. **GitHub settings** (Settings → Secrets and variables → Actions):
   - secret `SIGNPATH_API_TOKEN`: the CI user's token;
   - variable `SIGNPATH_ORGANIZATION_ID`: the SignPath organization ID. **Setting this switches
     signing on**;
   - optional variables `SIGNPATH_PROJECT_SLUG` (default `OpenBA`), `SIGNPATH_SIGNING_POLICY_SLUG`
     (default `release-signing`), `SIGNPATH_ARTIFACT_CONFIGURATION_SLUG` (default: the project's
     default configuration).
4. **Check it with a test signature:** Actions → Release → Run workflow on `main` with `test_sign`
   ticked. The Windows job signs with SignPath's test certificate (signed, but not trusted by Windows)
   and leaves the package as the run's artifact. No release is published. A metadata error here means
   the artifact configuration and `src/app/version.rc.in` disagree.
5. **Release as usual** (README "Versioning"): bump the CMake `VERSION`, add the `CHANGELOG.md`
   section, push the tag. Approve the signing request in SignPath while the Windows job waits. In the
   same release, change the status note at the top of `CODE_SIGNING.md`.

## Open points before applying

- **The fonts.** The Windows zip carries FrancophilSans (`tools/release/fonts/`), which is under
  Manfred Klein's own terms, not an OSI licence. SignPath Foundation's terms ask for OSI-licensed
  components. The fonts are not compiled into the exe and are not signed, but ask SignPath in the
  application whether shipping them in the same zip is acceptable. If it is not, the zip can leave
  them out (Sharp UI then falls back to the game's glyph sheets) and they can be offered as a
  separate download.
- **The team roles** in `CODE_SIGNING.md` name only the owner. Add every member with commit or
  review rights, and every SignPath approver, before you apply.
- **SmartScreen still warns at first.** A signature names the publisher, but SmartScreen decides
  whether to warn from download reputation, which a certificate builds over time. The first signed
  releases may still show the warning.
