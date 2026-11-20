#include "pak.h"
#include <string.h>

#ifndef PAK_FILE
#define PAK_FILE "assets/gweled.pak"
#endif

__asm__(".section .rodata\n"
        ".balign 8\n"
        ".global gweled_pak_start\n"
        "gweled_pak_start:\n"
        ".incbin \"" PAK_FILE "\"\n"
        ".global gweled_pak_end\n"
        "gweled_pak_end:\n"
        ".balign 8\n"
        ".previous\n");
extern const uint8_t gweled_pak_start[];

static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

const uint8_t* pak_get(const char* name, uint32_t* size) {
    const uint8_t* p = gweled_pak_start;
    uint32_t n, i;
    if (memcmp(p, "GWPK", 4) != 0) return NULL;
    n = rd32(p + 4);
    for (i = 0; i < n; i++) {
        const uint8_t* e = p + 8 + i * 32;
        if (strncmp((const char*)e, name, 24) == 0) {
            if (size) *size = rd32(e + 28);
            return p + rd32(e + 24);
        }
    }
    return NULL;
}
