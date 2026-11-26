# Monocypher in OpenBA

- **What:** Monocypher 4.0.2, a small cryptography library by Loup Vaillant and contributors.
- **Source:** https://github.com/LoupVaillant/Monocypher, tag `4.0.2`
  (`archive/refs/tags/4.0.2.tar.gz`, sha256 `bc1ca30b1b2654e4e7daf2492c0d204200e55137f23fda6b7142fd7d523bd6b4`).
- **Licence:** CC0-1.0 or BSD-2-Clause, at the user's choice (`LICENCE.md`, verbatim). OpenBA takes
  it under BSD-2-Clause and ships `LICENCE.md` with every package.
- **Files, unmodified:**

  | File | sha256 |
  |---|---|
  | `src/monocypher.c` | `02174117935699d418443c75a558a287deb06ef8cf7c1adced61d9047d2f323d` |
  | `src/monocypher.h` | `fcaf6ed771358bb4f40fba016f6518ae86ec02b1b877d2cc35ad92d3a26fd7b3` |
  | `src/optional/monocypher-ed25519.c` | `97d581639dfa72be08a6d57deb7d79b736be001cb416819cab196d22559d242b` |
  | `src/optional/monocypher-ed25519.h` | `3a3035181f991a158d0e1c7567258f0bae8ba0f1f23c5512b4a1db1b3c9730ce` |

- **What uses it:** `ext.auto_update` (`docs/fork/AUTO_UPDATE.md`): `crypto_ed25519_check`
  verifies the update manifest's signature, which is standard Ed25519 (RFC 8032, SHA-512) from the
  optional `monocypher-ed25519` file. The release tool `update_sign`
  (`tools/release/update_sign.cpp`) also uses `crypto_ed25519_key_pair` and `crypto_ed25519_sign`.
  The library is linked statically into `OpenBA`, `unit` and `update_sign`.
- **Updating:** replace the four files from a newer tag, update the checksums above and in
  `THIRD_PARTY_NOTICES.md`, and run `tools/build.sh unit` (`auto_update_test.cpp` checks the RFC 8032
  test vector).
