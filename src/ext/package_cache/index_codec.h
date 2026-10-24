#pragma once
// ============================================================================
// src/ext/package_cache/index_codec.h -- ext.package_cache: the package
//   directory tree, written to bytes and built back node for node
// ============================================================================
// NOT RETAIL (docs/extensions/handins/PKGCACHE.md, issue #13).
//
// LoadPackages' third loop builds `FileSystem::_packfileIndex`, an AVL tree of
// {path, packIndex/fileIndex} pairs, by one `_stricmp` descent and one insert
// (or override) per package entry: 401,167 of them under MegaMod, twice a boot.
// What the loop builds is a function of its inputs alone: the packages in
// `_packfiles` order, each one's entry names in order and its `_overridable`
// byte. `KeyBytes` is exactly those inputs, so equal key bytes mean an equal
// tree. `Encode` writes the built tree in pre-order: per node, which entry's
// name buffer is its key (the node's key SHARES that buffer: GetFileName's copy,
// the insert's copy and the node's copy all adopt it), its value pair, its AVL
// balance and height, and which children it has. `Decode` allocates the nodes
// with the loop's own allocator and constructor (rtavl::FilenameNodeAlloc: a
// copy of the entry's name, so the key buffer and its reference count end as the
// loop leaves them) and links them in the same shape. The result is the loop's
// tree: same shape, same key buffers, same values, same balance and height.
// Only the nodes' addresses and their allocation order differ, and nothing
// reads either.
// ============================================================================

#include <cstdint>
#include <string>
#include <vector>

#include "io/FileSystem.h"

namespace Ext {
namespace PackageCache {

// One mounted package as the third loop reads it.
struct PackView {
    const RtIO::PackageFileEntry* entries = nullptr;
    int32_t count = 0;
    bool overridable = false;
};

// The loop's inputs as bytes, for the cache key. false when an entry's name is
// null (such a node orders below everything; the cache stays out of it).
bool KeyBytes(const std::vector<PackView>& packs, std::vector<uint8_t>& out);

// The tree as bytes. false when a node's key is not one of the entries' name
// buffers (then the tree was not built by the loop alone: nothing is stored).
bool Encode(const RtIO::PackfileIndexTree& tree, const std::vector<PackView>& packs, std::vector<uint8_t>& out);

// Builds the tree into `tree`, which must be empty. false on a payload that does
// not fit `packs` or whose keys are not in strictly increasing `_stricmp` order;
// `tree` is then left empty and every node made is freed. `onNode` (may be null)
// is called for every node made, after the tree is complete.
bool Decode(const std::vector<uint8_t>& payload, const std::vector<PackView>& packs, RtIO::PackfileIndexTree& tree,
            void (*onNode)(const RtIO::PackfileIndexTree& tree, const RtIO::PackfileIndexNode* node) = nullptr);

// Two trees with the same shape, key buffers (by address), values, balance and
// height. `why` says where they first differ.
bool SameTree(const RtIO::PackfileIndexTree& a, const RtIO::PackfileIndexTree& b, std::string* why);

}  // namespace PackageCache
}  // namespace Ext
