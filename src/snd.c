/* The three original sound effects (click, swap, explode), baked into the pack as 48 kHz stereo PCM. */
#include "snd.h"
#include "plat.h"
#include "pak.h"
#include <string.h>

static int g_on = 1, g_have[3];
static float g_vol = 1.0f;
static const float k_gain[3] = { 0.55f, 0.85f, 0.85f };   /* the pack holds them normalised; these restore a sensible balance */

void snd_init(void) {
    static const char* const names[3] = { "snd_click", "snd_swap", "snd_explode" };
    int i;
    for (i = 0; i < 3; i++) {
        const uint8_t* p = pak_get(names[i], NULL);
        if (!p) continue;
        plat_snd_set(i, (const short*)(p + 4), (int)(p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24)));
        g_have[i] = 1;
    }
}
void snd_enable(int on) { g_on = on; }
void snd_set_volume(int percent) { g_vol = (percent < 0 ? 0 : percent > 100 ? 100 : percent) / 100.0f; }
void snd_play(int id) { if (g_on && g_vol > 0 && id >= 0 && id < 3 && g_have[id]) plat_snd_play(id, k_gain[id] * g_vol); }
