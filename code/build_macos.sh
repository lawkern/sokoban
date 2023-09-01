#!/usr/bin/env bash

DEVELOPMENT_BUILD=1

# Compilation flags:
COMPILER_FLAGS="-g -O0"
COMPILER_FLAGS="${COMPILER_FLAGS} -fdiagnostics-absolute-paths"
COMPILER_FLAGS="${COMPILER_FLAGS} -fmodules"

# Macro definitions:
COMPILER_FLAGS="${COMPILER_FLAGS} -DDEVELOPMENT_BUILD=${DEVELOPMENT_BUILD}"

# Warning flags:
COMPILER_FLAGS="${COMPILER_FLAGS} -Werror"
COMPILER_FLAGS="${COMPILER_FLAGS} -Wall"
COMPILER_FLAGS="${COMPILER_FLAGS} -Wno-missing-braces"
COMPILER_FLAGS="${COMPILER_FLAGS} -Wno-deprecated-declarations"

if [[ "$DEVELOPMENT_BUILD" == 1 ]]
then
    COMPILER_FLAGS="${COMPILER_FLAGS} -Wno-unused-variable"
    COMPILER_FLAGS="${COMPILER_FLAGS} -Wno-unused-function"
fi

# Linker flags and frameworks:
LINKER_FLAGS="-lm -ldl"
LINKER_FLAGS="${LINKER_FLAGS} -framework Cocoa"
LINKER_FLAGS="${LINKER_FLAGS} -framework Metal"
LINKER_FLAGS="${LINKER_FLAGS} -framework MetalKit"

mkdir -p ../build
pushd ../build > /dev/null

# # Shader compilation:
# xcrun -sdk macosx metal -c ../code/sokoban.metal -o sokoban.air
# xcrun -sdk macosx metallib sokoban.air -o sokoban.metallib

# Executable compilation:
clang ../code/platform_macos.m $COMPILER_FLAGS -o sokoban $LINKER_FLAGS

# TODO(law): Generate the standard macOS application bundle.

popd > /dev/null
