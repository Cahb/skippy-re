/* Engine-agnostic gameplay sim (LOGIC layer). 2D grid + grid-step movement with
 * render-pos interpolation, per verified reference/gameplay_mechanics.md. No
 * rendering here — main.c reads render pos/facing and draws. Movement basis is
 * the verified facing (see reference coordinate model): forward = -X, left = -Y. */
#ifndef SIM_H
#define SIM_H

#include <stdbool.h>
#include "formats/jjm.h"

/* 4 grid directions (delta below). */
typedef enum { DIR_NX = 0, DIR_PX, DIR_NY, DIR_PY } sim_dir;

/* one-shot events raised during a tick, each tagged with the world position it
 * happened at (for positional SFX). main drains the queue, plays the matching
 * theme sound (see thm "Sound<event>" map), then resets num_events. */
enum {
	SIM_EV_STEP = 0,   /* player hopped/turned a cell (MoveJJ) */
	SIM_EV_CRYSTAL,    /* crystal collected (Crystal) */
	SIM_EV_PICKUP,     /* heart/bonus collected */
	SIM_EV_SPLAT,      /* player died by hard landing (SplatJJ) */
	SIM_EV_FALL,       /* player fell off the map (FallJJ = scream) */
	SIM_EV_CAUGHT,     /* player caught by an enemy */
	SIM_EV_ESTEP,      /* an enemy hopped/turned (MoveCatcher) */
	SIM_EV_EXITOPEN,   /* crystals just reached the level requirement (exit opens) */
	SIM_EV_GLUE,       /* an entity got stuck on a glue tile (Glue) */
	SIM_EV_SLIDE,      /* the player started sliding across ice (IceSliding) */
	SIM_EV_BOMBDROP,   /* a bomb was just placed (start its tick loop) */
	SIM_EV_EXPLODE,    /* a bomb detonated (ExplosionBomb + FX burst) */
	SIM_EV_OBSTACLE,   /* a destructible obstacle (0x17) was blown open (shatter it + Obstacle sound) */
	SIM_EV_ENEMYDIE,   /* an enemy was killed by a blast (ExplosionCatcher death cry) */
	SIM_EV_JUMPPAD,    /* the player launched off a jump pad (MoveJumpPad) */
	SIM_EV_ETHROW,     /* a thrower hopped (MoveThrower — distinct from the catcher's MoveCatcher) */
	SIM_EV_DESTRUCT,   /* a DestructField (0x0d) collapsed into a hole (DestructStart) */
	SIM_EV_REGEN,      /* a DestructField regenerated back to solid (DestructRegen) */
	SIM_EV_TELEPORT,   /* the player used a teleporter (Teleporter sound) */
	SIM_EV_SWITCH,     /* a bridge switch was toggled (theme Switch.wav — not a pickup) */
};

typedef struct { unsigned char type; float x, y, z; } sim_event;
#define SIM_MAX_EVENTS 32

/* per-level play statistics (zeroed by sim_init's memset): the score inputs the
 * original tallies in its god object (reference/found_structs_ghidra.h:539-566).
 * Point multipliers and the exact semantics of the tile-use counters are RE-gated
 * on the draw_summary_screen (0x435420) decompile — every increment site is a
 * provisional one-liner until then. */
typedef struct {
	unsigned short jumps;          /* player cell hops ("jumping energy") */
	unsigned short crystals;       /* collected (pickup byte 1) */
	unsigned short collected[16];  /* bonus pickups by pickup byte (5..13 used) */
	unsigned short teleports;      /* player teleporter warps */
	unsigned short obstacles;      /* rocks blown open */
	unsigned short enemies;        /* enemies destroyed (the x50 summary row) */
	unsigned short bridges;        /* bridge switches the player landed on */
	unsigned short glue;           /* glue pads stuck to */
	unsigned short slides, ices;   /* chute runs / ice runs entered */
} sim_stats;

typedef struct {
	int     cx, cy;        /* current grid cell */
	sim_dir facing;        /* way John faces */

	bool    moving;        /* mid grid-step */
	int     fx, fy;        /* step source cell */
	int     tx, ty;        /* step dest cell */
	float   move_t;        /* 0..1 progress across the step */

	bool    turning;       /* mid 90-deg turn (plays the turn clip) */
	float   turn_t;        /* 0..1 progress of the turn */
	float   yaw_from, yaw_to;
	int     turn_dir;      /* -1 = left, +1 = right (selects turn clip) */

	bool    falling;       /* airborne (hopped off a ledge) */
	float   fall_v;        /* vertical velocity (units/s, downward negative) */
	float   fall_from;     /* height hopped off at; land only on surfaces <= this */
	bool    fall_fatal;    /* drop was >1 block -> death on landing (splat) */

	float   glue_t;        /* seconds still stuck on a glue tile (can't move; glue anim) */
	signed char buf_move;  /* one-deep input buffer (player): dir held during a hop, chained on
	                          arrival with the overshoot so held movement has no 1-frame gap; -1 none */
	bool    sliding;       /* mid ice-slide run (auto-steps; slide anim) */
	bool    slide_ice;     /* slide kind: ice run (true) vs rutsche chute (false) — picks anim/SFX */
	sim_dir slide_dir;     /* the ice-run direction = entry momentum (kept across the run) */
	bool    gliding;       /* paraglide deployed: slow forward-drifting descent, safe landing */

	/* jumppad launch (RE skippy_tick 0x0e): phase 1 = ballistic rise IN PLACE to the pad's
	 * clip_rule height; phase 2 (launch_gliding) = a normal forward hop-GLIDE one cell in
	 * the momentum dir, descending to its floor — NOT a teleport at the apex. */
	bool    launching, launch_gliding;
	float   launch_t, launch_v0, launch_z0, launch_zT;
	int     launch_fx, launch_fy;   /* landing cell (one forward) */

	/* enemy death (bomb): plays a death anim while harmless, then is removed for
	 * good — does NOT respawn (even when the player dies and restarts the level). */
	float   dying_t;       /* >0 = death anim playing (harmless) */
	bool    removed;       /* permanently gone this level attempt */

	/* riding a mover: carried by a platform/elevator (positional attach). */
	signed char ride_kind; /* 0 = none, 1 = platform, 2 = elevator */
	int     ride_idx;      /* index into the matching mover array */

	bool    alive;
	int     crystals;      /* collected */
	bool    is_thrower;    /* enemy variant: chases like a catcher but slower, lobs bombs,
	                        * and is HARMLESS on contact (dangerous only via its bombs) */
	float   throw_cd;      /* thrower bomb cooldown (seconds) */

	/* interpolated render pose (world Z-up; x/y are cell-centered) */
	float   rx, ry, rz;
	float   yaw_deg;       /* facing as a Z yaw for the mesh */
	float   fwd_x, fwd_y;  /* current facing unit vector (for the follow cam) */
	int     home_x, home_y;/* spawn cell (enemy respawn) */
	int     tele_lock_x, tele_lock_y;/* teleporter just warped ONTO (don't bounce back); per-entity so enemies teleport too */
} sim_player;             /* also used for enemy entities */

#define SIM_MAX_ENEMIES  16
#define SIM_MAX_BOMBS    8
#define SIM_MAX_FACTORIES 8

/* a stationary thrower (pickup byte 3): lobs a bomb toward John when he's within a
 * tile (Chebyshev 1), on a cooldown. Shares the bomb pipeline. */
/* enemy factory (pickup byte 100): spawns a chaser catcher every `interval` seconds,
 * up to `cap` alive at once. `next` counts down to the next spawn. */
typedef struct { int cx, cy; float interval, next; int cap; } sim_factory;

#define SIM_MAX_BRIDGES 16
/* extend/retract bridge (tile 0x12 = X-axis, 0x13 = Y). Deploys planks over the void
 * cells from its origin along `axis` in `step` direction; a same-id switch toggles it. */
typedef struct {
	int  ox, oy, oz;    /* origin cell + plank height */
	signed char axis;   /* 0 = X, 1 = Y */
	signed char step;   /* +1 / -1 along the axis */
	int  span;          /* number of plank cells when fully extended */
	int  filled;        /* planks currently placed (0..span) */
	bool extended, animating, target_ext;
	float anim_t;
	int  id;            /* clip_rule - 1 (matches its switch) */
} sim_bridge;

#define SIM_MAX_MOVERS 32

/* Horizontal moving platform (tile 0x0a = X axis, 0x0b = Y axis): slides from its
 * home cell across void cells to the far end, waits, slides back (ping-pong). One
 * cell/200ms, 1.5s wait. It's the floor at its current position; riders are carried. */
typedef struct {
	int   hx, hy, hz;      /* home cell + height */
	int   dx, dy;          /* axis unit: (1,0) for 0x0a, (0,1) for 0x0b */
	int   range;           /* cells from home to the far end (scanned at load) */
	float t;               /* current offset 0..range along the axis */
	int   phase;           /* 0 = waiting at an end, 1 = moving out, 2 = returning */
	float wait;            /* time waited at the current end */
} sim_platform;

/* Vertical moving elevator (tile 0x09): oscillates between z0 (home) and z1 (target
 * = clip_rule). Same speed/wait as platforms; carries a rider. */
typedef struct {
	int   cx, cy;
	int   z0, z1;
	float z;               /* current height */
	int   phase;           /* 0 = wait, 1 = up, 2 = down */
	float wait;
} sim_elevator;

/* a live bomb (player- or thrower-thrown): glides forward a few cells during a 2s
 * fuse, then a 3x3 blast on its z-plane. See reference bomb_tick RE. */
typedef struct {
	bool    alive;
	int     cx, cy;        /* current grid cell */
	int     bz;            /* z-plane of the blast (cell top at spawn) */
	sim_dir facing;
	bool    moving;        /* mid forward glide-hop */
	int     fx, fy;        /* hop source cell */
	float   move_t;        /* 0..1 hop progress */
	float   rx, ry, rz;    /* render pos */
	float   fuse;          /* seconds since spawn */
	bool    boomed;        /* blast already applied */
	int     ride_plat;     /* platform it's riding (-1 = none); horizontal carry */
	bool    launching;     /* mid jump-pad launch (ballistic rise, then forward hop) */
	float   launch_t, launch_z0, launch_zT, launch_v0;
	int     launch_fx, launch_fy;
	bool    sliding;       /* mid ice/chute run: auto-hops a cell per hop, like the player */
	bool    falling;       /* slid/launched past an edge: dropping to the floor below */
	float   fall_v, fall_from;
	bool    from_player;   /* player-thrown (awards a crystal per enemy killed); not thrower-lobbed */
} sim_bomb;

typedef struct {
	const jjm_level *lvl;
	sim_player       p;
	sim_player       enemies[SIM_MAX_ENEMIES];        /* catchers (pickup byte 2) */
	int              num_enemies;
	float            move_dur;                        /* seconds per cell */
	float            turn_dur;                        /* seconds per 90-deg turn */
	bool             picked[JJM_MAX_DIM][JJM_MAX_DIM]; /* pickups already collected (crystals + bonuses) */
	bool             glue_used[JJM_MAX_DIM][JJM_MAX_DIM]; /* glue tiles already spent (single-use pads) */
	float            time_bonus;                       /* transient: +seconds from Time pickups, drained by main */
	int              crystals_total;                  /* crystals present at start */
	int              inv[16];                         /* bonuses collected this level, indexed by pickup byte */
	int              hp_gain;                          /* transient: hearts collected since main last drained it */
	float            enemy_freeze_t;                   /* seconds enemies stay frozen (Freeze pickup; stacks +5) */
	float            speed_t;                          /* seconds the player hops 2x faster (Speed pickup; stacks +5) */
	float            slow_t;                           /* seconds the player hops 2x slower (Slowdown debuff) */
	float            inverse_t;                         /* seconds the player's controls are inverted (Inverse debuff) */
	float            protect_t;                         /* seconds the player is invulnerable (Protection pickup) */
	float            bomb_cd;                           /* player bomb-release cooldown remaining */
	sim_bomb         bombs[SIM_MAX_BOMBS];              /* live bombs in flight / fusing */
	int              num_bombs;
	sim_factory      factories[SIM_MAX_FACTORIES];      /* enemy spawners (pickup byte 100) */
	int              num_factories;
	sim_platform     platforms[SIM_MAX_MOVERS];         /* horizontal movers (0x0a/0x0b) */
	int              num_platforms;
	sim_elevator     elevators[SIM_MAX_MOVERS];         /* vertical movers (0x09) */
	int              num_elevators;
	bool             obstacle_gone[JJM_MAX_DIM][JJM_MAX_DIM]; /* destructible (type 0x17) tiles blown open */
	/* DestructField (type 0x0d): step arms it -> collapses to a hole -> regenerates.
	 * destruct_t < 0 = idle (re-armable); >= 0 = seconds since armed. */
	float            destruct_t[JJM_MAX_DIM][JJM_MAX_DIM];
	bool             destruct_open[JJM_MAX_DIM][JJM_MAX_DIM]; /* currently collapsed (a hole) */
	/* teleporters (0x0f): dest cell per tile (-1 = none), paired by clip_rule at load. */
	short            tele_dx[JJM_MAX_DIM][JJM_MAX_DIM], tele_dy[JJM_MAX_DIM][JJM_MAX_DIM];
	int              tele_lock_x, tele_lock_y;         /* the tile just warped onto (don't bounce back) */
	/* switch-toggled bridges (0x12/0x13); deployed planks live in a plank overlay
	 * (plank_z >= 0 = a walkable plank at that height on an otherwise-void cell). */
	sim_bridge       bridges[SIM_MAX_BRIDGES];
	int              num_bridges;
	signed char      plank_z[JJM_MAX_DIM][JJM_MAX_DIM];
	sim_stats        stats;                             /* per-level score inputs */

	sim_event        events[SIM_MAX_EVENTS];           /* SFX events raised this frame */
	int              num_events;                        /* main drains + resets to 0 */
} sim_state;

/* place John at the spawn tile, facing -X, alive. */
void sim_init(sim_state *s, const jjm_level *lvl);

/* TANK controls (faithful to the original): forward/back step along the current
 * facing; turn rotates the facing 90 deg. All ignored if busy/dead. */
void sim_forward(sim_state *s);
void sim_back(sim_state *s);
void sim_turn(sim_state *s, int cw);   /* cw>0 = right, cw<0 = left */

/* teleport John onto cell (x,y) (debug); clears motion, alive, on the surface. */
void sim_warp(sim_state *s, int x, int y);

/* release a bomb (player): if ammo (inv[9]) > 0 and off cooldown, drop one on the
 * player's tile facing forward. Costs 1 ammo. */
void sim_drop_bomb(sim_state *s);

/* respawn the player at the spawn tile after a death (keeps crystals/pickups).
 * The player death path leaves p.alive=false and does NOT auto-respawn — the
 * caller (main) drives the death sequence, then calls this to continue. */
void sim_respawn_player(sim_state *s);

/* advance the sim by dt seconds. */
void sim_tick(sim_state *s, float dt);

#endif /* SIM_H */
