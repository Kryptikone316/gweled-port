#!/bin/bash
# Assembles the device package: package/Gweled.sh + package/gweled/{run.sh,gweled.mipsel,README.txt,NOTICE.md,boxart.png,music/,licenses/}
# Run ./build_sf3500.sh first (it needs assets/gweled.pak from tools/bake.py). Box art: package-src/boxart.png (500x500).
set -e
cd "$(dirname "$0")"
OUT=package
rm -rf "$OUT"; mkdir -p "$OUT/gweled/licenses"
cp package-src/Gweled.sh "$OUT/"
cp package-src/{run.sh,README.txt,NOTICE.md} "$OUT/gweled/"
cp build-sf3500/gweled.stripped.mipsel "$OUT/gweled/gweled.mipsel"
[ -f package-src/boxart.png ] && cp package-src/boxart.png "$OUT/gweled/"
cp licenses/*.txt "$OUT/gweled/licenses/"
# The music: eight Ogg Vorbis tracks (two per scene) made for this port, plus the loudness table (tools/make_loudness.py)
MUSIC_SRC=${MUSIC_SRC:-$HOME/handhelds/Development/Datafrog/SF3500/ports/gweled/Custom/Music}
if [ -d "$MUSIC_SRC" ]; then
    mkdir -p "$OUT/gweled/music"
    for d in Menu Normal Timed Endless; do cp -r "$MUSIC_SRC/$d" "$OUT/gweled/music/"; done
    cp assets-src/music-loudness.txt "$OUT/gweled/music/loudness.txt"
else
    echo "WARNING: no music folder at $MUSIC_SRC, package has no music" >&2
fi
# GPL: the corresponding source travels with the binary
tar -C . --exclude='./build-*' --exclude='./package' --exclude='./work' --exclude='./.git' --exclude='./assets/gweled.pak' \
    --transform 's,^\./,gweled-sf3500/,' -cJf "$OUT/gweled/licenses/gweled-sf3500-source.tar.xz" .
[ -f "$HOME/handhelds/Development/Datafrog/SF3500/ports/gweled/Installer/gweled-1.0-beta2.tar.xz" ] && cp "$HOME/handhelds/Development/Datafrog/SF3500/ports/gweled/Installer/gweled-1.0-beta2.tar.xz" "$OUT/gweled/licenses/"
chmod +x "$OUT/Gweled.sh" "$OUT/gweled/run.sh" "$OUT/gweled/gweled.mipsel"
(cd "$OUT" && find . -type f -print0 | sort -z | xargs -0 md5sum)
