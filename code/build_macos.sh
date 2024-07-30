#!/usr/bin/env bash

DEVELOPMENT_BUILD=1

# Compilation flags:
COMPILER_FLAGS="-g -fdiagnostics-absolute-paths -fmodules"

# Macro definitions:
COMPILER_FLAGS="${COMPILER_FLAGS} -DDEVELOPMENT_BUILD=${DEVELOPMENT_BUILD}"

# Warning flags:
COMPILER_FLAGS="${COMPILER_FLAGS} -Wall -Werror"
COMPILER_FLAGS="${COMPILER_FLAGS} -Wno-missing-braces"
COMPILER_FLAGS="${COMPILER_FLAGS} -Wno-deprecated-declarations"
COMPILER_FLAGS="${COMPILER_FLAGS} -Wno-unused-variable"
COMPILER_FLAGS="${COMPILER_FLAGS} -Wno-unused-function"

# Linker flags and frameworks:
LINKER_FLAGS="-lm -ldl -framework Cocoa -framework Metal -framework MetalKit"

mkdir -p ../build
pushd ../build > /dev/null

# # Shader compilation:
# xcrun -sdk macosx metal -c ../code/sokoban.metal -o sokoban.air
# xcrun -sdk macosx metallib sokoban.air -o sokoban.metallib

# Executable compilation:
clang ../code/platform_macos_main.m -O0 -DDEVELOPMENT_BUILD=1 $COMPILER_FLAGS -target arm64-apple-macos11     -o sokoban_arm_debug $LINKER_FLAGS
clang ../code/platform_macos_main.m -O0 -DDEVELOPMENT_BUILD=1 $COMPILER_FLAGS -target x86_64-apple-macos10.12 -o sokoban_x64_debug $LINKER_FLAGS
lipo -create -output sokoban_debug sokoban_arm_debug sokoban_x64_debug

clang ../code/platform_macos_main.m -O2 -DDEVELOPMENT_BUILD=0 $COMPILER_FLAGS -target arm64-apple-macos11     -o sokoban_arm_release $LINKER_FLAGS
clang ../code/platform_macos_main.m -O2 -DDEVELOPMENT_BUILD=0 $COMPILER_FLAGS -target x86_64-apple-macos10.12 -o sokoban_x64_release $LINKER_FLAGS
lipo -create -output sokoban_release sokoban_arm_release sokoban_x64_release


# TODO(law): Generate the standard macOS application bundle.

popd > /dev/null
