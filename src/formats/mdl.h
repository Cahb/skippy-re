/* .mdl model loader — ported 1:1 from reference/mdl_format.md
 * (original: load_model @0x437bc0). Vertex-morph animation: each frame is a FULL
 * snapshot of the mesh's vertices. Drawn as one TRIANGLELIST (FVF 0x212 =
 * D3DFVF_XYZ|NORMAL|TEX2 -> pos + normal + 2 UV sets = 10 floats = 40 bytes). */
#ifndef MDL_H
#define MDL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    float x, y, z;      /* position */
    float nx, ny, nz;   /* normal */
    float u0, v0;       /* tex coord set 0 */
    float u1, v1;       /* tex coord set 1 (TEX2) */
} mdl_vertex;           /* 40 bytes (0x28) */

typedef struct {
    float bbox_min[3];  /* per-frame position bounding-box min (x,y,z) — VERIFIED == vertex bbox */
    float bbox_max[3];  /* per-frame position bounding-box max (x,y,z) */
} mdl_frame_hdr;        /* 24 bytes; the original's per-frame anim header */

typedef struct {
    uint16_t num_frames;         /* animation frames */
    uint32_t num_verts;          /* vertices per frame */
    mdl_frame_hdr *frame_hdrs;   /* [num_frames] */
    mdl_vertex    *verts;        /* [num_frames * num_verts]; frame f starts at f*num_verts */
    char          *name;         /* strdup of load path (mirrors original mdl_name) */
} mdl_model;

bool mdl_load(const char *path, mdl_model *out);
void mdl_free(mdl_model *m);

/* pointer to the vertex block for frame f */
static inline const mdl_vertex *mdl_frame(const mdl_model *m, uint32_t f) {
    return m->verts + (size_t)f * m->num_verts;
}

#endif /* MDL_H */
