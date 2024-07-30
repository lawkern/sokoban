#!/usr/bin/env bash

COMPILER_FLAGS="-g -fdiagnostics-absolute-paths -Wall -Werror"
COMPILER_FLAGS="${COMPILER_FLAGS} -Wno-unused-variable"
COMPILER_FLAGS="${COMPILER_FLAGS} -Wno-unused-function"

LINKER_FLAGS="-lm -lX11 -lGL"

mkdir -p ../build
pushd ../build > /dev/null

clang ../code/platform_linux_main.c -O0 -DDEVELOPMENT_BUILD=1 $COMPILER_FLAGS -o sokoban_debug   $LINKER_FLAGS
clang ../code/platform_linux_main.c -O2 -DDEVELOPMENT_BUILD=0 $COMPILER_FLAGS -o sokoban_release $LINKER_FLAGS

popd > /dev/null
