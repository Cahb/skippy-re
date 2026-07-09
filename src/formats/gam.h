/* .gam manifest loader — the game's level list. Ported from reference/gam_format.md
 * (original: game_load_jj_gamefile @0x41cbf0). Header 06 06 06 + count, then
 * count x 256-byte level-path strings, each byte obfuscated by +5 on disk. */
#ifndef GAM_H
#define GAM_H

#include <stdbool.h>

#define GAM_MAX_LEVELS 256
#define GAM_NAME       256

typedef struct {
    int  num_levels;
    char levels[GAM_MAX_LEVELS][GAM_NAME];  /* level paths, e.g. "Forest\\Start" */
} gam_manifest;

bool gam_load(const char *path, gam_manifest *out);

#endif /* GAM_H */
