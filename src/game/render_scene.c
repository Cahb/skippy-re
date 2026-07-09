/* 3D scene rendering (see game/render_scene.h). Split out of main.c: the game
 * loop builds a render_frame and calls render_scene_3d each frame. */
#include "game/render_scene.h"
#include "game/scene.h"
#include "game/assets.h"
#include "game/fx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strcasecmp */
#include <math.h>

/* ray vs axis-aligned box (slab method); returns entry distance >=0 or -1 */
static float ray_aabb(r_vec3 o, r_vec3 d, r_vec3 mn, r_vec3 mx)
{
	float od[3] = { o.x, o.y, o.z }, dd[3] = { d.x, d.y, d.z };
	float lo[3] = { mn.x, mn.y, mn.z }, hi[3] = { mx.x, mx.y, mx.z };
	float tmin = 0.0f, tmax = 1e30f;
	for (int a = 0; a < 3; a++) {
		if (fabsf(dd[a]) < 1e-8f) {
			if (od[a] < lo[a] || od[a] > hi[a])
				return -1.0f;
			continue;
		}
		float t1 = (lo[a] - od[a]) / dd[a];
		float t2 = (hi[a] - od[a]) / dd[a];
		if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
		if (t1 > tmin) tmin = t1;
		if (t2 < tmax) tmax = t2;
		if (tmin > tmax)
			return -1.0f;
	}
	return tmin;
}

/* A tile edge needs a side face unless a same-height neighbour sits flush against
 * it. Platforms are thin floating slabs, so a neighbour at ANY different height
 * (higher or lower) leaves a gap -> the edge is exposed; void/off-grid too. */
static bool edge_exposed(const jjm_level *l, int x, int y, uint8_t z)
{
	if (x < 0 || y < 0 || x >= l->dim_x || y >= l->dim_y)
		return true;
	const jjm_tile *n = &l->tiles[x][y];
	/* mover cells (elevator/platform) draw no static tile — the mesh moves through them —
	 * so they never seal an adjacent edge; treat them as exposed like void. */
	/* cells that render no flush opaque slab never seal a neighbour's edge: movers (the
	 * mesh moves through) and destruct (thin semi-transparent leaves, intact or collapsed). */
	if (n->type == TT_ELEVATOR || n->type == TT_MOVER_X || n->type == TT_MOVER_Y
	    || n->type == TT_DESTRUCT || n->type == TT_BRIDGE_X || n->type == TT_BRIDGE_Y
	    || n->type == TT_SLIDE)
		return true;   /* draws no flush slab (mesh/force-field/gap) -> neighbour edge exposed */
	return n->type == 0 || n->z_pos != z;
}

/* yaw (deg) orienting the rutsche ramp to descend toward the tile's slide dir (clip 1-4).
 * The mesh default descends -X, so: NX(clip1)=0, PX(clip3)=180 (verified right), PY(clip2)=270,
 * NY(clip4)=90 — i.e. clips 2 & 4 are the 180° flip of a naive (clip-1)*90. */
static float slide_yaw(int clip)
{
	static const float y[5] = { 0.0f, 0.0f, 270.0f, 180.0f, 90.0f };
	return (clip >= 1 && clip <= 4) ? y[clip] : 0.0f;
}

/* --- generic .thm Field-layer drawing ------------------------------------- */

/* does a layer's Condition pass for the tile's CURRENT game state? */
static bool field_cond_pass(int cond, int type, const jjm_tile *tl)
{
	if (cond == THM_COND_NONE)
		return true;
	bool active = false;
	if (type == TT_EXIT) {
		active = lvl.crystals_needed == 0
		         || sim.p.crystals >= (int)lvl.crystals_needed;   /* exit open */
	} else if (type == TT_SWITCH && tl->clip_rule > 0) {
		for (int i = 0; i < sim.num_bridges; i++) {              /* its bridge out */
			const sim_bridge *b = &sim.bridges[i];
			if (b->id != tl->clip_rule - 1)
				continue;
			active = b->animating ? b->target_ext : b->extended;
			break;
		}
	}
	if (cond == THM_COND_ACTIVE)
		return active;
	if (cond == THM_COND_INACTIVE)
		return !active;
	return true;   /* other conditions never appear on tile Fields */
}

/* additive brightness from the layer's Pulse (jp_glow, ice shimmer) or Flash
 * (the exits' self-flashing glows; param 3 differs per layer = a phase offset —
 * approximated waveform, the original's packed curve isn't decoded). */
static unsigned char field_brightness(const struct field_layer *l, float t)
{
	float b = 1.0f;
	if (l->pulse != 0.0f)
		b = 0.55f + 0.45f * sinf(l->pulse * 1000.0f * t);
	else if (l->flash[0] != 0.0f)
		b = 0.55f + 0.45f * sinf(t * 2.2f + l->flash[2] * 1.7f);
	if (b < 0.0f)
		b = 0.0f;
	return (unsigned char)(b * 255.0f);
}

/* draw a tile stack's `want_blend` layers coplanar over the tile tops (called
 * inside the matching pass's depth-write-off bracket). Plank cells are covered
 * by draw_bridge_panes (per-bridge flow orientation); glue/destruct keep their
 * dedicated paths. */
static void draw_tile_field_layers(int want_blend, float t)
{
	for (int x = 0; x < lvl.dim_x; x++)
		for (int y = 0; y < lvl.dim_y; y++) {
			const jjm_tile *tl = &lvl.tiles[x][y];
			int tt = tl->type;
			if (tt <= 0 || tt >= 64 || tt == TT_PLANK)
				continue;
			const struct field_stack *fs = &g_scene.tile_field[tt];
			for (int li = 0; li < fs->n; li++) {
				const struct field_layer *l = &fs->l[li];
				if (l->blend != want_blend || l->tex < 0
				    || !field_cond_pass(l->cond, tt, tl))
					continue;
				unsigned char br = field_brightness(l, t);
				r_draw_layer(l->tex, x + 0.5f, y + 0.5f,
				             (float)tl->z_pos + 0.004f + 0.003f * li, 0.5f,
				             l->turn * 1000.0f * t,
				             l->scroll[0] * 1000.0f * t, l->scroll[1] * 1000.0f * t,
				             l->wobble[0] * 1000.0f * t, l->wobble[1], l->wobble[2],
				             (r_color){ br, br, br, 255 }, 0);
			}
		}
}

/* deployed bridge planks: the TT_PLANK stack's non-opaque layers as flat panes
 * at plank height, the Scroll flowing outward ALONG each bridge's axis (V is
 * the flow axis; X-axis bridges transpose the UVs). Space = One/One force
 * field, Candy = translucent alpha pane — same code, the blend picks the pass. */
static void draw_bridge_panes(int want_blend, float t)
{
	const struct field_stack *fs = &g_scene.tile_field[TT_PLANK];
	for (int li = 0; li < fs->n; li++) {
		const struct field_layer *l = &fs->l[li];
		if (l->blend != want_blend || l->tex < 0)
			continue;
		float rate = fabsf(l->scroll[0]) > fabsf(l->scroll[1]) ? l->scroll[0]
		                                                       : l->scroll[1];
		float flow = -rate * 1500.0f * t;   /* negative = outward toward the far end */
		unsigned char br = field_brightness(l, t);
		for (int i = 0; i < sim.num_bridges; i++) {
			const sim_bridge *b = &sim.bridges[i];
			int swap = (b->axis == 0);
			float vo = flow * b->step;
			for (int k = 0; k <= b->span; k++) {   /* k=0 = the anchor cell */
				int cx = b->ox + (b->axis == 0 ? b->step * k : 0);
				int cy = b->oy + (b->axis == 1 ? b->step * k : 0);
				if (cx < 0 || cy < 0 || cx >= lvl.dim_x || cy >= lvl.dim_y
				    || sim.plank_z[cx][cy] < 0)
					continue;
				r_draw_layer(l->tex, cx + 0.5f, cy + 0.5f,
				             (float)sim.plank_z[cx][cy] + 0.01f + 0.002f * li, 0.5f,
				             l->turn * 1000.0f * t, 0.0f, vo,
				             l->wobble[0] * 1000.0f * t, l->wobble[1], l->wobble[2],
				             (r_color){ br, br, br, 255 }, swap);
			}
		}
	}
}

/* the tile's slab-top texture + Turn: the stack's first condition-passing
 * OPAQUE layer, else the plate. (Candy's pressed switch spins here because
 * only its Active button layer carries Turn — the frame/shadow overlays are
 * separate alpha layers and stay static.) */
static r_tex field_top(const jjm_tile *tl, float t, float *turn_out)
{
	*turn_out = 0.0f;
	const struct field_stack *fs = &g_scene.tile_field[tl->type];
	for (int li = 0; li < fs->n; li++) {
		const struct field_layer *l = &fs->l[li];
		if (l->blend == FL_OPAQUE && l->tex >= 0
		    && field_cond_pass(l->cond, tl->type, tl)) {
			*turn_out = l->turn * 1000.0f * t;
			return l->tex;
		}
	}
	return g_scene.tex_plate;
}

/* stable per-tile seed for instance randomness: a spatial hash (primes from Teschner
 * et al.) so each tile decorrelates but the same tile yields the same seed every frame. */
static unsigned tile_seed(int x, int y)
{
	return (unsigned)(x * 73856093) ^ (unsigned)(y * 19349663);
}

/* stable per-instance pseudo-random in [0,1) from an integer seed — deterministic
 * across frames (no global RNG), varies per tile/instance. */
static float anim_rand(unsigned seed)
{
	seed = seed * 2654435761u + 1013904223u;
	seed ^= seed >> 15;
	return (float)(seed & 0xffffffu) / (float)0x1000000;
}

/* Evaluate a theme mesh_anim at time t (s) for instance `seed`: yaw (deg about Z),
 * dz (Z bob offset), and uniform scale. Theme rates are ~rad/ms, so *1000 -> rad/s.
 * This replaces the hand-coded crystal/pickup sinf() constants with the .thm values. */
static void mesh_anim_eval(const struct mesh_anim *a, float t, unsigned seed,
                           float *yaw_deg, float *dz, float *scale)
{
	float yaw = a->random_yaw ? anim_rand(seed) * 360.0f : 0.0f;
	yaw += a->rotate_z * 1000.0f * t * (180.0f / 3.14159265f);      /* continuous spin */
	float z = 0.0f;
	if (a->oscillate) {
		/* explicit phase composes on top of the random base (e.g. two counter-phase
		 * meshes written "random" and "random 3.1415"). */
		float ph = a->osc[2] + (a->osc_random ? anim_rand(seed ^ 0x9e3779b9u) * 6.2831853f : 0.0f);
		z = a->osc[0] * sinf(a->osc[1] * 1000.0f * t + ph);
	}
	*yaw_deg = yaw;
	*dz = z;
	*scale = a->pump ? 1.0f + a->pump_p[0] * sinf(a->pump_p[1] * 1000.0f * t) : 1.0f;
}

/* Draw every sub-mesh of a loaded object at `pos`, rotated by base_yaw + each mesh's
 * own spin, bobbed/scaled per its mesh_anim. `seed` is the per-instance seed (shared by
 * all sub-meshes so they animate coherently). `want_additive` selects the pass. */
static void draw_obj(const struct obj_render *o, r_vec3 pos, float base_yaw,
                     float t, unsigned seed, bool want_additive)
{
	for (int i = 0; i < o->n; i++) {
		const struct obj_submesh *sm = &o->sm[i];
		if (sm->mesh < 0 || sm->additive != want_additive)
			continue;
		float yaw, dz, sc;
		mesh_anim_eval(&sm->anim, t, seed, &yaw, &dz, &sc);
		r_vec3 p = { pos.x + sm->pos.x, pos.y + sm->pos.y, pos.z + sm->pos.z + dz };
		/* theme-Alpha sub-mesh (e.g. elevator): cut out transparent texels instead of
		 * drawing them black — alpha-test in this opaque pass. */
		bool at = !want_additive && sm->alpha;
		if (at) r_alpha_test(1);
		if (sm->acount > 1) {
			/* looping vertex animation (e.g. the flying-carpet flutter): lerp between
			 * consecutive .ani frames by wall-clock time. */
			float fp = t * sm->afps;
			int ia = (int)fp % sm->acount, ib = (ia + 1) % sm->acount;
			r_draw_mesh_lerp(sm->mesh, sm->tex, p, (r_vec3){ sc, sc, sc }, base_yaw + yaw,
			                 sm->afirst + ia, sm->afirst + ib, fp - (int)fp);
		} else {
			r_draw_mesh(sm->mesh, sm->tex, p, (r_vec3){ sc, sc, sc }, base_yaw + yaw);
		}
		if (at) r_alpha_test(0);
	}
	if (!want_additive)
		return;
	/* Environment reflection: re-draw each mesh sphere-mapped + additive over its base. */
	for (int i = 0; i < o->n; i++) {
		const struct obj_submesh *sm = &o->sm[i];
		if (sm->mesh < 0 || sm->env_tex < 0)
			continue;
		float yaw, dz, sc;
		mesh_anim_eval(&sm->anim, t, seed, &yaw, &dz, &sc);
		r_vec3 p = { pos.x + sm->pos.x, pos.y + sm->pos.y, pos.z + sm->pos.z + dz };
		r_draw_mesh_env(sm->mesh, sm->env_tex, p, (r_vec3){ sc, sc, sc }, base_yaw + yaw, 0, 0, 0.0f);
	}
	/* Billboard glow sprites (camera-facing). */
	for (int i = 0; i < o->nbb; i++)
		r_draw_billboard(o->bb[i].tex,
		                 (r_vec3){ pos.x + o->bb[i].pos.x, pos.y + o->bb[i].pos.y, pos.z + o->bb[i].pos.z },
		                 o->bb[i].size, (r_color){ 255, 255, 255, 255 });
}

/* Catmull-Rom interpolation of segment (b..c) with neighbours a,d at u in [0,1]. */
static void catmull3(const float a[3], const float b[3], const float c[3],
                     const float d[3], float u, float out[3])
{
	float u2 = u * u, u3 = u2 * u;
	for (int k = 0; k < 3; k++)
		out[k] = 0.5f * (2.0f * b[k] + (-a[k] + c[k]) * u
		                 + (2.0f * a[k] - 5.0f * b[k] + 4.0f * c[k] - d[k]) * u2
		                 + (-a[k] + 3.0f * b[k] - 3.0f * c[k] + d[k]) * u3);
}

/* SPLINE_DYNAMIC position at time t, AS-WRITTEN coords (Y,X,Z). The path is a closed
 * loop (last waypoint == first), so we drop the duplicate and wrap. */
static void leo_spline_pos(const leo_object *o, float t, float out[3])
{
	int m = o->spline_n - 1;                 /* unique points (loop closes: last==first) */
	if (m < 2) m = o->spline_n;
	float period = o->spline_ms > 0.0f ? o->spline_ms / 1000.0f : 10.0f;
	float u = fmodf(t, period) / period;     /* 0..1 around the loop */
	if (u < 0.0f) u += 1.0f;
	float fseg = u * m;
	int seg = (int)fseg;
	float lu = fseg - seg;
	catmull3(o->spline[(seg - 1 + m) % m], o->spline[seg % m],
	         o->spline[(seg + 1) % m], o->spline[(seg + 2) % m], lu, out);
}

/* draw one .leo object at time t: animated (looped clip frame) if it has an ANI, else
 * static frame 0. A SPLINE_DYNAMIC prop moves along its loop (facing its travel dir);
 * otherwise yaw from rot.z at the entry's cell-centred world pos. */
static void leo_draw_one(const char *b, int i, float t)
{
	const leo_object *o = &g_scene.leo.objs[i];
	r_vec3 wp = { o->pos[1] + 0.5f, o->pos[0] + 0.5f, o->pos[2] };
	float yaw = o->rot[2] * (180.0f / 3.14159265f);
	if (o->spline_n >= 2) {
		float a[3], c[3];
		leo_spline_pos(o, t, a);
		leo_spline_pos(o, t + 0.05f, c);     /* a hair ahead -> travel direction */
		wp = (r_vec3){ a[1] + 0.5f, a[0] + 0.5f, a[2] };
		/* face travel direction. +90 is the mesh nose offset (raw nose = -Y, same as
		 * JOHN_YAW); static props bake this into rot.z, the spline path must add it. */
		yaw = atan2f(c[0] - a[0], c[1] - a[1]) * (180.0f / 3.14159265f) + 90.0f;
	}
	r_tex tx = tex_cached(b, o->tex);
	if (g_scene.leo_amesh[i] >= 0 && g_scene.leo_acount[i] > 0) {
		int fr = g_scene.leo_afirst[i] + (int)(t * g_scene.leo_afps[i]) % g_scene.leo_acount[i];
		r_draw_mesh_frame(g_scene.leo_amesh[i], tx, wp, (r_vec3){ 1, 1, 1 }, yaw, fr);
	} else {
		r_mesh m = mesh_cached(b, o->model);
		if (m >= 0)
			r_draw_mesh(m, tx, wp, (r_vec3){ 1, 1, 1 }, yaw);
	}
}

/* a .leo Model that wants alpha blending (SRCALPHA/INVSRCALPHA) — e.g. the butterfly
 * and fish (32bpp cutout textures). Drawn in the transparent pass, not opaque. */
static bool leo_is_alpha(const leo_object *o)
{
	return strcasecmp(o->src, "srcalpha") == 0 || strcasecmp(o->src, "invsrcalpha") == 0;
}

/* map a world sound position to (volume, pan) for the current camera: nearer =
 * louder, and pan left/right by which side of the view direction it's on. */
/* Movement clip for an animated entity's current step: dedicated stair-transition clips
 * when a stair tile (types 5..8) is involved — much smoother on staircases than the flat
 * walk — else WALK_FWD. Falls back to WALK_FWD if this entity's .ani lacks the clip. */
int move_clip_for(const sim_player *e, const ani_set *A)
{
	int fx = e->fx, fy = e->fy, tx = e->tx, ty = e->ty;
	if (fx < 0 || fy < 0 || tx < 0 || ty < 0
	    || fx >= lvl.dim_x || fy >= lvl.dim_y || tx >= lvl.dim_x || ty >= lvl.dim_y)
		return ANI_WALK_FWD;
	int fz = lvl.tiles[fx][fy].z_pos, tz = lvl.tiles[tx][ty].z_pos;
	if (fz == tz)
		return ANI_WALK_FWD;                          /* flat hop */
	int ft = lvl.tiles[fx][fy].type, tt = lvl.tiles[tx][ty].type;
	bool fs = ft >= TT_STAIR_LO && ft <= TT_STAIR_HI;
	bool ts = tt >= TT_STAIR_LO && tt <= TT_STAIR_HI;
	bool up = tz > fz;
	int clip = (fs && ts)  ? (up ? ANI_STAIR_STAIR_UP : ANI_STAIR_STAIR_DOWN)
	         : (!fs && ts)  ? (up ? ANI_FIELD_STAIR_UP : ANI_FIELD_STAIR_DOWN)
	         : (fs && !ts)  ? (up ? ANI_STAIR_FIELD_UP : ANI_STAIR_FIELD_DOWN)
	                        : ANI_WALK_FWD;
	if (clip != ANI_WALK_FWD && !(A->clips[clip].present && A->clips[clip].count > 0))
		clip = ANI_WALK_FWD;
	return clip;
}

/* frame pair + lerp weight for entity `e`'s clip within anim set A. The clip is
 * progress-synced (advances with move_t / turn_t). clip < 0 or absent -> frame 0. */
static void anim_frame_pair(const ani_set *A, int clip, const sim_player *e,
                            int *fa, int *fb, float *t)
{
	*fa = 0; *fb = 0; *t = 0.0f;
	if (clip < 0 || !A->clips[clip].present || A->clips[clip].count <= 0)
		return;
	const ani_clip *c = &A->clips[clip];
	float prog = e->moving ? e->move_t : e->turn_t;
	float f = prog * (float)c->count;
	if (f > c->count - 0.001f) f = c->count - 0.001f;
	int ia = (int)f;
	*fa = c->first + ia;
	*fb = c->first + (ia + 1) % c->count;
	*t = f - (float)ia;
}

	/* ---- debug picker: left-click ray-cast against tiles/John/crystals/props ---- */
static void scene_pick(const struct render_frame *rf, struct picker *pick)
{
	if (r_mouse_pressed(0)) {
		r_vec3 ro, rd;
		r_mouse_ray(&ro, &rd);
		float best = 1e30f;
		int k = PICK_NONE, px = 0, py = 0;
		for (int x = 0; x < lvl.dim_x; x++)
			for (int y = 0; y < lvl.dim_y; y++) {
				const jjm_tile *t = &lvl.tiles[x][y];
				if (t->type == TT_VOID)
					continue;
				float h = (float)t->z_pos;
				float d = ray_aabb(ro, rd, (r_vec3){x, y, h - g_scene.thk}, (r_vec3){x + 1, y + 1, h});
				if (d >= 0 && d < best) { best = d; k = PICK_TILE; px = x; py = y; }
				if (t->pickup_type == 1 && !sim.picked[x][y]) {
					float dc = ray_aabb(ro, rd, (r_vec3){x + 0.3f, y + 0.3f, h + 0.2f},
					                    (r_vec3){x + 0.7f, y + 0.7f, h + 0.9f});
					if (dc >= 0 && dc < best) { best = dc; k = PICK_CRYSTAL; px = x; py = y; }
				}
			}
		float dj = ray_aabb(ro, rd,
		                    (r_vec3){rf->jw.x + g_scene.jmin.x, rf->jw.y + g_scene.jmin.y, rf->jw.z + g_scene.jmin.z},
		                    (r_vec3){rf->jw.x + g_scene.jmax.x, rf->jw.y + g_scene.jmax.y, rf->jw.z + g_scene.jmax.z});
		if (dj >= 0 && dj < best) { best = dj; k = PICK_JOHN; }
		for (int i = 0; i < g_scene.leo.num; i++) {
			if (strstr(g_scene.leo.objs[i].model, "platte.mdl"))
				continue;
			r_vec3 bmn, bmx;
			if (!mesh_bbox(g_scene.leo.objs[i].model, &bmn, &bmx))
				continue;
			float wx = g_scene.leo.objs[i].pos[1] + 0.5f, wy = g_scene.leo.objs[i].pos[0] + 0.5f, wz = g_scene.leo.objs[i].pos[2];
			float dl = ray_aabb(ro, rd, (r_vec3){wx + bmn.x, wy + bmn.y, wz + bmn.z},
			                    (r_vec3){wx + bmx.x, wy + bmx.y, wz + bmx.z});
			if (dl >= 0 && dl < best) { best = dl; k = PICK_LEO; px = i; }
		}
		pick->kind = k; pick->x = px; pick->y = py;
		if (k == PICK_TILE) {
			const jjm_tile *t = &lvl.tiles[px][py];
			printf("[pick] TILE (%d,%d) z=%d type=%d(0x%02x) clip=%d pickup=%d\n",
			       px, py, t->z_pos, t->type, t->type, t->clip_rule, t->pickup_type);
		} else if (k == PICK_JOHN) {
			printf("[pick] JOHN cell(%d,%d,z%d) rz=%.2f facing=%d crystals=%d %s\n",
			       sim.p.cx, sim.p.cy, lvl.tiles[sim.p.cx][sim.p.cy].z_pos, sim.p.rz,
			       sim.p.facing, sim.p.crystals,
			       sim.p.falling ? "FALLING" : sim.p.moving ? "MOVING" : "idle");
		} else if (k == PICK_CRYSTAL) {
			printf("[pick] CRYSTAL (%d,%d)\n", px, py);
		} else if (k == PICK_LEO) {
			const leo_object *o = &g_scene.leo.objs[px];
			printf("[pick] LEO[%d] %s  pos(gridY,X,z)=%.1f,%.1f,%.1f  rz=%.3f  tex=%s\n",
			       px, o->model, o->pos[0], o->pos[1], o->pos[2], o->rot[2], o->tex);
		} else {
			printf("[pick] (nothing)\n");
		}
	}
}

	/* ---- world tiles (closed slabs; exit tile shows the exit decal) ---- */
static void draw_world_tiles(const struct render_frame *rf)
{
	for (int x = 0; x < lvl.dim_x; x++)
		for (int y = 0; y < lvl.dim_y; y++) {
			jjm_tile *t = &lvl.tiles[x][y];
			/* the bridge ANCHOR (0x12/0x13) is part of the bridge, not solid ground: treat
			 * it like void so it's invisible + a gap until activated (its plank_z is set by
			 * bridges_tick while the bridge is extended). */
			if (t->type == TT_VOID || t->type == TT_BRIDGE_X || t->type == TT_BRIDGE_Y) {
				/* a deployed bridge plank: the theme's OWN Field stack decides — a
			 * solid slab only when the stack has an opaque layer (no bridge Field
			 * at all -> plate slab); Space's One/One force-field and Candy's
			 * translucent pane draw in their passes (draw_bridge_panes). */
			if (sim.plank_z[x][y] >= 0) {
				const struct field_stack *pfs = &g_scene.tile_field[TT_PLANK];
				r_tex pt = pfs->n > 0 ? -1 : g_scene.tex_plate;
				float pturn = 0.0f;
				for (int li = 0; li < pfs->n; li++)
					if (pfs->l[li].blend == FL_OPAQUE && pfs->l[li].tex >= 0) {
						pt = pfs->l[li].tex;
						pturn = pfs->l[li].turn * 1000.0f * rf->t;
						break;
					}
				if (pt >= 0) {
					float ph = (float)sim.plank_z[x][y];
					r_vec3 pc = { x + 0.5f, y + 0.5f, ph - g_scene.thk * 0.5f };
					r_draw_tile(pc, (r_vec3){ 1, 1, g_scene.thk }, pt, g_scene.tex_side,
					            (r_color){255,255,255,255},
					            R_SIDE_NY|R_SIDE_PY|R_SIDE_NX|R_SIDE_PX, pturn, 1);
				}
				}
				continue;
			}
			/* movers are drawn as animated meshes (below), not static slabs */
			if (t->type == TT_ELEVATOR || t->type == TT_MOVER_X || t->type == TT_MOVER_Y)
				continue;
			if (t->type == TT_DESTRUCT) {
				/* no plate slab. Model destruct (Castle) = a mesh at the tile when intact;
				 * Field destruct (Forest/Space) = alpha leaves in the transparent pass. */
				if (!sim.destruct_open[x][y] && g_scene.destruct_obj.n > 0)
					draw_obj(&g_scene.destruct_obj, (r_vec3){ x + 0.5f, y + 0.5f, (float)t->z_pos },
					         0.0f, rf->t, tile_seed(x, y), false);
				continue;
			}
			float h = (float)t->z_pos;
			/* stair tiles (5-8 = 4 facings) ARE the Treppe mesh; don't also
			 * draw a flat platform there (that's the tile poking out beneath). */
			if (t->type >= TT_STAIR_LO && t->type <= TT_STAIR_HI && g_scene.stair_obj.n > 0) {
				float yaw = (float)(t->type - 5) * 90.0f;   /* stair facing */
				/* all stair sub-meshes: the steps + e.g. Castle candle holders */
				draw_obj(&g_scene.stair_obj, (r_vec3){ x + 0.5f, y + 0.5f, h }, yaw, rf->t,
				         tile_seed(x, y), false);
			} else if (t->type == TT_SLIDE && g_scene.tile_obj[TT_SLIDE].n > 0) {
				/* the rutsche chute mesh IS the tile (drawn by the tile_obj pass); no base slab */
			} else {
				/* vine-curtain themes get a normal-thickness solid slab (the dirt
				 * underside sits at a sane depth for freecam, not the full SideHeight);
				 * the vine fringe hangs BELOW it in the cutout side pass. */
				float slab = g_scene.side_alpha ? g_scene.thk * 0.5f : g_scene.thk;
				r_vec3 c = { x + 0.5f, y + 0.5f, h - slab * 0.5f };
				r_vec3 s = { 1.0f, 1.0f, slab };
				float top_turn = 0.0f;
				r_tex top_tex = (t->type > 0 && t->type < 64)
				                ? field_top(t, rf->t, &top_turn) : g_scene.tex_plate;
				int mask = 0;
				if (edge_exposed(&lvl, x, y - 1, t->z_pos)) mask |= R_SIDE_NY;
				if (edge_exposed(&lvl, x, y + 1, t->z_pos)) mask |= R_SIDE_PY;
				if (edge_exposed(&lvl, x - 1, y, t->z_pos)) mask |= R_SIDE_NX;
				if (edge_exposed(&lvl, x + 1, y, t->z_pos)) mask |= R_SIDE_PX;
				/* a full-image cutout Side draws its sides in the batched alpha-test pass below;
				 * keep the dirt underside (never seen in play — original camera locks above —
				 * but grounds freecam and is harmless). */
				r_draw_tile(c, s, top_tex, g_scene.tex_side, (r_color){255,255,255,255},
				            g_scene.side_alpha ? 0 : mask, top_turn, 1);
			}
			/* destructible rock (type 0x17): whole until a blast clears it, then it
			 * Explode-shatters into its triangles (like enemies/bombs) as it goes. */
			if (t->type == TT_OBSTACLE && g_scene.obstacle_obj.n > 0) {
				if (!sim.obstacle_gone[x][y])
					draw_obj(&g_scene.obstacle_obj, (r_vec3){ x + 0.5f, y + 0.5f, h },
					         0.0f, rf->t, tile_seed(x, y), false);
				else if (g_scene.obstacle_shatter[x][y] > 0.0f)   /* shatter the primary mesh */
					r_draw_mesh_shatter(g_scene.obstacle_obj.sm[0].mesh, g_scene.obstacle_obj.sm[0].tex,
					                    (r_vec3){x + 0.5f, y + 0.5f, h}, (r_vec3){1, 1, 1}, 0.0f, 0,
					                    1.0f - g_scene.obstacle_shatter[x][y] / 0.6f);
			}
			/* jumppad (type 0x0e): its jello mesh sits on the floor tile */
			if (t->type == TT_JUMPPAD && g_scene.jumppad_obj.n > 0)
				draw_obj(&g_scene.jumppad_obj, (r_vec3){ x + 0.5f, y + 0.5f, h },
				         0.0f, rf->t, tile_seed(x, y), false);
			/* pickup 1 = crystal (transparent pass); 2 = catcher, 3 = thrower —
			 * both now drawn live from sim.enemies (they move); others = box */
			if (t->pickup_type >= 5 && t->pickup_type < 16 && g_scene.pickup_obj[t->pickup_type].n > 0
			           && !sim.picked[x][y]) {
				/* bonus (heart/freeze/speed/...): float height is the theme's Position Y
				 * (applied by draw_obj), spin/bob/pump the theme's Rotate/Oscillate/Pump/
				 * RandomYAngle — no hardcoded offset or rate. */
				draw_obj(&g_scene.pickup_obj[t->pickup_type], (r_vec3){ x + 0.5f, y + 0.5f, h },
				         0.0f, rf->t, tile_seed(x, y), false);
			} else if (t->pickup_type == PU_SURPRISE && g_scene.surprise_obj.n > 0
			           && !sim.picked[x][y]) {
				/* the surprise box (theme slot 24); its roll happens in the sim
				 * on landing, so it draws as the crate until collected */
				draw_obj(&g_scene.surprise_obj, (r_vec3){ x + 0.5f, y + 0.5f, h },
				         0.0f, rf->t, tile_seed(x, y), false);
			} else if (t->pickup_type && t->pickup_type != 1 && t->pickup_type != 2
			           && t->pickup_type != 3 && t->pickup_type != 100
			           && !sim.picked[x][y]) {
				r_vec3 pc = { x + 0.5f, y + 0.5f, h + 0.5f };   /* unmapped (255) fallback */
				r_draw_box(pc, (r_vec3){0.3f, 0.3f, 0.3f}, (r_color){230, 60, 90, 255});
			}
		}
}

	/* full-image cutout tile sides (Water SIDE64 vine curtain): one alpha-test pass after
	 * the opaque tops — full-texture V so the whole curtain shows, transparent gaps discard. */
static void draw_cutout_sides(void)
{
	if (g_scene.side_alpha) {
		r_alpha_test(1);
		for (int x = 0; x < lvl.dim_x; x++)
			for (int y = 0; y < lvl.dim_y; y++) {
				const jjm_tile *t = &lvl.tiles[x][y];
				if (t->type == 0 || t->type == TT_ELEVATOR || t->type == TT_MOVER_X
				    || t->type == TT_MOVER_Y || t->type == TT_DESTRUCT)
					continue;
				if (t->type >= TT_STAIR_LO && t->type <= TT_STAIR_HI && g_scene.stair_obj.n > 0)
					continue;
				int mask = 0;
				if (edge_exposed(&lvl, x, y - 1, t->z_pos)) mask |= R_SIDE_NY;
				if (edge_exposed(&lvl, x, y + 1, t->z_pos)) mask |= R_SIDE_PY;
				if (edge_exposed(&lvl, x - 1, y, t->z_pos)) mask |= R_SIDE_NX;
				if (edge_exposed(&lvl, x + 1, y, t->z_pos)) mask |= R_SIDE_PX;
				if (!mask)
					continue;
				float h = (float)t->z_pos;
				/* SIDE64 is a complete side: a solid wall in the top ~60% of the texture,
				 * hanging vine tendrils below, and a 1px transparent margin at the very top.
				 * Hang it the full SideHeight (the fringe reaches below the solid slab) and
				 * tuck that transparent top margin a hair above the grass so there's no seam. */
				float drop = g_scene.thk;
				float over = drop * (2.0f / 64.0f);
				r_draw_tile_sides((r_vec3){ x + 0.5f, y + 0.5f, h - drop * 0.5f + over * 0.5f },
				                  (r_vec3){ 1, 1, drop + over }, g_scene.tex_side,
				                  (r_color){ 255, 255, 255, 255 }, mask);
			}
		r_alpha_test(0);
	}
}

	/* ---- John: sim pose, yaw = facing, vertex-morph interpolated frame ---- */
static void draw_john(const struct render_frame *rf)
{
	{
		ani_clip *c = &g_scene.john_anim.clips[rf->anim_clip];
		int fa = 0, fb = 0;
		float t = 0.0f;
		if (c->present && c->count > 0) {
			int ia = (int)rf->anim_frame % c->count;
			int ib = (ia + 1) % c->count;
			t = rf->anim_frame - (float)(int)rf->anim_frame;
			fa = c->first + ia;
			fb = c->first + ib;
		}
		r_vec3 sc = { g_scene.jscale, g_scene.jscale, g_scene.jscale };
		if (!rf->dying)   /* the angel (dead meshes) is drawn additively in the transparent pass */
			for (int i = 0; i < g_scene.john_n; i++)
				r_draw_mesh_lerp(g_scene.john[i].mesh, g_scene.john[i].tex, rf->jw, sc, sim.p.yaw_deg, fa, fb, t);
		/* paraglide, two states while owned:
		 *  - GROUNDED: stowed on the back — the theme's Condition-Paraglide John
		 *    submeshes (Candy schirm3 / Egypt Fallschirm / Space), riding the SAME
		 *    k.ani frames as the body so it tracks every pose.
		 *  - AIRBORNE (falling with a charge, or gliding): held open OVERHEAD — the
		 *    canopy floats above him (the pickup mesh, gentle bob). Both visuals
		 *    coexist per state; themes without a submesh only get the canopy. */
		if (!rf->dying && (sim.inv[PU_PARAGLIDE] > 0 || sim.p.gliding)) {
			bool airborne = sim.p.gliding
			                || (sim.p.falling && sim.inv[PU_PARAGLIDE] > 0);
			if (airborne && g_scene.pickup_obj[PU_PARAGLIDE].n > 0) {
				float bob = 0.06f * sinf(rf->t * 3.0f);
				const struct obj_submesh *sm = &g_scene.pickup_obj[PU_PARAGLIDE].sm[0];
				r_draw_mesh(sm->mesh, sm->tex,
				            (r_vec3){ rf->jw.x, rf->jw.y, rf->jw.z + 1.15f + bob },
				            sc, sim.p.yaw_deg);
			} else if (g_scene.john_glide_n > 0) {
				for (int i = 0; i < g_scene.john_glide_n; i++)
					r_draw_mesh_lerp(g_scene.john_glide[i].mesh, g_scene.john_glide[i].tex,
					                 rf->jw, sc, sim.p.yaw_deg, fa, fb, t);
			}
		}
	}
}

	/* ---- live bombs: the bomb model rides its tile while fusing; at detonation
	 * (fuse >= 2s) it vanishes and the explosion FX takes over. No spin. ---- */
static void draw_bombs(const struct render_frame *rf)
{
	bool space_theme = strcasecmp(lvl.world, "Space") == 0;
	float bomb_yaw = space_theme ? rf->t * 90.0f : 0.0f;   /* Space bombs spin */
	if (g_scene.bomb_mesh >= 0)
		for (int i = 0; i < sim.num_bombs; i++) {
			const sim_bomb *b = &sim.bombs[i];
			if (b->fuse < 2.0f)   /* whole bomb while fusing */
				r_draw_mesh(g_scene.bomb_mesh, g_scene.bomb_tex, (r_vec3){ b->rx, b->ry, b->rz },
				            (r_vec3){ 1, 1, 1 }, bomb_yaw);
			else                  /* detonated: the bomb model bursts into its triangles */
				r_draw_mesh_shatter(g_scene.bomb_mesh, g_scene.bomb_tex, (r_vec3){ b->rx, b->ry, b->rz },
				                    (r_vec3){ 1, 1, 1 }, 0.0f, 0, (b->fuse - 2.0f) / 0.6f);
		}
}

	/* ---- moving platforms + elevators: all sub-meshes at their live positions ---- */
/* a mover's walkable top pane: its Field-stack layers, alpha-tested at the
 * live position (drawn in the opaque region — transparent sorting artifacts
 * would show through a surface the player stands on). */
static void draw_mover_pane(const struct field_stack *fs, float cx, float cy, float z, float t)
{
	if (fs->n == 0)
		return;
	r_alpha_test(1);
	for (int li = 0; li < fs->n; li++) {
		const struct field_layer *l = &fs->l[li];
		if (l->tex < 0)
			continue;
		r_draw_layer(l->tex, cx, cy, z + 0.002f + 0.002f * li, 0.5f,
		             l->turn * 1000.0f * t,
		             l->scroll[0] * 1000.0f * t, l->scroll[1] * 1000.0f * t,
		             l->wobble[0] * 1000.0f * t, l->wobble[1], l->wobble[2],
		             (r_color){ 255, 255, 255, 255 }, 0);
	}
	r_alpha_test(0);
}

static void draw_movers(const struct render_frame *rf)
{
	if (g_scene.platform_obj.n > 0 || g_scene.mover_field[1].n > 0)
		for (int i = 0; i < sim.num_platforms; i++) {
			const sim_platform *pl = &sim.platforms[i];
			float px = pl->hx + 0.5f + pl->dx * pl->t, py = pl->hy + 0.5f + pl->dy * pl->t;
			if (g_scene.platform_obj.n > 0)
				draw_obj(&g_scene.platform_obj, (r_vec3){ px, py, (float)pl->hz },
				         0.0f, rf->t, tile_seed(pl->hx, pl->hy), false);
			/* the Field grid = the walkable top pane (Space platform.tga); the
			 * Model is only the side frame -> hollow without it. */
			draw_mover_pane(&g_scene.mover_field[1], px, py, (float)pl->hz, rf->t);
		}
	if (g_scene.elevator_obj.n > 0 || g_scene.mover_field[0].n > 0)
		for (int i = 0; i < sim.num_elevators; i++) {
			const sim_elevator *e = &sim.elevators[i];
			if (g_scene.elevator_obj.n > 0)
				draw_obj(&g_scene.elevator_obj, (r_vec3){ e->cx + 0.5f, e->cy + 0.5f, e->z },
				         0.0f, rf->t, tile_seed(e->cx, e->cy), false);
			draw_mover_pane(&g_scene.mover_field[0], e->cx + 0.5f, e->cy + 0.5f, e->z, rf->t);
		}
}

	/* per-tile Model sub-meshes (teleporter ring/mesh): opaque part at each such tile. */
static void draw_tile_models_opaque(const struct render_frame *rf)
{
	for (int x = 0; x < lvl.dim_x; x++)
		for (int y = 0; y < lvl.dim_y; y++) {
			int tt = lvl.tiles[x][y].type;
			if (tt < 0 || tt >= 64 || g_scene.tile_obj[tt].n == 0)
				continue;
			draw_obj(&g_scene.tile_obj[tt], (r_vec3){ x + 0.5f, y + 0.5f, (float)lvl.tiles[x][y].z_pos },
			         tt == TT_SLIDE ? slide_yaw(lvl.tiles[x][y].clip_rule) : 0.0f,
			         rf->t, tile_seed(x, y), false);
		}
}

	/* ---- enemies (catchers): WALK synced to movement, TURN to turns, else rest ---- */
static void draw_enemies(void)
{
	if (g_scene.catcher_mesh >= 0) {
		for (int i = 0; i < sim.num_enemies; i++) {
			const sim_player *e = &sim.enemies[i];
			if (e->removed)                       /* bombed enemy: gone for good */
				continue;
			if (e->is_thrower) {                  /* thrower: base+barrel cannon, Kanone.ani hop */
				r_vec3 tp = { e->rx, e->ry, e->rz + g_scene.thrower_off_z };   /* Space robot floats */
				int clip = e->launching ? ANI_JUMP    /* airborne off a jump-pad */
				         : e->falling   ? ANI_FALL
				         : e->moving  ? move_clip_for(e, &g_scene.thrower_anim)
				         : e->turning ? (e->turn_dir > 0 ? ANI_TURN_RIGHT : ANI_TURN_LEFT)
				                      : -1;
				int tfa, tfb;
				float tt;
				anim_frame_pair(&g_scene.thrower_anim, clip, e, &tfa, &tfb, &tt);
				float st = e->dying_t > 0.0f ? 1.0f - e->dying_t / 0.6f : 0.0f;
				for (int m = 0; m < g_scene.thrower_nmesh; m++) {
					if (g_scene.thrower_mesh[m] < 0 || g_scene.thrower_madd[m])   /* glow layers: skip opaque pass */
						continue;
					if (e->dying_t > 0.0f)
						r_draw_mesh_shatter(g_scene.thrower_mesh[m], g_scene.thrower_mtex[m], tp, (r_vec3){1,1,1}, e->yaw_deg, tfa, st);
					else
						r_draw_mesh_lerp(g_scene.thrower_mesh[m], g_scene.thrower_mtex[m], tp, (r_vec3){1,1,1}, e->yaw_deg, tfa, tfb, tt);
				}
				continue;
			}
			int clip = e->glue_t > 0.0f ? ANI_GLUE
			         : e->launching ? ANI_JUMP    /* airborne off a jump-pad */
			         : e->falling   ? ANI_FALL
			         : e->moving  ? move_clip_for(e, &g_scene.catcher_anim)
			         : e->turning ? (e->turn_dir > 0 ? ANI_TURN_RIGHT : ANI_TURN_LEFT)
			                      : -1;
			int efa, efb;
			float et;
			anim_frame_pair(&g_scene.catcher_anim, clip, e, &efa, &efb, &et);
			if (e->dying_t > 0.0f) {   /* Explode FX: mesh flies apart into its triangles */
				float st = 1.0f - e->dying_t / 0.6f;   /* 0 -> 1 */
				r_draw_mesh_shatter(g_scene.catcher_mesh, g_scene.catcher_tex, (r_vec3){e->rx, e->ry, e->rz},
				                    (r_vec3){1, 1, 1}, e->yaw_deg, efa, st);
			} else {
				r_draw_mesh_lerp(g_scene.catcher_mesh, g_scene.catcher_tex, (r_vec3){e->rx, e->ry, e->rz},
				                 (r_vec3){1, 1, 1}, e->yaw_deg, efa, efb, et);
			}
		}
	}
}

	/* ---- extra 3D objects (.leo): pos (gridY,gridX,z) -> world (x,y,z) ---- */
static void draw_leo_opaque(const char *base, const struct render_frame *rf)
{
	for (int i = 0; i < g_scene.leo.num; i++) {
		const leo_object *o = &g_scene.leo.objs[i];
		if (strstr(o->model, "platte.mdl"))   /* redundant floor: tilemap draws it */
			continue;
		if (leo_is_alpha(o))                   /* alpha props drawn later in the transparent pass */
			continue;
		leo_draw_one(base, i, rf->t);
	}
}

	/* ---- picker highlight ---- */
static void draw_pick_highlight(const struct render_frame *rf, struct picker *pick)
{
	if (pick->kind != PICK_NONE) {
		r_vec3 hc = {0}, hs = {0};
		if (pick->kind == PICK_TILE) {
			float h = (float)lvl.tiles[pick->x][pick->y].z_pos;
			hc = (r_vec3){ pick->x + 0.5f, pick->y + 0.5f, h - g_scene.thk * 0.5f };
			hs = (r_vec3){ 1.0f, 1.0f, g_scene.thk };
		} else if (pick->kind == PICK_JOHN) {
			hc = (r_vec3){ rf->jw.x + (g_scene.jmin.x + g_scene.jmax.x) * 0.5f, rf->jw.y + (g_scene.jmin.y + g_scene.jmax.y) * 0.5f, rf->jw.z + (g_scene.jmin.z + g_scene.jmax.z) * 0.5f };
			hs = (r_vec3){ g_scene.jmax.x - g_scene.jmin.x, g_scene.jmax.y - g_scene.jmin.y, g_scene.jmax.z - g_scene.jmin.z };
		} else if (pick->kind == PICK_CRYSTAL) {
			float h = (float)lvl.tiles[pick->x][pick->y].z_pos;
			hc = (r_vec3){ pick->x + 0.5f, pick->y + 0.5f, h + 0.55f };
			hs = (r_vec3){ 0.45f, 0.45f, 0.75f };
		} else {
			const leo_object *o = &g_scene.leo.objs[pick->x];
			r_vec3 bmn, bmx;
			if (mesh_bbox(o->model, &bmn, &bmx)) {
				float wx = o->pos[1] + 0.5f, wy = o->pos[0] + 0.5f, wz = o->pos[2];
				hc = (r_vec3){ wx + (bmn.x + bmx.x) * 0.5f, wy + (bmn.y + bmx.y) * 0.5f, wz + (bmn.z + bmx.z) * 0.5f };
				hs = (r_vec3){ bmx.x - bmn.x, bmx.y - bmn.y, bmx.z - bmn.z };
			}
		}
		r_draw_box_wires(hc, hs, (r_color){255, 240, 60, 255});
	}
}

static void draw_transparent(const char *base, const struct render_frame *rf)
{
	/* Glue tiles. Two theme flavours (matching the .thm Glue object):
	 *  - web themes (Castle): a static web mesh is the *InActive* texture, so it
	 *    appears once the pad is SPENT ("sticks blocking the glue").
	 *  - goo themes (Forest/Candy/Space, no mesh): the sticky floor texture carries
	 *    a Wobble FX -> overlay a gently wobbling copy so the goo "flows". */
	if (g_scene.glue_mesh >= 0) {
		for (int x = 0; x < lvl.dim_x; x++)
			for (int y = 0; y < lvl.dim_y; y++) {
				if (lvl.tiles[x][y].type != TT_GLUE || sim.glue_used[x][y])
					continue;               /* web is the live-pad warning; gone once stepped on */
				float h = (float)lvl.tiles[x][y].z_pos;
				r_draw_mesh(g_scene.glue_mesh, g_scene.glue_tex, (r_vec3){x + 0.5f, y + 0.5f, h},
				            (r_vec3){1, 1, 1}, 0.0f);   /* mesh geometry defines its own size */
			}
	} else if (g_scene.glue_tex >= 0) {
		r_color white = { 255, 255, 255, 255 };
		/* theme Wobble params: [0]=speed (per-ms in the original -> *1000 for /s),
		 * [1],[2]=UV warp amplitude. Fall back to gentle defaults if absent. */
		float rate = g_scene.glue_wobble[0] > 0.0f ? g_scene.glue_wobble[0] * 1000.0f : 1.6f;
		float ampu = g_scene.glue_wobble[1] > 0.0f ? g_scene.glue_wobble[1] : 0.05f;
		float ampv = g_scene.glue_wobble[2] > 0.0f ? g_scene.glue_wobble[2] : 0.05f;
		/* Goo + state frame are ONE coplanar surface: depth-write off so the two
		 * layers don't occlude each other or z-fight — they stack purely by draw
		 * order (goo, then the frame texture that inscribes it). */
		r_depth_write(0);
		for (int x = 0; x < lvl.dim_x; x++)
			for (int y = 0; y < lvl.dim_y; y++) {
				if (lvl.tiles[x][y].type != TT_GLUE)
					continue;
				float cx = x + 0.5f, cy = y + 0.5f, z = (float)lvl.tiles[x][y].z_pos + 0.01f;
				/* per-tile phase + slightly varied rate so pools don't ripple in sync */
				float ph = (x * 7 + y * 13) * 0.9f;
				float w = rf->t * rate * (1.0f + ((x * 5 + y * 3) % 7) * 0.02f) + ph;
				r_draw_tile_wobble(g_scene.glue_tex, cx, cy, z, 0.5f, w, ampu, ampv, white);
				r_tex ov = sim.glue_used[x][y] ? g_scene.glue_spent_tex : g_scene.glue_fresh_tex;
				if (ov >= 0)   /* same z: fresh frame (InActive) or stepped grid (Active) */
					r_draw_tile_scroll(ov, cx, cy, z, 0.5f, 0, 0, 1.0f, white, 0);
			}
		r_flush();          /* commit the glue quads while depth-write is still off */
		r_depth_write(1);
	}
	/* DestructField grid over intact destruct tiles (alpha, coplanar with the tile top). */
	if (g_scene.destruct_tex >= 0) {
		r_color white = { 255, 255, 255, 255 };
		r_depth_write(0);
		for (int x = 0; x < lvl.dim_x; x++)
			for (int y = 0; y < lvl.dim_y; y++) {
				if (lvl.tiles[x][y].type != TT_DESTRUCT || sim.destruct_open[x][y])
					continue;
				r_draw_tile_scroll(g_scene.destruct_tex, x + 0.5f, y + 0.5f,
				                   (float)lvl.tiles[x][y].z_pos + 0.01f, 0.5f, 0, 0, 1.0f, white, 0);
			}
		r_flush();
		r_depth_write(1);
	}
	/* .thm Field ALPHA layers (teleporter swirls, Candy ice sheen, the switch's
	 * shadow + framed hole, ...): every condition-passing alpha layer, in the
	 * theme's declaration order, coplanar over the tile top; the bridge panes
	 * flow along their axis. */
	{
		r_depth_write(0);
		draw_tile_field_layers(FL_ALPHA, rf->t);
		draw_bridge_panes(FL_ALPHA, rf->t);
		r_flush();
		r_depth_write(1);
	}
	if (g_scene.crystal_mesh >= 0) {
		for (int x = 0; x < lvl.dim_x; x++)
			for (int y = 0; y < lvl.dim_y; y++) {
				if (lvl.tiles[x][y].pickup_type != 1 || sim.picked[x][y])
					continue;
				float h = (float)lvl.tiles[x][y].z_pos;
				float yaw, bob, sc;   /* data-driven spin/bob from the theme (Rotate/Oscillate/RandomYAngle) */
				mesh_anim_eval(&g_scene.crystal_anim, rf->t,
				               tile_seed(x, y), &yaw, &bob, &sc);
				r_vec3 cpos = { x + 0.5f, y + 0.5f, h + 0.25f + bob };
				r_draw_mesh(g_scene.crystal_mesh, g_scene.crystal_tex, cpos, (r_vec3){ sc, sc, sc }, yaw);
			}
	}
	/* transparent .leo props: butterflies, fish, rays — 32bpp cutout textures whose
	 * alpha we ignored in the opaque pass (hence the black box). Alpha-blended here
	 * (blend is ALPHA, depth-write on) like the cutout crystals. */
	for (int i = 0; i < g_scene.leo.num; i++) {
		const leo_object *o = &g_scene.leo.objs[i];
		if (!leo_is_alpha(o) || strstr(o->model, "platte.mdl"))
			continue;
		leo_draw_one(base, i, rf->t);
	}
}

static void draw_additive_fx(const struct render_frame *rf)
{
	/* Space bombs spin. draw_bombs' bomb_yaw local does not cross into this pass, so
	 * recompute the identical deterministic value from lvl.world + rf->t. */
	float bomb_yaw = strcasecmp(lvl.world, "Space") == 0 ? rf->t * 90.0f : 0.0f;
	/* CrystalFX spark: the theme's notMovable crystal particle system, bound to the gem
	 * — one static additive sprite at the gem's centre, riding its bob (drawn just toward
	 * the camera so it glows from within the depth-writing gem). Sprite/colour/size/height
	 * are all the .par's; no invented animation. (A subtle pulse looked fine too and may
	 * turn out to be a real .par over-life ramp or the notMovable `N` flag — see notes.) */
	if (g_scene.crystal_fx_tex >= 0)
		for (int x = 0; x < lvl.dim_x; x++)
			for (int y = 0; y < lvl.dim_y; y++) {
				if (lvl.tiles[x][y].pickup_type != 1 || sim.picked[x][y])
					continue;
				float h = (float)lvl.tiles[x][y].z_pos;
				float yaw, bob, sc;   /* same theme anim as the gem, so the spark rides it */
				mesh_anim_eval(&g_scene.crystal_anim, rf->t,
				               tile_seed(x, y), &yaw, &bob, &sc);
				r_vec3 gc = { x + 0.5f, y + 0.5f, h + 0.25f + bob + g_scene.crystal_fx_up };
				float dx = rf->view.pos.x - gc.x, dy = rf->view.pos.y - gc.y, dz = rf->view.pos.z - gc.z;
				float dl = sqrtf(dx * dx + dy * dy + dz * dz);
				if (dl > 1e-4f) { dx /= dl; dy /= dl; dz /= dl; }
				r_draw_billboard(g_scene.crystal_fx_tex,
				                 (r_vec3){ gc.x + dx * 0.20f, gc.y + dy * 0.20f, gc.z + dz * 0.20f },
				                 g_scene.crystal_fx_size, g_scene.crystal_fx_col);
			}
	/* pickup additive layer (Billboard glow flares + Environment reflection shine): the
	 * additive pass of the same generic drawer used for their meshes in the opaque pass. */
	for (int x = 0; x < lvl.dim_x; x++)
		for (int y = 0; y < lvl.dim_y; y++) {
			int pk = lvl.tiles[x][y].pickup_type;
			if (pk < 5 || pk >= 16 || sim.picked[x][y] || g_scene.pickup_obj[pk].n == 0)
				continue;
			float h = (float)lvl.tiles[x][y].z_pos;
			draw_obj(&g_scene.pickup_obj[pk], (r_vec3){ x + 0.5f, y + 0.5f, h },
			         0.0f, rf->t, tile_seed(x, y), true);
		}
	/* jump pad jelly: its Environment reflection shine (Candy jelly ships a
	 * reflect_stark32 Environment layer) — the additive env pass of the same drawer
	 * that renders the jelly opaquely in the tile loop. The shine belongs to the
	 * MESH, not the host tile. */
	if (g_scene.jumppad_obj.n > 0)
		for (int x = 0; x < lvl.dim_x; x++)
			for (int y = 0; y < lvl.dim_y; y++)
				if (lvl.tiles[x][y].type == TT_JUMPPAD)
					draw_obj(&g_scene.jumppad_obj,
					         (r_vec3){ x + 0.5f, y + 0.5f, (float)lvl.tiles[x][y].z_pos },
					         0.0f, rf->t, tile_seed(x, y), true);
	/* .thm Field ADDITIVE layers (jump-pad glow, ice shimmer pair, the exits'
	 * two Flash glows + Condition-Active open ring, Egypt's teleporter wobble
	 * layer, ...) with Pulse/Flash brightness, plus the bridge glow panes. All
	 * straight from each theme's own stack. */
	draw_tile_field_layers(FL_ADD, rf->t);
	draw_bridge_panes(FL_ADD, rf->t);
	/* per-tile Model additive sub-meshes (teleporter glowing ring, One/One). */
	for (int x = 0; x < lvl.dim_x; x++)
		for (int y = 0; y < lvl.dim_y; y++) {
			int tt = lvl.tiles[x][y].type;
			if (tt < 0 || tt >= 64 || g_scene.tile_obj[tt].n == 0)
				continue;
			draw_obj(&g_scene.tile_obj[tt], (r_vec3){ x + 0.5f, y + 0.5f, (float)lvl.tiles[x][y].z_pos },
			         tt == TT_SLIDE ? slide_yaw(lvl.tiles[x][y].clip_rule) : 0.0f,
			         rf->t, tile_seed(x, y), true);
		}
	/* John's Environment shine (Castle knight's helmet/armor): the
	 * reflection layer of his sub-meshes, riding the same animated frame as the body. */
	if (!rf->dying) {
		ani_clip *jc = &g_scene.john_anim.clips[rf->anim_clip];
		int jfa = 0, jfb = 0;
		float jt = 0.0f;
		if (jc->present && jc->count > 0) {
			int ia = (int)rf->anim_frame % jc->count;
			jfa = jc->first + ia;
			jfb = jc->first + (ia + 1) % jc->count;
			jt = rf->anim_frame - (float)(int)rf->anim_frame;
		}
		r_vec3 jsc = { g_scene.jscale, g_scene.jscale, g_scene.jscale };
		for (int i = 0; i < g_scene.john_n; i++)
			if (g_scene.john[i].env_tex >= 0)
				r_draw_mesh_env(g_scene.john[i].mesh, g_scene.john[i].env_tex,
				                rf->jw, jsc, sim.p.yaw_deg, jfa, jfb, jt);
	}
	/* bomb fuse spark: an additive flicker at the bomb top (theme Bomb par puts
	 * lunte2/star3_32 at +0.387); dies out once it detonates. */
	if (g_scene.fx_star >= 0)
		for (int i = 0; i < sim.num_bombs; i++) {
			const sim_bomb *b = &sim.bombs[i];
			if (b->fuse >= 2.0f)
				continue;
			float fl = 0.12f + 0.05f * sinf(rf->t * 40.0f + i);
			r_draw_billboard(g_scene.fx_star, (r_vec3){ b->rx, b->ry, b->rz + 0.4f }, fl,
			                 (r_color){ 255, 210, 120, 255 });
		}
	/* Space bomb glow: the additive halo mesh (bomb01_gl), spinning with the bomb. */
	if (g_scene.bomb_glow_mesh >= 0)
		for (int i = 0; i < sim.num_bombs; i++) {
			const sim_bomb *b = &sim.bombs[i];
			if (b->fuse >= 2.0f)
				continue;
			r_draw_mesh(g_scene.bomb_glow_mesh, g_scene.bomb_glow_tex, (r_vec3){ b->rx, b->ry, b->rz },
			            (r_vec3){ 1, 1, 1 }, bomb_yaw);
		}
	/* Space thrower headlight: the additive light-rays sub-mesh (a forward beam glow),
	 * drawn here in the additive pass — it's the mesh skipped by the opaque draw. */
	for (int i = 0; i < sim.num_enemies && g_scene.thrower_nmesh > 0; i++) {
		const sim_player *e = &sim.enemies[i];
		if (!e->is_thrower || e->removed || e->dying_t > 0.0f)
			continue;
		r_vec3 tp = { e->rx, e->ry, e->rz + g_scene.thrower_off_z };
		for (int m = 0; m < g_scene.thrower_nmesh; m++)
			if (g_scene.thrower_madd[m] && g_scene.thrower_mesh[m] >= 0)
				r_draw_mesh_lerp(g_scene.thrower_mesh[m], g_scene.thrower_mtex[m], tp,
				                 (r_vec3){ 1, 1, 1 }, e->yaw_deg, 0, 0, 0.0f);
	}
	fx_draw(R_BLEND_ADD);
	r_set_blend(R_BLEND_ALPHA);   /* flushes the additive batch (still depth-write off) */
	fx_draw(R_BLEND_ALPHA);       /* masked sprites (bees): normal transparency */
}

	/* death/angel: John's Dead-condition meshes (grey body + fluegel wings)
	 * rendered ADDITIVELY as the spirit ascends (rf->jw.z already raised). */
static void draw_death_angel(const struct render_frame *rf)
{
	if (rf->dying && g_scene.john_dead_n > 0) {
		r_set_blend(R_BLEND_ADD);
		r_vec3 sc = { g_scene.jscale, g_scene.jscale, g_scene.jscale };
		int ghost_fr = g_scene.john_anim.clips[ANI_GHOST].present ? g_scene.john_anim.clips[ANI_GHOST].first : 0;
		for (int i = 0; i < g_scene.john_dead_n; i++) {
			/* body holds the GHOST float pose; wings flap (loop their frames) */
			int fr = g_scene.john_dead_wings[i]
			         ? (g_scene.john_dead_nf[i] > 1 ? (int)(rf->death_t * 18.0f) % g_scene.john_dead_nf[i] : 0)
			         : ghost_fr;
			r_draw_mesh_frame(g_scene.john_dead[i].mesh, g_scene.john_dead[i].tex, rf->jw, sc, sim.p.yaw_deg, fr);
		}
		r_set_blend(R_BLEND_ALPHA);   /* restore before 2D/HUD */
	}
}

void render_scene_3d(const char *base, const struct render_frame *rf,
                     struct picker *pick)
{
	r_set_camera(rf->view);
	r_draw_skybox(g_scene.sky);

	/* ---- debug picker: left-click ray-cast against tiles/John/crystals/props ---- */
	scene_pick(rf, pick);

	/* ---- world tiles (opaque slabs) ---- */
	draw_world_tiles(rf);

	/* ---- full-image cutout tile sides (vine curtain alpha-test pass) ---- */
	draw_cutout_sides();

	/* ---- John (opaque body) ---- */
	draw_john(rf);

	/* ---- live bombs ---- */
	draw_bombs(rf);

	/* ---- moving platforms + elevators ---- */
	draw_movers(rf);

	/* ---- per-tile Model sub-meshes opaque (teleporter/slide) ---- */
	draw_tile_models_opaque(rf);

	/* ---- enemies (catchers/throwers) ---- */
	draw_enemies();

	/* ---- extra 3D .leo objects opaque ---- */
	draw_leo_opaque(base, rf);

	/* ---- picker highlight ---- */
	draw_pick_highlight(rf, pick);

	/* ---- transparent pass: alpha-blended overlays ---- */
	r_begin_transparent();
	draw_transparent(base, rf);
	r_flush();

	/* additive FX: crystal glow, pulsing exit glow, sparkle particles.
	 * Depth WRITE off (test stays on): transparent quads mustn't box-clip each
	 * other via the depth buffer. Gem was already drawn with depth write on. */
	r_set_blend(R_BLEND_ADD);
	r_depth_write(0);
	draw_additive_fx(rf);
	r_flush();                    /* commit them while depth-write is still off */
	r_depth_write(1);             /* re-enable only AFTER the flush */

	/* ---- death/angel meshes ---- */
	draw_death_angel(rf);
	r_end_3d();
}
