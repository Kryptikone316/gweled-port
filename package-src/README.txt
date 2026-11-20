GWELED  -  for the Datafrog SF3500
==================================

Swap two neighbouring gems so that three or more of the same kind line up,
horizontally or vertically. The gems vanish, the ones above fall down, new
ones drop in and chains of matches score extra. This is Gweled, the free
Bejeweled-style game by Sebastien Delestaing and Daniele Napolitano
(gweled.org), brought to the SF3500 with its own SDL2 front end. The game
rules, the gem pictures and the sounds are the original's.

CONTROLS
--------
  D-pad        move the white cursor over the board
  A            pick the gem under the cursor
  D-pad (after A)  swap the picked gem with the neighbour in that direction
               (A on a neighbour works too; A on the same gem, or B, drops it)
  SELECT       show a hint (a gem bounces)
  START        pause menu: Resume, Hints, Sound, High Scores, Quit to Menu

GAME TYPES
----------
  Normal       Play until there are no moves left.
  Timed        The bar drains; matches refill it. Run out of time and it is over.
  Endless      No end: when no moves are left the board is reshuffled.
               The score is not recorded.
Reach the end of a level's bar for a bonus round (eight gems vanish for 10
points each) and a higher level; every second level raises the points per gem.

HINTS
-----
After 15 seconds without a move a possible move bounces. Turn hints off in the
menu if you would rather work it out.

SAVING
------
Quit to Menu (pause menu) offers to save the game. "Continue Saved Game"
appears on the title screen; a saved game is used once.

FILES (in this folder)
----------------------
  gweled.cfg      sound and hints on/off, the name used for high scores
  gweled.scores   the ten best scores of the Normal and Timed games
  gweled.sav      a saved game (removed when you continue it)
  log.txt         what the game printed on its last run

LICENCE
-------
GNU General Public License, version 2 or (at your option) any later version.
Original game (c) 2003-2005 Sebastien Delestaing, (c) 2010-2026 Daniele
Napolitano. SF3500 version (c) 2026 AthruZ (Kryptikone316). See NOTICE.md and
the licenses folder; the complete source of this port is in
licenses/gweled-sf3500-source.tar.xz.
