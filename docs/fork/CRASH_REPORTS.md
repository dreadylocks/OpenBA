# Crash reports (this fork)

When OpenBA crashes, the next start keeps the crash's logs and, if a crash report address is set,
sends them to it in the background. The address is a **Sentry DSN**: Sentry itself (its free
Developer plan takes 5,000 errors a month for one person), a self-hosted Sentry, or a Sentry-compatible
server such as GlitchTip or Bugsink. Code: `src/ext/crash_report/` (extension `crash_reports`, class A).

## What happens after a crash

1. The crash handler writes `standalone_crash.log` (a freeze writes `standalone_hang.log`) in the game's
   log folder: the Banished folder on Windows and Linux, `~/Library/Application Support/OpenBA/Run`
   on a Mac.
2. The next start, before it opens the logs again (which empties them), copies that run's logs into
   `crash_reports/<date>-<time>/` beside them: the crash or hang log, and the start and end of
   `standalone_error.log` (512 KB at most; it can grow to hundreds of MB) and `standalone_debug.log`.
   The newest five crashes are kept. This happens whatever the settings say, so a player can always
   send that folder by hand.
3. Once the settings are read, if **Send crash reports** is on (the Extensions dialog; settings key
   `ext.crash_reports`, on by default) and there is an address, each kept crash not sent yet (three at
   most per start, oldest first) goes to the address as one Sentry envelope: an event (level fatal,
   the fault, the stack the crash log lists, demangled on Mac and Linux) with the logs attached. A
   crash the server took gets a `sent` file in its folder and is not sent again. With no line (or a
   busy server) it waits for the next start.

The event log says what happened (`standalone_error.log`, lines starting `[CrashReport]`).

## What is sent

The crash log and the logs' start and end, as text. Before sending, the user's name is taken out of
file paths (`/Users/<name>`, `C:\Users\<name>`, `/home/<name>` become `<user>`). Nothing else from
the machine is added: no user id, no machine name. The logs do name the game's version, the mods in
use, the extension settings and, in neighbor towns, the room, town and relay names. The server also
sees the sender's internet address, as any web request does; in Sentry, turn on **Prevent Storing of
IP Addresses** (project settings, Security & Privacy) if you do not want it kept.

## Setting the address

The address is looked up in this order:

1. `crashreport.dsn=<DSN>` in the player's `settings.cfg` (`Documents/Banished/settings.cfg`; Mac:
   `~/Documents/Banished/settings.cfg`).
2. The address the build was made with: the CMake cache variable `OPENBA_CRASH_DSN` (empty by
   default). The release workflow passes the repository variable `OPENBA_CRASH_DSN`, so a released
   download sends to it without any setup.

No address: nothing leaves the machine (the copies in `crash_reports/` are still kept). To stop
sending, switch **Send crash reports** off in the Extensions dialog (or `ext.crash_reports=0`).

## Setting up Sentry (once)

1. Make a free account at sentry.io and a project of platform **Native** (C/C++).
2. Copy its DSN (Project Settings > Client Keys (DSN)); it looks like
   `https://<key>@o<number>.ingest.<region>.sentry.io/<project>`. A DSN is meant to be public (it can
   only send events), but anyone holding it can send events to the project.
3. For released downloads: in the GitHub repository, Settings > Secrets and variables > Actions >
   Variables, add `OPENBA_CRASH_DSN` with the DSN. The next release tag builds it in.
   For one machine only: put `crashreport.dsn=<DSN>` in its `settings.cfg`.

Crashes from Windows have module offsets but no function names (the Windows crash handler does not
symbolize); Mac and Linux crash logs name the functions.

## How it was tested

- Unit: `tests/unit/ext_crash_report_test.cpp` (the DSN, the Mac and Windows crash log formats, the
  name scrub, the log cut, the envelope's framing, the POST request and status line).
- On the Mac against a local stand-in for Sentry's envelope endpoint (plain HTTP): a planted crash
  log and a 2.4 MB error log were kept in `crash_reports/`, sent as one envelope (event, crash log,
  the error log cut to 512 KB) with no user name left in it, and marked sent; the next start sent
  nothing; with `ext.crash_reports=0` the crash was kept and not sent.
- Not yet: a real Sentry project over HTTPS; the Windows (WinHTTP) and Linux (OpenSSL) senders run
  only in CI's build so far.
