#include "formats/mdl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* file is x86 little-endian IEEE-754; the rewrite targets LE hosts, so we read
 * the vertex/header blocks directly. These asserts guard the packed sizes. */
_Static_assert(sizeof(mdl_vertex) == 40, "mdl_vertex must be 40 bytes");
_Static_assert(sizeof(mdl_frame_hdr) == 24, "mdl_frame_hdr must be 24 bytes");

static bool rd(FILE *f, void *p, size_t n) { return fread(p, 1, n, f) == n; }

static char *dupstr(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

bool mdl_load(const char *path, mdl_model *out) {
    memset(out, 0, sizeof *out);

    FILE *f = fopen(path, "rb");
    if (!f) return false;

    uint8_t h[6];
    if (!rd(f, h, 6)) { fclose(f); return false; }
    out->num_frames = (uint16_t)(h[0] | (h[1] << 8));
    out->num_verts  = (uint32_t)h[2] | ((uint32_t)h[3] << 8) | ((uint32_t)h[4] << 16) | ((uint32_t)h[5] << 24);
    if (out->num_frames == 0 || out->num_verts == 0 || out->num_verts > 2000000u) { fclose(f); return false; }

    out->frame_hdrs = malloc((size_t)out->num_frames * sizeof(mdl_frame_hdr));
    out->verts      = malloc((size_t)out->num_frames * out->num_verts * sizeof(mdl_vertex));
    if (!out->frame_hdrs || !out->verts) { mdl_free(out); fclose(f); return false; }

    for (uint16_t fr = 0; fr < out->num_frames; fr++) {
        if (!rd(f, &out->frame_hdrs[fr], sizeof(mdl_frame_hdr))) { mdl_free(out); fclose(f); return false; }
        mdl_vertex *fv = out->verts + (size_t)fr * out->num_verts;
        if (!rd(f, fv, (size_t)out->num_verts * sizeof(mdl_vertex))) { mdl_free(out); fclose(f); return false; }
    }

    out->name = dupstr(path);
    fclose(f);
    return true;
}

void mdl_free(mdl_model *m) {
    if (!m) return;
    free(m->frame_hdrs);
    free(m->verts);
    free(m->name);
    memset(m, 0, sizeof *m);
}
