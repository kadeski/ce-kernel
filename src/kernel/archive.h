#ifndef KERNEL_ARCHIVE_H
#define KERNEL_ARCHIVE_H

#include <stdbool.h>
#include <stdint.h>

#define ARCHIVE_NAME_MAX 8

/* One variable in TI-OS's archive. Everything points into flash. */
struct archive_entry {
    uint24_t addr;                      /* the entry's flag byte */
    uint8_t type;                       /* low 5 bits of the type byte */
    uint8_t name_len;
    const char *name;                   /* not NUL-terminated */
    const uint8_t *data;                /* the variable's data as stored */
    uint24_t size;                      /* bytes at data */
};

/* Steps through the valid entries in the archive. Start with *cursor = 0;
 * returns false when there are no more. Reads flash only. */
bool archive_next(uint24_t *cursor, struct archive_entry *entry);

/* Finds the first entry whose name matches, ignoring case. */
bool archive_find(const char *name, struct archive_entry *entry);

#endif
