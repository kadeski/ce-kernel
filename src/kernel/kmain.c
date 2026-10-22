#include "kernel.h"
#include "console.h"
#include "shell.h"
#include "../drivers/keypad.h"
#include "../drivers/lcd.h"
#include "../drivers/timer.h"

const struct boot_info *kernel_boot;

static void set_palette(void)
{
    lcd_set_palette(COLOR_BG, LCD_RGB(16, 20, 28));
    lcd_set_palette(COLOR_TEXT, LCD_RGB(220, 222, 228));
    lcd_set_palette(COLOR_ACCENT, LCD_RGB(110, 190, 255));
    lcd_set_palette(COLOR_BAR_BG, LCD_RGB(40, 64, 104));
    lcd_set_palette(COLOR_BAR_TEXT, LCD_RGB(240, 240, 240));
    lcd_set_palette(COLOR_ERROR, LCD_RGB(255, 110, 100));
    lcd_set_palette(COLOR_OK, LCD_RGB(120, 220, 130));
    lcd_set_palette(COLOR_DIM, LCD_RGB(120, 126, 140));
}

int kmain(const struct boot_info *boot)
{
    kernel_boot = boot;

    timer_init();
    keypad_init();
    set_palette();
    lcd_init();
    console_init();

    shell_run();

    /* With nothing to return to, leave the screen as it is for the loader to
     * halt on. */
    if (!boot->can_exit)
        return 0;

    /* Don't hand back control while ON or another key is still down, or
     * TI-OS would see it as a fresh key press. */
    uint8_t state[KEYPAD_GROUPS];
    for (;;) {
        keypad_scan(state);
        uint8_t any = 0;
        for (uint8_t g = 1; g < KEYPAD_GROUPS; g++)
            any |= state[g];
        if (!any && !keypad_on_pressed())
            break;
    }

    lcd_restore();
    keypad_restore();
    timer_restore();
    return 0;
}
