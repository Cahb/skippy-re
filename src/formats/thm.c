#include "formats/thm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* SkippyThemeObject slot order (index = slot), from reference/theme_object_slots.md */
static const char *SLOT_NAMES[] = {
    "john","catcher","catcherfx","thrower","throwerfx","plate","side","platform",
    "paraglide","paraglidefx","elevator","exit","glue","destructfield","destructfieldfx",
    "jumppad","slide","stair","teleporter","crystal","crystalfx","ammunition","bomb",
    "explosion","surprise","freeze","speed","speedfx","collfx","life","switch","time",
    "ice","obstacle","obstaclefx","protection","protectionfx","bridge",
};
#define NUM_SLOTS ((int)(sizeof(SLOT_NAMES)/sizeof(SLOT_NAMES[0])))

static int ieq(const char *a, const char *b) {
    for (; *a && *b; a++, b++) if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}
int thm_slot_for(const char *name) {
    for (int i = 0; i < NUM_SLOTS; i++) if (ieq(name, SLOT_NAMES[i])) return i;
    return -1;
}

const char *thm_slot_tex(const thm_theme *t, int slot) {
    for (int o = 0; o < t->num_objects; o++) {
        if (t->objects[o].slot != slot) continue;
        const thm_object *ob = &t->objects[o];
        if (ob->num_meshes > 0 && ob->meshes[0].num_tex > 0 && ob->meshes[0].tex[0].tga[0])
            return ob->meshes[0].tex[0].tga;
        return NULL;
    }
    return NULL;
}
static int cond_val(const char *s) {
    if (ieq(s,"active"))    return THM_COND_ACTIVE;
    if (ieq(s,"inactive"))  return THM_COND_INACTIVE;
    if (ieq(s,"dead"))      return THM_COND_DEAD;
    if (ieq(s,"alive"))     return THM_COND_ALIVE;
    if (ieq(s,"paraglide")) return THM_COND_PARAGLIDE;
    return THM_COND_OTHER;
}
static void cpy(char *d, size_t n, const char *s) { size_t i = 0; for (; i + 1 < n && s[i]; i++) d[i] = s[i]; d[i] = 0; }

/* --- line tokenizer: one logical line -> words + opens/closes flags --- */
#define MAXW 12
typedef struct { char w[MAXW][THM_PATH]; int n; int opens, closes; } stmt;

static int next_stmt(FILE *f, stmt *s) {
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        char *cm = strstr(line, "//"); if (cm) *cm = 0;
        s->n = 0; s->opens = s->closes = 0;
        for (char *c = line; *c; ) {
            if (isspace((unsigned char)*c)) { c++; continue; }
            if (*c == '{') { s->opens = 1; c++; continue; }
            if (*c == '}') { s->closes = 1; c++; continue; }
            char *st = c;
            while (*c && !isspace((unsigned char)*c) && *c != '{' && *c != '}') c++;
            if (s->n < MAXW) { int L=(int)(c-st); if(L>THM_PATH-1)L=THM_PATH-1; memcpy(s->w[s->n],st,L); s->w[s->n][L]=0; s->n++; }
        }
        if (s->n || s->opens || s->closes) return 1;
    }
    return 0;
}

enum { CTX_TOP, CTX_OBJECT, CTX_MODEL, CTX_TEXTURE, CTX_ENV, CTX_PARTICLE, CTX_BILLBOARD, CTX_SKIP };

typedef struct {
    thm_theme *out;
    int ctx[64], sp;
    thm_object *obj; thm_mesh *mesh; thm_texture *tex; thm_particle *part; thm_billboard *bb;
} pstate;

/* create a texture on the current mesh from a "Texture <file> [Alpha]" stmt */
static thm_texture *add_texture(thm_mesh *m, const stmt *s) {
    if (m->num_tex >= THM_MAX_TEX) return NULL;
    thm_texture *t = &m->tex[m->num_tex++];
    if (s->n >= 2) cpy(t->tga, sizeof t->tga, s->w[1]);
    for (int i = 2; i < s->n; i++) if (ieq(s->w[i],"Alpha")) t->alpha = true;
    return t;
}

static void open_block(pstate *P, const stmt *s) {
    int cur = P->ctx[P->sp]; const char *kw = s->w[0]; int nc = CTX_SKIP;
    if (cur == CTX_TOP && ieq(kw,"Environment")) {
        nc = CTX_ENV;
    } else if (cur == CTX_TOP && thm_slot_for(kw) != -1 && P->out->num_objects < THM_MAX_OBJECTS) {
        P->obj = &P->out->objects[P->out->num_objects++];
        cpy(P->obj->name, sizeof P->obj->name, kw);
        P->obj->slot = thm_slot_for(kw);
        nc = CTX_OBJECT;
    } else if (cur == CTX_OBJECT && P->obj && ieq(kw,"Model") && P->obj->num_meshes < THM_MAX_MESHES) {
        P->mesh = &P->obj->meshes[P->obj->num_meshes++];
        if (s->n >= 2) cpy(P->mesh->mesh, sizeof P->mesh->mesh, s->w[1]);
        if (s->n >= 3 && strchr(s->w[2], '.')) cpy(P->mesh->anim, sizeof P->mesh->anim, s->w[2]);
        for (int i = 2; i < s->n; i++) if (ieq(s->w[i],"NoMoveStates")) P->mesh->nomovestates = true;
        nc = CTX_MODEL;
    } else if (cur == CTX_OBJECT && P->obj && ieq(kw,"Field") && P->obj->num_meshes < THM_MAX_MESHES) {
        /* engine-generated quad (Plate/Side/...) — no .mdl, holds Texture(s) */
        P->mesh = &P->obj->meshes[P->obj->num_meshes++];
        P->mesh->is_field = true;
        nc = CTX_MODEL;
    } else if (cur == CTX_MODEL && P->mesh && ieq(kw,"Texture")) {
        P->tex = add_texture(P->mesh, s);
        nc = CTX_TEXTURE;
    } else if (cur == CTX_OBJECT && P->obj && ieq(kw,"ParticleSystem") && s->n >= 2) {
        /* every ParticleSystem on the object (Exit ausgang.par, thrower triebwerk.par,
         * a stair's candles, ...): keep the path + parse its Position/Texture. */
        P->part = (P->obj->num_particles < THM_MAX_PART)
                  ? &P->obj->particles[P->obj->num_particles++] : NULL;
        if (P->part) cpy(P->part->par, sizeof P->part->par, s->w[1]);
        nc = CTX_PARTICLE;
    } else if (cur == CTX_PARTICLE && P->part && ieq(kw,"Texture")) {
        if (s->n >= 2 && !P->part->tex[0])
            cpy(P->part->tex, sizeof P->part->tex, s->w[1]);
        nc = CTX_SKIP;   /* skip its nested SrcBlend/DestBlend (particles render additive) */
    } else if (cur == CTX_OBJECT && P->obj && ieq(kw,"Billboard")) {
        /* "Billboard <size> { Position; Texture }" — an additive glow sprite. */
        P->bb = (P->obj->num_billboards < THM_MAX_MESHES)
                ? &P->obj->billboards[P->obj->num_billboards++] : NULL;
        if (P->bb && s->n >= 2) P->bb->size = (float)atof(s->w[1]);
        nc = CTX_BILLBOARD;
    } else if (cur == CTX_BILLBOARD && P->bb && ieq(kw,"Texture")) {
        if (s->n >= 2 && !P->bb->tex[0]) cpy(P->bb->tex, sizeof P->bb->tex, s->w[1]);
        nc = CTX_SKIP;   /* skip nested SrcBlend/DestBlend (billboards render additive) */
    }
    if (P->sp < 63) P->ctx[++P->sp] = nc;
}

static void handle_leaf(pstate *P, const stmt *s) {
    int cur = P->ctx[P->sp]; const char *kw = s->w[0];
    if (cur == CTX_ENV) {
        if (ieq(kw,"Sound") && s->n >= 3 && P->out->num_sounds < THM_MAX_SOUNDS) {
            thm_sound *sd = &P->out->sounds[P->out->num_sounds++];
            cpy(sd->event, sizeof sd->event, s->w[1]); cpy(sd->wav, sizeof sd->wav, s->w[2]);
        } else if (ieq(kw,"Sky") && s->n >= 2) {
            cpy(P->out->sky_base, sizeof P->out->sky_base, s->w[1]);
        } else if (ieq(kw,"HUD") && s->n >= 2) {
            cpy(P->out->hud_tex, sizeof P->out->hud_tex, s->w[1]);
        } else if (ieq(kw,"Radar") && s->n >= 2) {
            cpy(P->out->radar_tex, sizeof P->out->radar_tex, s->w[1]);
        } else if (ieq(kw,"Freeze") && s->n >= 2) {
            cpy(P->out->bonus_tex[THM_BONUS_FREEZE], THM_PATH, s->w[1]);
        } else if (ieq(kw,"InverseControl") && s->n >= 2) {
            cpy(P->out->bonus_tex[THM_BONUS_INVERSE], THM_PATH, s->w[1]);
        } else if (ieq(kw,"Protection") && s->n >= 2) {
            cpy(P->out->bonus_tex[THM_BONUS_PROTECT], THM_PATH, s->w[1]);
        } else if ((ieq(kw,"Slowdwon") || ieq(kw,"Slowdown")) && s->n >= 2) {
            cpy(P->out->bonus_tex[THM_BONUS_SLOW], THM_PATH, s->w[1]);   /* sic: misspelled in every stock .thm */
        } else if (ieq(kw,"Speed") && s->n >= 2) {
            cpy(P->out->bonus_tex[THM_BONUS_SPEED], THM_PATH, s->w[1]);
        } else if (ieq(kw,"HUDTextColors") && s->n >= 2) {
            P->out->hud_text_color[0] = (unsigned)strtoul(s->w[1], NULL, 16);
            P->out->hud_text_color[1] = (unsigned)strtoul(s->n >= 3 ? s->w[2] : s->w[1], NULL, 16);
        } else if ((kw[0] == 'M' || kw[0] == 'm') && s->n >= 3) {
            static const struct { const char *kw; int idx; } MC[] = {
                { "MenuNewGameTextColors",           THM_MC_NEWGAME },
                { "MenuLoadGameTextColors",          THM_MC_LOADGAME },
                { "MenuHighscoresTextColors",        THM_MC_HIGHSCORES },
                { "MenuOptionsTextColors",           THM_MC_OPTIONS },
                { "MenuCreditsTextColors",           THM_MC_CREDITS },
                { "MenuQuitTextColors",              THM_MC_QUIT },
                { "MenuLoadGameEntriesTextColors",   THM_MC_LOAD_ENTRIES },
                { "MenuSaveGameEntriesTextColors",   THM_MC_SAVE_ENTRIES },
                { "MenuHighscoresEntriesTextColors", THM_MC_HS_ENTRIES },
            };
            for (size_t i = 0; i < sizeof MC / sizeof MC[0]; i++)
                if (ieq(kw, MC[i].kw)) {
                    P->out->menu_color[MC[i].idx][0] = (unsigned)strtoul(s->w[1], NULL, 16);
                    P->out->menu_color[MC[i].idx][1] = (unsigned)strtoul(s->w[2], NULL, 16);
                    break;
                }
        } else if (ieq(kw,"SideHeight") && s->n >= 2) {
            P->out->side_height = (float)atof(s->w[1]);
        }
    } else if (cur == CTX_TEXTURE && P->tex) {
        if      (ieq(kw,"Condition") && s->n>=2) P->tex->condition = cond_val(s->w[1]);
        else if (ieq(kw,"Wobble")    && s->n>=4) { for (int i=0;i<3;i++) P->tex->wobble[i]=(float)atof(s->w[i+1]); }
        else if (ieq(kw,"SrcBlend")  && s->n>=2) cpy(P->tex->src_blend, sizeof P->tex->src_blend, s->w[1]);
        else if (ieq(kw,"DestBlend") && s->n>=2) cpy(P->tex->dest_blend, sizeof P->tex->dest_blend, s->w[1]);
        else if (ieq(kw,"NoZWrite")) P->tex->nozwrite = true;
        else if (ieq(kw,"NoShadow")) P->tex->noshadow = true;
        else if (ieq(kw,"Environment")) P->tex->environment = true;
        else if (ieq(kw,"Pulse") && s->n>=2) P->tex->pulse = (float)atof(s->w[1]);
        else if (ieq(kw,"Turn")  && s->n>=2) P->tex->turn  = (float)atof(s->w[1]);
        else if (ieq(kw,"Scroll") && s->n>=3) { P->tex->scroll[0]=(float)atof(s->w[1]); P->tex->scroll[1]=(float)atof(s->w[2]); }
        else if (ieq(kw,"Flash") && s->n>=4) { for (int i=0;i<3;i++) P->tex->flash[i]=(float)atof(s->w[i+1]); }
        else if (ieq(kw,"TextureAdress") && s->n>=2 && ieq(s->w[1],"Wrap")) P->tex->wrap = true;
    } else if (cur == CTX_MODEL && P->mesh) {
        thm_mesh *m = P->mesh;
        if (ieq(kw,"Texture")) {                 /* block-less "Texture <file>" (e.g. Plate) */
            add_texture(m, s);
        } else if (ieq(kw,"Position") && s->n>=4) {
            for (int i=0;i<3;i++) m->pos[i]=(float)atof(s->w[i+1]);
        } else if (ieq(kw,"RandomYAngle")) {
            m->random_yaw = true;
        } else if (ieq(kw,"Specular")) {
            m->specular = true;
        } else if (ieq(kw,"Rotate") && s->n>=4) {
            for (int i=0;i<3;i++) m->rotate[i]=(float)atof(s->w[i+1]);
        } else if (ieq(kw,"Oscillate") && s->n>=3) {
            /* Oscillate <amp> <speed> [random] [phase] */
            m->oscillate = true;
            m->osc[0]=(float)atof(s->w[1]); m->osc[1]=(float)atof(s->w[2]);
            for (int i=3;i<s->n;i++) {
                if (ieq(s->w[i],"random")) m->oscillate_random = true;
                else m->osc[2] = (float)atof(s->w[i]);   /* explicit phase offset */
            }
        } else if (ieq(kw,"Pump") && s->n>=3) {
            /* "Pump <amp> ... <speed>": amplitude first, speed LAST (0.005 = same
             * rad/ms unit as Oscillate; the middle args are unused). */
            m->pump = true;
            m->pump_p[0]=(float)atof(s->w[1]);
            m->pump_p[1]=(float)atof(s->w[s->n-1]);
        } else if (m->num_tex > 0) {
            if      (ieq(kw,"NoZWrite")) m->tex[m->num_tex-1].nozwrite = true;
            else if (ieq(kw,"NoShadow")) m->tex[m->num_tex-1].noshadow = true;
        }
    } else if (cur == CTX_PARTICLE && P->part) {
        if      (ieq(kw,"Position") && s->n>=4) { for (int i=0;i<3;i++) P->part->pos[i]=(float)atof(s->w[i+1]); }
        else if (ieq(kw,"Texture")  && s->n>=2 && !P->part->tex[0]) cpy(P->part->tex, sizeof P->part->tex, s->w[1]);
    } else if (cur == CTX_BILLBOARD && P->bb) {
        if      (ieq(kw,"Position") && s->n>=4) { for (int i=0;i<3;i++) P->bb->pos[i]=(float)atof(s->w[i+1]); }
        else if (ieq(kw,"Texture")  && s->n>=2 && !P->bb->tex[0]) cpy(P->bb->tex, sizeof P->bb->tex, s->w[1]);
    }
}

bool thm_load(const char *path, thm_theme *out) {
    memset(out, 0, sizeof *out);
    FILE *f = fopen(path, "rb");
    if (!f) return false;

    pstate P; memset(&P, 0, sizeof P); P.out = out; P.ctx[0] = CTX_TOP;
    stmt s, pend; int havep = 0;

    while (next_stmt(f, &s)) {
        if (s.opens && s.n == 0) {                 /* lone "{" -> opens the pending header */
            if (havep) { open_block(&P, &pend); havep = 0; }
            else if (P.sp < 63) P.ctx[++P.sp] = CTX_SKIP;
            if (s.closes && P.sp > 0) P.sp--;      /* "{ }" on one line (rare) */
            continue;
        }
        if (havep) { handle_leaf(&P, &pend); havep = 0; }   /* pending was a leaf, not a header */

        if (s.n > 0 && s.opens) {                  /* "kw args {" on one line */
            open_block(&P, &s);
            if (s.closes && P.sp > 0) P.sp--;
        } else if (s.n > 0) {                       /* keyword line, brace (if any) on next line */
            pend = s; havep = 1;
            if (s.closes) { handle_leaf(&P, &pend); havep = 0; if (P.sp > 0) P.sp--; }
        } else if (s.closes) {                      /* lone "}" */
            if (P.sp > 0) P.sp--;
        }
    }
    if (havep) handle_leaf(&P, &pend);

    fclose(f);
    return true;
}
