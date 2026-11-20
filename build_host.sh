#!/bin/bash
# Desktop build for development and tests: ./build_host.sh  ->  build-host/gweled
# Needs assets/gweled.pak (tools/bake.py) first.
set -e
cd "$(dirname "$0")"
mkdir -p build-host
gcc -O2 -g -Wall -Wextra -Wno-unused-result -Wno-unused-parameter -Isrc $(sdl2-config --cflags) src/*.c -o build-host/gweled $(sdl2-config --libs) -lm
echo built build-host/gweled
