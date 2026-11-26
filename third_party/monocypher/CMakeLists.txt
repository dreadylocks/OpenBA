# third_party/monocypher -- Monocypher 4.0.2 (CC0-1.0 or BSD-2-Clause; LICENCE.md), unmodified.
# README.banished.md has the source, the checksums and what uses it. One static C library, built
# with fixed flags in every flavour (as third_party/wmadec): -O2 whatever the game's flavour, and
# -w because any warnings are upstream's.
enable_language(C)

add_library(monocypher STATIC
  src/monocypher.c
  src/optional/monocypher-ed25519.c)
set_target_properties(monocypher PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
target_include_directories(monocypher PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/src/optional)
target_compile_options(monocypher PRIVATE -O2 -w)
