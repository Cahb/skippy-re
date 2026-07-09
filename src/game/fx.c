/* Lightweight particle FX — see fx.h. Moved verbatim out of main.c (no logic change). */
#include "game/fx.h"

#include <stdlib.h>
#include <math.h>

typedef struct {
	r_vec3        pos, vel;
	float         life, life0;   /* seconds left / at spawn */
	float         sz0, sz1;      /* size at spawn / death */
	float         gz;            /* vertical gravity accel (units/s^2) */
	r_tex         tex;
	unsigned char cr, cg, cb;
	unsigned char blend;         /* R_BLEND_ADD (glow) or R_BLEND_ALPHA (masked, e.g. bees) */
} fx_particle;
#define FX_MAX 4096   /* many .par emitters run at once + high-rate ones (fountain 500/s) */
static fx_particle  g_fx[FX_MAX];
static int          g_fx_n;

static float frand(void)          { return (float)rand() / ((float)RAND_MAX + 1.0f); }
static float rrange(float a, float b) { return a + (b - a) * frand(); }   /* order-agnostic */

/* weighted-random colour from a .par's palette (white if it has none). */
static r_color par_pick_color(const par_system *ps)
{
	if (ps->num_colors <= 0)
		return (r_color){ 255, 255, 255, 255 };
	unsigned tot = 0;
	for (int i = 0; i < ps->num_colors; i++)
		tot += ps->colors[i].weight ? ps->colors[i].weight : 1;
	unsigned r = (unsigned)(frand() * (float)tot);
	int pick = ps->num_colors - 1;
	for (int i = 0; i < ps->num_colors; i++) {
		unsigned w = ps->colors[i].weight ? ps->colors[i].weight : 1;
		if (r < w) { pick = i; break; }
		r -= w;
	}
	return (r_color){ ps->colors[pick].rgb[0], ps->colors[pick].rgb[1],
	                  ps->colors[pick].rgb[2], 255 };
}

void fx_spawn(r_vec3 pos, r_vec3 vel, float life, float sz0, float sz1, float gz,
              r_tex tex, r_color c, int blend)
{
	if (g_fx_n >= FX_MAX || tex < 0)
		return;
	g_fx[g_fx_n++] = (fx_particle){ pos, vel, life, life, sz0, sz1, gz, tex,
	                                c.r, c.g, c.b, (unsigned char)blend };
}

/* radial sparkle burst (crystal pickup) — additive */
void fx_burst(r_vec3 pos, int n, r_tex tex, r_color c)
{
	for (int i = 0; i < n; i++) {
		float a = (float)i / n * 6.2831853f;
		float sp = 1.2f + (rand() % 100) / 100.0f;
		r_vec3 v = { cosf(a) * sp, sinf(a) * sp, 1.0f + (rand() % 100) / 80.0f };
		fx_spawn(pos, v, 0.45f + (rand() % 50) / 200.0f, 0.70f, 0.04f, -4.0f, tex, c, R_BLEND_ADD);
	}
}

void fx_reset(void)
{
	g_fx_n = 0;
}

void fx_update(float dt)
{
	for (int i = 0; i < g_fx_n; ) {
		fx_particle *p = &g_fx[i];
		p->life -= dt;
		if (p->life <= 0.0f) { g_fx[i] = g_fx[--g_fx_n]; continue; }
		p->vel.z += p->gz * dt;                /* per-particle gravity */
		p->pos.x += p->vel.x * dt;
		p->pos.y += p->vel.y * dt;
		p->pos.z += p->vel.z * dt;
		i++;
	}
}

/* draw particles matching `blend` as camera-facing billboards (caller has the
 * matching blend mode set). Additive fades via RGB (alpha ignored); alpha-blended
 * (masked textures, e.g. bees) keeps RGB and fades the alpha channel. */
void fx_draw(int blend)
{
	for (int i = 0; i < g_fx_n; i++) {
		const fx_particle *p = &g_fx[i];
		if (p->blend != blend)
			continue;
		float t = p->life / p->life0;                       /* 1 -> 0 */
		float sz = p->sz1 + (p->sz0 - p->sz1) * t;
		r_color c = (blend == R_BLEND_ADD)
			? (r_color){ (unsigned char)(p->cr * t), (unsigned char)(p->cg * t),
			             (unsigned char)(p->cb * t), 255 }
			: (r_color){ p->cr, p->cg, p->cb, (unsigned char)(255 * t) };
		/* alpha-blend specks (bees) drawn as diamonds so they aren't tiny squares */
		r_draw_billboard_ex(p->tex, p->pos, sz, c, blend == R_BLEND_ALPHA);
	}
}

/* --- generic .par-driven emitter (see fx.h) ------------------------------- */

/* map a .par vector (Y-up, right-handed) into our world basis (Z-up): a proper +90°
 * rotation about X, (x,y,z) -> (x,-z,y). NOT the naive axis-swap (x,z,y) — that's a
 * reflection that mirrors the lateral axis, which put torch/candle flames on the
 * wrong side of their model (verified: Forest torch cup is at world Y=-0.45, and only
 * -z lands the flame there; the model meshes render raw, so this must be handed-consistent). */
static r_vec3 par_to_world(float x, float y, float z) { return (r_vec3){ x, -z, y }; }

/* spawn ONE particle from the .par into the pool, at `anchor` rotated by (cs,sn). Shared
 * by the continuous emitter (par_emit) and the one-shot burst (par_burst). vjit>0 adds a
 * random horizontal DRIFT velocity (units/s) so particles fan out as they travel — a point
 * flame billows into a plume, tile debris scatters outward — rather than being born spread. */
static void par_spawn_one(const par_system *ps, r_tex tex, int blend, r_vec3 anchor,
                          float cs, float sn, float gz, float sz0, float sz1, float vjit)
{
	r_vec3 lp, lv;   /* local (world-basis) spawn offset + velocity, pre-yaw */
	if (ps->kind == PAR_GEN_CYLINDER) {
		/* ring of `radius` about `center`; velocity comes from the DIR range, not the
		 * axis — the axis only orients the ring. Castle's exit Ausgang.par spawns its
		 * ring 2 units UP (center y=2) with dir (0,-1,0): a shimmer RAINING DOWN onto
		 * the exit. Jetting along the axis (up) sent it skyward from the raised ring —
		 * the "fountain floating in the air". Zero dir (some cylinders) -> axis. */
		float a = frand() * 6.2831853f;
		lp = par_to_world(ps->center[0] + cosf(a) * ps->radius,
		                  ps->center[1],
		                  ps->center[2] + sinf(a) * ps->radius);
		float spd = rrange(ps->speed_lo, ps->speed_hi);
		float dx = rrange(ps->dir_lo[0], ps->dir_hi[0]);
		float dy = rrange(ps->dir_lo[1], ps->dir_hi[1]);
		float dz = rrange(ps->dir_lo[2], ps->dir_hi[2]);
		float dl = sqrtf(dx * dx + dy * dy + dz * dz);
		if (dl < 1e-3f) {
			dx = ps->axis[0]; dy = ps->axis[1]; dz = ps->axis[2];
			dl = sqrtf(dx * dx + dy * dy + dz * dz);
			if (dl < 1e-3f) { dx = 0.0f; dy = 1.0f; dz = 0.0f; dl = 1.0f; }
		}
		lv = par_to_world(dx / dl * spd, dy / dl * spd, dz / dl * spd);
	} else {
		lp = par_to_world(rrange(ps->pos_lo[0], ps->pos_hi[0]),
		                  rrange(ps->pos_lo[1], ps->pos_hi[1]),
		                  rrange(ps->pos_lo[2], ps->pos_hi[2]));
		/* random direction in [dir_lo,dir_hi], normalized * speed, + constant drift. */
		float dx = rrange(ps->dir_lo[0], ps->dir_hi[0]);
		float dy = rrange(ps->dir_lo[1], ps->dir_hi[1]);
		float dz = rrange(ps->dir_lo[2], ps->dir_hi[2]);
		float dl = sqrtf(dx * dx + dy * dy + dz * dz);
		float spd = rrange(ps->speed_lo, ps->speed_hi);
		if (dl > 1e-5f) { dx = dx / dl * spd; dy = dy / dl * spd; dz = dz / dl * spd; }
		else            { dx = dy = 0.0f; dz = spd; }
		lv = par_to_world(dx + ps->vel_off[0], dy + ps->vel_off[1], dz + ps->vel_off[2]);
	}
	/* rotate the horizontal offset + velocity into the anchor's facing. */
	r_vec3 sp = { anchor.x + lp.x * cs - lp.y * sn,
	              anchor.y + lp.x * sn + lp.y * cs,
	              anchor.z + lp.z };
	r_vec3 v  = { lv.x * cs - lv.y * sn, lv.x * sn + lv.y * cs, lv.z };
	if (vjit > 0.0f) {
		/* random horizontal drift so a point/straight emitter fans into a cone as it
		 * travels (flame plume, fountain dome), plus a smaller spawn-base spread so the
		 * base is wide too — a point nozzle reads as a rounded dome, not a thin funnel. */
		float a = frand() * 6.2831853f, r = vjit * sqrtf(frand());
		v.x += cosf(a) * r;
		v.y += sinf(a) * r;
		float ab = frand() * 6.2831853f, rb = vjit * 0.45f * sqrtf(frand());
		sp.x += cosf(ab) * rb;
		sp.y += sinf(ab) * rb;
	}
	fx_spawn(sp, v, rrange(ps->life_lo, ps->life_hi), sz0, sz1, gz, tex,
	         par_pick_color(ps), blend);
}

void par_emit(const par_system *ps, r_tex tex, int blend,
              r_vec3 anchor, float yaw_deg, float dt, float *accum,
              float sz0_ovr, float sz1_ovr)
{
	if (!ps->valid || tex < 0 || dt <= 0.0f)
		return;
	/* the .par emit_rate is the real spawns/second (torch 20, candle 18, thruster 80,
	 * fountain 500). 0 = unspecified (some cylinder exits) -> a sensible default; cap the
	 * top only to protect the fixed pool from the pathological values (crystalFX 100000,
	 * which anyway draws attached, not here). Earlier this was clamped to [4,40], which
	 * starved the fountain to a thin trickle (real 500 -> 40). */
	float rate = ps->emit_rate;
	if (rate <= 0.0f)   rate = 24.0f;
	if (rate > 600.0f)  rate = 600.0f;
	*accum += rate * dt;
	int n = (int)*accum;
	*accum -= (float)n;
	if (n <= 0)
		return;

	float rad = yaw_deg * 0.01745329252f;
	float cs = cosf(rad), sn = sinf(rad);
	float gz  = ps->gravity[1];                                   /* .par Y-up gravity -> our Z */
	float sz0 = ps->face_size > 0.02f ? ps->face_size : 0.12f;
	float sz1 = sz0 * 0.2f;
	if (sz0_ovr > 0.0f) {   /* per-emitter override (bee specks: 0.020 -> 0.013) */
		sz0 = sz0_ovr;
		sz1 = sz1_ovr > 0.0f ? sz1_ovr : sz0_ovr;
	}
	/* many .par emitters are POINT sources with a single forward direction (Fackel2: point +
	 * gravity-up = a pencil "welding jet"; Springbrunnen: a straight 4 m/s water column). Real
	 * plumes/jets billow: give each particle a random drift velocity so it fans out over time.
	 * Turbulence grows with BOTH particle size (chunky flames) and forward speed (fast
	 * fountains spray into a cone) — take the max so slow flames and fast jets both spread and
	 * tiny sparks stay tight. */
	float vjit = fmaxf(sz0 * 0.9f, ps->speed_hi * 0.22f);

	for (int i = 0; i < n; i++)
		par_spawn_one(ps, tex, blend, anchor, cs, sn, gz, sz0, sz1, vjit);
}

/* one-shot: spawn `count` particles from the .par at once (e.g. the crumb burst when a
 * step-and-break tile collapses). Same per-particle math as par_emit, no rate/accumulator.
 * vjit>0 gives each a random horizontal drift so they fan out across the plane over time. */
void par_burst(const par_system *ps, r_tex tex, int blend,
               r_vec3 anchor, float yaw_deg, int count, float vjit)
{
	if (!ps->valid || tex < 0 || count <= 0)
		return;
	float rad = yaw_deg * 0.01745329252f;
	float cs = cosf(rad), sn = sinf(rad);
	float gz  = ps->gravity[1];
	float sz0 = ps->face_size > 0.02f ? ps->face_size : 0.12f;
	float sz1 = sz0 * 0.2f;
	for (int i = 0; i < count; i++)
		par_spawn_one(ps, tex, blend, anchor, cs, sn, gz, sz0, sz1, vjit);
}
