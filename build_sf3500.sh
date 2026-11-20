#!/bin/bash
# Cross build for the Datafrog SF3500 (MIPS32r2, little endian). Output: build-sf3500/gweled.stripped.mipsel
# Needs the toolchain from ~/handhelds/sf3000-work/buildroot (Codescape mips-mti-linux-gnu, glibc 2.20 sysroot with SDL2),
# and assets/gweled.pak (tools/bake.py). Override the toolchain with SF3500_TC=<buildroot/output/host>.
set -e
cd "$(dirname "$0")"
TC=${SF3500_TC:-$HOME/handhelds/sf3000-work/buildroot/output/host}
SYSROOT=$TC/mipsel-buildroot-linux-gnu/sysroot
CC=$TC/bin/mips-mti-linux-gnu-gcc
CXX=$TC/bin/mips-mti-linux-gnu-g++
ARCH="-mips32r2 -mtune=74kc -mhard-float -EL --sysroot=$SYSROOT"
# -O2 on purpose; no fast-math, no dspr2
CFLAGS="$ARCH -O2 -fno-strict-aliasing -Wall -Wno-unused-result -DSF3500 -Isrc -Iplat -I$SYSROOT/usr/include/SDL2"
CXXFLAGS="$ARCH -O2 -w -fno-exceptions -fno-rtti -Iplat"
OUT=${OUT:-build-sf3500}
mkdir -p "$OUT"
for f in src/*.c; do $CC $CFLAGS -c "$f" -o "$OUT/$(basename "${f%.c}").o"; done
for f in hwdisp joykey hwaudio sf3500_mixer; do $CXX $CXXFLAGS -c plat/$f.cpp -o "$OUT/$f.o"; done
$CXX $ARCH "$OUT"/*.o -o "$OUT/gweled.mipsel" -lSDL2 -lpthread -ldl -lm
$TC/bin/mips-mti-linux-gnu-strip -o "$OUT/gweled.stripped.mipsel" "$OUT/gweled.mipsel"
ls -l "$OUT"/gweled*.mipsel
