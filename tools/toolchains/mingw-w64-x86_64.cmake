# tools/toolchains/mingw-w64-x86_64.cmake -- cross-build the Windows OpenBA.exe off Windows.
#
# NOT the reference toolchain (that is MSYS2 UCRT64 GCC on Windows, docs/reference/environment.md).
# This file lets a macOS or Linux host build the same x86-64 PE with mingw-w64 GCC, e.g.
#   brew install mingw-w64 cmake ninja                      (macOS)
#   apt install g++-mingw-w64-x86-64-posix cmake ninja-build (Debian / Ubuntu)
#   cmake -S . -B build/cross-debug -G Ninja -DBANISHED_FLAVOUR=debug \
#         -DCMAKE_TOOLCHAIN_FILE=tools/toolchains/mingw-w64-x86_64.cmake
#   cmake --build build/cross-debug -t OpenBA
# tools/build.sh does this by itself on a non-Windows host when it is given --cross.
#
# It must be GCC, not clang / llvm-mingw: CMakeLists.txt passes -fno-lifetime-dse, which clang
# rejects, and the port relies on what it does. The output only runs on Windows (or under Wine).
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(BANISHED_MINGW_TRIPLE x86_64-w64-mingw32 CACHE STRING "mingw-w64 target triple (tool prefix)")
set(CMAKE_C_COMPILER   ${BANISHED_MINGW_TRIPLE}-gcc)
set(CMAKE_CXX_COMPILER ${BANISHED_MINGW_TRIPLE}-g++)
set(CMAKE_RC_COMPILER  ${BANISHED_MINGW_TRIPLE}-windres)

# Search the target's own headers and libraries, never the host's; host programs (python3 for
# tools/ext_rsc_embed.py) still come from PATH.
execute_process(COMMAND ${CMAKE_C_COMPILER} -print-sysroot
                OUTPUT_VARIABLE _banished_mingw_sysroot OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET)
if(_banished_mingw_sysroot)
  set(CMAKE_FIND_ROOT_PATH ${_banished_mingw_sysroot})
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
