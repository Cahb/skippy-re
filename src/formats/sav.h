/* SavedGames/<name><idx>.sav — the original's savegame slots (RE:
 * reference/savegame_highscore_format.md, loader game_load_savegame_gamefiles
 * 0x43b4a0): 42 bytes per slot, every stored byte = actual + 55. Fields:
 * savename[10]@0, current_lvl@14, hearts_left@15, total_score WORD(le)@16.
 * The pad bytes are carried verbatim through a round-trip — what the original
 * WRITES into them is still unverified RE (see the roadmap's RE task 5). */
#ifndef SAV_H
#define SAV_H

#include <stdbool.h>
#include <stdint.h>

#define SAV_SIZE  42
#define SAV_NAME  10
#define SAV_SLOTS 6

typedef struct {
    char     name[SAV_NAME + 1];  /* NUL-terminated copy of savename[10] */
    uint8_t  current_lvl;
    uint8_t  hearts_left;
    uint16_t total_score;
    uint8_t  raw[SAV_SIZE];       /* decoded slot image (pads kept verbatim) */
} sav_slot;

bool sav_read(const char *path, sav_slot *out);
/* overlay the fields onto the slot image, re-obfuscate, write. */
bool sav_write(const char *path, const sav_slot *s);

#endif /* SAV_H */
