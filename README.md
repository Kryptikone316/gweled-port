# Gweled for the Datafrog SF3500

A port of **Gweled** 1.0-beta2 ([gweled.org](https://gweled.org)), the free match-three gem game by
Sebastien Delestaing and Daniele Napolitano, to the Datafrog SF3500 handheld (TreeFrogUI, MIPS32r2).
The original needs GTK 3 and Clutter, which the device does not have, so the front end is new C code
on SDL2 and the game rules are a C port of the original `board_engine.c`: tile balancing, alignment
search, scoring, levels and the timed-mode drain behave as in the original. The gem pictures and the
three sound effects are the original's. The port is therefore under the same licence,
**GPL-2.0-or-later**.

* Game types: Normal, Timed, Endless; a saved game; top-10 high scores for Normal and Timed (each list
  can be cleared on its own); hints; sound effects and music, each with a volume.
* 640x480, software-rendered RGB565 straight to the device's display driver; D-pad and buttons.
* Music: eight tracks (two each for the menus, Normal, Timed and Endless) made for this port and
  released as CC0. They are a separate download, see below. The game runs silent without them.

## Controls

D-pad moves the cursor, **A** picks a gem (then a direction swaps it with that neighbour), **B** puts it
down, **SELECT** shows a hint, **START** opens the pause menu, **SELECT + START** leaves the game from any screen.

## Building

The tools need Python 3 with Pillow, `rsvg-convert` and `ffmpeg`, and the original Gweled 1.0-beta2
source tree (for the gem SVGs and sounds).

```sh
tools/bake.py <gweled-1.0-beta2-dir> assets/gweled.pak     # gems, sounds and fonts into one pack
./build_host.sh                                           # desktop build (SDL2) for development
SF3500_TC=<toolchain>/output/host ./build_sf3500.sh        # MIPS build (Codescape mips-mti-linux-gnu, glibc 2.20 sysroot with SDL2)
```

`DEV-NOTES.md` describes the code, the test hooks (`GWL_HEADLESS`, `GWL_BOT`, scripted pad input,
screenshots) and the behaviour notes.

## Music

The tracks go in `music/Menu`, `music/Normal`, `music/Timed` and `music/Endless` next to the binary
(Ogg Vorbis, any number of files per folder; `music/loudness.txt` only evens out the supplied tracks).
The music pack is attached to this repository's `music-pack` release. Music and its volume are in
Settings and in the pause menu.

## Licences

* Game and port code: GPL-2.0-or-later (`LICENSE`, `licenses/`).
* Gem pictures and sound effects: from the original Gweled (GPL-2.0-or-later; the sound credits are in
  `licenses/CREDITS.txt`).
* Music: CC0 1.0, public domain (`licenses/Music-LICENCE.txt`).
* Text is set in DejaVu Sans Bold (`licenses/DejaVu-fonts-LICENSE.txt`); the Ogg Vorbis decoder is
  stb_vorbis (Sean Barrett, public domain).
