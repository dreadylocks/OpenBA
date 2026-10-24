// ============================================================================
// src/ext/large_packages/large_packages.h -- fix.large_packages: packages past 2 GiB
// ============================================================================
// NOT RETAIL. docs/extensions/handins/LARGEPKM.md, docs/retail_bugs.md section 9.
// The pure rule is large_packages_unwrap.h.
//
// WHAT. Retail reads a packed entry at the int32 `baseOffset + cursor` with a
// 32-bit SetFilePointer (FileSystem::ReadFile 0x1800ace43..67), so every entry
// of a package that starts at or past 2 GiB fails to read or reads the wrong
// bytes (the single-file MegaMod.pkm, 10.76 GB, crashes retail ~2 s into the
// content load at MaterialInstance::BuildParameters). With the fix on, a
// mounted package larger than kRetailLimit gets 64-bit entry offsets, unwrapped
// from its index and validated (large_packages_unwrap.h); its packed reads
// seek 64-bit. A package up to 2 GiB is never looked at: it reads exactly as
// retail reads it.
//
// WHEN. OnMounted is IO::FileSystem::_extPackagesMounted, run at the end of
// every LoadPackages (the boot's .pkg mount runs before the seam is installed
// and mounts no .pkm; every mods mount and remount after it runs the hook).
// The fix's bool is read there, so a toggle applies at the next mount.
//
// WHAT IT NEVER TOUCHES. The index, the entries' names, sizes and uint32
// offsets, the pack order, the path index, the mod list, the save: only which
// bytes a packed read fetches. The mod scan (Modification::RefreshList) reads
// each .pkm's index and closes it without reading an entry, so it needs nothing.
//
// FIX OFF. Retail's reads, and its crash. Unless the run is forced faithful
// (the gate's /profile:faithful, where the seam stays null), the hook still
// runs and logs ONE line per package past 2 GiB, naming the fix, so the crash
// that follows has its cause in standalone_error.log.
// ============================================================================
#pragma once

namespace IO {
class FileSystem;
}

namespace Ext {
namespace LargePackages {

// The IO::FileSystem::_extPackagesMounted body.
void OnMounted(IO::FileSystem& fs);

// registry_boot.cpp: the seam, null under the faithful profile.
void Sync();

}  // namespace LargePackages
}  // namespace Ext
