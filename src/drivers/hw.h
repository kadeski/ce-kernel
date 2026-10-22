/*
 * TI-84 Plus CE hardware register map.
 *
 * Only the registers the kernel touches are listed. Addresses come from the
 * CE C toolchain headers (sys/lcd.h, sys/timers.h, keypadc.h, ti84pceg.inc)
 * and were cross-checked against the CEmu emulator core (core/lcd.c,
 * core/keypad.c, core/timers.c, core/spi.c).
 */

#ifndef DRIVERS_HW_H
#define DRIVERS_HW_H

#include <stdint.h>

#define REG8(a)  (*(volatile uint8_t *)(a))
#define REG16(a) (*(volatile uint16_t *)(a))
#define REG24(a) (*(volatile uint24_t *)(a))
#define REG32(a) (*(volatile uint32_t *)(a))

/* Memory map */
#define RAM_START   0xD00000
#define RAM_END     0xD65800            /* exclusive; includes VRAM */
#define VRAM_START  0xD40000
#define FLASH_START 0x000000
#define FLASH_END   0x400000            /* exclusive; 4 MB */

/* LCD controller (ARM PrimeCell PL111) */
#define LCD_TIMING0 REG32(0xE30000)
#define LCD_TIMING1 REG32(0xE30004)
#define LCD_TIMING2 REG32(0xE30008)
#define LCD_TIMING3 REG24(0xE3000C)
#define LCD_UPBASE  REG24(0xE30010)
#define LCD_CONTROL REG24(0xE30018)
#define LCD_IMSC    REG8(0xE3001C)
#define LCD_PALETTE ((volatile uint16_t *)0xE30200)

#define LCD_CTRL_ENABLE   (1 << 0)
#define LCD_CTRL_BPP_MASK (7 << 1)
#define LCD_CTRL_BPP8     (3 << 1)
#define LCD_CTRL_TFT      (1 << 5)
#define LCD_CTRL_BGR      (1 << 8)
#define LCD_CTRL_POWER    (1 << 11)

/* Backlight PWM level: lower is brighter */
#define BACKLIGHT_LEVEL REG8(0xF60024)

/* General-purpose timers. Each timer has counter, reload and two match regs. */
#define TMR1_COUNTER REG32(0xF20000)
#define TMR1_RELOAD  REG32(0xF20004)
#define TMR_CONTROL  REG16(0xF20030)

#define TMR1_ENABLE  (1 << 0)
#define TMR1_CRYSTAL (1 << 1)           /* count the 32768 Hz crystal, not the CPU clock */
#define TMR1_INT     (1 << 2)
#define TMR1_COUNTUP (1 << 9)

/* Keypad controller */
#define KB_MODE   REG8(0xF50000)        /* bits 0-1: mode; bits 2-7: low bits of row wait */
#define KB_CONFIG REG32(0xF50000)       /* mode:2, row wait:14, scan wait:16 */
#define KB_SIZE   REG16(0xF50004)       /* low byte: rows, high byte: columns */
#define KB_STATUS REG8(0xF50008)
#define KB_GROUP(n) REG16(0xF50010 + 2 * (n))

#define KB_MODE_MASK      3
#define KB_MODE_IDLE      0
#define KB_MODE_SCAN_ONCE 2
#define KB_MODE_SCAN      3

/* ON key, in the interrupt controller's second bank. With latching off, bit 0
 * of the status register is the live key state. TI-OS may leave latching on. */
#define INT_ON_STATUS REG8(0xF00020)
#define INT_ON_ACK    REG8(0xF00028)    /* write 1 to clear a latched press */
#define INT_ON_LATCH  REG8(0xF0002C)
#define INT_ON_BIT    (1 << 0)

/* SPI controller (drives the LCD panel; shared with the ARM coprocessor on
 * Python models). Only the bare ROM build touches it. */
#define SPI_CTRL0  REG24(0xF80000)
#define SPI_CTRL1  REG24(0xF80004)
#define SPI_CTRL2  REG24(0xF80008)
#define SPI_STATUS REG24(0xF8000C)
#define SPI_DATA_LO REG8(0xF80018)      /* writing this byte pushes a FIFO entry */
#define SPI_DATA_HI REG8(0xF80019)

#define SPI_CR0_CLK_POLARITY (1 << 0)
#define SPI_CR0_CLK_PHASE    (1 << 1)
#define SPI_CR0_MASTER_MONO  (2 << 2)
#define SPI_CR2_CHIP_EN      (1 << 0)
#define SPI_CR2_RX_CLR       (1 << 2)
#define SPI_CR2_TX_CLR       (1 << 3)
#define SPI_CR2_TX_EN        (1 << 8)
#define SPI_ST_TX_NOT_FULL   (1 << 1)
#define SPI_ST_BUSY          (1 << 2)
#define SPI_ST_TX_COUNT(s)   (((s) >> 12) & 0x1F)

#endif
