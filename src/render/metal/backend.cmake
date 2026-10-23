# src/render/metal/backend.cmake -- the Metal renderer backend (BANISHED_RENDER_BACKEND=metal; macOS).
#
# Included by the top-level CMakeLists.txt from src/render/${BANISHED_RENDER_BACKEND}/backend.cmake,
# before the targets exist. The contract (src/render/common/README.md) is filled as in dx11:
#   RENDER_SRCS              the backend's own sources (Objective-C++ .mm and plain .cpp)
#   RENDER_BACKEND_INCLUDES  the backend's include directories
#   RENDER_BACKEND_READY     ON when OpenBA can be configured with this backend on this host
# plus RENDER_BACKEND_LINK_LIBRARIES (frameworks, linked into OpenBA by the top level).

if(NOT APPLE)
  set(RENDER_SRCS)
  set(RENDER_BACKEND_INCLUDES)
  set(RENDER_BACKEND_READY OFF)
  message(STATUS "OpenBA not configured: BANISHED_RENDER_BACKEND=metal needs macOS")
  return()
endif()

enable_language(OBJCXX)
set(CMAKE_OBJCXX_STANDARD 17)

file(GLOB_RECURSE RENDER_METAL_MM CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/render/metal/*.mm)
file(GLOB_RECURSE RENDER_METAL_CPP CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/render/metal/*.cpp)
set(RENDER_SRCS ${RENDER_METAL_MM} ${RENDER_METAL_CPP})
set(RENDER_BACKEND_INCLUDES ${CMAKE_SOURCE_DIR}/src/render/metal)
set(RENDER_BACKEND_READY ON)
set(RENDER_BACKEND_LINK_LIBRARIES "-framework Metal" "-framework QuartzCore")

# Metal objects use ARC. The converter (DXMT airconv, LGPL) is dlopened at run time from
# libairconv.dylib and never linked; its C interface is declared in airconv_abi.h, so no DXMT
# checkout is needed to compile the engine.
set_source_files_properties(${RENDER_METAL_MM} PROPERTIES
                            COMPILE_OPTIONS "-fobjc-arc")

# The shader conversion check (tools/metal/shader_check.mm): converts every shader through the
# dylib without opening the game. Not part of the default build.
add_executable(metal-shader-check EXCLUDE_FROM_ALL
               ${CMAKE_SOURCE_DIR}/tools/metal/shader_check.mm
               ${CMAKE_SOURCE_DIR}/src/render/metal/shaders.mm
               ${CMAKE_SOURCE_DIR}/src/render/metal/device_support.mm
               ${CMAKE_SOURCE_DIR}/src/app/options.cpp)
target_include_directories(metal-shader-check PRIVATE
                           ${CMAKE_SOURCE_DIR}/src
                           ${CMAKE_SOURCE_DIR}/src/render/metal
                           ${CMAKE_SOURCE_DIR}/src/engine
                           ${CMAKE_SOURCE_DIR}/src/platform/common)
target_compile_options(metal-shader-check PRIVATE -fobjc-arc)
target_link_libraries(metal-shader-check PRIVATE "-framework Metal" "-framework Foundation")
