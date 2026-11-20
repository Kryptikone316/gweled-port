#ifndef SND_H
#define SND_H
enum { SND_CLICK = 0, SND_SWAP = 1, SND_EXPLODE = 2 };
void snd_init(void);
void snd_enable(int on);
void snd_set_volume(int percent);   /* 0..100; 100 is the balance the effects were tuned at */
void snd_play(int id);
#endif
