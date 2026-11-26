#pragma once
// ============================================================================
// src/ext/auto_update/update_key.h -- NOT RETAIL. ext.auto_update: the update signing key's public half
// ============================================================================
// docs/fork/AUTO_UPDATE.md §5. A manifest is installed only when its Ed25519 signature checks against
// this key. Its secret half is the release's one secret: ~/.config/openba/update_signing.key on the
// publishing machine and a copy in the maintainer's password manager, never in the repo
// (tools/release/update_sign.cpp made it on 2026-11-25; `update_sign public <key file>` prints this).
// Replacing the key means every player installs, by hand, the one build that carries the new one.
//   0a91d1cce308f8139bb881ab8640393e1dd4d0203dcec6f9e81e8676dfb30dd0
// ============================================================================

#include <cstdint>

namespace AutoUpdate {

constexpr uint8_t kUpdatePublicKey[32] = {
    0x0a, 0x91, 0xd1, 0xcc, 0xe3, 0x08, 0xf8, 0x13,
    0x9b, 0xb8, 0x81, 0xab, 0x86, 0x40, 0x39, 0x3e,
    0x1d, 0xd4, 0xd0, 0x20, 0x3d, 0xce, 0xc6, 0xf9,
    0xe8, 0x1e, 0x86, 0x76, 0xdf, 0xb3, 0x0d, 0xd0,
};

}  // namespace AutoUpdate
