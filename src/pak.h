/* Embedded asset pack (assets/gweled.pak, built by tools/bake.py, linked in with .incbin). */
#ifndef PAK_H
#define PAK_H
#include <stdint.h>

const uint8_t* pak_get(const char* name, uint32_t* size);   /* NULL if missing */

#endif
