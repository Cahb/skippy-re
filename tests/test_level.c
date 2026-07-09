/* M0 capstone: load a whole level end-to-end.
 *   gam (manifest) -> pick level -> jjm (grid) -> thm (theme) -> mdl + tga (assets)
 * Reports totals and any asset that fails to resolve/load. */
#include "formats/gam.h"
#include "formats/jjm.h"
#include "formats/thm.h"
#include "formats/mdl.h"
#include "formats/tga.h"
#include "formats/asset.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    const char *base = (argc > 1) ? argv[1] : "../game_root/SkippyAdventure";
    int idx = (argc > 2) ? atoi(argv[2]) : 0;   /* level index (0 = Forest\Start) */

    char p[1024], rel[512];

    /* --- manifest --- */
    gam_manifest g;
    if (!asset_resolve(base, "JJ.GAM", p, sizeof p) || !gam_load(p, &g)) { printf("gam load FAILED\n"); return 1; }
    if (idx < 0 || idx >= g.num_levels) idx = 0;
    printf("game: %d levels; loading level %d = \"%s\"\n", g.num_levels, idx + 1, g.levels[idx]);

    /* --- level grid --- */
    snprintf(rel, sizeof rel, "Levels\\%s.jjm", g.levels[idx]);
    jjm_level lvl;
    if (!asset_resolve(base, rel, p, sizeof p) || !jjm_load(p, &lvl)) { printf("jjm load FAILED (%s)\n", rel); return 1; }
    printf("  grid %ux%u, world=\"%s\", crystals_needed=%u\n", lvl.dim_x, lvl.dim_y, lvl.world, lvl.crystals_needed);

    /* --- theme --- */
    snprintf(rel, sizeof rel, "Themes\\%s.thm", lvl.world);
    thm_theme th;
    if (!asset_resolve(base, rel, p, sizeof p) || !thm_load(p, &th)) { printf("thm load FAILED (%s)\n", rel); return 1; }
    printf("  theme: %d objects, %d sounds\n", th.num_objects, th.num_sounds);

    /* --- resolve + load every referenced mesh and texture --- */
    int meshes_ok = 0, meshes_miss = 0, tex_ok = 0, tex_miss = 0;
    long total_verts = 0;
    for (int o = 0; o < th.num_objects; o++) {
        thm_object *ob = &th.objects[o];
        for (int mi = 0; mi < ob->num_meshes; mi++) {
            thm_mesh *m = &ob->meshes[mi];
            if (m->mesh[0]) {
                mdl_model mdl;
                if (asset_resolve(base, m->mesh, p, sizeof p) && mdl_load(p, &mdl)) {
                    meshes_ok++; total_verts += (long)mdl.num_frames * mdl.num_verts; mdl_free(&mdl);
                } else { meshes_miss++; printf("    MISS mesh: %s\n", m->mesh); }
            }
            for (int ti = 0; ti < m->num_tex; ti++) {
                thm_texture *x = &m->tex[ti];
                if (!x->tga[0]) continue;
                tga_image img;
                if (asset_resolve(base, x->tga, p, sizeof p) && tga_load(p, &img)) {
                    tex_ok++; tga_free(&img);
                } else { tex_miss++; printf("    MISS tex : %s\n", x->tga); }
            }
        }
    }
    printf("  meshes: %d loaded (%ld total verts), %d missing\n", meshes_ok, total_verts, meshes_miss);
    printf("  textures: %d loaded, %d missing\n", tex_ok, tex_miss);
    printf("%s\n", (meshes_miss == 0 && tex_miss == 0) ? "ALL ASSETS RESOLVED ✓" : "some assets missing ✗");
    return 0;
}
