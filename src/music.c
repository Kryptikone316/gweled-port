/* Background music for the SF3500 port.
 *
 * Each scene (menu, Normal, Timed, Endless) has its own folder of Ogg Vorbis files, music/<Folder>/ (*.ogg). A track is picked
 * at random (never the one that just played in that scene), plays to its end and the next one follows. When the scene changes
 * the music fades out and the new scene's music fades in. The files are decoded by stb_vorbis while they play. Opening a file
 * takes a good fraction of a second on this CPU, so that is done on a thread of its own.
 * music/loudness.txt ("<Folder>/<file> = <dB>", made by tools/make_loudness.py) levels the tracks to the quietest one.
 * A missing music folder just means silence: the game does not need it.
 *
 * Test hooks: GWL_MUSIC=<dir> (instead of ./music), GWL_MUSICTEST=<speed> (no audio device: decode the tracks anyway, at that
 * many times real time, and print what happens). */
#include "music.h"
#include "plat.h"
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.h"
#include <dirent.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define MAXTRK 12
#define FADE_IN_MS 1600
#define FADE_OUT_MS 700

static const char* const k_dir[MS_COUNT] = { "Menu", "Normal", "Timed", "Endless" };
static char g_root[256] = "music";
static char g_trk[MS_COUNT][MAXTRK][300];
static float g_tgain[MS_COUNT][MAXTRK];
static int g_ntrk[MS_COUNT], g_last[MS_COUNT] = { -1, -1, -1, -1 };

static int g_enabled = 1, g_volume = 50;
static int g_want = MS_MENU, g_cur = -1;         /* scene wanted / scene whose music is loaded or playing */
static float g_fade, g_fade_to = 1.0f;
static unsigned g_last_now;
static int g_fail = -1;                          /* a scene that has nothing to play (no files, or they would not open) */
static int g_test_speed;                         /* >0: no device, pull the audio ourselves */

static stb_vorbis* g_vorbis;
static int g_rate = 48000, g_idx = -1;
static float g_gain_now = 1.0f;
static char g_title[300];
static double g_dec_s, g_aud_s;

/* ---- async open ---- */
static pthread_t g_th;
static volatile int g_load;                      /* 0 idle, 1 opening, 2 done */
static int g_threaded, g_load_err;
static char g_load_path[300];
static stb_vorbis* g_loaded;

static double now_sec(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

static void* load_main(void* arg) {
    int err = 0;
    (void)arg;
    g_loaded = stb_vorbis_open_filename(g_load_path, &err, NULL);
    g_load_err = err;
    __sync_synchronize();
    g_load = 2;
    return NULL;
}

static void cancel_load(void) {
    if (g_load == 0) return;
    if (g_threaded) pthread_join(g_th, NULL);
    g_threaded = 0;
    if (g_loaded) stb_vorbis_close(g_loaded);
    g_loaded = NULL;
    g_load = 0;
}

static int fill(void* ud, short* dst, int frames) {
    double t0 = now_sec();
    int got;
    (void)ud;
    got = stb_vorbis_get_samples_short_interleaved(g_vorbis, 2, dst, frames * 2);   /* always stereo, whatever the file holds */
    g_dec_s += now_sec() - t0;
    g_aud_s += (double)got / g_rate;
    return got;
}

static float level(void) { return 0.9f * g_volume / 100.0f * g_gain_now * g_fade * g_fade; }

static void drop(void) {
    cancel_load();
    plat_stream_stop();
    if (g_vorbis) { stb_vorbis_close(g_vorbis); g_vorbis = NULL; }
    g_title[0] = 0;
}

static void begin_load(const char* path) {
    drop();
    snprintf(g_load_path, sizeof g_load_path, "%s", path);
    g_loaded = NULL;
    g_load = 1;
    g_threaded = pthread_create(&g_th, NULL, load_main, NULL) == 0;
    if (!g_threaded) load_main(NULL);
}

static void make_title(const char* path) {
    const char* b = strrchr(path, '/');
    char* dash;
    snprintf(g_title, sizeof g_title, "%s", b ? b + 1 : path);
    if (strlen(g_title) > 4) g_title[strlen(g_title) - 4] = 0;                      /* ".ogg" */
    dash = strrchr(g_title, '-');                                                    /* "01 - Menu - Spacious" -> "Spacious" */
    if (dash && dash[1] == ' ') memmove(g_title, dash + 2, strlen(dash + 2) + 1);
}

static void finish_load(void) {
    stb_vorbis_info info;
    if (g_threaded) pthread_join(g_th, NULL);
    g_threaded = 0;
    g_vorbis = g_loaded;
    g_loaded = NULL;
    g_load = 0;
    if (!g_vorbis) { fprintf(stderr, "music: cannot decode %s (error %d)\n", g_load_path, g_load_err); g_fail = g_cur; g_cur = -1; return; }
    info = stb_vorbis_get_info(g_vorbis);
    g_rate = (int)info.sample_rate;
    g_gain_now = g_tgain[g_cur][g_idx];
    make_title(g_load_path);
    g_fade = 0.0f; g_fade_to = 1.0f;
    if (plat_stream_ok()) {
        if (!plat_stream_start(fill, NULL, g_rate, level())) { drop(); g_fail = g_cur; g_cur = -1; return; }
    } else if (!g_test_speed) { drop(); g_fail = g_cur; g_cur = -1; return; }
        fprintf(stderr, "music: %s (%.1f dB)\n", g_load_path, 20 * log10f(g_gain_now));
}

static void play_next(int scene) {
    int n = g_ntrk[scene], i;
    if (n <= 0) { g_fail = scene; g_cur = -1; return; }
    i = rand() % n;
    if (n > 1 && i == g_last[scene]) i = (i + 1 + rand() % (n - 1)) % n;
    g_last[scene] = g_idx = i;
    g_cur = scene;
    begin_load(g_trk[scene][i]);
}

static int cmp_str(const void* a, const void* b) { return strcmp((const char*)a, (const char*)b); }

void music_init(void) {
    const char* e = getenv("GWL_MUSIC");
    char path[300], line[400];
    FILE* f;
    int s, i;
    srand((unsigned)time(NULL));
    if (e && *e) snprintf(g_root, sizeof g_root, "%s", e);
    if (getenv("GWL_MUSICTEST")) g_test_speed = atoi(getenv("GWL_MUSICTEST")) > 0 ? atoi(getenv("GWL_MUSICTEST")) : 1;
    for (s = 0; s < MS_COUNT; s++) {
        struct dirent* de;
        DIR* d;
        snprintf(path, sizeof path, "%s/%s", g_root, k_dir[s]);
        d = opendir(path);
        if (!d) continue;
        while ((de = readdir(d)) && g_ntrk[s] < MAXTRK) {
            size_t n = strlen(de->d_name);
            if (n > 4 && !strcasecmp(de->d_name + n - 4, ".ogg") && strlen(path) + n + 2 < sizeof g_trk[0][0])
                snprintf(g_trk[s][g_ntrk[s]++], sizeof g_trk[0][0], "%s/%s", path, de->d_name);
        }
        closedir(d);
        qsort(g_trk[s], (size_t)g_ntrk[s], sizeof g_trk[0][0], cmp_str);
        for (i = 0; i < g_ntrk[s]; i++) g_tgain[s][i] = 1.0f;
    }
    snprintf(path, sizeof path, "%s/loudness.txt", g_root);
    f = fopen(path, "r");
    if (f) {
        while (fgets(line, sizeof line, f)) {
            char* eq = strrchr(line, '=');
            float db;
            if (line[0] == '#' || !eq) continue;
            db = (float)atof(eq + 1);
            while (eq > line && (eq[-1] == ' ' || eq[-1] == '\t')) eq--;
            *eq = 0;
            if (db > 0) db = 0;
            if (db < -12) db = -12;
            for (s = 0; s < MS_COUNT; s++)
                for (i = 0; i < g_ntrk[s]; i++) {
                    char key[300];
                    snprintf(key, sizeof key, "%s/%s", k_dir[s], strrchr(g_trk[s][i], '/') + 1);
                    if (!strcmp(key, line)) g_tgain[s][i] = powf(10.0f, db / 20.0f);
                }
        }
        fclose(f);
    }
    fprintf(stderr, "music: %d/%d/%d/%d tracks (menu/normal/timed/endless) in %s\n", g_ntrk[0], g_ntrk[1], g_ntrk[2], g_ntrk[3], g_root);
}

void music_set_scene(int scene) { if (scene >= 0 && scene < MS_COUNT) g_want = scene; }

void music_set_enabled(int on) {
    on = on != 0;
    if (on == g_enabled) return;
    g_enabled = on;
    g_fail = -1;
    if (!on) { drop(); g_cur = -1; }            /* off means off: the audio stops at once */
}

void music_set_volume(int percent) {
    g_volume = percent < 0 ? 0 : percent > 100 ? 100 : percent;
    if (g_vorbis && plat_stream_ok()) plat_stream_gain(level());
}

const char* music_title(void) { return g_title; }

void music_update(unsigned now) {
    unsigned dt = g_last_now ? now - g_last_now : 0;
    g_last_now = now;
    if (dt > 100) dt = 100;
    if (!g_enabled || g_volume <= 0) { if (g_cur >= 0 || g_vorbis || g_load) { drop(); g_cur = -1; } return; }
    if (g_load == 2) finish_load();
    if (g_load == 1) return;                                         /* a file is being opened */

    if (g_vorbis) {                                                  /* a track is playing (or has just ended) */
        int ended;
        g_fade_to = g_want == g_cur ? 1.0f : 0.0f;                   /* another scene is wanted: fade this one out */
        if (g_fade < g_fade_to) { g_fade += (float)dt / FADE_IN_MS; if (g_fade > g_fade_to) g_fade = g_fade_to; }
        else if (g_fade > g_fade_to) { g_fade -= (float)dt / FADE_OUT_MS; if (g_fade < g_fade_to) g_fade = g_fade_to; }
        if (plat_stream_ok()) {
            plat_stream_gain(level());
            ended = !plat_stream_playing();
        } else {                                                     /* test mode, no device: pull the decoder along */
            static short buf[4096 * 2];
            int want = (int)((double)g_rate * dt / 1000.0 * g_test_speed);
            ended = 0;
            while (want > 0 && !ended) {
                int n = want > 4096 ? 4096 : want;
                if (fill(NULL, buf, n) < n) {
                    ended = 1;
                    fprintf(stderr, "music: track ended (decoding took %.1f%% of its play time)\n", g_aud_s > 0 ? 100 * g_dec_s / g_aud_s : 0.0);
                }
                want -= n;
            }
        }
        if (g_fade_to == 0.0f && g_fade <= 0.0f) { drop(); g_cur = -1; }
        else if (ended) {
            int sc = g_cur;
            drop();
            g_cur = -1;
            if (sc == g_want) { play_next(sc); return; }
        } else return;
    }
    if (g_fail != g_want) play_next(g_want);                         /* nothing loaded: start this scene's music */
}
