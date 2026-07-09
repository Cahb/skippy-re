/* Highscores/<name>.hsc — the original's highscore table (RE:
 * reference/savegame_highscore_format.md; loader 0x41ef20 / saver 0x41efe0,
 * defaults 0x41ee30). 10 records x 55 bytes, every stored byte = actual + 75
 * ('K' — NOT the savegames' +55). Record: name[<=50] NUL-terminated @0,
 * score DWORD(le) @50, level BYTE @54. */
#ifndef HSC_H
#define HSC_H

#include <stdbool.h>
#include <stdint.h>

#define HSC_RECORDS  10
#define HSC_REC_SIZE 55
#define HSC_NAME     49   /* longest storable name (byte 49 stays NUL) */

typedef struct {
    char     name[HSC_NAME + 1];
    uint32_t score;
    uint8_t  level;       /* 1-based level reached */
} hsc_record;

typedef struct {
    hsc_record rec[HSC_RECORDS];
    uint8_t    raw[HSC_RECORDS][HSC_REC_SIZE];  /* decoded images (pads verbatim) */
} hsc_table;

bool hsc_read(const char *path, hsc_table *out);
bool hsc_write(const char *path, const hsc_table *t);

/* the stock table when no file exists: ten "Bernie Boulder" rows, scores/levels
 * exactly as highscore_set_defaults hardcodes them. */
void hsc_defaults(hsc_table *t);

/* insertion slot for `score` (records are kept sorted descending), or -1. */
int  hsc_qualifies(const hsc_table *t, uint32_t score);
void hsc_insert(hsc_table *t, const char *name, uint32_t score, int level);

#endif /* HSC_H */
