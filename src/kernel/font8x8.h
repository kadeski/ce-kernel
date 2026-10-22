#ifndef KERNEL_FONT8X8_H
#define KERNEL_FONT8X8_H

#include <stdint.h>

#define FONT_FIRST  0x20
#define FONT_LAST   0x7E
#define FONT_GLYPHS (FONT_LAST - FONT_FIRST + 1)

extern const uint8_t font8x8[FONT_GLYPHS][8];

#endif
