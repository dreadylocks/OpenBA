// ============================================================================
// src/engine/io/packfile_fold_index.cpp -- the package directory's hash index
//                                          (block PATHINDEX, NOT RETAIL, exact)
// ============================================================================
// See packfile_fold_index.h and src/engine/collection/fold_index.h.
// ============================================================================

#include "io/packfile_fold_index.h"

#include "collection/fold_index.h"

namespace IO {

namespace {

const char* PackNodeKey(const void* node) {
    return static_cast<const RtIO::PackfileIndexNode*>(node)->element.key._data;
}

// Never destroyed, for the reason expand_memo.cpp gives (a loader thread at exit).
Collection::FoldIndex& Index() {
    static Collection::FoldIndex* const index = new Collection::FoldIndex(&PackNodeKey);
    return *index;
}

}  // namespace

void PackIndexInsert(const RtIO::PackfileIndexTree* tree, const RtIO::PackfileIndexNode* node) {
    Index().Insert(tree, node);
}

void PackIndexClear(const RtIO::PackfileIndexTree* tree) {
    Index().Clear(tree);
}

bool PackIndexFind(const RtIO::PackfileIndexTree* tree, const char* key, RtIO::PackfileIndexNode** out) {
    const void* node = nullptr;
    if (!Index().Find(tree, tree->count, key, &node))
        return false;
    *out = const_cast<RtIO::PackfileIndexNode*>(static_cast<const RtIO::PackfileIndexNode*>(node));
    return true;
}

void PackIndexCheck(bool answered, const char* key, const RtIO::PackfileIndexNode* indexed,
                    const RtIO::PackfileIndexNode* walked) {
    Collection::FoldIndex::Check("packages", key, answered, indexed, walked);
}

}  // namespace IO
