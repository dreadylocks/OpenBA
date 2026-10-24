#pragma once
// src/ext/package_cache/package_cache.h -- ext.package_cache (class A, live at each mount)
//
// NOT RETAIL (docs/extensions/handins/PKGCACHE.md, issue #13). LoadPackages builds the merged
// directory of every mounted package (`FileSystem::_packfileIndex`) by one search and one insert
// per package entry: 401,167 entries under the seven-part MegaMod, at both of a boot's remounts.
// With the row on, the tree a mount builds is kept in the derived-data cache (src/ext/derived_cache/,
// <Documents>\Banished\Cache\), under a key made from everything that tree is built from (the
// packages in mount order, their entry names, their override flags), and the next mount with the
// same packages builds the identical tree from it without a single comparison
// (index_codec.h). Never under the faithful profile: the seams stay null and the cache is not
// opened.

namespace Ext { namespace PackageCache {

// Boot only (registry_boot.cpp): installs the IO::FileSystem::_extIndexRestore / _extIndexBuilt
// seams (null under the faithful profile). The hooks read the row at each mount.
void Sync();

}}  // namespace Ext::PackageCache
