/* Loads a .mdl and validates: file size == 6 + frames*(24 + verts*40),
 * and checks whether the 24-byte frame header is the frame's pos bounding box. */
#include "formats/mdl.h"

#include <stdio.h>
#include <sys/stat.h>

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <path.mdl>\n", argv[0]); return 2; }

    mdl_model m;
    if (!mdl_load(argv[1], &m)) { fprintf(stderr, "FAILED to load %s\n", argv[1]); return 1; }

    printf("== %s ==\n", argv[1]);
    printf("frames      : %u\n", m.num_frames);
    printf("verts/frame : %u\n", m.num_verts);

    long expect = 6L + (long)m.num_frames * (24L + (long)m.num_verts * 40L);
    struct stat st; long actual = -1;
    if (stat(argv[1], &st) == 0) actual = (long)st.st_size;
    printf("file size   : actual=%ld expected=%ld -> %s\n",
           actual, expect, actual == expect ? "MATCH" : "MISMATCH");

    if (m.num_verts) {
        const mdl_vertex *v = mdl_frame(&m, 0);
        printf("f0 v0       : pos(%.3f %.3f %.3f) nrm(%.3f %.3f %.3f) uv0(%.3f %.3f) uv1(%.3f %.3f)\n",
               v->x, v->y, v->z, v->nx, v->ny, v->nz, v->u0, v->v0, v->u1, v->v1);
        /* pos bbox over frame 0 -> compare to the 6-float frame header */
        float mn[3] = {v->x, v->y, v->z}, mx[3] = {v->x, v->y, v->z};
        for (uint32_t i = 0; i < m.num_verts; i++) {
            const mdl_vertex *p = v + i;
            float c[3] = {p->x, p->y, p->z};
            for (int k = 0; k < 3; k++) { if (c[k] < mn[k]) mn[k] = c[k]; if (c[k] > mx[k]) mx[k] = c[k]; }
        }
        const mdl_frame_hdr *H = &m.frame_hdrs[0];
        printf("f0 hdr bbox : min(%.3f %.3f %.3f) max(%.3f %.3f %.3f)\n",
               H->bbox_min[0],H->bbox_min[1],H->bbox_min[2], H->bbox_max[0],H->bbox_max[1],H->bbox_max[2]);
        printf("f0 vtx bbox : min(%.3f %.3f %.3f) max(%.3f %.3f %.3f)  [should match hdr]\n",
               mn[0],mn[1],mn[2], mx[0],mx[1],mx[2]);
    }

    mdl_free(&m);
    return 0;
}
