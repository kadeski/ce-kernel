#ifndef DRIVERS_LCD_H
#define DRIVERS_LCD_H

#include <stdint.h>

#define LCD_WIDTH  320
#define LCD_HEIGHT 240

/* 8 bits per pixel, palette indexed, row-major. */
#define LCD_FRAMEBUFFER ((uint8_t *)0xD40000)
#define LCD_FB_SIZE     ((uint24_t)LCD_WIDTH * LCD_HEIGHT)

/* Palette entries are 1555 with the controller's BGR bit set: R in bits 10-14. */
#define LCD_RGB(r, g, b) \
    ((uint16_t)((((r) >> 3) << 10) | (((g) >> 3) << 5) | ((b) >> 3)))

/* Saves the controller state and switches to the 8bpp framebuffer. */
void lcd_init(void);
/* Restores the controller state saved by lcd_init. */
void lcd_restore(void);

void lcd_set_palette(uint8_t index, uint16_t color);
void lcd_fill(uint8_t color);

#endif
