#include "lcd.h"
#include "hw.h"

#include <string.h>

static uint24_t saved_control;
static uint24_t saved_upbase;

void lcd_init(void)
{
    saved_control = LCD_CONTROL;
    saved_upbase = LCD_UPBASE;

    /* Only the pixel depth changes. Timing and panel setup are left as
     * whoever powered the screen on (TI-OS or the ROM's cold init) set them. */
    lcd_fill(0);
    LCD_UPBASE = (uint24_t)LCD_FRAMEBUFFER;
    LCD_CONTROL = (saved_control & ~LCD_CTRL_BPP_MASK) | LCD_CTRL_BPP8;
}

void lcd_restore(void)
{
    LCD_CONTROL = saved_control;
    LCD_UPBASE = saved_upbase;
}

void lcd_set_palette(uint8_t index, uint16_t color)
{
    LCD_PALETTE[index] = color;
}

void lcd_fill(uint8_t color)
{
    memset(LCD_FRAMEBUFFER, color, LCD_FB_SIZE);
}
