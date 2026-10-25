#pragma once
// ============================================================================
// src/ext/crash_report/crash_report.h -- NOT RETAIL. ext.crash_reports: crashes kept and sent
// ============================================================================
// A crash leaves standalone_crash.log (a hang standalone_hang.log) in the working directory, and
// the next start truncates both with the other logs. So the next start first keeps a copy of a
// crashed run's logs in crash_reports/<time>/ (the error log's start and end only: it can be
// hundreds of MB), the newest five. That happens whatever the settings say.
//
// Then, with ext.crash_reports on (class A, the Extensions dialog's "Send crash reports") and a
// crash report address, the kept crashes not sent yet go to it in the background, oldest first,
// at most three a start: one Sentry envelope each (report.h), the user's name taken out of the
// logs' paths. The address is a Sentry DSN (Sentry, self-hosted Sentry, GlitchTip, Bugsink):
// settings.cfg `crashreport.dsn`, or else the one the build was made with (CMake
// OPENBA_CRASH_DSN; empty unless the release workflow's OPENBA_CRASH_DSN variable is set). No
// address: nothing leaves the machine. A sent crash keeps its folder, marked by a `sent` file.
// docs/fork/CRASH_REPORTS.md.
// ============================================================================

#include <string>

namespace CrashReport {

// main / WinMain, first, before the logs are opened again (it prints nothing: stderr is not
// redirected yet; Start says what it kept).
void KeepLastCrash();

// Ext::InitFromSettings, after the registry is installed. `settingsDsn`: settings.cfg's
// crashreport.dsn ("" when absent).
void Start(const std::string& settingsDsn);

}  // namespace CrashReport
