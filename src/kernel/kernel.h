#ifndef KERNEL_KERNEL_H
#define KERNEL_KERNEL_H

#include <stdbool.h>
#include <stdint.h>

#define KERNEL_NAME    "ce-kernel"
#define KERNEL_VERSION "0.2.0"

/* What the loader tells the kernel about how it was started. */
struct boot_info {
    const char *loader;                 /* shown by `about` */
    bool can_exit;                      /* false when there is nothing to return to */
    uint24_t image_start;               /* where the kernel image lives */
    uint24_t image_end;
};

/* Runs the kernel. Returns when the user exits; the loader then hands the
 * machine back (TI-OS build) or halts (bare ROM build). */
int kmain(const struct boot_info *boot);

extern const struct boot_info *kernel_boot;

#endif
