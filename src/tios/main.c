/*
 * TI-OS loader: the kernel packaged as an assembly program (KERNEL.8xp).
 *
 * The toolchain's startup code handles the handoff with TI-OS: it disables
 * interrupts and the USB timer before main(), and on return restores flash
 * wait states, timer and flag state and redraws the home screen. Between
 * those two points no TI-OS code runs; the kernel drives the hardware itself.
 */

#include "../kernel/kernel.h"

extern uint8_t load_addr[] __asm__("LOAD_ADDR");
extern uint8_t __data_high[];

int main(void)
{
    static const struct boot_info boot = {
        .loader = "TI-OS (asm program)",
        .can_exit = true,
    };
    struct boot_info info = boot;
    info.image_start = (uint24_t)load_addr;
    info.image_end = (uint24_t)__data_high;
    return kmain(&info);
}
