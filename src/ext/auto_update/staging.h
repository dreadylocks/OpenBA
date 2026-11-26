#pragma once
// ============================================================================
// src/ext/auto_update/staging.h -- NOT RETAIL. ext.auto_update: the staging folder on disk
// ============================================================================
// docs/fork/AUTO_UPDATE.md §6. The folder is Platform::Update::StagingDir(). Its state is a few
// one-line text files, each written atomically:
//   ready      a verified, unpacked build waiting in staged/<version>/
//   pending    a build installed (InPlace) or handed over to (Versioned) that has not reached its menu
//   started    the last build that reached its menu
//   bad        builds that failed to start, one per line: never installed again
//   current    Versioned: Updates/<current>/OpenBA.app is the build to run
//   previous   Versioned: what current was before ("" = the player's own bundle)
//   notice     one line for the main menu (a rollback, an install that failed); removed once shown
//   resume     the save Restart now wrote: the next start loads it
//   restart    Windows: the PID of the process that asked for the restart; the new one waits for it
// and these folders:
//   download/          a package being downloaded       unpack/   a package being unpacked
//   staged/<version>/  a build ready to install         previous-program/   InPlace: the replaced program
//   <version>/         Versioned: an installed build (OpenBA.app inside)
// update.log keeps the last lines AtLaunch and the check wrote (AtLaunch runs before the game's logs
// are open, and the Windows supervisor never opens them).
// ============================================================================

#include <filesystem>
#include <set>
#include <string>

#include "ext/auto_update/launch_plan.h"

namespace AutoUpdate {
namespace Staging {

namespace fs = std::filesystem;

// The first line of <dir>/<name> without its line end; "" when absent.
std::string ReadLine(const fs::path& dir, const char* name);
// <dir>/<name> = value + "\n", atomically. False on failure.
bool WriteLine(const fs::path& dir, const char* name, const std::string& value);
void Remove(const fs::path& dir, const char* name);
std::set<std::string> ReadBad(const fs::path& dir);
bool AddBad(const fs::path& dir, const std::string& version);

// Everything launch_plan.h decides on. `programName`: Platform::Update::ProgramName(), the file (or
// bundle) a staged or installed build must hold.
StagingState Read(const fs::path& dir, const char* programName, bool versioned);

// A path from / to UTF-8 (std::filesystem's own narrow strings are the ANSI code page on Windows).
fs::path    Path(const std::string& utf8);
std::string Utf8(const fs::path& p);

// Append one line to <dir>/update.log (kept under 64 KB).
void AppendLog(const fs::path& dir, const std::string& line);

}  // namespace Staging
}  // namespace AutoUpdate
