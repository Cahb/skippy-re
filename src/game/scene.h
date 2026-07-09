/* Per-level scene: the render assets (meshes/textures/sounds/fx/.leo props +
 * John's meshes) bundled in one struct, plus the level/sim/camera state and the
 * loaders that (re)build them. base + the manifest stay owned by main. */
#ifndef GAME_SCENE_H
#define GAME_SCENE_H

#include <stdbool.h>
#include "render/renderer.h"
#include "formats/gam.h"
#include "formats/jjm.h"
#include "formats/thm.h"
#include "formats/leo.h"
#include "formats/ani.h"
#include "formats/par.h"
#include "sim/sim.h"

/* one loaded sub-mesh ready to draw */
typedef struct {
    r_mesh mesh;
    r_tex  tex;
    r_tex  env_tex;      /* Environment reflection layer (e.g. John's helmet); -1 = none */
    r_vec3 bmin, bmax;   /* model-space bbox (frame 0) */
} draw_mesh;

#define THROWER_MAX_MESH 6
#define FX_EMIT_MAX 512   /* placed particle-emitter instances per level */

/* what a particle emitter is glued to; resolves its world anchor each frame.
 * (The crystal spark is NOT here: it's a notMovable FX bound to the animated gem,
 * drawn attached at the gem transform in render_scene, not spawned into the pool.) */
enum fx_anchor {
	FXA_STATIC = 0,   /* fixed world pos (stair candles/torches, teleporter, .leo props) */
	FXA_EXIT,         /* exit tile (rx,ry): on only once the exit opens */
	FXA_PLAYER,       /* follows John (helmet bubbles): pos = offset from the player */
	FXA_ENEMY,        /* follows enemy[ref] (thruster exhaust): off when removed/dying */
};

/* per-mesh spin/bob/pulse distilled from the theme (RandomYAngle/Rotate/Oscillate/
 * Pump). mesh_anim_eval() turns it into a per-frame yaw + Z bob + uniform scale, so the
 * animation is data-driven instead of hand-coded sinf() constants. See thm_coverage.md. */
struct mesh_anim {
	bool  random_yaw;                /* RandomYAngle: stable per-instance random start yaw */
	float rotate_z;                  /* Rotate about up (theme Rotate[1]); rad/ms */
	bool  oscillate, osc_random;     /* Oscillate + its 'random' phase sub-flag */
	float osc[3];                    /* amplitude, speed (rad/ms), phase offset */
	bool  pump;                      /* Pump: uniform scale pulse */
	float pump_p[2];                 /* amplitude, speed (rad/ms) */
};

/* one sub-mesh of a theme object, loaded and ready to draw with its own material +
 * transform + animation. An object can have several (stair + candle holders, robot body
 * + dome + light-rays, ...). */
struct obj_submesh {
	r_mesh       mesh;               /* all frames uploaded */
	int          frames;
	r_tex        tex;                /* tex[0] */
	r_tex        env_tex;            /* Environment reflection layer (tex w/ Environment); -1 none */
	struct mesh_anim anim;           /* Rotate/Oscillate/Pump/RandomYAngle */
	r_vec3       pos;                /* Position offset, theme Y-up -> world (x,-z,y) */
	bool         additive;           /* SrcBlend One -> glow layer (drawn in additive pass) */
	bool         alpha;              /* theme "Alpha" (SrcAlpha) -> cutout (alpha-test in opaque pass) */
	/* looping vertex animation from the Model's own .ani (e.g. Egypt flying-carpet
	 * flutter, tepisch.ani): loops [afirst, afirst+acount) at afps. acount<=1 = static. */
	int          afirst, acount;
	float        afps;
};

/* a fully-loaded theme object: ALL its Model sub-meshes. Drawn generically by draw_obj;
 * replaces the per-type single-mesh fields (stair_mesh, ...) one object at a time. */
struct obj_render {
	struct obj_submesh sm[THM_MAX_MESHES];
	int          n;
	/* Billboard sub-objects: camera-facing additive glow sprites (e.g. the Time bonus's
	 * flare). Drawn in the additive pass. */
	struct { r_tex tex; float size; r_vec3 pos; } bb[THM_MAX_MESHES];
	int          nbb;
};

/* one placed .par emitter: a decoded system + its sprite, an anchor, and a
 * fractional-spawn carry. See load_emitters / fx_emit_world. */
struct fx_emitter {
	par_system    par;       /* decoded .par (spawn ranges, speed, life, gravity, colours) */
	r_tex         tex;       /* the .par's own sprite (flare / star / bubble) */
	unsigned char blend;     /* R_BLEND_ADD / R_BLEND_ALPHA */
	unsigned char anchor;    /* enum fx_anchor */
	short         ref;       /* enemy index (FXA_ENEMY) */
	short         rx, ry;    /* tile coords (FXA_EXIT) */
	r_vec3        pos;       /* FXA_STATIC world anchor; per-anchor base offset otherwise */
	float         yaw_deg;   /* local->world rotation about Z (candles on rotated stairs) */
	float         accum;     /* fractional-spawn carry */
	float         sz0, sz1;  /* >0 = override the .par sizes (the bee-speck quirk) */
};

/* one resolved .thm Field layer, ready to draw (the generic tile-top pipeline).
 * A Field is an ORDERED stack of these: base first, overlays after, each with
 * its own blend, state Condition, and UV animation. */
enum { FL_OPAQUE = 0, FL_ALPHA, FL_ADD };
struct field_layer {
	r_tex         tex;
	unsigned char blend;      /* FL_* */
	unsigned char cond;       /* THM_COND_NONE / _ACTIVE / _INACTIVE */
	float         turn;       /* UV spin rate (rad/ms) */
	float         scroll[2];  /* UV pan rate (units/ms) */
	float         pulse;      /* additive brightness pulse rate (rad/ms) */
	float         wobble[3];  /* UV warp: speed, ampU, ampV */
	float         flash[3];   /* exit self-flash (param 3 = the phase offset) */
};
struct field_stack { int n; struct field_layer l[6]; };

/* ---------- per-level render assets (rebuilt by load_scene on every level) ---------- */
struct scene {
	r_tex        sky[6];                 /* skybox faces */
	r_tex        tex_plate, tex_side, tex_exit;
	bool         side_alpha;             /* Side is a theme-Alpha full-image cutout (Water SIDE64 vine curtain) */
	float        thk;                    /* platform thickness (theme SideHeight) */
	/* generic .thm Field-layer stacks (per tile type + the mover top panes): the
	 * theme's ordered texture list, each layer with its own blend/condition/anim,
	 * drawn base-first across the opaque/alpha/additive passes. Replaces the old
	 * one-texture-per-heuristic extraction (tile_tex/glow/alpha/turn). */
	struct field_stack tile_field[64];
	struct field_stack mover_field[2];   /* [0] = elevator top pane, [1] = platform */
	struct obj_render tile_obj[64];      /* per-tile-type Model sub-meshes (teleporter ring/mesh); n=0 none */
	r_tex        destruct_tex;           /* DestructField as a Field: grid (alpha) on intact tiles; -1 none */
	struct obj_render destruct_obj;      /* DestructField as a Model (e.g. Castle castle_destroy.mdl); n=0 if Field */
	par_system   destruct_fx_par;        /* DestructFieldFX (slot 14): crumb burst when a 0x0d tile collapses */
	r_tex        destruct_fx_tex;        /* its sprite (e.g. Candy kruemel16); -1 = no DestructFieldFX */
	int          destruct_fx_blend;      /* R_BLEND_ALPHA (kruemel is SrcAlpha) or ADD */
	r_mesh       crystal_mesh;
	r_tex        crystal_tex;
	r_color      crystal_color;          /* gem tint -> glow + pickup sparks (per theme) */
	struct mesh_anim crystal_anim;       /* gem spin/bob from the theme (Rotate/Oscillate/RandomYAngle) */
	/* CrystalFX: a notMovable spark bound to the gem, drawn attached at its animated
	 * transform (not a world-pool emitter). Data-driven from the theme CrystalFX .par. */
	r_tex        crystal_fx_tex;         /* its sprite (e.g. star3_32); -1 = no CrystalFX */
	r_color      crystal_fx_col;         /* weighted-mean colour from the .par ramp */
	float        crystal_fx_size;        /* .par face_size */
	float        crystal_fx_up;          /* .par spawn point mapped to world Z (inside the gem) */
	struct obj_render stair_obj;         /* Stair (5-8): Treppe.mdl + Treppe_Kerzen.mdl candle holders */
	r_mesh       glue_mesh;              /* glue overlay Model (e.g. Castle spinnweben webs) */
	r_tex        glue_tex;               /* base goo (Field cond 0, e.g. kleb) / Castle web */
	r_tex        glue_fresh_tex;         /* Field InActive overlay (never stepped, e.g. rahmen64 outline) */
	r_tex        glue_spent_tex;         /* Field Active overlay (stepped, e.g. rahmen_gitter64 grid) */
	float        glue_wobble[3];         /* base goo's theme Wobble params: speed, ampU, ampV */
	r_mesh       catcher_mesh;           /* enemy (theme slot 1, e.g. Forest frosch/frog) */
	r_tex        catcher_tex;
	int          catcher_frames;
	ani_set      catcher_anim;
	r_mesh       bomb_mesh;              /* Bomb model (slot 22) — live/flying bombs */
	r_tex        bomb_tex;
	r_mesh       bomb_glow_mesh;         /* Bomb 2nd mesh (e.g. Space bomb01_gl) — additive "lasery" glow */
	r_tex        bomb_glow_tex;
	struct obj_render obstacle_obj;      /* destructible rock (slot 33) on type-0x17 tiles (all sub-meshes) */
	float        obstacle_shatter[JJM_MAX_DIM][JJM_MAX_DIM];  /* >0 = mid Explode shatter */
	struct obj_render platform_obj;      /* horizontal mover (slot 7) */
	struct obj_render elevator_obj;      /* vertical mover (slot 10): the side frame Model(s) */
	struct obj_render jumppad_obj;       /* launch pad (slot 15) */
	/* thrower (slot 3): N sub-meshes each with its own texture (Castle cannon = 2, Space
	 * robot = body + light-rays + dome). Additive sub-meshes (light rays, One/One) are the
	 * glow layers — drawn in the additive pass, not opaque. */
	r_mesh       thrower_mesh[THROWER_MAX_MESH];
	r_tex        thrower_mtex[THROWER_MAX_MESH];
	bool         thrower_madd[THROWER_MAX_MESH];   /* additive glow layer */
	int          thrower_nmesh;
	int          thrower_frames;         /* Kanone.mdl frame count (meshes share it) */
	ani_set      thrower_anim;           /* Kanone.ani clips (WALK_FORWARD + stairs) */
	float        thrower_off_z;          /* mesh "Position" up-offset: Space thrower floats */
	/* bonus pickups indexed by pickup byte: 5=paraglide 6=time 7=heart 8=freeze
	 * 9=bomb 10=speed 13=protection (crystal=1 handled separately). */
	struct obj_render pickup_obj[16];    /* bonus pickups by pickup byte: all sub-meshes + theme spin/bob/pump */
	struct obj_render surprise_obj;      /* surprise box (pickup 255, theme slot 24) */
	r_tex        hud_left, hud_right;    /* HUD_ALL.TGA split (diagonally-packed panels) */
	r_tex        hud_numbers;            /* NUMBERS.TGA digit atlas */
	r_tex        font2_tex;              /* Font2.tga: 16x16 glyph grid, byte-indexed (captions/menu) */
	r_tex        menu_panel;             /* theme Menu.tga (the rounded panel) */
	int          menu_panel_w, menu_panel_h;
	r_tex        hud_radar;              /* per-theme RADAR.TGA minimap disc */
	draw_mesh    john_glide[4];          /* Condition-Paraglide John submeshes: the chute
	                                        on his back, animated by his own k.ani frames */
	int          john_glide_n;
	r_tex        bonus_tex[THM_BONUS_N]; /* bonus-timer ring icons (env Freeze..Speed) */
	int          bonus_w[THM_BONUS_N], bonus_h[THM_BONUS_N];
	unsigned     th_menu_color[THM_MC_N][2]; /* theme Menu*TextColors (0 = unset) */
	r_tex        menu_selector;          /* SELECTOR.TGA: the boxing-glove selection marker */
	int          menu_selector_w, menu_selector_h;
	/* SFX handles, (re)loaded per theme from the thm "Sound<event>" map (snd_exit is
	 * a global wav: the level-passed / exit-open fanfare) */
	r_sound      snd_move, snd_crystal, snd_pickup, snd_splat, snd_fall, snd_caught, snd_emove, snd_exit;
	r_sound      snd_glue;               /* glue stick (slide/ice are loops: mus_* below) */
	r_sound      snd_menu;               /* MenuUpDown.wav: menu navigation tick */
	r_sound      snd_switch;             /* theme Switch.wav: bridge switch toggle */
	r_sound      snd_bombtick, snd_explode; /* fuse burn + detonation */
	r_sound      snd_destruct, snd_regen;    /* DestructField collapse (DestructStart) + regen (DestructRegen) */
	r_sound      snd_obstacle, snd_ecaught;   /* obstacle blown open (Obstacle) + enemy blast-death (ExplosionCatcher) */
	r_sound      snd_jumppad;                 /* jump-pad launch (MoveJumpPad; NONE in some themes) */
	r_sound      snd_ethrow;                   /* thrower hop (MoveThrower — e.g. Castle cannon Kanone.wav) */
	r_sound      snd_teleport;                /* teleporter warp (Teleporter) */
	r_sound      snd_timeout, snd_lastsec;   /* global: time-up + final-10s per-second tick */
	/* sustained state loops (original: DSBPLAY_LOOPING buffers), gapless streams
	 * mixed each frame by audio_update_loops; -1 = theme has none (NONE). */
	r_music      mus_slide, mus_ice, mus_glide;           /* player chute / ice run / paraglide */
	r_music      mus_elevator, mus_platform, mus_bridge;  /* mover travel hums (Space, Egypt) */
	r_sound      snd_levelcomplete;          /* clapping when you enter the open exit */
	r_sound      add_snd[11][3];             /* ADD01..ADD10, variants A/B/C (pickup + exit-open announce) */
	r_tex        fx_star;                /* star3_32 sparkle (crystal glow + pickup burst) */
	r_tex        fx_flare;               /* flare01 sparkle (open-exit fountain) */
	r_tex        fx_blow;                /* soft blob (blow.tga) for explosion puffs */
	/* generic .par-driven particle emitters: one per (theme/leo particle system x placed
	 * instance) — crystal sparkles, stair candles, exit fountain, teleporter shimmer,
	 * John's bubbles, thrower exhaust, .leo fountains/swarms. Built by load_emitters,
	 * walked every frame by fx_emit_world -> par_emit. */
	struct fx_emitter emitters[FX_EMIT_MAX];
	int          num_emitters;
	r_music      leo_amb_mus[LEO_MAX_SOUNDS];  /* .leo Sound positional ambience (bees/bird/fountain), looped */
	r_vec3       leo_amb_pos[LEO_MAX_SOUNDS];
	int          leo_amb_n;
	leo_scene    leo;                    /* extra 3D objects */
	/* per-.leo-object animation: an all-frames mesh (r_upload_anim) + the loop clip, for
	 * props with an "ANI <path>" (butterflies flap, castle flags wave, fish/ray swim).
	 * leo_amesh[i] < 0 = static (drawn via mesh_cached frame 0). */
	r_mesh       leo_amesh[LEO_MAX_OBJECTS];
	int          leo_afirst[LEO_MAX_OBJECTS], leo_acount[LEO_MAX_OBJECTS];
	float        leo_afps[LEO_MAX_OBJECTS];
	draw_mesh    john[THM_MAX_MESHES];   /* player sub-meshes, alive (all frames) */
	int          john_n, john_frames;
	draw_mesh    john_dead[THM_MAX_MESHES]; /* Dead-condition meshes (angel: k+kopf grey + fluegel), additive */
	int          john_dead_n;
	bool         john_dead_wings[THM_MAX_MESHES]; /* fluegel wings (flap animation) vs body (static GHOST pose) */
	int          john_dead_nf[THM_MAX_MESHES];    /* frame count per dead mesh (wings loop over it) */
	r_vec3       jmin, jmax;             /* combined model bbox */
	float        jscale;
	ani_set      john_anim;              /* player clip table */
};


/* the loaded scene + level/sim/camera state (set-once base + manifest live in main) */
extern struct scene g_scene;
extern jjm_level    lvl;                /* current grid */
extern sim_state    sim;                /* gameplay */
extern r_camera     cam;                /* freecam seed (grid overview) */
extern char         cur_scene_name[128];/* level whose assets are loaded (.leo path etc.) */

/* (Re)load a level by name / by manifest index: frees the previous level's GPU
 * assets, parses grid/theme/.leo, loads assets, inits the sim. false on hard fail. */
bool load_scene_named(const char *base, const char *name);
bool load_scene(const char *base, const gam_manifest *g, int idx);

#endif /* GAME_SCENE_H */
