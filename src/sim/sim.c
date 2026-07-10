#include "sim/sim.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* per-direction grid deltas, indexed by sim_dir */
static const int DX[4] = { -1, +1, 0, 0 };   /* NX, PX, NY, PY */
static const int DY[4] = { 0, 0, -1, +1 };

/* tuning (feel; move_dur ~ verified move_interval, gravity per §3) */
#define MOVE_DUR    0.20f   /* seconds per cell */
#define TURN_DUR    0.20f   /* seconds per 90-deg turn */
#define MAX_STEP_UP 1       /* can climb at most +1 z per step; higher = wall */
#define GRAVITY     10.81f  /* u/s^2 (½g = 5.405, verified) */
#define FALL_V0     (-3.0f) /* seed downward velocity (verified) */
#define DEATH_Z     (-4.0f) /* fall below this world-z -> respawn */
#define JOHN_YAW    90.0f   /* nose-first: mesh points along facing (raw nose = -Y) */
#define ENEMY_SLOW  2.5f    /* enemies move/turn this much slower than the player */
#define GLUE_DUR    3.0f    /* seconds stuck on a glue tile (~3000ms, verified) */
#define BOMB_FUSE   2.0f    /* fly forward this long, then detonate (RE: 2000ms) */
#define BOMB_GONE   2.6f    /* remove the spent bomb (RE: 2600ms) */
#define BOMB_CD     2.0f    /* player bomb-release cooldown (RE: 2000ms) */
#define BOMB_THROW  0.42f   /* parabolic-throw duration to the target tile */
#define BOMB_ARC    0.55f   /* peak height of the throw arc */
#define ENEMY_DEATH_DUR 0.6f /* enemy death-anim length before it's removed */
#define MOVER_SPEED 5.0f     /* 1 cell / 200ms (verified 0.005 cells/ms) */
#define MOVER_WAIT  1.5f     /* pause at each end of the ping-pong (verified 1500ms) */
#define JUMPPAD_DUR 0.6f     /* ballistic launch time to the target height */
#define JUMPPAD_BIAS 7.2f    /* RE arc bias (0x45d6c0): v0 = sqrt((zT-z0+bias)*2g) */
#define JUMPPAD_G2   19.62f  /* 2*9.81 (0x45d6bc) */
#define JUMPPAD_HG   4.905f  /* 9.81/2 (0x45d6b8) — arc gravity term */
#define DESTRUCT_ARM   1.5f  /* step -> collapse delay (RE: 1500ms) */
#define DESTRUCT_REGEN 5.0f  /* arm -> regenerate (RE: 5000ms; hole open 3.5s) */
#define GLIDE_TRIGGER  2.0f  /* deploy paraglide once fallen more than this (RE: >2) */
#define GLIDE_DESCENT  4.0f  /* paraglide sink rate (u/s) — RE: pos_z -= dt_ms*0.004 */

typedef enum { CK_FLOOR, CK_SOLID, CK_VOID } cell_kind;

/* A moving platform/elevator provides floor at its CURRENT position, overriding
 * the static grid (mover-home + void path cells). true + *z if one is here now.
 * Platforms count only when near grid-aligned (so you can't board/leave mid-slide);
 * elevators always occupy their cell (at their live z). */
static bool dynamic_floor(const sim_state *s, int x, int y, float *z)
{
	for (int i = 0; i < s->num_platforms; i++) {
		const sim_platform *p = &s->platforms[i];
		int rt = (int)(p->t + 0.5f);
		if (p->hx + p->dx * rt == x && p->hy + p->dy * rt == y
		    && (p->t - (float)rt) < 0.3f && (p->t - (float)rt) > -0.3f) {
			if (z) *z = (float)p->hz;
			return true;
		}
	}
	for (int i = 0; i < s->num_elevators; i++) {
		const sim_elevator *e = &s->elevators[i];
		if (e->cx == x && e->cy == y) {
			if (z) *z = e->z;
			return true;
		}
	}
	return false;
}

/* dynamic-aware integer height of a cell (for step dz): mover surface if present. */
static int tile_z(const sim_state *s, int x, int y)
{
	float dz;
	if (dynamic_floor(s, x, y, &dz))
		return (int)(dz + 0.5f);
	if (x < 0 || y < 0 || x >= s->lvl->dim_x || y >= s->lvl->dim_y)
		return 0;
	return s->lvl->tiles[x][y].z_pos;
}

static cell_kind kind_at(const sim_state *s, int x, int y)
{
	const jjm_level *l = s->lvl;
	if (x < 0 || y < 0 || x >= l->dim_x || y >= l->dim_y)
		return CK_VOID;
	uint8_t t = l->tiles[x][y].type;
	if (t == TT_DECOR)           /* permanently solid decoration */
		return CK_SOLID;
	if (t == TT_OBSTACLE)        /* destructible: blocks until a blast clears it */
		return s->obstacle_gone[x][y] ? CK_FLOOR : CK_SOLID;
	if (t == TT_DESTRUCT)        /* collapsing field: a hole while collapsed, else floor */
		return s->destruct_open[x][y] ? CK_VOID : CK_FLOOR;
	/* void + mover-home + bridge-anchor cells are floor only when something's here now
	 * (a deployed plank/force-field, or a platform/elevator). The bridge anchor (0x12/0x13)
	 * is itself invisible + a gap until its bridge is activated. */
	if (t == TT_VOID || t == TT_ELEVATOR || t == TT_MOVER_X || t == TT_MOVER_Y
	    || t == TT_BRIDGE_X || t == TT_BRIDGE_Y)
		return (s->plank_z[x][y] >= 0 || dynamic_floor(s, x, y, NULL)) ? CK_FLOOR : CK_VOID;
	return CK_FLOOR;
}

/* the surface an entity stands on: a mover's live z if present, else the static
 * tile top (stair tiles 5-8 ride at z_pos + 0.5). */
static float cell_top(const sim_state *s, int x, int y)
{
	float dz;
	if (dynamic_floor(s, x, y, &dz))
		return dz;
	const jjm_level *l = s->lvl;
	if (x < 0 || y < 0 || x >= l->dim_x || y >= l->dim_y)
		return 0.0f;
	if (s->plank_z[x][y] >= 0)              /* a deployed bridge plank */
		return (float)s->plank_z[x][y];
	uint8_t t = l->tiles[x][y].type;
	float z = (float)l->tiles[x][y].z_pos;
	if (t >= TT_STAIR_LO && t <= TT_STAIR_HI)
		z += 0.5f;
	return z;
}

static bool is_stair(const jjm_level *l, int x, int y)
{
	if (x < 0 || y < 0 || x >= l->dim_x || y >= l->dim_y)
		return false;
	uint8_t t = l->tiles[x][y].type;
	return t >= TT_STAIR_LO && t <= TT_STAIR_HI;
}

/* STATIC ground at a cell (ignores movers) + its top z. A fall lands only on this,
 * so a platform sliding under a falling entity never "catches" it — you board a
 * mover by stepping onto it, not by dropping onto it. */
static bool land_floor(const sim_state *s, int x, int y, float *z)
{
	const jjm_level *l = s->lvl;
	if (x < 0 || y < 0 || x >= l->dim_x || y >= l->dim_y)
		return false;
	uint8_t t = l->tiles[x][y].type;
	if (s->plank_z[x][y] >= 0) {            /* a deployed bridge plank is solid ground */
		if (z) *z = (float)s->plank_z[x][y];
		return true;
	}
	if (t == TT_VOID || t == TT_DECOR || t == TT_ELEVATOR || t == TT_MOVER_X || t == TT_MOVER_Y)
		return false;
	if (t == TT_BRIDGE_X || t == TT_BRIDGE_Y)
		return false;   /* a retracted anchor is a GAP — its deployed plank (above) is the
		                 * floor. Without this a faller "lands" here, the idle check sees
		                 * void and re-drops him: an endless land/fall loop re-screaming
		                 * SIM_EV_FALL every cycle (the double/looping scream). */
	if (t == TT_OBSTACLE && !s->obstacle_gone[x][y])
		return false;
	if (t == TT_DESTRUCT && s->destruct_open[x][y])   /* collapsed hole: nothing to land on */
		return false;
	float zz = (float)l->tiles[x][y].z_pos;
	if (t >= TT_STAIR_LO && t <= TT_STAIR_HI)
		zz += 0.5f;
	if (z)
		*z = zz;
	return true;
}

/* A move (fx,fy)->(tx,ty) is a "step" (follow surface, no fall) when: flat
 * tile-to-tile; or a stair move — along the stair axis (5/7=X, 6/8=Y) within
 * +-1, or a +1 climb onto the tile at the stair's top from the side. */
static bool is_step(const sim_state *s, int fx, int fy, int tx, int ty)
{
	const jjm_level *l = s->lvl;
	if (kind_at(s, tx, ty) != CK_FLOOR)
		return false;
	int dz = tile_z(s, tx, ty) - tile_z(s, fx, fy);   /* mover-aware heights */
	if (dz == 0)
		return true;   /* a same-height step is always legal (incl. onto a stair base) */
	bool sf = is_stair(l, fx, fy), st = is_stair(l, tx, ty);
	if (!sf && !st)
		return false;  /* non-stairs can't change height in one step */
	if (sf && st)
		return dz >= -MAX_STEP_UP && dz <= MAX_STEP_UP;   /* stairs interconnect */
	int axis = (fx != tx) ? 0 : 1;
	int saxis = sf ? ((l->tiles[fx][fy].type - TT_STAIR_LO) & 1) : ((l->tiles[tx][ty].type - TT_STAIR_LO) & 1);
	if (saxis == axis)
		return dz >= -MAX_STEP_UP && dz <= MAX_STEP_UP;
	return dz == MAX_STEP_UP;
}

/* where a slide hop's surface ends up: flat onto any floor, or DOWN a rutsche ramp
 * (<=2 — the chute mesh itself descends, so the run chains down it). A LOWER plain
 * tile is NOT followed: ice slabs are flat, so a run exiting onto one keeps its
 * height across the edge and falls there, instead of gliding diagonally through
 * the slab models. */
static bool slide_surface(const sim_state *s, int fx, int fy, int tx, int ty, float *z)
{
	float ft;
	if (!land_floor(s, tx, ty, &ft))
		return false;
	float z0 = cell_top(s, fx, fy);
	bool chute = s->lvl->tiles[tx][ty].type == TT_SLIDE;   /* in bounds: land_floor passed */
	if (ft > z0 + 0.01f)
		return false;                     /* higher lip: not a surface to follow */
	if (ft >= z0 - 0.01f || (chute && z0 - ft <= 2.01f)) {
		if (z)
			*z = ft;
		return true;
	}
	return false;
}

static float dir_yaw(sim_dir d)
{
	float a = atan2f((float)DY[d], (float)DX[d]) * (180.0f / 3.14159265f);
	return a + JOHN_YAW;
}

static void ride_attach(sim_state *s, sim_player *p);   /* mover boarding (defined below) */

/* spawn facing from a spawn tile's clip byte: 1=TOP(-X) 2=RIGHT(+Y) 3=DOWN(+X) 4=LEFT(-Y) */
static const sim_dir SPAWN_FACE[5] = { DIR_NX, DIR_NX, DIR_PY, DIR_PX, DIR_NY };
static const sim_dir OPP[4] = { DIR_PX, DIR_NX, DIR_PY, DIR_NY };
static const sim_dir CW[4]  = { DIR_PY, DIR_NY, DIR_NX, DIR_PX };  /* turn right */
static const sim_dir CCW[4] = { DIR_NY, DIR_PY, DIR_PX, DIR_NX };  /* turn left  */

/* queue an SFX event at a world position (dropped if the queue is full). */
static void raise_event(sim_state *s, unsigned char type, float x, float y, float z)
{
	if (s->num_events < SIM_MAX_EVENTS)
		s->events[s->num_events++] = (sim_event){ type, x, y, z };
}

/* place entity `p` at rest on cell (x,y) facing f; records it as its home. */
static void place_entity(sim_state *s, sim_player *p, int x, int y, sim_dir f)
{
	p->cx = x;
	p->cy = y;
	p->home_x = x;
	p->home_y = y;
	p->tele_lock_x = p->tele_lock_y = -1;   /* not fresh off a teleporter */
	p->buf_move = -1;
	p->facing = f;
	p->fwd_x = DX[f];
	p->fwd_y = DY[f];
	p->moving = false;
	p->turning = false;
	p->turn_t = 0.0f;
	p->falling = false;
	p->fall_v = 0.0f;
	p->glue_t = 0.0f;
	p->sliding = false;
	p->gliding = false;
	p->launching = false;
	p->ride_kind = 0;
	p->alive = true;
	p->rx = x + 0.5f;
	p->ry = y + 0.5f;
	p->rz = cell_top(s, x, y);
	p->yaw_deg = dir_yaw(f);
}

static void place_at_spawn(sim_state *s)   /* player -> the spawn tile (type 3) */
{
	int sx = s->lvl->dim_x / 2, sy = s->lvl->dim_y / 2, sclip = 1;
	for (int x = 0; x < s->lvl->dim_x; x++)
		for (int y = 0; y < s->lvl->dim_y; y++)
			if (s->lvl->tiles[x][y].type == TT_SPAWN) { sx = x; sy = y; sclip = s->lvl->tiles[x][y].clip_rule; }
	sim_dir f = (sclip >= 1 && sclip <= 4) ? SPAWN_FACE[sclip] : DIR_NX;
	place_entity(s, &s->p, sx, sy, f);
}

static void init_bridges(sim_state *s, const jjm_level *lvl)
{
	/* register bridges (0x12 = X-axis, 0x13 = Y): scan the void gap from the origin to
	 * measure the span. id = clip_rule - 1 (matches the switch that toggles it). */
	for (int x = 0; x < lvl->dim_x; x++)
		for (int y = 0; y < lvl->dim_y; y++) {
			uint8_t tt = lvl->tiles[x][y].type;
			if ((tt != TT_BRIDGE_X && tt != TT_BRIDGE_Y) || lvl->tiles[x][y].clip_rule == 0
			    || s->num_bridges >= SIM_MAX_BRIDGES)
				continue;
			/* TT_BRIDGE_X (0x13) deploys along grid X, TT_BRIDGE_Y (0x12) along grid Y —
			 * the type defines were swapped (0x13 verified to extend +X). */
			int ax = (tt == TT_BRIDGE_X) ? 0 : 1;
			int dxa = ax == 0 ? 1 : 0, dya = ax == 0 ? 0 : 1;
			int step = 0, span = 0;
			for (int sgn = 1; sgn >= -1 && span == 0; sgn -= 2) {   /* try +axis then -axis */
				int k = 0;
				while (1) {
					int nx = x + dxa * sgn * (k + 1), ny = y + dya * sgn * (k + 1);
					if (nx < 0 || ny < 0 || nx >= lvl->dim_x || ny >= lvl->dim_y
					    || lvl->tiles[nx][ny].type != TT_VOID)
						break;
					k++;
				}
				if (k > 0) { step = sgn; span = k; }
			}
			s->bridges[s->num_bridges++] = (sim_bridge){
				.ox = x, .oy = y, .oz = lvl->tiles[x][y].z_pos,
				.axis = (signed char)ax, .step = (signed char)step, .span = span,
				.id = lvl->tiles[x][y].clip_rule - 1 };
		}
}

static void init_teleport_pairs(sim_state *s, const jjm_level *lvl)
{
	/* pair teleporters (0x0f) by clip_rule: first-found match, 1:1, bidirectional. */
	for (int x = 0; x < lvl->dim_x; x++)
		for (int y = 0; y < lvl->dim_y; y++) {
			if (lvl->tiles[x][y].type != TT_TELEPORT || s->tele_dx[x][y] >= 0
			    || lvl->tiles[x][y].clip_rule == 0)
				continue;
			int id = lvl->tiles[x][y].clip_rule, found = 0;
			for (int bx = 0; bx < lvl->dim_x && !found; bx++)
				for (int by = 0; by < lvl->dim_y && !found; by++) {
					if ((bx == x && by == y) || lvl->tiles[bx][by].type != TT_TELEPORT
					    || s->tele_dx[bx][by] >= 0 || lvl->tiles[bx][by].clip_rule != id)
						continue;
					s->tele_dx[x][y] = bx; s->tele_dy[x][y] = by;   /* link both ways */
					s->tele_dx[bx][by] = x; s->tele_dy[bx][by] = y;
					found = 1;
				}
		}
}

/* behaviour type + search cap from the spawn tile's clip_rule (re_g6_enemy_ai.md §3):
 * the map authors each enemy's AI; clip >= 0x65 is a model/skin variant -> plain
 * chaser. The cap is the pathfinder's pop budget = the de-facto aggro range. */
static void ai_assign(sim_player *e, uint8_t clip)
{
	static const unsigned short caps[8] = { 50, 400, 100, 150, 50, 50, 50, 150 };
	e->ai_type = clip >= 0x65 ? 0 : clip;
	e->ai_cap  = e->ai_type < 8 ? caps[e->ai_type] : 50;
}

static void init_spawns(sim_state *s, const jjm_level *lvl)
{
	/* spawn catcher enemies at pickup-byte 2 cells; throwers at pickup-byte 3.
	 * Throwers are enemies too (same chase AI) — just slower + bomb-lobbing + harmless
	 * on contact — so they live in the enemies[] array with is_thrower set. */
	for (int x = 0; x < lvl->dim_x; x++)
		for (int y = 0; y < lvl->dim_y; y++) {
			if (lvl->tiles[x][y].pickup_type == PU_CATCHER && s->num_enemies < SIM_MAX_ENEMIES) {
				sim_player *c = &s->enemies[s->num_enemies++];
				place_entity(s, c, x, y, DIR_NX);
				ai_assign(c, lvl->tiles[x][y].clip_rule);
			}
			if (lvl->tiles[x][y].pickup_type == PU_THROWER && s->num_enemies < SIM_MAX_ENEMIES) {
				sim_player *th = &s->enemies[s->num_enemies++];
				place_entity(s, th, x, y, DIR_NX);
				th->is_thrower = true;
				ai_assign(th, lvl->tiles[x][y].clip_rule);
			}
			if (lvl->tiles[x][y].pickup_type == PU_FACTORY && s->num_factories < SIM_MAX_FACTORIES) {
				int cl = lvl->tiles[x][y].clip_rule;
				s->factories[s->num_factories] = (sim_factory){
					.cx = x, .cy = y,
					.interval = cl > 0 && cl < 100 ? (float)cl : 5.0f,  /* clip = seconds/spawn */
					.next = (float)s->num_factories,   /* stagger first fire by index seconds */
					.cap = 5 };                        /* RE: at most 5 alive at once */
				s->num_factories++;
			}

			uint8_t tt = lvl->tiles[x][y].type;
			if (tt == TT_ELEVATOR && s->num_elevators < SIM_MAX_MOVERS) {
				int z0 = lvl->tiles[x][y].z_pos, z1 = lvl->tiles[x][y].clip_rule;
				if (z1 < z0) { int t = z0; z0 = z1; z1 = t; }   /* z0 = bottom */
				s->elevators[s->num_elevators++] =
					(sim_elevator){ .cx = x, .cy = y, .z0 = z0, .z1 = z1, .z = (float)z0 };
			}
			if ((tt == TT_MOVER_Y || tt == TT_MOVER_X) && s->num_platforms < SIM_MAX_MOVERS) {
				int dx = (tt == TT_MOVER_X), dy = (tt == TT_MOVER_Y);
				/* scan along the axis through void cells until the first solid tile */
				int r = 0, nx = x + dx, ny = y + dy;
				while (nx >= 0 && ny >= 0 && nx < lvl->dim_x && ny < lvl->dim_y
				       && lvl->tiles[nx][ny].type == TT_VOID) {
					r++; nx += dx; ny += dy;
				}
				s->platforms[s->num_platforms++] = (sim_platform){
					.hx = x, .hy = y, .hz = lvl->tiles[x][y].z_pos,
					.dx = dx, .dy = dy, .range = r };
			}
		}
}

void sim_init(sim_state *s, const jjm_level *lvl)
{
	memset(s, 0, sizeof *s);
	s->lvl = lvl;
	s->move_dur = MOVE_DUR;
	s->turn_dur = TURN_DUR;
	for (int x = 0; x < lvl->dim_x; x++)
		for (int y = 0; y < lvl->dim_y; y++) {
			if (lvl->tiles[x][y].pickup_type == PU_CRYSTAL)
				s->crystals_total++;
			s->destruct_t[x][y] = -1.0f;   /* DestructField idle (re-armable) */
			s->tele_dx[x][y] = s->tele_dy[x][y] = -1;
			s->plank_z[x][y] = -1;         /* no bridge plank yet */
		}
	s->tele_lock_x = s->tele_lock_y = -1;
	init_bridges(s, lvl);
	init_teleport_pairs(s, lvl);
	place_at_spawn(s);
	init_spawns(s, lvl);
}

/* start a grid step in direction d (facing unchanged); only obstacles block. */
/* a thrower is a SOLID cannon (unlike catchers, which catch on contact): nothing may
 * share its cell. true if a thrower (other than `self`) stands on or is moving into (cx,cy). */
static bool thrower_occupies(const sim_state *s, int cx, int cy, const sim_player *self)
{
	for (int i = 0; i < s->num_enemies; i++) {
		const sim_player *e = &s->enemies[i];
		if (e == self || !e->is_thrower || e->removed || e->dying_t > 0.0f)
			continue;
		if ((e->cx == cx && e->cy == cy) || (e->moving && e->tx == cx && e->ty == cy))
			return true;
	}
	return false;
}

/* enemies are solid to EACH OTHER: a catcher can't hop into a cell another live
 * enemy stands on or is already hopping into — it waits until the cell frees
 * (no more frame-perfect stacks of 2-3 catchers blended into one tile). */
static bool enemy_occupies(const sim_state *s, int cx, int cy, const sim_player *self)
{
	for (int i = 0; i < s->num_enemies; i++) {
		const sim_player *e = &s->enemies[i];
		if (e == self || e->removed)
			continue;
		if ((e->cx == cx && e->cy == cy) || (e->moving && e->tx == cx && e->ty == cy))
			return true;
	}
	return false;
}

static void step_entity(sim_state *s, sim_player *p, sim_dir d)
{
	if (!p->alive || p->moving || p->falling || p->turning || p->glue_t > 0.0f || p->launching)
		return;   /* locked mid jumppad launch until it lands */
	/* paraglide: an airborne grid hop to any in-bounds cell (over gaps, onto ledges) — the
	 * normal floor/step gates don't apply mid-air; the glide handler sinks + lands. */
	if (p->gliding) {
		int gx2 = p->cx + DX[d], gy2 = p->cy + DY[d];
		if (gx2 < 0 || gy2 < 0 || gx2 >= s->lvl->dim_x || gy2 >= s->lvl->dim_y)
			return;
		p->moving = true; p->move_t = 0.0f;
		p->fx = p->cx; p->fy = p->cy; p->tx = gx2; p->ty = gy2;
		return;
	}
	int gx = p->cx + DX[d], gy = p->cy + DY[d];
	if (thrower_occupies(s, gx, gy, p))
		return;                                    /* can't walk into/through a solid thrower */
	if (p != &s->p && enemy_occupies(s, gx, gy, p))
		return;                                    /* enemy-vs-enemy: blocked, wait */
	if (p->is_thrower) {
		if (gx == s->p.cx && gy == s->p.cy)
			return;                                /* a thrower won't shove onto John's cell */
		if (tile_z(s, p->cx, p->cy) - tile_z(s, gx, gy) >= 2)
			return;   /* throwers won't leap off a ledge (>=2 drop): a capped static threat */
	}
	if (p->ride_kind == 1 && p->ride_idx < s->num_platforms) {   /* on a platform */
		sim_platform *pl = &s->platforms[p->ride_idx];
		float frac = pl->t - (float)(int)(pl->t + 0.5f);
		if (frac > 0.2f || frac < -0.2f)
			return;                        /* mid-slide: can only step when grid-aligned */
	}
	int nx = p->cx + DX[d], ny = p->cy + DY[d];
	cell_kind k = kind_at(s, nx, ny);
	if (k == CK_SOLID)
		return;
	/* a +1 step-up onto a plain (non-stair) floor is REFUSED — the entity stays put,
	 * it does NOT hop-and-fall (RE @0x43a31f: move cancelled). Only stairs climb +1, and
	 * a bigger (+2) gap still hops-and-falls past into the void. Movers keep their own
	 * board rule (skip the check when a mover occupies the target). */
	if (k == CK_FLOOR && !dynamic_floor(s, nx, ny, NULL)
	    && tile_z(s, nx, ny) - tile_z(s, p->cx, p->cy) == 1
	    && !is_step(s, p->cx, p->cy, nx, ny))
		return;
	p->ride_kind = 0;                          /* hopping off any mover */
	p->moving = true;
	p->move_t = 0.0f;
	if (p == &s->p)
		s->stats.jumps++;                  /* every player cell hop = jumping energy */
	p->fx = p->cx;
	p->fy = p->cy;
	p->tx = nx;
	p->ty = ny;
	if (p->sliding)    /* each ice tile crossed slips (not a hop) — the hop/stomp SFX itself
	                      now fires on LANDING (arrive_entity), not here at push-off. */
		raise_event(s, SIM_EV_SLIDE, p->rx, p->ry, p->rz);
}

/* turn 90 deg; mesh yaw interpolates over turn_dur while the turn clip plays. */
static void turn_entity(sim_state *s, sim_player *p, int cw)
{
	(void)s;   /* the turn's SFX moved to its completion (tick_entity), so s is unused here */
	if (!p->alive || p->moving || p->falling || p->turning || p->glue_t > 0.0f || p->launching)
		return;   /* no turning mid jumppad launch */
	p->yaw_from = p->yaw_deg;
	p->facing = cw > 0 ? CW[p->facing] : CCW[p->facing];
	p->fwd_x = DX[p->facing];
	p->fwd_y = DY[p->facing];
	float to = dir_yaw(p->facing);
	while (to - p->yaw_from > 180.0f)  to -= 360.0f;
	while (to - p->yaw_from < -180.0f) to += 360.0f;
	p->yaw_to = to;
	p->turn_dir = cw;
	p->turn_t = 0.0f;
	p->turning = true;
	/* the hop-turn's stomp fires on completion (landing), not here at the start. */
}

/* record the held direction each frame (for the seamless chain on arrival) + start a hop now
 * if idle. buf_move is cleared at the end of sim_tick, so it only reflects this frame's input. */
void sim_forward(sim_state *s) { s->p.buf_move = s->p.facing;      step_entity(s, &s->p, s->p.facing); }
void sim_back(sim_state *s)    { s->p.buf_move = OPP[s->p.facing]; step_entity(s, &s->p, OPP[s->p.facing]); }
void sim_turn(sim_state *s, int cw) { turn_entity(s, &s->p, cw); }

static void respawn_entity(sim_state *s, sim_player *p, bool is_player)
{
	if (is_player)
		place_at_spawn(s);
	else
		place_entity(s, p, p->home_x, p->home_y, p->facing);
}

/* player death: stop and mark dead — do NOT reposition. main runs the death
 * sequence (angel rise + prompt) then calls sim_respawn_player / reloads. */
static void kill_player(sim_state *s)
{
	s->p.alive = false;
	s->p.moving = s->p.falling = s->p.turning = false;
	s->p.ride_kind = 0;   /* a dead player is not carried by any mover */
}

void sim_respawn_player(sim_state *s)
{
	place_at_spawn(s);
	/* death cancels any active power-ups/debuffs + live bombs — the board resets.
	 * (Already-destroyed obstacles stay open.) */
	s->enemy_freeze_t = 0.0f;
	s->speed_t = 0.0f;
	s->slow_t = 0.0f;
	s->inverse_t = 0.0f;
	s->protect_t = 0.0f;
	s->num_bombs = 0;
	s->bomb_cd = 0.0f;
	/* respawn the bomb (ammunition) pickups + reset ammo, so a level that needs
	 * bombs to clear destructibles can't be softlocked by spending them then dying.
	 * Time pickups respawn too: the retry gets a full clock, so the level's time
	 * economy resets with it. */
	s->inv[PU_BOMBS] = 0;
	for (int x = 0; x < s->lvl->dim_x; x++)
		for (int y = 0; y < s->lvl->dim_y; y++) {
			int pt = s->lvl->tiles[x][y].pickup_type;
			if (pt == PU_BOMBS || pt == PU_TIME)
				s->picked[x][y] = false;
		}
	/* like the original: every still-alive catcher teleports back to its spawn cell
	 * (no post-respawn invuln). Bomb-killed catchers stay GONE — they don't return. */
	for (int i = 0; i < s->num_enemies; i++) {
		sim_player *e = &s->enemies[i];
		if (e->removed || e->dying_t > 0.0f)
			continue;
		place_entity(s, e, e->home_x, e->home_y, DIR_NX);
	}
}

/* collect whatever pickup sits on the player's current cell (once). Crystals
 * bump the counter; hearts grant an HP (drained by main); other bonuses just
 * tally into inv[] for now (effects TBD). Enemy spawns (2/3) and specials are
 * never "collected". */
static void try_collect(sim_state *s, sim_player *p)
{
	int x = p->cx, y = p->cy;
	if (s->picked[x][y])
		return;
	int t = s->lvl->tiles[x][y].pickup_type;
	if (t == PU_PARAGLIDE && (p->falling || p->gliding))
		return;   /* chute refills only collect grounded (RE 0x41fcb0: is_falling gate;
		           * everything else collects mid-air — glide-through crystal runs) */
	if (t == PU_SURPRISE) {
		/* the surprise box rolls a random bonus/debuff the moment the player
		 * lands, then collects it in the same pass (RE 0x41fcb0: byte =
		 * rand()*8/0x7fff + 5, rand 15-bit -> uniform paraglide..slowdown,
		 * protection only on the exact-max edge). */
		t = (rand() & 0x7fff) * 8 / 0x7fff + PU_PARAGLIDE;
	}
	if (t == PU_CRYSTAL) {
		s->picked[x][y] = true;
		p->crystals++;
		s->stats.crystals++;
		raise_event(s, SIM_EV_CRYSTAL, p->rx, p->ry, p->rz);
		if (s->lvl->crystals_needed > 0 && p->crystals == (int)s->lvl->crystals_needed)
			raise_event(s, SIM_EV_EXITOPEN, p->rx, p->ry, p->rz);   /* exit just opened */
	} else if (t >= PU_PARAGLIDE && t <= PU_PROTECT) {   /* all bonuses/debuffs (paraglide..protection) */
		s->picked[x][y] = true;
		s->inv[t]++;
		s->stats.collected[t]++;      /* inv[] is consumed; this tally isn't */
		raise_event(s, SIM_EV_PICKUP, p->rx, p->ry, p->rz);
		if (t == PU_HEART)
			s->hp_gain++;              /* heart -> extra HP */
		if (t == PU_BOMBS)
			s->inv[PU_BOMBS] += 2;            /* box of bombs = +3 ammo total (the ++ above gave 1) */
		if (t == PU_TIME)
			s->time_bonus += 5.0f;     /* time: +5s to the level clock (main drains this) */
		if (t == PU_FREEZE)
			s->enemy_freeze_t += 5.0f; /* freeze: +5s to the enemy-freeze window (stacks) */
		if (t == PU_SPEED)
			s->speed_t += 10.0f;        /* speed: 10s of 2x-faster hops (RE _DAT_45d430=10000) (stacks) */
		if (t == PU_INVERSE)
			s->inverse_t += 10.0f;     /* inverse: 10s of reversed controls (debuff) */
		if (t == PU_SLOWDOWN)
			s->slow_t += 10.0f;        /* slowdown: 10s of 2x-slower hops (debuff) */
		if (t == PU_PROTECT)
			s->protect_t += 10.0f;     /* protection: 10s invulnerable (bombs + catches) */
	}
}

/* 3x3 blast centred on the bomb's tile (single z-plane, ±1z reach): destroy
 * destructible obstacles, kill the player (unless protected) and any catchers. */
static void bomb_explode(sim_state *s, sim_bomb *b)
{
	int bz = (int)cell_top(s, b->cx, b->cy);
	raise_event(s, SIM_EV_EXPLODE, b->cx + 0.5f, b->cy + 0.5f, (float)bz);
	for (int dx = -1; dx <= 1; dx++)
		for (int dy = -1; dy <= 1; dy++) {
			int ex = b->cx + dx, ey = b->cy + dy;
			if (ex < 0 || ey < 0 || ex >= s->lvl->dim_x || ey >= s->lvl->dim_y)
				continue;
			if (s->lvl->tiles[ex][ey].type == TT_OBSTACLE && !s->obstacle_gone[ex][ey]
			    && abs((int)s->lvl->tiles[ex][ey].z_pos - bz) <= 1) {
				s->obstacle_gone[ex][ey] = true;   /* rock blown open -> passable */
				s->stats.obstacles++;
				raise_event(s, SIM_EV_OBSTACLE, ex + 0.5f, ey + 0.5f,
				            (float)s->lvl->tiles[ex][ey].z_pos);   /* main shatters its mesh */
			}
			if (s->p.alive && s->protect_t <= 0.0f && s->p.cx == ex && s->p.cy == ey
			    && fabsf(s->p.rz - (float)bz) <= 1.01f)
				kill_player(s);                    /* explosion death (angel in main) */
			for (int i = 0; i < s->num_enemies; i++) {
				sim_player *e = &s->enemies[i];
				if (e->is_thrower)
					continue;              /* throwers are immune to blasts (no ExplosionThrower sound) */
				if (!e->removed && e->dying_t <= 0.0f && e->cx == ex && e->cy == ey
				    && fabsf(e->rz - (float)bz) <= 1.01f) {
					e->dying_t = ENEMY_DEATH_DUR;  /* play death anim, then remove for good */
					e->moving = e->turning = false;
					s->stats.enemies++;
					if (b->from_player) {          /* reward: each enemy the player bombs = 1 crystal */
						int need = (int)s->lvl->crystals_needed;
						s->p.crystals++;
						s->stats.crystals++;
						raise_event(s, SIM_EV_CRYSTAL, e->rx, e->ry, e->rz);
						if (need > 0 && s->p.crystals >= need && s->p.crystals - 1 < need)
							raise_event(s, SIM_EV_EXITOPEN, e->rx, e->ry, e->rz);
					}
					raise_event(s, SIM_EV_ENEMYDIE, e->rx, e->ry, e->rz);  /* ExplosionCatcher cry */
				}
			}
		}
}

/* advance one live bomb: a small parabolic throw to the target tile, then it rests
 * and fuses; at 2s it detonates (3x3), and it's removed at 2.6s. */
/* start a bomb's jump-pad launch (mirrors the player's try_launch_jumppad): a bomb that
 * lands on a pad rises ballistically then hops one cell forward, like the player. */
static bool bomb_launch_jumppad(sim_state *s, sim_bomb *b)
{
	if (b->launching || b->cx < 0 || b->cy < 0
	    || b->cx >= s->lvl->dim_x || b->cy >= s->lvl->dim_y
	    || s->lvl->tiles[b->cx][b->cy].type != TT_JUMPPAD)
		return false;
	float z0 = cell_top(s, b->cx, b->cy);
	float zT = (float)s->lvl->tiles[b->cx][b->cy].clip_rule;
	if (zT <= z0)
		return false;
	b->launching = true;
	b->launch_t = 0.0f;
	b->launch_z0 = z0;
	b->launch_zT = zT;
	b->launch_v0 = sqrtf((zT - z0 + JUMPPAD_BIAS) * JUMPPAD_G2);
	b->launch_fx = b->cx + DX[b->facing];
	b->launch_fy = b->cy + DY[b->facing];
	raise_event(s, SIM_EV_JUMPPAD, b->rx, b->ry, b->rz);
	return true;
}

/* attach the bomb to a platform if one is at its cell (persistent horizontal carry). */
static void bomb_attach_platform(sim_state *s, sim_bomb *b)
{
	for (int i = 0; i < s->num_platforms; i++) {
		sim_platform *pl = &s->platforms[i];
		int rt = (int)(pl->t + 0.5f);
		if (pl->hx + pl->dx * rt == b->cx && pl->hy + pl->dy * rt == b->cy) {
			b->ride_plat = i;
			return;
		}
	}
}

/* mirror of the player's ice/rutsche landing for a live bomb: ice keeps it gliding one
 * cell per hop in its momentum dir (a chute forces the tile's dir) until the first
 * non-slide tile; a wall/obstacle ahead settles it on the ice. */
static void bomb_slide(sim_state *s, sim_bomb *b)
{
	if (b->cx < 0 || b->cy < 0 || b->cx >= s->lvl->dim_x || b->cy >= s->lvl->dim_y) {
		b->sliding = false;
		return;
	}
	uint8_t tt = s->lvl->tiles[b->cx][b->cy].type;
	if (tt != TT_ICE && tt != TT_SLIDE) {
		b->sliding = false;               /* ordinary ground ends the run */
		return;
	}
	int mdx = b->cx - b->fx, mdy = b->cy - b->fy;
	int fc = s->lvl->tiles[b->cx][b->cy].clip_rule;
	sim_dir d = (tt == TT_SLIDE) ? ((fc >= 1 && fc <= 4) ? SPAWN_FACE[fc] : b->facing)
	          : mdx < 0 ? DIR_NX : mdx > 0 ? DIR_PX
	          : mdy < 0 ? DIR_NY : mdy > 0 ? DIR_PY : b->facing;
	int nx = b->cx + DX[d], ny = b->cy + DY[d];
	cell_kind k = kind_at(s, nx, ny);
	if (k == CK_SOLID
	    || (k == CK_FLOOR && tile_z(s, nx, ny) > tile_z(s, b->cx, b->cy)
	        && !is_step(s, b->cx, b->cy, nx, ny))) {
		b->sliding = false;               /* wall ahead: settle on the ice */
		return;
	}
	b->sliding = true;                    /* advance a cell (void ahead = a doomed hop) */
	b->facing = d;
	b->ride_plat = -1;
	b->fx = b->cx; b->fy = b->cy;
	b->cx = nx;    b->cy = ny;
	b->moving = true;
	b->move_t = 0.0f;
}

/* a bomb hop just completed: drop off a run's edge, else chain a jump-pad / board a
 * platform / continue an ice run. */
static void bomb_land(sim_state *s, sim_bomb *b)
{
	if (b->sliding && !slide_surface(s, b->fx, b->fy, b->cx, b->cy, NULL)) {
		b->sliding = false;               /* slid past the edge: fall from the run's height */
		b->falling = true;
		b->fall_v = FALL_V0;
		b->fall_from = b->rz;
		return;
	}
	if (bomb_launch_jumppad(s, b))
		return;
	bomb_attach_platform(s, b);
	bomb_slide(s, b);
}

static void bomb_update(sim_state *s, sim_bomb *b, float dt)
{
	b->fuse += dt;
	if (b->launching) {   /* jump-pad: rise along the arc, then commit one cell forward */
		b->launch_t += dt;
		float t = b->launch_t;
		b->rx = b->cx + 0.5f;
		b->ry = b->cy + 0.5f;
		b->rz = b->launch_z0 + b->launch_v0 * t - JUMPPAD_HG * t * t;
		if (b->rz >= b->launch_zT && t > 0.0f) {
			b->launching = false;
			int fx = b->launch_fx, fy = b->launch_fy;
			bool floor = fx >= 0 && fy >= 0 && fx < s->lvl->dim_x && fy < s->lvl->dim_y
			             && kind_at(s, fx, fy) == CK_FLOOR;
			b->cx = fx; b->cy = fy;
			b->rx = fx + 0.5f; b->ry = fy + 0.5f;
			if (floor) {
				b->rz = cell_top(s, fx, fy);
				bomb_land(s, b);          /* chained pad / platform / ice run */
			} else {                          /* void ahead: fall from the glide height */
				b->rz = b->launch_zT;
				b->falling = true;
				b->fall_v = 0.0f;
				b->fall_from = b->launch_zT;
			}
		}
	} else if (b->moving) {
		b->move_t += dt / BOMB_THROW;
		bool done = b->move_t >= 1.0f;
		if (done)
			b->move_t = 1.0f;
		float t = b->move_t;
		float z0 = cell_top(s, b->fx, b->fy), z1 = cell_top(s, b->cx, b->cy);
		if (b->sliding) {   /* an ice hop glides flat, a chute hop rides the ramp down;
		                       any other exit keeps the run's height, then falls */
			float lz;
			z1 = slide_surface(s, b->fx, b->fy, b->cx, b->cy, &lz) ? lz : z0;
		}
		b->rx = (b->fx + 0.5f) * (1.0f - t) + (b->cx + 0.5f) * t;
		b->ry = (b->fy + 0.5f) * (1.0f - t) + (b->cy + 0.5f) * t;
		b->rz = z0 * (1.0f - t) + z1 * t
		        + (b->sliding ? 0.0f : BOMB_ARC * sinf(3.14159265f * t));  /* arc */
		if (done) {         /* position settled first: bomb_land reads rz (fall_from)
		                       and may re-arm the next slide hop */
			b->moving = false;
			bomb_land(s, b);   /* edge-drop / pad / platform / ice run */
		}
	} else if (b->falling) {
		b->fall_v -= GRAVITY * dt;
		b->rz += b->fall_v * dt;
		b->rx = b->cx + 0.5f;
		b->ry = b->cy + 0.5f;
		float ct;
		if (land_floor(s, b->cx, b->cy, &ct) && ct <= b->fall_from + 0.01f && b->rz <= ct) {
			b->rz = ct;
			b->falling = false;
			/* fell onto the floor: pad launch (no momentum) or board a platform —
			 * the player's fall-landing rule; a fall does NOT restart an ice run. */
			if (!bomb_launch_jumppad(s, b))
				bomb_attach_platform(s, b);
		} else if (b->rz < DEATH_Z) {
			b->alive = false;   /* tumbled into the abyss: gone (no blast) */
		}
	} else if (b->ride_plat >= 0 && b->ride_plat < s->num_platforms) {
		/* carried by its platform: cell + render pos follow it (puzzle mechanic) */
		sim_platform *pl = &s->platforms[b->ride_plat];
		int rt = (int)(pl->t + 0.5f);
		b->cx = pl->hx + pl->dx * rt;
		b->cy = pl->hy + pl->dy * rt;
		b->rx = pl->hx + 0.5f + pl->dx * pl->t;
		b->ry = pl->hy + 0.5f + pl->dy * pl->t;
		b->rz = (float)pl->hz;
	} else {
		b->rx = b->cx + 0.5f;
		b->ry = b->cy + 0.5f;
		b->rz = cell_top(s, b->cx, b->cy);   /* elevator z follows via cell_top */
	}
	if (!b->boomed && b->fuse >= BOMB_FUSE) {
		b->boomed = true;
		b->moving = false;
		b->sliding = false;   /* the fuse doesn't care that it's mid-run/mid-drop */
		b->falling = false;
		bomb_explode(s, b);
	}
	if (b->fuse >= BOMB_GONE)
		b->alive = false;
}

/* place a bomb at (cx,cy) thrown toward `facing`: lands on the tile in front if
 * that's a floor, else on the origin tile. Shared by player + thrower. */
static void spawn_bomb_at(sim_state *s, int cx, int cy, sim_dir facing, bool from_player)
{
	if (s->num_bombs >= SIM_MAX_BOMBS)
		return;
	int tx = cx + DX[facing], ty = cy + DY[facing];
	bool front = kind_at(s, tx, ty) == CK_FLOOR;   /* else it'd fall / hit a wall */
	sim_bomb *b = &s->bombs[s->num_bombs++];
	*b = (sim_bomb){ .alive = true, .facing = facing, .moving = true, .ride_plat = -1,
	                 .fx = cx, .fy = cy, .from_player = from_player,
	                 .cx = front ? tx : cx, .cy = front ? ty : cy };
	b->rx = cx + 0.5f;
	b->ry = cy + 0.5f;
	b->rz = cell_top(s, cx, cy);
	raise_event(s, SIM_EV_BOMBDROP, b->rx, b->ry, b->rz);
}

void sim_drop_bomb(sim_state *s)
{
	if (s->bomb_cd > 0.0f || s->inv[PU_BOMBS] <= 0 || !s->p.alive || s->p.falling
	    || s->p.moving || s->p.turning || s->p.glue_t > 0.0f)
		return;
	s->inv[PU_BOMBS]--;                       /* consume one ammo */
	s->bomb_cd = BOMB_CD;
	spawn_bomb_at(s, s->p.cx, s->p.cy, s->p.facing, true);
}

/* a thrower enemy lobs a bomb at John when he's within one tile (Chebyshev-1), 2s cd.
 * It keeps chasing while it throws (doesn't stop). Called per-thrower from the enemy loop. */
static void thrower_throw(sim_state *s, sim_player *t, float dt, bool frozen)
{
	if (t->throw_cd > 0.0f)
		t->throw_cd -= dt;
	if (frozen || t->throw_cd > 0.0f || t->removed || t->dying_t > 0.0f
	    || !s->p.alive || s->p.falling)
		return;
	int dx = s->p.cx - t->cx, dy = s->p.cy - t->cy;
	if (abs(dx) > 1 || abs(dy) > 1)
		return;                        /* only when John is adjacent (incl. diagonal) */
	sim_dir f = (abs(dx) >= abs(dy)) ? (dx < 0 ? DIR_NX : DIR_PX)
	                                 : (dy < 0 ? DIR_NY : DIR_PY);
	spawn_bomb_at(s, t->cx, t->cy, f, false);
	t->throw_cd = BOMB_CD;
}

/* enemy factories (pickup 100): spawn a chaser catcher every `interval` s, capped at
 * `cap` live enemies, when the spawn cell is clear. */
static void factory_tick(sim_state *s, float dt, bool frozen)
{
	if (frozen)
		return;
	for (int i = 0; i < s->num_factories; i++) {
		sim_factory *f = &s->factories[i];
		f->next -= dt;
		if (f->next > 0.0f)
			continue;
		f->next = f->interval;
		/* the cap limits how many of THIS factory's spawns are alive AT ONCE (home =
		 * the factory cell) — it is NOT a lifetime total: kills free capacity and the
		 * belt keeps rolling (EnemyFactory needs 20 kills off a cap-5 factory). */
		int alive = 0;
		for (int e = 0; e < s->num_enemies; e++)
			if (!s->enemies[e].removed
			    && s->enemies[e].home_x == f->cx && s->enemies[e].home_y == f->cy)
				alive++;
		if (alive >= f->cap)
			continue;
		bool occupied = false;                         /* don't spawn onto a live enemy */
		for (int e = 0; e < s->num_enemies && !occupied; e++)
			occupied = !s->enemies[e].removed && s->enemies[e].cx == f->cx && s->enemies[e].cy == f->cy;
		if (occupied)
			continue;
		sim_player *slot = NULL;                       /* recycle a bombed corpse's slot */
		for (int e = 0; e < s->num_enemies && !slot; e++)
			if (s->enemies[e].removed && s->enemies[e].dying_t <= 0.0f)
				slot = &s->enemies[e];
		if (!slot && s->num_enemies < SIM_MAX_ENEMIES)
			slot = &s->enemies[s->num_enemies++];
		if (!slot)
			continue;                                  /* board truly full of live enemies */
		memset(slot, 0, sizeof *slot);                 /* clear removed/dying/thrower state */
		place_entity(s, slot, f->cx, f->cy, DIR_NX);
		ai_assign(slot, 0);                            /* factory spawns are plain chasers */
	}
}

#define BRIDGE_CELL 0.1f   /* seconds per plank cell (RE: ~100ms/cell) */

/* the world cell of a bridge's k-th plank (k = 1..span) along its axis/step. */
static void bridge_cell(const sim_bridge *b, int k, int *cx, int *cy)
{
	*cx = b->ox + (b->axis == 0 ? b->step * k : 0);
	*cy = b->oy + (b->axis == 1 ? b->step * k : 0);
}

/* a switch flips its bridge: start extending (retracted) or retracting (extended). */
static void bridge_toggle(sim_state *s, int id)
{
	for (int i = 0; i < s->num_bridges; i++) {
		sim_bridge *b = &s->bridges[i];
		if (b->id != id || b->animating || b->span <= 0)
			continue;
		b->animating = true;
		b->target_ext = !b->extended;
		b->anim_t = 0.0f;
	}
}

/* bridges deploy/retract one plank cell every BRIDGE_CELL seconds while animating. */
static void bridges_tick(sim_state *s, float dt)
{
	for (int i = 0; i < s->num_bridges; i++) {
		sim_bridge *b = &s->bridges[i];
		if (!b->animating)
			continue;
		b->anim_t += dt;
		int want = b->target_ext ? b->span : 0;
		while (b->filled != want && b->anim_t >= BRIDGE_CELL) {
			b->anim_t -= BRIDGE_CELL;
			int cx, cy;
			if (b->target_ext) {              /* extend: add the next plank */
				b->filled++;
				bridge_cell(b, b->filled, &cx, &cy);
				if (cx >= 0 && cy >= 0 && cx < s->lvl->dim_x && cy < s->lvl->dim_y)
					s->plank_z[cx][cy] = (signed char)b->oz;
			} else {                          /* retract: remove the far plank */
				bridge_cell(b, b->filled, &cx, &cy);
				if (cx >= 0 && cy >= 0 && cx < s->lvl->dim_x && cy < s->lvl->dim_y)
					s->plank_z[cx][cy] = -1;
				b->filled--;
			}
		}
		if (b->filled == want) {
			b->animating = false;
			b->extended = b->target_ext;
		}
	}
	/* the anchor cell (0x12/0x13) is part of the bridge: give it a plank (walkable +
	 * force-field render) whenever the bridge is extended/extending, none when retracted
	 * (so the anchor tile is invisible + a gap until the switch activates it). */
	for (int i = 0; i < s->num_bridges; i++) {
		sim_bridge *b = &s->bridges[i];
		bool active = b->extended || (b->animating && b->target_ext);
		if (b->ox >= 0 && b->oy >= 0 && b->ox < s->lvl->dim_x && b->oy < s->lvl->dim_y)
			s->plank_z[b->ox][b->oy] = active ? (signed char)b->oz : -1;
	}
}

/* DestructField (0x0d) tiles: an entity standing on one arms it; DESTRUCT_ARM later it
 * collapses to a hole (anything on it then falls), and at DESTRUCT_REGEN from arming it
 * regenerates. Arming is one-shot (leaving doesn't cancel) and re-armable once idle. */
static void destruct_tick(sim_state *s, float dt, bool frozen)
{
	const jjm_level *l = s->lvl;
	for (int x = 0; x < l->dim_x; x++)
		for (int y = 0; y < l->dim_y; y++) {
			if (l->tiles[x][y].type != TT_DESTRUCT)
				continue;
			float *t = &s->destruct_t[x][y];
			if (*t < 0.0f) {   /* idle: arm when an entity occupies the tile */
				if (frozen)
					continue;
				bool on = s->p.alive && s->p.cx == x && s->p.cy == y;
				for (int i = 0; i < s->num_enemies && !on; i++)   /* catchers + throwers */
					on = !s->enemies[i].removed && s->enemies[i].cx == x && s->enemies[i].cy == y;
				if (on)
					*t = 0.0f;
				continue;
			}
			*t += dt;
			if (!s->destruct_open[x][y] && *t >= DESTRUCT_ARM) {
				s->destruct_open[x][y] = true;   /* collapse -> hole */
				raise_event(s, SIM_EV_DESTRUCT, x + 0.5f, y + 0.5f, (float)l->tiles[x][y].z_pos);
			}
			if (*t >= DESTRUCT_REGEN) {
				s->destruct_open[x][y] = false;  /* regenerate -> solid, idle again */
				*t = -1.0f;
				raise_event(s, SIM_EV_REGEN, x + 0.5f, y + 0.5f, (float)l->tiles[x][y].z_pos);
			}
		}
}

/* advance the ping-pong movers (1 cell/200ms, 1.5s wait at each end). */
static void movers_tick(sim_state *s, float dt)
{
	for (int i = 0; i < s->num_platforms; i++) {
		sim_platform *p = &s->platforms[i];
		if (p->range == 0)
			continue;
		if (p->phase == 0) {
			p->wait += dt;
			if (p->wait >= MOVER_WAIT) { p->wait = 0.0f; p->phase = (p->t < 0.5f) ? 1 : 2; }
		} else if (p->phase == 1) {
			p->t += MOVER_SPEED * dt;
			if (p->t >= (float)p->range) { p->t = (float)p->range; p->phase = 0; }
		} else {
			p->t -= MOVER_SPEED * dt;
			if (p->t <= 0.0f) { p->t = 0.0f; p->phase = 0; }
		}
	}
	for (int i = 0; i < s->num_elevators; i++) {
		sim_elevator *e = &s->elevators[i];
		if (e->z1 <= e->z0)
			continue;
		if (e->phase == 0) {
			e->wait += dt;
			if (e->wait >= MOVER_WAIT) { e->wait = 0.0f; e->phase = (e->z <= e->z0 + 0.01f) ? 1 : 2; }
		} else if (e->phase == 1) {
			e->z += MOVER_SPEED * dt;
			if (e->z >= (float)e->z1) { e->z = (float)e->z1; e->phase = 0; }
		} else {
			e->z -= MOVER_SPEED * dt;
			if (e->z <= (float)e->z0) { e->z = (float)e->z0; e->phase = 0; }
		}
	}
}

/* attach a resting entity to a mover currently under its cell (called on landing).
 * The attach PERSISTS (ride_kind/idx) so the mover keeps carrying it across cells. */
static void ride_attach(sim_state *s, sim_player *p)
{
	p->ride_kind = 0;
	for (int i = 0; i < s->num_platforms; i++) {
		sim_platform *pl = &s->platforms[i];
		int rt = (int)(pl->t + 0.5f);
		if (pl->hx + pl->dx * rt == p->cx && pl->hy + pl->dy * rt == p->cy) {
			p->ride_kind = 1; p->ride_idx = i;
			return;
		}
	}
	for (int i = 0; i < s->num_elevators; i++)
		if (s->elevators[i].cx == p->cx && s->elevators[i].cy == p->cy) {
			p->ride_kind = 2; p->ride_idx = i;
			return;
		}
}

/* carry the entity WITH its attached mover each frame (positional, per RE). A
 * platform drags the entity's logical cell along so it can step off onto ground. */
static void ride_carry(sim_state *s, sim_player *p)
{
	/* a hop LEAVES the mover (don't carry mid-hop), and a faller isn't riding — but a
	 * TURN is rotate-in-place, so keep carrying through it (else the platform slides out
	 * from under the turning rider, who drifts to the stale cell then snaps back). */
	if (p->moving || p->falling)
		return;
	if (p->ride_kind == 1 && p->ride_idx < s->num_platforms) {
		sim_platform *pl = &s->platforms[p->ride_idx];
		int rt = (int)(pl->t + 0.5f);
		p->cx = pl->hx + pl->dx * rt;
		p->cy = pl->hy + pl->dy * rt;
		p->rx = pl->hx + 0.5f + pl->dx * pl->t;
		p->ry = pl->hy + 0.5f + pl->dy * pl->t;
		p->rz = (float)pl->hz;
	} else if (p->ride_kind == 2 && p->ride_idx < s->num_elevators) {
		p->rz = s->elevators[p->ride_idx].z;
	}
}

/* teleporter: warp to the paired tile if standing on one (not the tile just warped ONTO).
 * Per-entity lock (tele_lock) so you don't bounce straight back; cleared on step-off. Works
 * for enemies too (lured catchers warp — trap levels). Returns true if it warped. */
static bool try_teleport(sim_state *s, sim_player *p)
{
	bool on_lock = p->cx == p->tele_lock_x && p->cy == p->tele_lock_y;
	if (s->lvl->tiles[p->cx][p->cy].type == TT_TELEPORT && !on_lock
	    && s->tele_dx[p->cx][p->cy] >= 0) {
		int dx = s->tele_dx[p->cx][p->cy], dy = s->tele_dy[p->cx][p->cy];
		p->cx = dx; p->cy = dy;
		p->rx = dx + 0.5f; p->ry = dy + 0.5f; p->rz = cell_top(s, dx, dy);
		p->tele_lock_x = dx; p->tele_lock_y = dy;
		if (p == &s->p)
			s->stats.teleports++;
		raise_event(s, SIM_EV_TELEPORT, p->rx, p->ry, p->rz);
		return true;
	}
	if (!on_lock)
		p->tele_lock_x = p->tele_lock_y = -1;   /* stepped off the destination: re-arm */
	return false;
}

/* jump-pad launch: rise ballistically to the tile's clip_rule height, then hop one cell
 * forward (committed in tick_entity's launching branch). Returns true if it launched.
 * Called BOTH on arrival (so a held-forward step can't skip the pad) and from the idle
 * check (spawned/placed directly on a pad). */
static bool try_launch_jumppad(sim_state *s, sim_player *p, bool is_player, int dx, int dy)
{
	if (p->launching || s->lvl->tiles[p->cx][p->cy].type != TT_JUMPPAD)
		return false;
	float z0 = cell_top(s, p->cx, p->cy);
	float zT = (float)s->lvl->tiles[p->cx][p->cy].clip_rule;
	if (zT <= z0)
		return false;
	if (dx == 0 && dy == 0) { dx = DX[p->facing]; dy = DY[p->facing]; }  /* fall/idle: use facing */
	p->launching = true;
	p->launch_gliding = false;
	p->launch_t = 0.0f;
	p->launch_z0 = z0;
	p->launch_zT = zT;
	p->launch_v0 = sqrtf((zT - z0 + JUMPPAD_BIAS) * JUMPPAD_G2);
	p->launch_fx = p->cx + dx;
	p->launch_fy = p->cy + dy;
	(void)is_player;   /* the pad's boing is a WORLD sound — it fires for enemies too */
	raise_event(s, SIM_EV_JUMPPAD, p->rx, p->ry, p->rz);   /* MoveJumpPad */
	return true;
}

/* effects when an entity comes to REST on a cell — after a hop, an ice slide, OR a
 * jump-pad glide landing: stomp SFX, mover attach, pickups, and the teleporter / jump-pad /
 * switch / glue / ice triggers. Shared so a jump-pad glide that ends on a switch/teleporter/
 * pad/ice behaves exactly like a walked arrival. `was_sliding` suppresses the stomp (a slide
 * slips, it doesn't stomp) and marks a continuing ice run. Momentum = p->cx-p->fx. */
static void land_effects(sim_state *s, sim_player *p, bool is_player, bool was_sliding)
{
	p->rz = cell_top(s, p->cx, p->cy);
	/* a slide/ice run ONLY continues if this landed tile is itself slide/ice; any other
	 * exit (teleporter, jump-pad, switch, plain ground) ends it. Clear here — the ice/slide
	 * branches below re-set it — so you don't stay stuck in the slide anim/SFX after warping. */
	p->sliding = false;
	if (!was_sliding)
		raise_event(s, (p == &s->p) ? SIM_EV_STEP
		               : (p->is_thrower ? SIM_EV_ETHROW : SIM_EV_ESTEP), p->rx, p->ry, p->rz);
	ride_attach(s, p);   /* landed on a platform/elevator? start riding it */
	if (is_player)
		try_collect(s, p);
	/* an OPEN exit swallows the run: drop the buffered/held step so the player comes to
	 * rest here and main can trigger the level-complete — otherwise the seamless hop
	 * chain carries him straight across the tile (same skip bug as the jump-pad below). */
	if (is_player && s->lvl->tiles[p->cx][p->cy].type == TT_EXIT
	    && p->crystals >= (int)s->lvl->crystals_needed)
		p->buf_move = -1;
	/* teleporter: warp NOW, before ai_step/input hops the entity off (enemies warp too). */
	if (try_teleport(s, p))
		return;
	/* jump-pad: launch NOW (momentum dir) before a buffered step can skip it. */
	if (try_launch_jumppad(s, p, is_player, p->cx - p->fx, p->cy - p->fy))
		return;
	uint8_t tt = s->lvl->tiles[p->cx][p->cy].type;
	/* switch: landing on it toggles the bridge (id = clip_rule-1) — player AND enemies. */
	if (tt == TT_SWITCH && s->lvl->tiles[p->cx][p->cy].clip_rule > 0) {
		bridge_toggle(s, s->lvl->tiles[p->cx][p->cy].clip_rule - 1);
		if (is_player) {
			s->stats.bridges++;
			raise_event(s, SIM_EV_SWITCH, p->rx, p->ry, p->rz);   /* Switch.wav, no sparkles */
		}
	}
	/* glue pad: sticks whoever lands (player or enemy) for ~3s, once per pad */
	if (tt == TT_GLUE && !s->glue_used[p->cx][p->cy]) {
		p->glue_t = GLUE_DUR;
		p->sliding = false;
		s->glue_used[p->cx][p->cy] = true;
		if (is_player)
			s->stats.glue++;
		if (is_player)
			raise_event(s, SIM_EV_GLUE, p->rx, p->ry, p->rz);
		return;
	}
	/* ice: the entity keeps sliding through the run (player AND enemies) until the first
	 * non-ice tile or a block. Slide follows ENTRY MOMENTUM (not facing) — a backward hop
	 * onto ice slides you backward — kept for the whole run. */
	if (tt == TT_ICE) {
		if (!was_sliding) {                    /* first tile of the run: lock the momentum dir */
			int mdx = p->cx - p->fx, mdy = p->cy - p->fy;
			p->slide_dir = (mdx == 0 && mdy == 0) ? p->facing   /* vertical drop onto ice */
			             : mdx < 0 ? DIR_NX : mdx > 0 ? DIR_PX
			             : mdy < 0 ? DIR_NY : DIR_PY;
			if (is_player)
				s->stats.ices++;       /* one per run entered */
		}
		p->sliding = true;
		p->slide_ice = true;
		step_entity(s, p, p->slide_dir);   /* auto-advance a cell (emits SIM_EV_SLIDE) */
		if (!p->moving)                    /* couldn't advance -> settle on the ice */
			p->sliding = false;
		return;
	}
	/* forced-direction slide (0x10, "rutsche"): like ice, but the DIRECTION comes from the
	 * TILE (clip_rule 1-4), not entry momentum, and the chute DESCENDS — auto-advance in the
	 * tile's forced dir until a non-slide tile (each 0x10 re-arms its own dir, so chutes
	 * curve; stacked-lower tiles make you slide down). */
	if (tt == TT_SLIDE) {
		int fc = s->lvl->tiles[p->cx][p->cy].clip_rule;
		if (is_player && !was_sliding)
			s->stats.slides++;             /* one per chute run entered */
		p->slide_dir = (fc >= 1 && fc <= 4) ? SPAWN_FACE[fc] : p->facing;
		p->sliding = true;
		p->slide_ice = false;
		step_entity(s, p, p->slide_dir);
		if (!p->moving)
			p->sliding = false;
		return;
	}
	p->sliding = false;   /* ordinary ground ends any slide */
}

static void arrive_entity(sim_state *s, sim_player *p, bool is_player)
{
	bool was_sliding = p->sliding;   /* a slide hop slips (SIM_EV_SLIDE), not a stomp */
	p->moving = false;
	p->cx = p->tx;
	p->cy = p->ty;
	p->rx = p->cx + 0.5f;
	p->ry = p->cy + 0.5f;

	/* a slide chute DESCENDS (each tile a step lower), so its hops aren't flat is_steps —
	 * a sliding entity lands + continues down the chute's ramp (slide_surface) so the run
	 * chains instead of being treated as a fall after the first tile. An ice run exiting
	 * onto a LOWER plain tile is NOT a surface to follow: it keeps the run's height and
	 * falls onto that tile (no diagonal glide through the slab). */
	if (is_step(s, p->fx, p->fy, p->cx, p->cy)
	    || (was_sliding && slide_surface(s, p->fx, p->fy, p->cx, p->cy, NULL))) {
		land_effects(s, p, is_player, was_sliding);
		return;
	}
	/* airborne: hop off and fall (land on the tile below, or fall to death) */
	p->sliding = false;
	p->falling = true;
	p->fall_v = FALL_V0;
	p->fall_from = p->rz;
	/* the falling scream: fired when this hop is doomed to drop off the bottom of
	 * the world — either the target cell is empty (VOID), or it's a floor too high
	 * to land on so we fall PAST it into the abyss. A reachable lower floor instead
	 * lands (silent) or splats (its own sound), so it's excluded here. */
	if (is_player && s->inv[PU_PARAGLIDE] <= 0) {   /* a held chute = survivable, no scream */
		cell_kind k = kind_at(s, p->cx, p->cy);
		bool too_high = k == CK_FLOOR
		                && cell_top(s, p->cx, p->cy) > p->fall_from + 0.01f;
		if (k == CK_VOID || too_high)
			raise_event(s, SIM_EV_FALL, p->rx, p->ry, p->rz);
	}
}

void sim_warp(sim_state *s, int x, int y)
{
	sim_player *p = &s->p;
	if (x < 0 || y < 0 || x >= s->lvl->dim_x || y >= s->lvl->dim_y)
		return;
	p->cx = x;
	p->cy = y;
	p->moving = p->turning = p->falling = false;
	p->alive = true;
	p->rx = x + 0.5f;
	p->ry = y + 0.5f;
	p->rz = cell_top(s, x, y);
}

static void tick_launching(sim_state *s, sim_player *p, float dt, bool is_player)
{
	p->launch_t += dt;
	float t = p->launch_t;
	if (!p->launch_gliding) {
		/* PHASE 1 — rise IN PLACE (decelerating ballistic) to the target height. */
		p->rx = p->cx + 0.5f;
		p->ry = p->cy + 0.5f;
		p->rz = p->launch_z0 + p->launch_v0 * t - JUMPPAD_HG * t * t;
		if (p->rz >= p->launch_zT && t > 0.0f) {
			p->rz = p->launch_zT;
			p->launch_gliding = true;
			p->launch_t = 0.0f;   /* restart the timer for the glide */
		}
		return;
	}
	/* PHASE 2 — a normal forward hop-GLIDE one cell (momentum dir), descending to its
	 * floor. (RE: at the top the pad re-issues stored_move_dir as a standard move — so
	 * it slides across to the destined tile, NOT an instant teleport at the apex.) */
	int fx = p->launch_fx, fy = p->launch_fy;
	bool floor = fx >= 0 && fy >= 0 && fx < s->lvl->dim_x && fy < s->lvl->dim_y
	             && kind_at(s, fx, fy) == CK_FLOOR;
	float land = floor ? cell_top(s, fx, fy) : p->launch_zT;
	float u = p->launch_t / (s->move_dur * 0.1f);   /* snappy ~fast leap (calibrated to the
	                                                  * original's brief phase-2 slide) */
	if (u >= 1.0f) {
		p->launching = p->launch_gliding = false;
		if (floor) {
			/* source = the pad cell so land_effects' momentum (cx-fx) = the glide dir;
			 * this runs the SAME landing effects as a walked arrival — switch toggle,
			 * teleporter, glue, ice-slide, chained pad. */
			p->fx = p->cx; p->fy = p->cy;
			p->cx = fx; p->cy = fy;
			p->rx = fx + 0.5f; p->ry = fy + 0.5f;
			land_effects(s, p, is_player, false);
		} else {                                /* void ahead: fall from the glide height */
			p->cx = fx; p->cy = fy;
			p->rx = fx + 0.5f; p->ry = fy + 0.5f;
			p->falling = true; p->fall_v = 0.0f; p->fall_from = p->launch_zT;
			p->rz = p->launch_zT;
			if (is_player && s->inv[PU_PARAGLIDE] <= 0)   /* chute in hand: no scream */
				raise_event(s, SIM_EV_FALL, p->rx, p->ry, p->rz);
		}
		return;
	}
	p->rx = (p->cx + 0.5f) * (1.0f - u) + (fx + 0.5f) * u;
	p->ry = (p->cy + 0.5f) * (1.0f - u) + (fy + 0.5f) * u;
	p->rz = p->launch_zT * (1.0f - u) + land * u;
	return;
}

/* PARAGLIDE (RE re_g3_paraglide.md): the player STEERS through the air cell-by-cell
 * (normal move/turn keys at walking speed) while sinking at a constant 4 u/s; landing on
 * ANY floor is survivable at any height. NOT an auto forward-drift. */
static void tick_gliding(sim_state *s, sim_player *p, float dt, bool is_player)
{
	float gm = is_player ? s->move_dur : s->move_dur * ENEMY_SLOW;
	float gtu = is_player ? s->turn_dur : s->turn_dur * ENEMY_SLOW;
	p->rz -= GLIDE_DESCENT * dt;                    /* constant sink, always */
	if (p->turning) {
		p->turn_t += dt / gtu;
		float u = p->turn_t < 1.0f ? p->turn_t : 1.0f;
		p->yaw_deg = p->yaw_from + (p->yaw_to - p->yaw_from) * u;
		if (p->turn_t >= 1.0f) { p->turning = false; p->yaw_deg = dir_yaw(p->facing); }
		p->rx = p->cx + 0.5f; p->ry = p->cy + 0.5f;
		return;
	}
	if (p->moving) {                                /* airborne grid hop (steered) */
		p->move_t += dt / gm;
		if (p->move_t < 1.0f) {                     /* mid-hop: lerp horizontally, keep sinking */
			float t = p->move_t;
			p->rx = (p->fx + 0.5f) + (p->tx - p->fx) * t;
			p->ry = (p->fy + 0.5f) + (p->ty - p->fy) * t;
			return;
		}
		p->moving = false;                          /* hop done -> arrive, then test landing */
		p->cx = p->tx; p->cy = p->ty;
	}
	p->rx = p->cx + 0.5f; p->ry = p->cy + 0.5f;
	/* gliding within a block above a cell top collects its pickup: the original's
	 * collection check runs EVERY tick and only asks trunc(pos_z) == the cell's
	 * z_pos (+ not mid-hop) — crystal runs on glide levels depend on it. */
	if (is_player && (int)p->rz == (int)s->lvl->tiles[p->cx][p->cy].z_pos)
		try_collect(s, p);
	float gt;
	/* land only when the sinking rz has JUST reached a floor at/just-below it (a narrow
	 * band) — so you settle onto whatever you descend onto. NOT `rz <= gt` alone, which
	 * teleported you UP onto any higher forward cell (or read as a warp when a lower cell
	 * was far below); a floor well above rz is passed under, one well below is sunk toward. */
	if (land_floor(s, p->cx, p->cy, &gt) && p->rz <= gt && gt - p->rz < 0.6f) {
		p->rz = gt; p->gliding = false;         /* settle onto the floor you sank to (safe) */
		land_effects(s, p, is_player, false);
		return;
	}
	if (p->rz < DEATH_Z) {                       /* steered over a void, sank too far */
		p->gliding = false;
		if (is_player) { p->rz = p->fall_from; kill_player(s); }
	}
	return;
}

static void tick_falling(sim_state *s, sim_player *p, float dt, bool is_player)
{
	/* deploy paraglide: once fallen more than 2 cells with a charge, spend it and switch
	 * from free-fall to the steerable glide above (handled next tick). */
	if (is_player && s->inv[PU_PARAGLIDE] > 0 && (p->fall_from - p->rz) > GLIDE_TRIGGER) {
		p->gliding = true;
		p->falling = false;
		s->inv[PU_PARAGLIDE]--;
		return;
	}
	p->fall_v -= GRAVITY * dt;
	p->rz += p->fall_v * dt;
	/* same per-tick height-band collection as the glide: falling through a
	 * pickup's block grabs it (paraglide refills excepted, gated above). */
	if (is_player && (int)p->rz == (int)s->lvl->tiles[p->cx][p->cy].z_pos)
		try_collect(s, p);
	float ct;
	if (land_floor(s, p->cx, p->cy, &ct)) {   /* static ground only — movers don't catch fallers */
		if (ct <= p->fall_from + 0.01f && p->rz <= ct) {
			if (p->fall_from - ct > 2.01f) {   /* fell more than 2 blocks -> splat */
				if (is_player) {
					raise_event(s, SIM_EV_SPLAT, p->rx, p->ry, p->rz);
					kill_player(s);
				} else {
					respawn_entity(s, p, false);
				}
				return;
			}
			p->rz = ct;                        /* drops of up to 2 blocks land safely */
			p->falling = false;
			/* a fall arrival is an arrival like any other: pickups, mover attach,
			 * teleporter/jump-pad/SWITCH/glue/ice triggers all fire (the original
			 * flags cell entry regardless of how the cell was reached — an ice
			 * run dropping a catcher onto a switch must still toggle it). A
			 * vertical drop has no momentum: fx/fy = the landing cell. */
			p->fx = p->cx;
			p->fy = p->cy;
			land_effects(s, p, is_player, false);
			return;
		}
	}
	if (p->rz < DEATH_Z) {
		if (is_player) {
			/* anchor the death cam + rising angel at the tile he leapt FOR
			 * (rx,ry already = the target cell) at the height he jumped from,
			 * not way down in the abyss where he physically is now. */
			p->rz = p->fall_from;
			kill_player(s);   /* scream already fired when he hopped off */
		} else {
			/* an enemy lured into a hole (collapsed destruct tile / off the
			 * edge) falls to its death — despawn for good, don't teleport back
			 * to its spawn cell. */
			p->removed = true;
			p->falling = false;
			raise_event(s, SIM_EV_ENEMYDIE, p->rx, p->ry, p->fall_from);
		}
	}
	return;
}

static void tick_turning(sim_state *s, sim_player *p, float dt, float tdur)
{
	p->turn_t += dt / tdur;
	float u = p->turn_t < 1.0f ? p->turn_t : 1.0f;
	p->yaw_deg = p->yaw_from + (p->yaw_to - p->yaw_from) * u;
	if (p->turn_t >= 1.0f) {
		p->turning = false;
		p->yaw_deg = dir_yaw(p->facing);
		/* hop-turn stomp on completion (landing), mirroring the move hop. */
		raise_event(s, (p == &s->p) ? SIM_EV_STEP
		               : (p->is_thrower ? SIM_EV_ETHROW : SIM_EV_ESTEP), p->rx, p->ry, p->rz);
	}
	p->rx = p->cx + 0.5f;
	p->ry = p->cy + 0.5f;
	p->rz = cell_top(s, p->cx, p->cy);
	return;
}

static void tick_moving(sim_state *s, sim_player *p, float dt, float mdur, bool is_player)
{
	p->move_t += dt / mdur;
	if (p->move_t >= 1.0f) {
		float over = p->move_t - 1.0f;   /* fractional overshoot to carry into a chained hop */
		arrive_entity(s, p, is_player);
		/* seamless chain: if a held direction was buffered and the arrival left us idle on
		 * plain ground (not launched/teleported/gliding/sliding/glued), start the next hop
		 * NOW carrying the overshoot — no 1-frame gap (the "stiff" pause between hops). */
		if (is_player && p->buf_move >= 0 && !p->moving && !p->turning && !p->falling
		    && !p->gliding && !p->launching && !p->sliding && p->glue_t <= 0.0f) {
			step_entity(s, p, (sim_dir)p->buf_move);
			if (p->moving && over > 0.0f && over < 1.0f)
				p->move_t = over;
		}
		return;
	}
	float t = p->move_t;
	p->rx = (p->fx + 0.5f) + ((p->tx) - (p->fx)) * t;
	p->ry = (p->fy + 0.5f) + ((p->ty) - (p->fy)) * t;
	float zf = cell_top(s, p->fx, p->fy);
	float zt2;
	if (is_step(s, p->fx, p->fy, p->tx, p->ty)) {
		float zt = cell_top(s, p->tx, p->ty);
		p->rz = zf + (zt - zf) * t;
	} else if (p->sliding && slide_surface(s, p->fx, p->fy, p->tx, p->ty, &zt2)) {
		p->rz = zf + (zt2 - zf) * t;   /* a chute hop rides the ramp down (not a snap) */
	} else {
		p->rz = zf;   /* doomed hop stays at source height, then falls */
	}
	return;
}

static void tick_idle(sim_state *s, sim_player *p, bool is_player)
{
	/* idle on a teleporter (spawned/placed on one, or the arrival warp was locked): warp. */
	if (try_teleport(s, p))
		return;

	/* idle on a jumppad (spawned/placed on one, not via arrival): launch (facing dir) */
	if (try_launch_jumppad(s, p, is_player, 0, 0))
		return;

	/* idle: if the ground vanished under us (a DestructField collapsed to a hole), drop.
	 * A rider is EXEMPT: it's carried by its mover (ride_carry) regardless of the mover's
	 * grid alignment — dynamic_floor only reports floor when the platform is near-aligned
	 * (the boarding gate), so mid-slide it reads void and would drop the rider every step.
	 * Riding persists until the entity hops off (which clears ride_kind). */
	if (p->ride_kind == 0
	    && kind_at(s, p->cx, p->cy) == CK_VOID && !dynamic_floor(s, p->cx, p->cy, NULL)) {
		p->falling = true;
		p->fall_v = FALL_V0;
		p->fall_from = p->rz;
		if (is_player && s->inv[PU_PARAGLIDE] <= 0)   /* chute in hand: calm, no scream */
			raise_event(s, SIM_EV_FALL, p->rx, p->ry, p->rz);   /* nothing below -> scream */
		return;
	}
	p->rx = p->cx + 0.5f;
	p->ry = p->cy + 0.5f;
	p->rz = cell_top(s, p->cx, p->cy);
}

static void tick_entity(sim_state *s, sim_player *p, float dt, bool is_player)
{
	if (is_player && !p->alive)
		return;   /* dead: hold still for the death cam. Death restores rz to the
		           * hop-off height OVER the void cell — without this the idle
		           * void-check re-arms the fall and re-screams SIM_EV_FALL there
		           * (the "second scream" ~1s after the first). */

	if (!is_player && (p->removed || p->dying_t > 0.0f)) {   /* enemy death anim: frozen, harmless */
		if (p->dying_t > 0.0f && (p->dying_t -= dt) <= 0.0f) {
			p->dying_t = 0.0f;
			p->removed = true;   /* gone for good this level attempt */
		}
		return;
	}

	if (p->launching) { tick_launching(s, p, dt, is_player); return; }

	float mdur = is_player ? s->move_dur : s->move_dur * ENEMY_SLOW;
	float tdur = is_player ? s->turn_dur : s->turn_dur * ENEMY_SLOW;
	if (!is_player && p->is_thrower) {
		mdur *= 1.4f;   /* thrower hop AND turn = 700ms (RE: 1.4x the catcher's 500ms) */
		tdur *= 1.4f;
	}
	if (is_player && s->speed_t > 0.0f)
		mdur *= 0.5f;   /* Speed pickup: hops complete twice as fast (turns unchanged) */
	if (is_player && s->slow_t > 0.0f)
		mdur *= 2.0f;   /* Slowdown debuff: hops take twice as long (mirror of Speed) */

	if (p->glue_t > 0.0f) {           /* stuck on glue: hold still until it wears off */
		p->glue_t -= dt;
		if (p->glue_t < 0.0f)
			p->glue_t = 0.0f;
	}

	if (p->gliding) { tick_gliding(s, p, dt, is_player); return; }
	if (p->falling) { tick_falling(s, p, dt, is_player); return; }
	if (p->turning) { tick_turning(s, p, dt, tdur); return; }
	if (p->moving)  { tick_moving(s, p, dt, mdur, is_player); return; }
	tick_idle(s, p, is_player);
}

/* ==== enemy chase AI — faithful port of the original pathfinder ================
 * RE re_g6_enemy_ai.md §2/§2b/§3/§4: ai_pathfind_astar 0x401db0, expand 0x401ef0,
 * insert/relax 0x402000/0x402170/0x4021b0, target+cap table in game_tick.
 *
 * The search runs BACKWARD — root = the target (usually the player), goal = the
 * enemy — so the goal node's parent is the enemy's next hop. Edges are checked in
 * the root->enemy direction with a CLIMB-only height rule (0 < dz <= 2): walked in
 * reverse by the enemy those climbs are its <=2 hop-downs — the original builds the
 * enemy's one-way movement graph by searching it backward. The pop cap is the
 * de-facto aggro range: budget spent -> no move this frame, and the whole thing is
 * re-run every frame with no memory (a live range gate, not an aggro latch). */

/* is (x,y) on a moving platform's track? The original registers the WHOLE track run
 * as plannable floor at the track z (register_platform_cell 0x417e20 stamps a
 * per-cell flag + z through the empty run) — the platform's live position only
 * gates EXECUTION (§4 / kind_at), which is what makes enemies walk up to a mover
 * gap and wait for the ride. */
static bool mover_track_z(const sim_state *s, int x, int y, int *z)
{
	for (int i = 0; i < s->num_platforms; i++) {
		const sim_platform *p = &s->platforms[i];
		for (int k = 0; k <= p->range; k++)
			if (p->hx + k * p->dx == x && p->hy + k * p->dy == y) {
				if (z)
					*z = p->hz;
				return true;
			}
	}
	return false;
}

/* edge-height for planning: like tile_z, but a bare void cell on a mover track
 * counts at the track z (the plannable-floor rule above). */
static int ai_z(const sim_state *s, int x, int y)
{
	int z;
	if (s->lvl->tiles[x][y].type == TT_VOID && s->plank_z[x][y] < 0
	    && !dynamic_floor(s, x, y, NULL) && mover_track_z(s, x, y, &z))
		return z;
	return tile_z(s, x, y);
}

/* the original stamps entity_reservation (tile +0x1a5) on cells an entity occupies
 * or is entering: 4 = the player, others = enemies. Pathing reads it in the switch
 * and glue clauses below. */
static int ai_reserved(const sim_state *s, int x, int y)
{
	if ((s->p.cx == x && s->p.cy == y) || (s->p.moving && s->p.tx == x && s->p.ty == y))
		return 4;
	if (enemy_occupies(s, x, y, NULL))
		return 2;
	return 0;
}

/* node walkability (is_cell_walkable 0x401cd0, catcher-mode rules): statically
 * typed cells count — an elevator shaft or a mover's home cell is a graph NODE even
 * while the car/platform is elsewhere (that's what lets enemies path to a mover and
 * WAIT for it; presence/alignment gates the actual step, not the plan). */
static bool ai_walkable(const sim_state *s, const sim_player *e, int x, int y)
{
	const jjm_level *l = s->lvl;
	if (x < 0 || y < 0 || x >= l->dim_x || y >= l->dim_y)
		return false;
	uint8_t t = l->tiles[x][y].type;
	if (t == TT_VOID)      /* bare void: a deployed plank, a parked mover, or any
	                          cell of a mover's registered track (plannable floor) */
		return s->plank_z[x][y] >= 0 || dynamic_floor(s, x, y, NULL)
		       || mover_track_z(s, x, y, NULL);
	if (t == TT_DECOR)
		return false;
	if (t == TT_OBSTACLE)
		return s->obstacle_gone[x][y];
	if (t == TT_DESTRUCT)
		return !s->destruct_open[x][y];
	if (t == TT_SWITCH)    /* mode-2 rule: catchers pass a switch only while it is
	                          occupied or player-reserved (0x401cd0) */
		return (e && e->is_thrower) || ai_reserved(s, x, y) != 0;
	return true;
}

/* does an elevator shaft at (x,y) have an endpoint parking the car level with z? */
static bool elev_endpoint(const sim_state *s, int x, int y, int z)
{
	for (int i = 0; i < s->num_elevators; i++)
		if (s->elevators[i].cx == x && s->elevators[i].cy == y)
			return s->elevators[i].z0 == z || s->elevators[i].z1 == z;
	return false;
}

/* edge legality for the enemy's hop (fx,fy)->(tx,ty) — tile_passable 0x41f500
 * transcribed clause-for-clause; LATER clauses OVERRIDE earlier ones exactly like
 * the original's write-then-override chain. NB: astar expands cur->nb but validates
 * the move nb->cur (expand 0x401ef0 passes (to=cur, from=nb)) — i.e. every edge is
 * checked in the ENEMY'S OWN travel direction, drops downhill toward the player. */
static bool ai_edge(const sim_state *s, int fx, int fy, int tx, int ty)
{
	const jjm_level *l = s->lvl;
	const jjm_tile *ft = &l->tiles[fx][fy], *tt = &l->tiles[tx][ty];
	int fz = ai_z(s, fx, fy), tz = ai_z(s, tx, ty);
	bool sf = is_stair(l, fx, fy), st = is_stair(l, tx, ty);
	bool pass = false;
	/* stair pairs + flat steps: the player's own verified surface-follow rule */
	if (sf || st)
		pass = is_step(s, fx, fy, tx, ty);
	else if (tz == fz && tt->type != TT_SLIDE)
		pass = true;
	/* elevator endpoint edges: board/leave a shaft whose car PARKS level with the
	 * neighbour floor — plannable while the car is away; timing is ai_step's gate */
	if (tz != fz || tt->type == TT_SLIDE) {
		if (ft->type == TT_ELEVATOR && elev_endpoint(s, fx, fy, tz))
			pass = true;
		else if (tt->type == TT_ELEVATOR && elev_endpoint(s, tx, ty, fz))
			pass = true;
	}
	/* hop-down 1..2 onto a plain (non-slide, non-planked) cell */
	if (!sf && tt->type != TT_SLIDE && fz - tz > 0 && fz - tz < 3)
		pass = s->plank_z[tx][ty] < 0;
	/* rutsche: enterable only FLAT along its forced direction; always exitable */
	if (tt->type == TT_SLIDE && tz == fz) {
		int fc = tt->clip_rule;
		pass = fc >= 1 && fc <= 4
		       && tx - fx == DX[SPAWN_FACE[fc]] && ty - fy == DY[SPAWN_FACE[fc]];
	}
	if (ft->type == TT_SLIDE)
		pass = true;
	/* jump-pad portal: entering requires the landing one-further along the travel
	 * dir to sit at the pad's launch height; exiting a pad ignores height */
	if (tt->type == TT_JUMPPAD) {
		int lx = tx + (tx - fx), ly = ty + (ty - fy);
		pass = lx >= 0 && ly >= 0 && lx < l->dim_x && ly < l->dim_y
		       && tile_z(s, lx, ly) == (int)tt->clip_rule;
	}
	if (ft->type == TT_JUMPPAD)
		pass = true;
	/* glue gate (the 2-cell look-ahead RETURN at 0x41f500's tail): an UNRESERVED
	 * glue cell may only be ENTERED when the cell straight beyond it is reserved
	 * BY THE PLAYER (entity_reservation == 4). A glue pen stays sealed until the
	 * player physically steps into the escape lane — the few-frames "i'm in its
	 * path" wake (verified vs Space\Bonus footage: dormant from spawn (7,9),
	 * woken from the ice at (7,8)/(10,7)). */
	if (tt->type == TT_GLUE && ai_reserved(s, tx, ty) == 0) {
		int lx = tx + (tx - fx), ly = ty + (ty - fy);
		return lx >= 0 && ly >= 0 && lx < l->dim_x && ly < l->dim_y
		       && ai_reserved(s, lx, ly) == 4;
	}
	return pass;
}

/* pathfinder node (the original's 0x44-byte layout): f = g + h with g = hop count
 * and h = squared grid distance to the goal (the enemy). The open list is a linked
 * list kept sorted ascending by f AT INSERT TIME — a tie lands BEFORE the equal-f
 * run (LIFO on plateaus, 0x402170's `<`) — and "pop best" just takes the head
 * (0x401ec0). In-open g-improvements update fields WITHOUT repositioning (stale
 * order kept, 0x402000); improvements reaching a CLOSED node propagate through its
 * recorded children (0x4021b0). All quirks replicated: they shape which cells a
 * 50-pop budget reaches, i.e. the exact wake boundaries. */
typedef struct {
	int   f, g, h;
	short x, y;
	short parent;          /* -1 = root (the target's cell) */
	short next;            /* sorted open-list link; -1 = tail */
	short child[4];        /* neighbors relaxed through this node (propagation) */
	short nchild;
	bool  closed;
} ai_node;

#define AI_NODE_MAX 1601   /* 1 root + 4 per pop at the largest cap (400) */

static ai_node ai_pool[AI_NODE_MAX];
static short   ai_cell[JJM_MAX_DIM][JJM_MAX_DIM];   /* cell -> node index; -1 none */
static short   ai_open;                             /* sorted open-list head */
static short   ai_nn;                               /* nodes allocated this search */

static short ai_alloc(int x, int y, int g, int h, short parent)
{
	if (ai_nn >= AI_NODE_MAX)
		return -1;
	ai_node *n = &ai_pool[ai_nn];
	n->f = g + h; n->g = g; n->h = h;
	n->x = (short)x; n->y = (short)y;
	n->parent = parent;
	n->next = -1;
	n->nchild = 0;
	n->closed = false;
	ai_cell[x][y] = ai_nn;
	return ai_nn++;
}

static void ai_open_insert(short id)
{
	short *link = &ai_open;
	while (*link >= 0 && ai_pool[*link].f < ai_pool[id].f)
		link = &ai_pool[*link].next;
	ai_pool[id].next = *link;
	*link = id;
}

/* a g-improvement reached a CLOSED node: push it down through the recorded children
 * (0x4021b0's work list; queue-full just stops propagating — harmless, rare). */
static void ai_propagate(short id)
{
	static short q[AI_NODE_MAX];
	int qh = 0, qt = 0;
	q[qt++] = id;
	while (qh < qt) {
		ai_node *n = &ai_pool[q[qh++]];
		for (int i = 0; i < n->nchild; i++) {
			ai_node *c = &ai_pool[n->child[i]];
			if (n->g + 1 < c->g) {
				c->g = n->g + 1;
				c->f = c->h + c->g;
				c->parent = (short)(n - ai_pool);
				if (qt < AI_NODE_MAX)
					q[qt++] = n->child[i];
			}
		}
	}
}

/* one chase decision for enemy e hunting (tx,ty): the enemy's next hop direction,
 * or -1 = no move this frame (out of budget = dormant / no route / mid-anything). */
static int ai_next_dir(sim_state *s, const sim_player *e, int tx, int ty)
{
	const jjm_level *l = s->lvl;
	int W = l->dim_x, H = l->dim_y;
	int gx = e->cx, gy = e->cy;                    /* GOAL = the enemy (backward) */
	if ((gx == tx && gy == ty) || tx < 0 || ty < 0 || tx >= W || ty >= H)
		return -1;
	if (!ai_walkable(s, e, tx, ty) || !ai_walkable(s, e, gx, gy))
		return -1;                                 /* ai_compute_path endpoint guard */

	for (int x = 0; x < W; x++)
		for (int y = 0; y < H; y++)
			ai_cell[x][y] = -1;
	ai_open = -1;
	ai_nn = 0;
	ai_open_insert(ai_alloc(tx, ty, 0,
	                        (tx - gx) * (tx - gx) + (ty - gy) * (ty - gy), -1));

	/* fixed expansion order (0x401ef0): (x,y-1) (x+1,y) (x,y+1) (x-1,y) in the
	 * ORIGINAL's axes — which are transposed vs the rewrite grid (its dir codes map
	 * 1 -> our -X in the verified SPAWN_FACE/slide tables), so in our coords: */
	static const sim_dir EXPAND[4] = { DIR_NX, DIR_PY, DIR_PX, DIR_NY };
	short found = -1;
	int pops = 0;
	do {
		short cur = ai_open;
		if (cur < 0)
			return -1;                             /* open list dry: no route at all */
		ai_open = ai_pool[cur].next;
		ai_pool[cur].closed = true;
		if (ai_pool[cur].x == gx && ai_pool[cur].y == gy) {
			found = cur;
			break;
		}
		for (int i = 0; i < 4; i++) {
			int nx = ai_pool[cur].x + DX[EXPAND[i]], ny = ai_pool[cur].y + DY[EXPAND[i]];
			if (nx < 0 || ny < 0 || nx >= W || ny >= H)
				continue;
			/* is_cell_walkable(nb) + tile_passable(to=cur, from=nb): the edge is
			 * validated as the enemy's own move nb->cur (0x401ef0) */
			if (!ai_walkable(s, e, nx, ny)
			    || !ai_edge(s, nx, ny, ai_pool[cur].x, ai_pool[cur].y))
				continue;
			int g2 = ai_pool[cur].g + 1;
			short nb = ai_cell[nx][ny];
			if (nb < 0) {
				nb = ai_alloc(nx, ny, g2,
				              (nx - gx) * (nx - gx) + (ny - gy) * (ny - gy), cur);
				if (nb < 0)
					continue;
				ai_open_insert(nb);
			} else if (g2 < ai_pool[nb].g) {
				ai_pool[nb].g = g2;
				ai_pool[nb].f = ai_pool[nb].h + g2;
				ai_pool[nb].parent = cur;
				if (ai_pool[nb].closed)
					ai_propagate(nb);
				/* in-open: fields updated, stale list position kept (0x402000) */
			}
			if (ai_pool[cur].nchild < 4)
				ai_pool[cur].child[ai_pool[cur].nchild++] = nb;
		}
		pops++;
	} while (pops < e->ai_cap);
	if (found < 0)
		return -1;                                 /* budget spent: out of aggro range */
	short par = ai_pool[found].parent;             /* adjacent to the enemy — its next cell */
	if (par < 0)
		return -1;
	for (int d = 0; d < 4; d++)
		if (gx + DX[d] == ai_pool[par].x && gy + DY[d] == ai_pool[par].y)
			return d;
	return -1;
}

/* per-frame chase target by behaviour type (re_g6_enemy_ai.md §3, chosen in
 * game_tick); false = idle this frame. Types 5/7 target-finders are only partially
 * traced (MEDIUM confidence): 5 follows the nearest other catcher, 7 idles. */
static bool ai_target(const sim_state *s, const sim_player *e, int *tx, int *ty)
{
	const jjm_level *l = s->lvl;
	int bx = -1, by = -1;
	long bd = 0;
	switch (e->ai_type) {
	case 1:                        /* walks to the level exit (the type-4 cell) */
		for (int x = 0; x < l->dim_x; x++)
			for (int y = 0; y < l->dim_y; y++)
				if (l->tiles[x][y].type == TT_EXIT) {
					*tx = x; *ty = y;
					return true;
				}
		break;
	case 2:                        /* crystal-seeker; the player once none remain */
		for (int x = 0; x < l->dim_x; x++)
			for (int y = 0; y < l->dim_y; y++) {
				if (l->tiles[x][y].pickup_type != PU_CRYSTAL || s->picked[x][y])
					continue;
				long dd = (long)(x - e->cx) * (x - e->cx) + (long)(y - e->cy) * (y - e->cy);
				if (bx < 0 || dd < bd) { bd = dd; bx = x; by = y; }
			}
		break;
	case 3:                        /* nearest type-5 cell (find_cell_of_type 0x41b810,
	                                  doc-literal); the player when none */
		for (int x = 0; x < l->dim_x; x++)
			for (int y = 0; y < l->dim_y; y++) {
				if (l->tiles[x][y].type != 5)
					continue;
				long dd = (long)(x - e->cx) * (x - e->cx) + (long)(y - e->cy) * (y - e->cy);
				if (bx < 0 || dd < bd) { bd = dd; bx = x; by = y; }
			}
		break;
	case 5:                        /* leader-follow: the nearest other catcher; else idle */
		for (int i = 0; i < s->num_enemies; i++) {
			const sim_player *o = &s->enemies[i];
			if (o == e || o->removed || !o->alive || o->is_thrower)
				continue;
			long dd = (long)(o->cx - e->cx) * (o->cx - e->cx)
			        + (long)(o->cy - e->cy) * (o->cy - e->cy);
			if (bx < 0 || dd < bd) { bd = dd; bx = o->cx; by = o->cy; }
		}
		if (bx < 0)
			return false;
		break;
	case 7:                        /* special finder (FUN_00412530) unresolved: idle */
		return false;
	default:
		break;
	}
	if ((e->ai_type == 2 || e->ai_type == 3 || e->ai_type == 5) && bx >= 0) {
		*tx = bx; *ty = by;
		return true;
	}
	*tx = s->p.cx;
	*ty = s->p.cy;
	return true;
}

/* §4 "mistimed platform" gate: will the platform under (x,y) still be there when a
 * hop STARTED NOW lands (MOVE_DUR later)? A mover crosses a full cell per hop
 * (MOVER_SPEED), so boarding a departing car always lands in the gap — the enemy
 * must decline the hop and wait for the next pass. True when no platform is
 * involved (static floor) or the landing is covered. */
static bool platform_safe_landing(const sim_state *s, int x, int y)
{
	for (int i = 0; i < s->num_platforms; i++) {
		const sim_platform *p = &s->platforms[i];
		int rt = (int)(p->t + 0.5f);
		if (p->hx + p->dx * rt != x || p->hy + p->dy * rt != y
		    || p->t - (float)rt >= 0.3f || p->t - (float)rt <= -0.3f)
			continue;                          /* this platform isn't the floor here */
		if (p->range == 0)
			return true;
		if (p->phase == 0)                     /* parked: departs before we land? */
			return MOVER_WAIT - p->wait > MOVE_DUR + 0.05f;
		float tl = p->t + (p->phase == 1 ? 1.0f : -1.0f) * MOVER_SPEED * MOVE_DUR;
		if (tl < 0.0f)
			tl = 0.0f;
		if (tl > (float)p->range)
			tl = (float)p->range;              /* arriving INTO this end cell is fine */
		return tl - (float)rt < 0.25f && tl - (float)rt > -0.25f;
	}
	return true;
}

/* chase: pick the target, pathfind, turn to face the hop, then step — with the §4
 * gates: never side-step off a jump-pad (ride it out), wait for a misaligned
 * elevator/platform, never step into a live gap. */
static void ai_step(sim_state *s, sim_player *e)
{
	if (!e->alive || e->removed || e->dying_t > 0.0f
	    || e->moving || e->turning || e->falling || e->glue_t > 0.0f || e->launching)
		return;
	if (s->lvl->tiles[e->cx][e->cy].type == TT_JUMPPAD)
		return;                    /* on a pad: must ride it (tick_idle fires the launch) */
	int tx, ty;
	if (!ai_target(s, e, &tx, &ty))
		return;
	int d = ai_next_dir(s, e, tx, ty);
	if (d < 0)
		return;
	if (e->facing != (sim_dir)d) {
		turn_entity(s, e, CW[e->facing] == (sim_dir)d ? 1 : -1);
		return;
	}
	int nx = e->cx + DX[d], ny = e->cy + DY[d];
	if (kind_at(s, nx, ny) != CK_FLOOR)
		return;                    /* §4: no live floor there NOW (mover away, hole open) */
	if (!platform_safe_landing(s, nx, ny))
		return;                    /* §4: the car departs before the hop lands — wait */
	if (s->lvl->tiles[nx][ny].type == TT_ELEVATOR) {
		float dz = cell_top(s, nx, ny) - cell_top(s, e->cx, e->cy);
		if (dz > 0.25f || dz < -0.25f)
			return;                /* §4: the car isn't level with this floor — wait */
	}
	step_entity(s, e, (sim_dir)d);
}

void sim_tick(sim_state *s, float dt)
{
	if (s->speed_t > 0.0f)   s->speed_t   -= dt;   /* player buff/debuff timers count down */
	if (s->slow_t > 0.0f)    s->slow_t    -= dt;
	if (s->inverse_t > 0.0f) s->inverse_t -= dt;
	if (s->protect_t > 0.0f) s->protect_t -= dt;
	if (s->bomb_cd > 0.0f)   s->bomb_cd   -= dt;
	movers_tick(s, dt);              /* platforms + elevators glide first */
	tick_entity(s, &s->p, dt, true);
	ride_carry(s, &s->p);            /* then carry whoever stands on one */

	/* bombs: fly + fuse + blast, then compact out the spent ones */
	for (int i = 0; i < s->num_bombs; i++)
		bomb_update(s, &s->bombs[i], dt);
	for (int i = 0; i < s->num_bombs; )
		if (!s->bombs[i].alive)
			s->bombs[i] = s->bombs[--s->num_bombs];
		else
			i++;

	/* Freeze pickup: enemies are inert (no AI, no motion, no catch) until the
	 * window elapses. Ones already mid-hop finish it, then hold still. */
	bool frozen = s->enemy_freeze_t > 0.0f;
	if (frozen)
		s->enemy_freeze_t -= dt;
	for (int i = 0; i < s->num_enemies; i++) {
		if (!frozen)
			ai_step(s, &s->enemies[i]);
		tick_entity(s, &s->enemies[i], dt, false);
		ride_carry(s, &s->enemies[i]);
		if (s->enemies[i].is_thrower)
			thrower_throw(s, &s->enemies[i], dt, frozen);
	}
	destruct_tick(s, dt, frozen);    /* collapsing fields: arm -> hole -> regen */
	factory_tick(s, dt, frozen);     /* enemy spawners */
	bridges_tick(s, dt);             /* switch-toggled bridges extend/retract */
	/* catch: an enemy overlapping John (same tile & level) -> death -> respawn.
	 * Protection makes John invulnerable to catches too. */
	if (s->p.alive && !s->p.falling && !frozen && s->protect_t <= 0.0f) {
		for (int i = 0; i < s->num_enemies; i++) {
			sim_player *e = &s->enemies[i];
			if (e->removed || e->dying_t > 0.0f || e->is_thrower)   /* dying/gone/thrower = harmless on touch */
				continue;
			float dx = e->rx - s->p.rx, dy = e->ry - s->p.ry;
			if (dx * dx + dy * dy < 0.36f && fabsf(e->rz - s->p.rz) < 1.0f) {
				raise_event(s, SIM_EV_CAUGHT, s->p.rx, s->p.ry, s->p.rz);
				kill_player(s);   /* caught -> death sequence (crystals kept) */
				break;
			}
		}
	}
	s->p.buf_move = -1;   /* input buffer is a per-frame signal (re-set each frame it's held) */
}
