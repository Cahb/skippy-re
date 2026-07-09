#include "formats/gam.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

bool gam_load(const char *path, gam_manifest *out) {
    memset(out, 0, sizeof *out);

    FILE *f = fopen(path, "rb");
    if (!f) return false;

    uint8_t hdr[4];
    if (fread(hdr, 1, 4, f) != 4) { fclose(f); return false; }
    if (!(hdr[0] == 6 && hdr[1] == 6 && hdr[2] == 6)) { fclose(f); return false; } /* (text-fallback form not handled) */

    int n = hdr[3];
    if (n > GAM_MAX_LEVELS) n = GAM_MAX_LEVELS;
    out->num_levels = n;

    for (int i = 0; i < n; i++) {
        uint8_t buf[GAM_NAME];
        if (fread(buf, 1, GAM_NAME, f) != GAM_NAME) { fclose(f); return false; }
        for (int j = 0; j < GAM_NAME; j++)
            out->levels[i][j] = (char)(uint8_t)(buf[j] - 5);   /* deobfuscate (-5) */
        out->levels[i][GAM_NAME - 1] = 0;
    }

    fclose(f);
    return true;
}
