/* Loads a .jjm and prints stats to diff against LevelReport.txt (the oracle). */
#include "formats/jjm.h"

#include <stdio.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <path-to-.jjm>\n", argv[0]);
        return 2;
    }
    jjm_level lvl;
    if (!jjm_load(argv[1], &lvl)) {
        fprintf(stderr, "FAILED to load %s\n", argv[1]);
        return 1;
    }

    printf("== %s ==\n", argv[1]);
    printf("dims           : dim_x=%u dim_y=%u  (%u cells)\n",
           lvl.dim_x, lvl.dim_y, (unsigned)lvl.dim_x * lvl.dim_y);
    printf("world/theme    : \"%s\"\n", lvl.world);
    printf("display name   : \"%s\"\n", lvl.level_display_name);
    printf("meta           : \"%s\"\n", lvl.meta_name);
    printf("crystals_needed: %u\n", lvl.crystals_needed);
    printf("time_limit     : %u   (LevelReport 'Time' = %u)\n",
           lvl.time_limit, lvl.time_limit * 50u / 100u);
    printf("is_bonus       : %u\n", lvl.is_bonus);

    int typ[256] = {0}, pick[256] = {0};
    for (int x = 0; x < lvl.dim_x; x++)
        for (int y = 0; y < lvl.dim_y; y++) {
            typ[lvl.tiles[x][y].type]++;
            pick[lvl.tiles[x][y].pickup_type]++;
        }

    /* locate the spawn (type 3) and exit (type 4) tiles — for cross-check vs the .jjs camera */
    for (int x = 0; x < lvl.dim_x; x++)
        for (int y = 0; y < lvl.dim_y; y++) {
            if (lvl.tiles[x][y].type == 3) printf("SPAWN(type3) at grid (x=%d, y=%d) z=%u\n", x, y, lvl.tiles[x][y].z_pos);
            if (lvl.tiles[x][y].type == 4) printf("EXIT (type4) at grid (x=%d, y=%d) z=%u\n", x, y, lvl.tiles[x][y].z_pos);
        }

    printf("tile types:\n");
    for (int i = 0; i < 256; i++)
        if (typ[i]) printf("  type   %3d (0x%02x): %d\n", i, i, typ[i]);
    printf("pickups:\n");
    for (int i = 0; i < 256; i++)
        if (pick[i]) printf("  pickup %3d (0x%02x): %d\n", i, i, pick[i]);
    return 0;
}
