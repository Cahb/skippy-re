/* .jjm level (tile grid) loader — ported 1:1 from reference/jjm_format.md
 * (original parser: level_manager_load_map_file @0x41f190). File formats are
 * read byte-exact; runtime structs are clean 64-bit (field meanings preserved). */
#ifndef JJM_H
#define JJM_H

#include <stdint.h>
#include <stdbool.h>

#define JJM_MAX_DIM 100          /* engine clears a fixed 100x100 grid */
#define JJM_NAME_LEN 128

/* SkippyTileType values (the cell `type` byte). Stairs occupy 5..8 (four facings). */
enum jjm_tile_type {
	TT_VOID      = 0,
	TT_FLOOR     = 1,
	TT_GLUE      = 2,     /* sticky pad */
	TT_SPAWN     = 3,     /* player start (1 per level) */
	TT_EXIT      = 4,     /* level exit (1 per level) */
	TT_STAIR_LO  = 5,     /* 5..8 = stair, one per facing (yaw = (type-5)*90) */
	TT_STAIR_PX  = 5,     /* +X */
	TT_STAIR_PY  = 6,     /* +Y */
	TT_STAIR_NX  = 7,     /* -X */
	TT_STAIR_NY  = 8,     /* -Y */
	TT_STAIR_HI  = 8,
	TT_ELEVATOR  = 0x09,  /* 9  — vertical moving elevator (z_pos..clip_rule) */
	TT_MOVER_Y   = 0x0a,  /* 10 — horizontal mover along +Y (rewrite grid) */
	TT_MOVER_X   = 0x0b,  /* 11 — horizontal mover along +X */
	TT_DESTRUCT  = 0x0d,  /* 13 — collapsing field: step arms it, it drops to a hole, then regens */
	TT_JUMPPAD   = 0x0e,  /* 14 — ballistic launch pad to clip_rule height */
	TT_TELEPORT  = 0x0f,  /* 15 — teleporter: clip_rule = pair id; warps to the matching tile */
	TT_SLIDE     = 0x10,  /* 16 — forced-dir slide (rutsche): clip_rule 1-4 = slide dir; descends */
	TT_SWITCH    = 0x11,  /* 17 — switch: toggles the bridge whose id = clip_rule-1 */
	TT_BRIDGE_Y  = 0x12,  /* 18 — extend/retract bridge along Y (clip_rule = bridge id) */
	TT_BRIDGE_X  = 0x13,  /* 19 — extend/retract bridge along X (verified: 0x13 extends +X) */
	TT_PLANK     = 0x14,  /* 20 — deployed bridge plank (walkable; written by the bridge tick) */
	TT_ICE       = 0x15,  /* 21 — slippery slide */
	TT_DECOR     = 0x16,  /* 22 — permanently solid decoration */
	TT_OBSTACLE  = 0x17,  /* 23 — destructible rock (solid until bombed) */
};

/* pickup_type bytes: the item/spawn on a cell. 5..13 are the bonus/debuff
 * pickups (they index sim inv[]/stats.collected[]). */
enum {
	PU_CRYSTAL   = 1,
	PU_CATCHER   = 2,     /* catcher enemy spawn */
	PU_THROWER   = 3,     /* thrower enemy spawn */
	PU_PARAGLIDE = 5,
	PU_TIME      = 6,
	PU_HEART     = 7,
	PU_FREEZE    = 8,
	PU_BOMBS     = 9,     /* ammo box */
	PU_SPEED     = 10,
	PU_INVERSE   = 11,    /* reversed-controls debuff */
	PU_SLOWDOWN  = 12,    /* slow-hops debuff */
	PU_PROTECT   = 13,
	PU_FACTORY   = 100,   /* enemy factory spawn */
	PU_SURPRISE  = 255,   /* surprise box: rolls a random 5..12 on landing */
};

/* One grid cell. First 4 bytes come straight from the file; the original then
 * keeps ~123 bytes of runtime state per cell (see reference/gameplay_mechanics.md
 * §7) — the rewrite holds that separately in the sim layer, not here. */
typedef struct {
    uint8_t z_pos;        /* height */
    uint8_t type;         /* SkippyTileType: 1=floor, 3=player START/spawn (1/level), 4=EXIT (1/level),
                             2=glue, 5-8=ladder, 0x0c=platform, 0x0e=jumppad, 0x0f=teleport/lose,
                             0x10=forced-dir, 0x11=switch, 0x15=ice, 0x16=stair, 0x17=obstacle, ...
                             (LevelReport "Fields" = count(type1) + count(type4) — verified) */
    uint8_t clip_rule;    /* walkability / collision */
    uint8_t pickup_type;  /* item on this cell (1=crystal,7=heart,...) */
} jjm_tile;

typedef struct {
    uint8_t  dim_x, dim_y;                        /* grid dims (file byte1->dim_x, byte0->dim_y) */
    jjm_tile tiles[JJM_MAX_DIM][JJM_MAX_DIM];     /* [x][y]; border row/col forced to 0 */
    uint32_t crystals_needed;                     /* NCryst — needed to complete */
    uint32_t time_limit;                          /* par time (report "Time" = time_limit*50/100) */
    char     world[JJM_NAME_LEN];                 /* theme name -> themes\<world>.thm */
    char     level_display_name[JJM_NAME_LEN];    /* human title ("Levelname" in LevelReport) */
    char     meta_name[JJM_NAME_LEN];             /* parsed, unused by the engine */
    uint32_t is_bonus;                            /* bonus-level flag */
} jjm_level;

/* Load a .jjm from an exact path. Returns false on open/read failure. */
bool jjm_load(const char *path, jjm_level *out);

#endif /* JJM_H */
