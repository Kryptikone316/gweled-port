# Gweled (SF3500) - dev notes

SDL2 front end for the rules of Gweled 1.0-beta2 (GPL-2.0-or-later, https://gweled.org). The original needs GTK 3 + Clutter +
GSound + libgnome-games-support, none of which exist on the device, so the UI is new and the rules are ported:
engine.c = board_engine.c without GLib/Clutter (same tile balancing, alignment search, scoring, level/bonus logic, timed drain).
Art (7 gem SVGs, cursor) and the 3 sounds are the original's, baked into assets/gweled.pak by tools/bake.py and linked in with .incbin.
Original source tree: ~/handhelds/Development/Datafrog/SF3500/ports/gweled/Installer/gweled-1.0-beta2 (bake.py takes it as argument).

## Build
    tools/bake.py <orig-src-dir> assets/gweled.pak     # needs rsvg-convert, ffmpeg, Pillow (~/venvs/pydev/bin/python)
    ./build_host.sh                                    # desktop, build-host/gweled
    ./build_sf3500.sh                                  # MIPS, build-sf3500/gweled.stripped.mipsel (toolchain in sf3000-work/buildroot)
    ./package.sh                                       # package/Gweled.sh + package/gweled/ (binary, README, NOTICE, licenses, source tarball)
Box art: package-src/boxart.png = 500x500 of ports/gweled/screenshot.png (the user's floppy-disk art); the card needs sync-boxart.sh.

## Layout
- src/engine.c/.h  rules; E (Eng) holds board, gem objects, score, level, bonus. Own xorshift RNG (same sequence everywhere).
- src/game.c       animation (tweens + Clutter easings), the 50 ms board state machine (port of board_engine_loop), screens, files.
- src/draw.c       RGB565 drawing, RGBA sprites (img_prepare makes fast planes), bilinear scaled blit, baked DejaVu text with outline.
- src/snd.c, pak.c, plat_sdl.c (+plat/ = SF3500 hwdisp/joykey/mixer, copied from the other ports).
Files in the port folder: gweled.cfg (sound, hints, name), gweled.scores (top 10 normal + timed), gweled.sav (one saved game).

## Behaviour notes (faithful to the original unless stated)
- Timings: swap 200 ms in-out-cubic, fall 100+20*maxrow ms out-cubic with 25 ms/gem delay, pop 80 ms to 1.2x then 150 ms to 0,
  score popup bounce-in 500 ms / float up TILE/3 over 800 ms / fade at 700-800 ms, blink 200 ms period halves, hint bounce 350+150 ms
  for 2 s, hint after 15 s idle. Level bar uses the original thresholds (100, 200, 400...); bonus = 8 random single gems (10 pts).
- Timed mode: every 1.25 s (25 board ticks) the bar drains by steps_for_timer; level-up resets it to the middle.
- Pad input is ours: D-pad cursor; A picks; D-pad after A swaps with that neighbour (A on neighbour, A on same gem, B as the original clicks).
  SELECT = hint now. START = pause menu (board hidden, the timer is stopped). Quit to Menu asks to save (normal/timed; endless asks to abort).
- Quirks kept: bonus gems use x,y in 0..6 only; combo scoring FIXME (6-in-a-row) unchanged; after a bonus the cascade is not scored.
- Deliberate difference: eng_part_of_alignment masks board values with & 7 (the original indexes bit_n[-1] for empty cells).

## Tests (host and QEMU)
Env hooks: GWL_HEADLESS=1, GWL_SEED, GWL_AUTOSTART=<mode>, GWL_BOT=1 (+GWL_BOT_FRAMES=n) self-play, GWL_JOY="frame:mask,..." (tools/joy.py),
GWL_SHOTS=<dir> + GWL_SHOT_FRAMES="f1,f2" or GWL_SHOT_EVERY=n (BMP; tools/shots.py makes a contact sheet and deletes the BMPs), GWL_MAXFRAMES.
Headless time = frame*20 ms. DO NOT write many BMPs to /tmp (tmpfs filled up once, 1.3 GB): use work/ in the project.
Bot run seed 11 Normal: level 2 at frame 1443 score 340, level 3 at 2688 score 1060, game over (no moves) at 4858 score 2240 - identical on x86 and in
QEMU mipsel (frames differ by at most 1 level per channel).
QEMU: flatpak-spawn --host --directory=$PWD --env=GWL_...=... qemu-mipsel-static -L <buildroot sysroot> build-sf3500/gweled.stripped.mipsel

## Perf (host x86): ~0.43 ms/frame while gems are animating, ~0.05 ms when idle (board is cached; panel fixed parts baked). Device not measured.

## Open
- Not device-tested (display, pad, audio balance: snd.c k_gain = click .55, swap .85, explode .85 on peak-normalised sounds).
- No collection entry/PR yet; source repo not created (the package carries the source tarball for the GPL).
- Translations: the original has 30+ .po files; the UI strings are collected at the top of game.c, baked fonts cover Latin-1 only.
