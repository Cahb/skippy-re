/* Loads a .thm and dumps the object->submesh->texture tree + sound-event map,
 * to eyeball against reference/theme_object_slots.md + the .thm text. */
#include "formats/thm.h"

#include <stdio.h>

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <path.thm>\n", argv[0]); return 2; }

    thm_theme t;
    if (!thm_load(argv[1], &t)) { fprintf(stderr, "FAILED to load %s\n", argv[1]); return 1; }

    printf("== %s ==\n", argv[1]);
    printf("objects=%d  sounds=%d  sky=\"%s\"\n", t.num_objects, t.num_sounds, t.sky_base);

    for (int o = 0; o < t.num_objects; o++) {
        const thm_object *ob = &t.objects[o];
        printf("[slot %2d] %-14s  meshes=%d\n", ob->slot, ob->name, ob->num_meshes);
        for (int mi = 0; mi < ob->num_meshes; mi++) {
            const thm_mesh *m = &ob->meshes[mi];
            printf("    mesh %-26s anim %-18s tex=%d%s\n",
                   m->mesh, m->anim[0] ? m->anim : "-", m->num_tex,
                   m->nomovestates ? " [NoMoveStates]" : "");
            for (int ti = 0; ti < m->num_tex; ti++) {
                const thm_texture *x = &m->tex[ti];
                printf("        tex %-24s cond=%d src=%s dst=%s%s%s%s\n",
                       x->tga, x->condition,
                       x->src_blend[0] ? x->src_blend : "-",
                       x->dest_blend[0] ? x->dest_blend : "-",
                       x->alpha ? " alpha" : "", x->nozwrite ? " noZ" : "", x->noshadow ? " noShadow" : "");
            }
        }
    }

    printf("sounds (%d):\n", t.num_sounds);
    for (int i = 0; i < t.num_sounds; i++)
        printf("    %-18s -> %s\n", t.sounds[i].event, t.sounds[i].wav);
    return 0;
}
