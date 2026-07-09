#include "formats/jjm.h"

#include <stdio.h>
#include <string.h>

/* little-endian read of a u32 (x86 native; explicit so it's portable to BE hosts) */
static bool rd_u32le(FILE *f, uint32_t *out) {
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4) return false;
    *out = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return true;
}

static bool rd_bytes(FILE *f, void *p, size_t n) { return fread(p, 1, n, f) == n; }

bool jjm_load(const char *path, jjm_level *out) {
    memset(out, 0, sizeof *out);

    FILE *f = fopen(path, "rb");
    if (!f) return false;

    /* dims: file byte0 -> dim_y, byte1 -> dim_x (matches original loader) */
    uint8_t dims[2];
    if (!rd_bytes(f, dims, 2)) { fclose(f); return false; }
    out->dim_y = dims[0];
    out->dim_x = dims[1];
    if (out->dim_x > JJM_MAX_DIM || out->dim_y > JJM_MAX_DIM) { fclose(f); return false; }

    /* grid: outer x in [0,dim_x), inner y in [0,dim_y); 4 bytes/cell.
       border row/col (x|y == 0 or == dim-1) is read then forced to 0. */
    for (int x = 0; x < out->dim_x; x++) {
        for (int y = 0; y < out->dim_y; y++) {
            uint8_t t[4];
            if (!rd_bytes(f, t, 4)) { fclose(f); return false; }
            bool border = (x == 0 || y == 0 || x == out->dim_x - 1 || y == out->dim_y - 1);
            jjm_tile *c = &out->tiles[x][y];
            if (!border) {
                c->z_pos = t[0]; c->type = t[1]; c->clip_rule = t[2]; c->pickup_type = t[3];
            } /* else: left zeroed by memset */
        }
    }

    /* trailing fields, in file order (see reference/jjm_format.md) */
    if (!rd_u32le(f, &out->crystals_needed)) { fclose(f); return false; }
    if (!rd_u32le(f, &out->time_limit))      { fclose(f); return false; }
    if (!rd_bytes(f, out->world, JJM_NAME_LEN))              { fclose(f); return false; }
    if (!rd_bytes(f, out->level_display_name, JJM_NAME_LEN)) { fclose(f); return false; }
    if (!rd_bytes(f, out->meta_name, JJM_NAME_LEN))          { fclose(f); return false; }
    if (!rd_u32le(f, &out->is_bonus))        { fclose(f); return false; }

    /* ensure name strings are NUL-terminated defensively */
    out->world[JJM_NAME_LEN - 1] = 0;
    out->level_display_name[JJM_NAME_LEN - 1] = 0;
    out->meta_name[JJM_NAME_LEN - 1] = 0;

    fclose(f);
    return true;
}
