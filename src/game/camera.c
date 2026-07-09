/* Debug free-look camera + third-person follow/death cameras — see camera.h.
 * Moved verbatim out of main.c (no logic change). */
#include "game/camera.h"

#include <math.h>

static r_vec3 v_madd(r_vec3 a, r_vec3 d, float s)
{
	return (r_vec3){ a.x + d.x * s, a.y + d.y * s, a.z + d.z * s };
}

void freecam_sync(freecam *fc, r_camera cam)
{
	r_vec3 d = { cam.target.x - cam.pos.x, cam.target.y - cam.pos.y, cam.target.z - cam.pos.z };
	float len = sqrtf(d.x*d.x + d.y*d.y + d.z*d.z);
	if (len < 1e-5f)
		len = 1.0f;
	fc->eye   = cam.pos;
	fc->yaw   = atan2f(d.y, d.x);
	fc->pitch = asinf(d.z / len);
}

r_camera freecam_view(const freecam *fc, float fovy)
{
	float cp = cosf(fc->pitch), sp = sinf(fc->pitch);
	float cy = cosf(fc->yaw),   sy = sinf(fc->yaw);
	r_vec3 fwd = { cp * cy, cp * sy, sp };
	r_camera c = {
		.pos    = fc->eye,
		.target = v_madd(fc->eye, fwd, 1.0f),
		.up     = { 0, 0, 1 },
		.fovy   = fovy,
	};
	return c;
}

/* WASD move, Q/E down/up, hold RIGHT mouse to look, wheel to dolly; dt seconds */
void freecam_update(freecam *fc, float dt)
{
	if (r_mouse_down(1)) {
		float mdx, mdy;
		r_mouse_delta(&mdx, &mdy);
		fc->yaw   -= mdx * 0.004f;
		fc->pitch -= mdy * 0.004f;
		float lim = 1.55f;                     /* ~89 deg */
		if (fc->pitch >  lim) fc->pitch =  lim;
		if (fc->pitch < -lim) fc->pitch = -lim;
	}

	float cp = cosf(fc->pitch), sp = sinf(fc->pitch);
	float cy = cosf(fc->yaw),   sy = sinf(fc->yaw);
	r_vec3 fwd   = { cp * cy, cp * sy, sp };
	r_vec3 right = { sy, -cy, 0 };             /* fwd x world-up on XY plane */
	r_vec3 up    = { 0, 0, 1 };

	float spd = (r_key_down(R_KEY_SHIFT) ? 24.0f : 8.0f) * dt;
	if (r_key_down(R_KEY_W)) fc->eye = v_madd(fc->eye, fwd,    spd);
	if (r_key_down(R_KEY_S)) fc->eye = v_madd(fc->eye, fwd,   -spd);
	if (r_key_down(R_KEY_D)) fc->eye = v_madd(fc->eye, right,  spd);
	if (r_key_down(R_KEY_A)) fc->eye = v_madd(fc->eye, right, -spd);
	if (r_key_down(R_KEY_E)) fc->eye = v_madd(fc->eye, up,     spd);
	if (r_key_down(R_KEY_Q)) fc->eye = v_madd(fc->eye, up,    -spd);

	float wheel = r_mouse_wheel();
	if (wheel != 0.0f)
		fc->eye = v_madd(fc->eye, fwd, wheel * 2.0f);
}

/* third-person boom/follow camera (occlusion-aware). Behaviour verbatim from main.c. */
r_camera follow_cam(const jjm_level *l, r_vec3 jw, float fwd_x, float fwd_y,
                    struct cam_smooth *cs, float dt, bool freecam_active, bool dying)
{
	if (!freecam_active) {
		float k = 1.0f - expf(-10.0f * dt);
		cs->fx += (fwd_x - cs->fx) * k;
		cs->fy += (fwd_y - cs->fy) * k;
		float len = sqrtf(cs->fx * cs->fx + cs->fy * cs->fy);
		if (len > 1e-4f) { cs->fx /= len; cs->fy /= len; }
	}
	/* Boom camera: eye sits `BOOM_D` behind John along a pitch angle. When a wall
	 * occludes the line from John to the eye, the pitch eases toward overhead so the
	 * camera rises above the obstruction and looks down (never through it) — per RE
	 * (occlusion raycast -> pitch snaps toward vertical). Tested at the DEFAULT pitch
	 * (a fixed reference) so it doesn't flip-flop once it's already swung up. */
	const float BOOM_D = 7.1f, PIT_DEF = 0.686f, PIT_OVER = 1.5691f;  /* ~39deg / ~89.9deg (per RE) */
	r_vec3 piv = { jw.x, jw.y, jw.z + 1.0f };   /* boom pivot ~ John's body centre */
	if (!freecam_active && !dying) {
		float ch = cosf(PIT_DEF), sh = sinf(PIT_DEF);
		r_vec3 de = { piv.x - cs->fx * BOOM_D * ch, piv.y - cs->fy * BOOM_D * ch,
		              piv.z + BOOM_D * sh };
		bool blocked = false;
		for (int s = 1; s <= 20 && !blocked; s++) {
			float u = (float)s / 20.0f;
			float sx = piv.x + (de.x - piv.x) * u;
			float sy = piv.y + (de.y - piv.y) * u;
			float sz = piv.z + (de.z - piv.z) * u;
			int cxx = (int)floorf(sx), cyy = (int)floorf(sy);
			if (cxx < 0 || cyy < 0 || cxx >= l->dim_x || cyy >= l->dim_y)
				continue;
			if (l->tiles[cxx][cyy].type == TT_VOID)
				continue;
			if (sz < (float)l->tiles[cxx][cyy].z_pos - 0.05f)
				blocked = true;   /* sample is inside a solid slab */
		}
		float want = blocked ? PIT_OVER : PIT_DEF;
		cs->pitch += (want - cs->pitch) * (1.0f - expf(-6.0f * dt));
	}
	float pch = cosf(cs->pitch), psh = sinf(cs->pitch);
	return (r_camera){
		.pos    = { piv.x - cs->fx * BOOM_D * pch, piv.y - cs->fy * BOOM_D * pch,
		            piv.z + BOOM_D * psh },
		.target = { jw.x + cs->fx * 2.0f, jw.y + cs->fy * 2.0f, jw.z + 1.2f },
		.up     = { 0, 0, 1 },
		.fovy   = 50.0f,
	};
}

/* frozen death overview latched on the death cell (see camera.h). */
r_camera death_cam(float dx, float dy, float dz, float a0)
{
	const float el = 60.0f * 0.01745329f, dd = 7.0f;   /* elevation / distance defaults */
	float hz = dd * cosf(el);
	return (r_camera){
		.pos    = { dx + cosf(a0) * hz, dy + sinf(a0) * hz, dz + dd * sinf(el) },
		.target = { dx, dy, dz + 1.2f },
		.up     = { 0, 0, 1 },
		.fovy   = 50.0f,
	};
}
