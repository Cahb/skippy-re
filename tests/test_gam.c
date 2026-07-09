/* Loads a .gam manifest and lists levels — diff against LevelReport.txt. */
#include "formats/gam.h"

#include <stdio.h>

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <path.gam>\n", argv[0]); return 2; }

    gam_manifest g;
    if (!gam_load(argv[1], &g)) { fprintf(stderr, "FAILED to load %s\n", argv[1]); return 1; }

    printf("== %s ==\nnum_levels: %d\n", argv[1], g.num_levels);
    for (int i = 0; i < g.num_levels; i++)
        printf("  %2d: %s\n", i + 1, g.levels[i]);
    return 0;
}
