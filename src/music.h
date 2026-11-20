/* Background music: Ogg Vorbis tracks in music/<Menu|Normal|Timed|Endless>/, streamed and decoded on the fly. */
#ifndef MUSIC_H
#define MUSIC_H

enum { MS_MENU = 0, MS_NORMAL = 1, MS_TIMED = 2, MS_ENDLESS = 3, MS_COUNT = 4 };   /* MS_NORMAL + game mode */

void music_init(void);                     /* finds the tracks and the loudness table; call after plat_init() */
void music_set_enabled(int on);
void music_set_volume(int percent);        /* 0..100 */
void music_set_scene(int scene);           /* what is on screen; the music follows with a fade */
void music_update(unsigned now_ms);        /* once per frame */
const char* music_title(void);             /* title of the track that is playing, or "" */

#endif
