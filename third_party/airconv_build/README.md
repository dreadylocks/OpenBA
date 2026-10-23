# Native dynamically loaded airconv

DXMT is read unchanged from the supplied sibling
`third_party/dxmt_src` (cloned by `tools/macos/setup_deps.sh`). No DXMT implementation source is copied into
OpenBA's `src/`. Its LGPL text is `COPYING.LIB`. OpenBA loads the separate arm64
`libairconv.dylib` using dlopen; `BANISHED_AIRCONV=/path/libairconv.dylib` permits
replacement. No DXMT implementation is statically linked into the executable. The separate
FFmpeg-derived wmadec decoder is LGPL-2.1-or-later and is statically linked into
OpenBA; see the root `THIRD_PARTY_NOTICES.md` for its relinking terms.
LLVM's permissively licensed static components are inside the separate dylib.

Build LLVM first using `../llvm15_arm64/README.md`, then:

```sh
python3 third_party/airconv_build/configure.py --llvm third_party/llvm15_arm64/build
cmake -S third_party/airconv_build/build/source -B third_party/airconv_build/build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0
cmake --build third_party/airconv_build/build -j4
echo 14.0 > third_party/airconv_build/build/.deployment_target
```

The generator reads the external DXMT build's compile_commands.json to select
21 airconv and DXBCParser translation units and generated shader headers. It
uses LLVM CMake component mapping for `bitwriter passes`, not every archive.
No DXMT patches were necessary. Redistribution must include the corresponding LGPL
source/build material and notices.

`metal-shader-check` exercises the same converter as the backend without opening
a window. 465 extracted retail blobs converted and loaded successfully on arm64.
The historical x86_64-link failure is superseded by this successful native build.

The generator writes CMakeLists.txt under `build/source/`, never into the recipe source
directory; `--output` overrides that directory. DXMT_SRC selects the external checkout.
Everything targets macOS 14.0 (engine, launcher, LLVM, converter). The converter needs it: DXMT uses `std::to_chars(double)` (macOS 13.3+) and its
shader header generation targets Metal 3.1 (macOS 14.0).

DXMT headers are needed only to build the separate converter dylib. OpenBA and
`metal-shader-check` compile against the local declarations in
`src/render/metal/airconv_abi.h`; neither requires the DXMT checkout to compile.
