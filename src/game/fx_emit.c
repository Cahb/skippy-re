/* Game-specific particle emitters (see game/fx_emit.h): the per-frame world FX
 * (exit fountain, .leo props, thruster exhaust, diver bubbles) and the per-event
 * bursts (pickup sparkle, explosion). Sits on top of the generic fx.c engine. */
#include "game/fx_emit.h"
#include "game/fx.h"
#include "game/scene.h"

#include <stdlib.h>   /* rand */
#include <math.h>

/* per-frame world emitters; dt is the (frozen-aware) sim timestep, so dt<=0 = paused.
 * Walks the generic emitter list built by load_emitters and drives each through
 * par_emit with its real .par numbers, resolving the live anchor per emitter kind. */
void fx_emit_world(float dt, bool player_dying)
{
	if (dt <= 0.0f)
		return;
	bool exit_open = (int)sim.p.crystals >= (int)lvl.crystals_needed;

	for (int i = 0; i < g_scene.num_emitters; i++) {
		struct fx_emitter *em = &g_scene.emitters[i];
		r_vec3 anchor = em->pos;
		float  yaw    = em->yaw_deg;

		switch (em->anchor) {
		case FXA_STATIC:
			break;
		case FXA_EXIT:                                     /* the open-exit fountain */
			if (!exit_open)
				continue;
			break;
		case FXA_PLAYER:                                   /* John's helmet bubbles */
			if (!sim.p.alive || player_dying)
				continue;
			anchor = (r_vec3){ sim.p.rx + em->pos.x, sim.p.ry + em->pos.y, sim.p.rz + em->pos.z };
			break;
		case FXA_ENEMY: {                                  /* thrower exhaust */
			const sim_player *e = &sim.enemies[em->ref];
			if (e->removed || e->dying_t > 0.0f)
				continue;
			anchor = (r_vec3){ e->rx + em->pos.x, e->ry + em->pos.y, e->rz + em->pos.z };
			yaw = e->yaw_deg + em->yaw_deg;
			break;
		}
		}
		par_emit(&em->par, em->tex, em->blend, anchor, yaw, dt, &em->accum,
		         em->sz0, em->sz1);
	}
}

/* one sim event's FX: a sparkle burst on any pickup, blob explosion on a detonation. */
void fx_emit_event(const sim_event *e)
{
	if (e->type == SIM_EV_CRYSTAL || e->type == SIM_EV_PICKUP)   /* sparkle burst on any pickup */
		fx_burst((r_vec3){ e->x, e->y, e->z + 0.3f }, 12, g_scene.fx_star,
		         e->type == SIM_EV_CRYSTAL ? g_scene.crystal_color : (r_color){ 255, 225, 140, 255 });
	if (e->type == SIM_EV_DESTRUCT && g_scene.destruct_fx_tex >= 0)   /* debris as the tile drops */
		/* the .par is a continuous point emitter for the collapse animation; fire one burst
		 * with a horizontal drift (vjit) so the crumbs fan out across the tile base plane. */
		par_burst(&g_scene.destruct_fx_par, g_scene.destruct_fx_tex, g_scene.destruct_fx_blend,
		          (r_vec3){ e->x, e->y, e->z }, 0.0f, 24, 0.6f);
	if (e->type == SIM_EV_EXPLODE && g_scene.fx_blow >= 0) {
		/* sprite explosion: Blow blobs fly OUTWARD from the bomb centre in a
		 * rough sphere (~2-tile reach), additive, growing + fading. */
		r_vec3 ctr = { e->x, e->y, e->z + 0.45f };
		for (int k = 0; k < 22; k++) {
			float a = (rand() % 628) / 100.0f;
			float el = -0.25f + (rand() % 100) / 100.0f * 1.1f;   /* bias upward */
			float sp = 3.0f + (rand() % 100) / 50.0f;             /* 3..5 -> ~1.5-2.5 tiles */
			r_vec3 v = { cosf(a) * cosf(el) * sp, sinf(a) * cosf(el) * sp, sinf(el) * sp };
			fx_spawn(ctr, v, 0.5f, 0.55f, 0.95f, -3.0f, g_scene.fx_blow,
			         (r_color){ 255, 165, 55, 255 }, R_BLEND_ADD);
		}
		/* bright, near-stationary core flash */
		fx_spawn(ctr, (r_vec3){ 0, 0, 0.4f }, 0.4f, 0.8f, 1.9f, 0.0f, g_scene.fx_blow,
		         (r_color){ 255, 235, 185, 255 }, R_BLEND_ADD);
	}
}
