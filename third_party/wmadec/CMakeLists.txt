# third_party/wmadec -- the WMA v2 decoder extracted from FFmpeg n8.1.3 (LGPL-2.1-or-later).
# EXTRACT.md has the file list, the shim and the licence notes. One static C library, built
# with the same fixed flags in every flavour (the Makefile's WMADEC rule is the twin):
# -O2 whatever the game's flavour, and -w because the warnings are upstream's (79 under
# -Wall -Wextra, all in the verbatim files; EXTRACT.md "Build").
enable_language(C)

set(WMADEC_SRCS
  ffmpeg/libavcodec/wmadec.c
  ffmpeg/libavcodec/wma.c
  ffmpeg/libavcodec/wma_common.c
  ffmpeg/libavcodec/wma_freqs.c
  ffmpeg/libavcodec/vlc.c
  ffmpeg/libavcodec/sinewin.c
  ffmpeg/libavutil/tx.c
  ffmpeg/libavutil/tx_float.c
  ffmpeg/libavutil/float_dsp.c
  ffmpeg/libavutil/float_scalarproduct.c
  ffmpeg/libavutil/reverse.c
  shim/aac_scalefactor_tables.c
  shim/wmadec_shim.c
  shim/wmadec_glue.c)

add_library(wmadec STATIC ${WMADEC_SRCS})
set_target_properties(wmadec PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS ON)
target_include_directories(wmadec
  PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shim
          ${CMAKE_CURRENT_SOURCE_DIR}/shim/libavcodec
          ${CMAKE_CURRENT_SOURCE_DIR}/shim/libavutil
          ${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg)
target_compile_definitions(wmadec PRIVATE HAVE_AV_CONFIG_H NDEBUG)
target_compile_options(wmadec PRIVATE -O2 -w)

if(APPLE)
  target_compile_options(wmadec PRIVATE -ffp-contract=off)
endif()
