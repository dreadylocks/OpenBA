# src/render/dx11/backend.cmake -- the DX11 renderer backend (BANISHED_RENDER_BACKEND=dx11).
#
# Included by the top-level CMakeLists.txt from src/render/${BANISHED_RENDER_BACKEND}/backend.cmake,
# before the targets exist. The contract every backend.cmake fills (src/render/common/README.md):
#   RENDER_SRCS              the backend's own sources (the top level appends src/render/common/*.cpp)
#   RENDER_BACKEND_INCLUDES  the backend's include directories (added to BANISHED_INCLUDES)
#   RENDER_BACKEND_READY     ON when OpenBA can be configured with this backend on this host
# plus any per-source properties of its own files (directory scope, so they reach OpenBA).

file(GLOB_RECURSE RENDER_SRCS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/render/dx11/*.cpp)
set(RENDER_BACKEND_INCLUDES ${CMAKE_SOURCE_DIR}/src/render/dx11)
set(RENDER_BACKEND_READY ON)

if(WIN32)
  # The verbatim upstream ASSAO copy (GameTechDev/ASSAO, MIT; src/render/dx11/ao/assao/README.md) is not
  # ours to tidy: its standing -Wsign-compare / unused warnings are silenced per file so the warning
  # gate measures only our code (gate FAA, 2026-10-16: five perf-build warnings, all in this file).
  set_source_files_properties(${CMAKE_SOURCE_DIR}/src/render/dx11/ao/assao/ASSAODX11.cpp PROPERTIES
                              COMPILE_OPTIONS "-Wno-sign-compare;-Wno-unused-function;-Wno-unused-variable")
  set_source_files_properties(${CMAKE_SOURCE_DIR}/src/render/dx11/ao/assao/ASSAODX11.cpp PROPERTIES
                              COMPILE_DEFINITIONS BANISHED_THIRD_PARTY_SOURCE)
else()
  # The Linux game (Phase D.4 LINUX-1, RENDER step 4): the same dx11 code over dxvk-native 3.1.1
  # (tools/fetch_dxvk_native.sh; third_party/dxvk-native/README.md). It links no graphics library:
  # src/platform/posix/gfx_posix.cpp dlopens libdxvk_dxgi / libdxvk_d3d11 at run time, so only
  # the headers are used, and only by src/render/dx11 (as -isystem: they are MinGW's and
  # dxvk's, not ours). Skipped, with a message, until the fetch has run.
  set(BANISHED_DXVK_NATIVE ${CMAKE_SOURCE_DIR}/third_party/dxvk-native/3.1.1/usr)
  if(EXISTS ${BANISHED_DXVK_NATIVE}/include/dxvk/d3d11.h)
    set_source_files_properties(${RENDER_SRCS} PROPERTIES
                                COMPILE_OPTIONS "-isystem;${BANISHED_DXVK_NATIVE}/include/dxvk")
  else()
    set(RENDER_BACKEND_READY OFF)
    message(STATUS "OpenBA (Linux) not configured: run tools/fetch_dxvk_native.sh for the dxvk-native headers")
  endif()
endif()
