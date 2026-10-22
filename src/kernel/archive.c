#include "archive.h"

/* TI-OS keeps archived variables in the flash sectors from ARCHIVE_START up to
 * ARCHIVE_END. The first byte of each 64 KB sector says what it holds; entries
 * follow it and never cross into the next sector. Seen in CEmu with OS 5.8.1:
 * the first entry goes at 0C0001, and the sector at 3B0000 holds TI data even
 * when the archive is empty. */
#define ARCHIVE_START  0x0C0000
#define ARCHIVE_END    0x3B0000         /* exclusive */
#define SECTOR_SIZE    0x010000

#define SECTOR_IN_USE  0xF0             /* others (FF when empty) are skipped */
#define ENTRY_VALID    0xFC
#define ENTRY_DELETED  0xF0             /* the flag byte is cleared to F0 */

/* Entry layout: flag, size (16 bits, counts the bytes after it), type, type2,
 * version, its own address (24 bits), name length, name, data. */
#define HEADER_SIZE    3
#define FIXED_SIZE     7                /* type through name length */

static bool parse(const uint8_t *p, uint24_t size, struct archive_entry *entry)
{
    if (size < FIXED_SIZE)
        return false;
    uint8_t name_len = p[9];
    if (name_len == 0 || name_len > ARCHIVE_NAME_MAX || name_len > size - FIXED_SIZE)
        return false;
    entry->addr = (uint24_t)p;
    entry->type = p[3] & 0x1F;
    entry->name_len = name_len;
    entry->name = (const char *)&p[10];
    entry->data = &p[10 + name_len];
    entry->size = size - FIXED_SIZE - name_len;
    return true;
}

bool archive_next(uint24_t *cursor, struct archive_entry *entry)
{
    uint24_t at = *cursor ? *cursor : ARCHIVE_START;

    while (at < ARCHIVE_END) {
        uint24_t sector = at & ~(uint24_t)(SECTOR_SIZE - 1);
        uint24_t sector_end = sector + SECTOR_SIZE;
        const uint8_t *p = (const uint8_t *)at;

        if (at == sector) {
            if (*p != SECTOR_IN_USE) {
                at = sector_end;
                continue;
            }
            at++;
            p++;
        }
        /* Anything but an entry flag is free space or something this reader
         * doesn't know; either way the rest of the sector is skipped. */
        if ((*p != ENTRY_VALID && *p != ENTRY_DELETED) || sector_end - at < HEADER_SIZE) {
            at = sector_end;
            continue;
        }
        uint24_t size = p[1] | (uint24_t)p[2] << 8;
        if (sector_end - at - HEADER_SIZE < size) {
            at = sector_end;            /* runs past the sector: malformed */
            continue;
        }
        at += HEADER_SIZE + size;
        if (*p == ENTRY_VALID && parse(p, size, entry)) {
            *cursor = at;
            return true;
        }
    }
    *cursor = at;
    return false;
}

static char upper(char c)
{
    return c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
}

bool archive_find(const char *name, struct archive_entry *entry)
{
    uint24_t cursor = 0;
    while (archive_next(&cursor, entry)) {
        uint8_t i = 0;
        while (i < entry->name_len && name[i] && upper(name[i]) == upper(entry->name[i]))
            i++;
        if (i == entry->name_len && name[i] == '\0')
            return true;
    }
    return false;
}
