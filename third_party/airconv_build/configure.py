"""Generate a build recipe for externally supplied DXMT airconv (LGPL shared library)."""
import argparse
import json
import os
from pathlib import Path
import shlex

root = Path(__file__).resolve().parent
src = Path(os.environ.get("DXMT_SRC", str(root.parent / "dxmt_src"))).resolve()
parser = argparse.ArgumentParser()
parser.add_argument("--llvm", type=Path, default=root.parent / "llvm15_arm64/build",
                    help="LLVM 15 build with arm64 libraries")
parser.add_argument("--output", type=Path, default=root / "build/source",
                    help="Generated CMake source directory")
args = parser.parse_args()
llvm = args.llvm.resolve()
output = args.output.resolve()
if output == root:
    parser.error("--output must not overwrite the recipe source directory")
entries = json.loads((src / "build/compile_commands.json").read_text())
selected = [entry for entry in entries
            if "darwin/libairconv.a.p/" in entry["output"]
            or "libDXBCParserNative.a.p/" in entry["output"]]
if not selected:
    parser.error("DXMT compile_commands.json contains no airconv sources")
files = []
includes = []
for entry in selected:
    files.append(str((src / "build" / entry["file"]).resolve()))
    for token in shlex.split(entry["command"]):
        if token.startswith("-I"):
            directory = str((src / "build" / token[2:]).resolve())
            if Path(directory).exists() and directory not in includes:
                includes.append(directory)
includes = [directory for directory in includes if "llvm15_x86_64/include" not in directory]


def quote(value):
    """Quote literal paths for CMake, including spaces, quotes and dollar signs."""
    return '"' + str(value).replace("\\", "\\\\").replace('"', '\\"').replace("$", "\\$").replace(";", "\\;") + '"'


text = "cmake_minimum_required(VERSION 3.20)\n"
text += "project(airconv_native LANGUAGES C CXX)\nset(CMAKE_CXX_STANDARD 20)\n"
text += "find_package(LLVM 15 REQUIRED CONFIG PATHS " + quote(llvm / "lib/cmake/llvm") + " NO_DEFAULT_PATH)\n"
text += "llvm_map_components_to_libnames(AIRCONV_LLVM_LIBS bitwriter passes)\n"
text += "add_library(airconv SHARED\n" + "".join(" " + quote(path) + "\n" for path in files) + ")\n"
text += "target_include_directories(airconv PRIVATE ${LLVM_INCLUDE_DIRS}\n"
text += "".join(" " + quote(path) + "\n" for path in includes) + ")\n"
text += "target_compile_options(airconv PRIVATE -fblocks -fno-rtti -Wno-extern-c-compat -Wno-microsoft-exception-spec)\n"
text += "target_compile_definitions(airconv PRIVATE NOMINMAX _WIN32_WINNT=0xa00 DXMT_PAGE_SIZE=4096)\n"
text += "target_link_libraries(airconv PRIVATE ${AIRCONV_LLVM_LIBS})\n"
output.mkdir(parents=True, exist_ok=True)
(output / "CMakeLists.txt").write_text(text)
print(len(files), "external sources; LLVM:", llvm)
