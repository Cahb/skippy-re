/* .thm theme loader — the asset orchestrator. Text format; grammar per
 * reference/theme_format.md, object slots per reference/theme_object_slots.md,
 * sound events per reference/sound_events.md. Ties object types -> sub-meshes
 * (.mdl + .ani + flags) -> textures (.tga + condition/blend), plus environment
 * textures/sky and the Sound<event> -> wav map. */
#ifndef THM_H
#define THM_H

#include <stdbool.h>

#define THM_PATH        128
#define THM_MAX_OBJECTS 40   /* 38 slots + slack */
#define THM_MAX_MESHES  8    /* sub-meshes per object */
#define THM_MAX_TEX     8    /* textures per sub-mesh */
#define THM_MAX_PART    8    /* ParticleSystems per object (an object can carry several) */
#define THM_MAX_SOUNDS  64

typedef struct {
    char tga[THM_PATH];
    int  condition;   /* 0=none,1=active,2=inactive,3=dead,4=alive,5=paraglide (theme decode) */
    char src_blend[24];  /* raw D3DBLEND keyword (e.g. "one","srcalpha"); "" = default */
    char dest_blend[24];
    bool alpha, nozwrite, noshadow;
    bool environment;    /* "Environment": sphere-mapped reflection layer (additive shine) */
    float pulse;         /* "Pulse <rate>": additive-glow brightness pulse rate (rad/ms); 0 = none */
    float turn;          /* "Turn <rate>": UV rotation rate (rad/ms), e.g. teleporter swirl; 0 = none */
    float wobble[3];     /* "Wobble <speed> <ampU> <ampV>" UV-warp FX; all 0 = none */
    float scroll[2];     /* "Scroll <u> <v>" UV scroll rate (units/ms), e.g. the space bridge; 0 = none */
    float flash[3];      /* "Flash <a> <b> <c>": self-flashing additive glow (exit layers);
                            all 0 = none. Param 3 differs per layer (3 vs 4) = phase offset. */
    bool  wrap;          /* "TextureAdress Wrap": UV tiling (sides, bridges) */
} thm_texture;

typedef struct {
    char mesh[THM_PATH];   /* .mdl path as written in the .thm ("" for a Field) */
    char anim[THM_PATH];   /* .ani path ("" if none) */
    bool nomovestates;
    bool is_field;         /* true = "Field" (engine-generated quad, e.g. Plate/Side), not a .mdl */
    float pos[3];          /* "Position x y z" mesh offset (e.g. Space thrower floats at 0,1,0) */
    /* per-mesh transform/animation modifiers (see reference/thm_coverage.md). Theme units;
     * rates read as ~radians-per-millisecond. The generic applier turns these into the
     * per-frame spin/bob/pump instead of hand-coded sinf() constants. */
    bool  random_yaw;      /* RandomYAngle: random initial Y rotation per instance */
    bool  specular;        /* Specular flag */
    float rotate[3];       /* Rotate <x y z>: continuous spin rate per axis */
    bool  oscillate;       /* has an "Oscillate" */
    bool  oscillate_random;/* its 'random' sub-flag: random phase per instance */
    float osc[3];          /* Oscillate: [0]=amplitude [1]=speed [2]=phase offset */
    bool  pump;            /* has a "Pump" */
    float pump_p[2];       /* Pump: [0]=amplitude [1]=speed (scale pulse) */
    int  num_tex;
    thm_texture tex[THM_MAX_TEX];
} thm_mesh;

/* one "ParticleSystem <file.par> { Position..; Texture.. }" on an object. An object
 * can carry several (e.g. a stair with candles), so they're kept as a list. */
typedef struct {
    char  par[THM_PATH];   /* .par path as written */
    char  tex[THM_PATH];   /* its "Texture <file>" (e.g. flare / bubble) */
    float pos[3];          /* its "Position x y z" offset */
} thm_particle;

/* one "Billboard <size> { Position..; Texture.. }" on an object: a camera-facing
 * additive sprite (glow flare), e.g. the Time bonus's flare01 or a crystal glow. */
typedef struct {
    char  tex[THM_PATH];   /* its Texture */
    float size;            /* "Billboard <size>" */
    float pos[3];          /* Position offset */
} thm_billboard;

typedef struct {
    char name[32];    /* object keyword as written (e.g. "John") */
    int  slot;        /* SkippyThemeObject index 0-37, or -1 if unrecognized */
    int  num_meshes;
    thm_mesh meshes[THM_MAX_MESHES];
    int  num_particles;
    thm_particle particles[THM_MAX_PART];   /* every ParticleSystem declared on this object */
    int  num_billboards;
    thm_billboard billboards[THM_MAX_MESHES];  /* Billboard glow sprites (Time flare, ...) */
} thm_object;

/* texture Condition keyword (see cond_val): which state the layer renders in. */
enum {
    THM_COND_NONE      = 0,   /* unconditioned */
    THM_COND_ACTIVE    = 1,
    THM_COND_INACTIVE  = 2,
    THM_COND_DEAD      = 3,
    THM_COND_ALIVE     = 4,
    THM_COND_PARAGLIDE = 5,
    THM_COND_OTHER     = 6,   /* unrecognized keyword */
};

typedef struct { char event[32]; char wav[THM_PATH]; } thm_sound;

/* Environment bonus-timer HUD ring icons (cfg5-9 of the original env block).
 * NB the stock .thm files misspell the slowdown keyword as "Slowdwon". */
enum {
    THM_BONUS_FREEZE = 0, THM_BONUS_INVERSE, THM_BONUS_PROTECT,
    THM_BONUS_SLOW, THM_BONUS_SPEED, THM_BONUS_N
};

/* Environment Menu*TextColors pairs (per-item normal + highlight, 0xRRGGBB). */
enum {
    THM_MC_NEWGAME = 0, THM_MC_LOADGAME, THM_MC_HIGHSCORES, THM_MC_OPTIONS,
    THM_MC_CREDITS, THM_MC_QUIT,
    THM_MC_LOAD_ENTRIES, THM_MC_SAVE_ENTRIES, THM_MC_HS_ENTRIES,
    THM_MC_N
};

typedef struct {
    int        num_objects;
    thm_object objects[THM_MAX_OBJECTS];
    int        num_sounds;
    thm_sound  sounds[THM_MAX_SOUNDS];
    char       sky_base[THM_PATH];  /* Sky <base> -> 6 cube faces */
    char       hud_tex[THM_PATH];   /* HUD <file> -> corner-panel atlas (name varies per theme) */
    char       radar_tex[THM_PATH]; /* Radar <file> -> minimap disc */
    unsigned   hud_text_color[2];   /* HUDTextColors: [0]=normal [1]=highlight, 0xRRGGBB */
    char       bonus_tex[THM_BONUS_N][THM_PATH]; /* Freeze/InverseControl/Protection/Slowdwon/Speed icons */
    unsigned   menu_color[THM_MC_N][2]; /* Menu*TextColors: [0]=normal [1]=highlight; 0 = unset */
    float      side_height;         /* Environment SideHeight: platform side/thickness depth */
} thm_theme;

bool thm_load(const char *path, thm_theme *out);

/* SkippyThemeObject slot for an object keyword (case-insensitive), or -1. */
int thm_slot_for(const char *name);

/* First texture .tga of whichever object occupies `slot` (e.g. 5=plate top,
 * 6=side face), or NULL if that object/texture isn't present in the theme. */
const char *thm_slot_tex(const thm_theme *t, int slot);

#endif /* THM_H */
