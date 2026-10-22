/*
 * Bare ROM cold init: brings the hardware from reset to the state TI-OS
 * normally leaves it in, so the shared kernel sees the same machine either way.
 *
 * This only runs in the emulator. On a real calculator the TI boot code
 * refuses unsigned images, and the Python models also need an extra SPI
 * sequence (the coprocessor shares the bus), which this code does not do.
 * The values here match what the CEmu core expects (core/panel.c, lcd.c,
 * keypad.c) and the TI-OS defaults documented in the toolchain's lcddrvce.h.
 */

#include "../src/drivers/hw.h"
#include "../src/drivers/timer.h"
#include "../src/kernel/kernel.h"

extern uint8_t __image_start[], __image_end[];

/* ST7789 panel commands */
#define PANEL_SLPOUT  0x11
#define PANEL_DISPON  0x29
#define PANEL_MADCTL  0x36
#define PANEL_COLMOD  0x3A
#define PANEL_RAMCTRL 0xB0

static void spi_send(uint8_t is_data, uint8_t byte)
{
    while (!(SPI_STATUS & SPI_ST_TX_NOT_FULL))
        ;
    /* 9-bit frames: bit 8 is the panel's data/command line. The high byte is
     * written first because writing the low byte pushes the FIFO entry. */
    SPI_DATA_HI = is_data;
    SPI_DATA_LO = byte;
}

static void panel_cmd(uint8_t cmd, const uint8_t *params, uint8_t count)
{
    spi_send(0, cmd);
    while (count--)
        spi_send(1, *params++);
}

static void spi_wait_idle(void)
{
    while (SPI_ST_TX_COUNT(SPI_STATUS) || (SPI_STATUS & SPI_ST_BUSY))
        ;
}

static void panel_init(void)
{
    static const uint8_t ramctrl[] = { 0x11, 0xF0 };    /* RGB interface, TI-OS defaults */
    static const uint8_t colmod[] = { 0x66 };           /* 18 bits per pixel */
    static const uint8_t madctl[] = { 0x08 };           /* BGR order */

    SPI_CTRL0 = SPI_CR0_MASTER_MONO | SPI_CR0_CLK_PHASE | SPI_CR0_CLK_POLARITY;
    SPI_CTRL1 = ((uint24_t)(9 - 1) << 16) | (2 - 1);    /* 9-bit frames, clock divider */
    SPI_CTRL2 = SPI_CR2_TX_EN | SPI_CR2_TX_CLR | SPI_CR2_RX_CLR | SPI_CR2_CHIP_EN;

    panel_cmd(PANEL_SLPOUT, 0, 0);
    spi_wait_idle();
    timer_delay_ms(120);                /* panel needs time after leaving sleep */
    panel_cmd(PANEL_RAMCTRL, ramctrl, sizeof ramctrl);
    panel_cmd(PANEL_COLMOD, colmod, sizeof colmod);
    panel_cmd(PANEL_MADCTL, madctl, sizeof madctl);
    panel_cmd(PANEL_DISPON, 0, 0);
    spi_wait_idle();
}

static void lcd_controller_init(void)
{
    /* 240 pixels x 320 lines (the panel is mounted rotated), about 60 Hz.
     * Porch values are the ones the toolchain's graphx library uses. */
    LCD_TIMING0 = (14 << 2) | (7 << 8) | ((uint32_t)87 << 16) | ((uint32_t)63 << 24);
    LCD_TIMING1 = 319 | ((uint32_t)179 << 16);
    LCD_TIMING2 = (uint32_t)(240 - 1) << 16;
    LCD_TIMING3 = 0;
    LCD_UPBASE = VRAM_START;
    LCD_IMSC = 0;
    /* 16bpp 5:6:5 like TI-OS; the kernel's LCD driver switches to 8bpp. */
    LCD_CONTROL = LCD_CTRL_ENABLE | (6 << 1) | LCD_CTRL_TFT | LCD_CTRL_BGR | LCD_CTRL_POWER;
    BACKLIGHT_LEVEL = 150;
}

static void keypad_init(void)
{
    KB_SIZE = 8 | (8 << 8);             /* 8 rows, 8 columns */
    KB_CONFIG = KB_MODE_IDLE | ((uint32_t)0x100 << 2) | ((uint32_t)0x100 << 16);
}

static void rtc_start(void)
{
    /* At reset the clock is stopped at day 0, 00:00:00. Nothing here knows
     * the real time, so it counts from there. TI-OS also enables the minute
     * and day interrupts, which the kernel doesn't use. */
    RTC_CONTROL = RTC_CTRL_ENABLE | RTC_CTRL_LATCH;
}

void rom_main(void)
{
    struct boot_info boot = {
        .loader = "bare ROM (no TI code)",
        .can_exit = false,
        .image_start = (uint24_t)__image_start,
        .image_end = (uint24_t)__image_end,
    };

    timer_init();                       /* panel_init needs delays */
    panel_init();
    lcd_controller_init();
    keypad_init();
    rtc_start();
    kmain(&boot);
}
