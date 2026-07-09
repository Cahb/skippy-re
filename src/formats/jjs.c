/* .jjs instruction-script VM — see jjs.h for the RE'd camera model. Statements are
 * re-parsed as they run; the VM computes a full camera (eye + look-at) each tick from
 * either the orbit model or an explicit fly-through, plus caption/wave requests. */
#include "formats/jjs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strcasecmp */
#include <ctype.h>
#include <math.h>

/* orbit tuning (world units / radians) */
#define JJS_ELEV_DEG   60.0f    /* orbit elevation default (RE: elev_deg +0x2ab576 = 60, range [50,89]) */
#define JJS_AUTO_ROT   1.0f     /* observe-2 orbit rate: EXACTLY 1 rad/s. RE 0x404179:
                                 * azimuth = fmod(azimuth + dt_ms * 0.001, 2pi) per frame
                                 * (0x45d308 = 0.001, 0x45d300 = 2pi, 0x45103a = fmod). */
#define JJS_DIST_DEF   7.0f     /* default follow distance (RE: base 7.0) */
#define JJS_DIST_LERP  5.0f     /* distance glide stiffness (RE: 0.005/ms, 0x45d32c) */
#define JJS_TGT_LERP   4.0f     /* look-at glide stiffness (RE: 0.004/ms, 0x45d340) */
#define JJS_TGT_Z      0.6f     /* look-at height above the observed actor's origin */
#define JJS_DEG2RAD    0.0174532925f

/* .jjs xyz -> our world xyz. Like the .jjm/.leo files, .jjs uses (gridY, gridX,
 * height) — so swap x/y into our tiles[x][y] world; height (z-up) stays. */
static void map_xyz(const float in[3], float out[3])
{
	/* +0.5 puts the point at the CELL CENTRE — the whole engine renders grid cell
	 * (gx,gy) at world (gx+0.5, gy+0.5, z), so a script look-at must match or it
	 * lands half a cell off (very visible when `distance` zooms in tight). */
	out[0] = in[1] + 0.5f;   /* our X = file gridX (2nd component) */
	out[1] = in[0] + 0.5f;   /* our Y = file gridY (1st component) */
	out[2] = in[2];          /* height is already a world Z, not a cell index */
}

/* parse up to `n` comma-separated floats from `s` into `out`; returns count read. */
static int parse_floats(const char *s, float *out, int n)
{
	int i = 0;
	while (i < n && *s) {
		while (*s && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n'))
			s++;
		if (!*s || (!isdigit((unsigned char)*s) && *s != '-' && *s != '+' && *s != '.'))
			break;
		out[i++] = (float)atof(s);
		const char *c = strchr(s, ',');
		if (!c)
			break;
		s = c + 1;
	}
	return i;
}

bool jjs_load(const char *path, jjs_vm *vm)
{
	memset(vm, 0, sizeof *vm);
	vm->distance  = JJS_DIST_DEF;
	vm->dist_want = JJS_DIST_DEF;
	vm->elevation = JJS_ELEV_DEG * JJS_DEG2RAD;
	FILE *f = fopen(path, "rb");
	if (!f)
		return false;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (sz <= 0) { fclose(f); return false; }
	char *buf = malloc((size_t)sz + 1);
	if (!buf) { fclose(f); return false; }
	size_t got = fread(buf, 1, (size_t)sz, f);
	buf[got] = 0;
	fclose(f);

	/* split on ';' into statements (internal newlines preserved for text/spline) */
	char *p = buf;
	while (*p && vm->n_stmt < JJS_MAX_STMT) {
		char *semi = strchr(p, ';');
		size_t len = semi ? (size_t)(semi - p) : strlen(p);
		while (len && (p[0] == '\r' || p[0] == '\n' || p[0] == ' ' || p[0] == '\t')) { p++; len--; }
		if (len > 0 && len < JJS_STMT_LEN) {
			memcpy(vm->stmt[vm->n_stmt], p, len);
			vm->stmt[vm->n_stmt][len] = 0;
			vm->n_stmt++;
		}
		if (!semi)
			break;
		p = semi + 1;
	}
	free(buf);
	return vm->n_stmt > 0;
}

void jjs_seed(jjs_vm *vm, const float eye[3], const float look[3])
{
	/* open framed on the overview: look-at from `look`, orbit azimuth from where `eye`
	 * sits, and the current distance from the overview span so `distance N` glides in. */
	vm->cam_tgt[0] = vm->exp_tgt[0] = look[0];
	vm->cam_tgt[1] = vm->exp_tgt[1] = look[1];
	vm->cam_tgt[2] = vm->exp_tgt[2] = look[2];
	vm->cam_pos[0] = vm->exp_pos[0] = eye[0];
	vm->cam_pos[1] = vm->exp_pos[1] = eye[1];
	vm->cam_pos[2] = vm->exp_pos[2] = eye[2];
	float dx = eye[0] - look[0], dy = eye[1] - look[1], dz = eye[2] - look[2];
	vm->azimuth  = atan2f(dy, dx);
	vm->distance = sqrtf(dx * dx + dy * dy + dz * dz);
	if (vm->distance < 1.0f)
		vm->distance = JJS_DIST_DEF;
}

/* de Casteljau evaluation of the Bézier defined by n control points at u in [0,1]. */
static void bezier(float pts[][3], int n, float u, float out[3])
{
	float tmp[JJS_MAX_CTRL][3];
	if (n > JJS_MAX_CTRL) n = JJS_MAX_CTRL;
	memcpy(tmp, pts, (size_t)n * 3 * sizeof(float));
	for (int k = 1; k < n; k++)
		for (int i = 0; i < n - k; i++)
			for (int c = 0; c < 3; c++)
				tmp[i][c] = tmp[i][c] * (1.0f - u) + tmp[i + 1][c] * u;
	out[0] = tmp[0][0]; out[1] = tmp[0][1]; out[2] = tmp[0][2];
}

/* execute one statement; returns true if it blocks the program counter this frame. */
static bool jjs_exec(jjs_vm *vm, const char *st)
{
	while (*st == ' ' || *st == '\t' || *st == '\r' || *st == '\n')
		st++;
	if (st[0] == '/' && st[1] == '/')
		return false;
	char kw[24];
	int i = 0;
	while (st[i] && !isspace((unsigned char)st[i]) && i < 23) { kw[i] = st[i]; i++; }
	kw[i] = 0;
	const char *a = st + i;
	while (*a == ' ' || *a == '\t')
		a++;

	if (!strcasecmp(kw, "setcamposxyz")) {
		/* explicit eye position — switch to fly-through mode. */
		float p[3] = {0}; parse_floats(a, p, 3); map_xyz(p, vm->exp_pos);
		vm->eye_explicit = true;
		vm->spline_on = false;
	} else if (!strcasecmp(kw, "setcamtargetxyz") || !strcasecmp(kw, "gotoxyz")) {
		/* explicit look-at — stop tracking the observed actor. */
		float p[3] = {0}; parse_floats(a, p, 3); map_xyz(p, vm->exp_tgt);
		vm->tgt_explicit = true;
		vm->move_on = false;
	} else if (!strcasecmp(kw, "movetoxyz")) {
		/* glide the look-at to a point at `speed` cells/sec — blocks until it arrives. */
		float p[4] = {0}; parse_floats(a, p, 4); map_xyz(p, vm->move_dst);
		if (!vm->tgt_explicit) {   /* seed the glide from wherever we're looking now */
			vm->exp_tgt[0] = vm->cam_tgt[0]; vm->exp_tgt[1] = vm->cam_tgt[1]; vm->exp_tgt[2] = vm->cam_tgt[2];
		}
		vm->move_speed  = p[3] > 0.0f ? p[3] : 2.0f;
		vm->tgt_explicit = true;
		vm->move_on = true;
		return true;
	} else if (!strcasecmp(kw, "observe")) {
		/* RE (script_parse_command 0x41dbe0 + tick 0x41d920): the argument only
		 * distinguishes 0 (script camera OFF, gameplay follow resumes) from non-0
		 * (ON; also ends a spline -> back to orbit). There is NO actor tracking —
		 * the look-at stays whatever gotoxyz/movetoxyz owns, so the observe 2/1
		 * toggles around every move (Castle TimeBonus) never recenter on John. */
		vm->observe = atoi(a);
		vm->eye_explicit = false;
		vm->spline_on = false;
	} else if (!strcasecmp(kw, "distance")) {
		vm->dist_want = (float)atof(a);
		if (vm->dist_want < 1.0f) vm->dist_want = 1.0f;
	} else if (!strcasecmp(kw, "anglexyz")) {
		/* extra orbit-angle offset: use the yaw component as an azimuth nudge. */
		float p[3] = {0}; parse_floats(a, p, 3);
		vm->azimuth += p[2] * JJS_DEG2RAD;
	} else if (!strcasecmp(kw, "fromhere")) {
		vm->loop_anchor = vm->pc;             /* pc already points past this stmt */
	} else if (!strcasecmp(kw, "splinexyz")) {
		/* explicit eye fly-through along a Bézier. NON-blocking: a following `wait`
		 * holds the sequence while it animates (spline_dur ~ that wait). */
		vm->spline_dur = (float)atof(a);      /* SECONDS of travel */
		if (vm->spline_dur <= 0.0f) vm->spline_dur = 1.0f;
		/* control points: seed from current eye, then every following x,y,z line */
		vm->spline[0][0] = vm->exp_pos[0]; vm->spline[0][1] = vm->exp_pos[1]; vm->spline[0][2] = vm->exp_pos[2];
		vm->spline_n = 1;
		const char *nl = strchr(a, '\n');    /* waypoints start after the duration line */
		while (nl && vm->spline_n < JJS_MAX_CTRL) {
			const char *line = nl + 1;
			float p[3] = {0};
			if (parse_floats(line, p, 3) == 3) {
				map_xyz(p, vm->spline[vm->spline_n]);
				vm->spline_n++;
			}
			nl = strchr(line, '\n');
		}
		vm->spline_t = 0.0f;
		vm->spline_on = vm->spline_n >= 2;
		vm->eye_explicit = true;
	} else if (!strcasecmp(kw, "wait")) {
		vm->wait_left = (float)atof(a);       /* SECONDS — the only blocker */
		return vm->wait_left > 0.0f;
	} else if (!strcasecmp(kw, "text")) {
		while (*a == '\r' || *a == '\n')      /* body starts after the "text" line */
			a++;
		snprintf(vm->text, sizeof vm->text, "%s", a);
	} else if (!strcasecmp(kw, "initwave")) {
		char path[JJS_WAVE_LEN]; int slot = -1;
		if (sscanf(a, "%127s %d", path, &slot) >= 2 && slot >= 0 && slot < JJS_MAX_WAVE)
			snprintf(vm->wave[slot], JJS_WAVE_LEN, "%s", path);
	} else if (!strcasecmp(kw, "playwave")) {
		int slot = atoi(a);
		if (slot >= 0 && slot < JJS_MAX_WAVE && vm->wave[slot][0])
			snprintf(vm->want_wave, sizeof vm->want_wave, "%s", vm->wave[slot]);
	} else if (!strcasecmp(kw, "again")) {
		vm->pc = vm->loop_anchor;
	} else if (!strcasecmp(kw, "break")) {
		vm->done = true;
	}
	return false;
}

/* recompute cam_pos/cam_tgt from the current model state. The look-at NEVER
 * snaps: the original's orbit path lerps the live target toward the focus every
 * frame (0.004/ms, const 0x45d340). The focus is script-owned (explicit target /
 * moveto glide) from the first target command on; the actor fallback below only
 * covers the pre-first-command state, mirroring the original's holdover of the
 * gameplay follow focus. */
static void jjs_camera(jjs_vm *vm, const float actor_pos[3], float dt)
{
	/* focus: explicit point, else the observed actor, else hold. */
	float want[3] = { vm->cam_tgt[0], vm->cam_tgt[1], vm->cam_tgt[2] };
	if (vm->tgt_explicit) {
		want[0] = vm->exp_tgt[0];
		want[1] = vm->exp_tgt[1];
		want[2] = vm->exp_tgt[2];
	} else if (vm->observe && actor_pos) {
		want[0] = actor_pos[0];
		want[1] = actor_pos[1];
		want[2] = actor_pos[2] + JJS_TGT_Z;
	}
	float k = 1.0f - expf(-JJS_TGT_LERP * dt);
	vm->cam_tgt[0] += (want[0] - vm->cam_tgt[0]) * k;
	vm->cam_tgt[1] += (want[1] - vm->cam_tgt[1]) * k;
	vm->cam_tgt[2] += (want[2] - vm->cam_tgt[2]) * k;

	/* eye: explicit fly-through position, else orbit around the look-at. */
	if (vm->eye_explicit) {
		vm->cam_pos[0] = vm->exp_pos[0];
		vm->cam_pos[1] = vm->exp_pos[1];
		vm->cam_pos[2] = vm->exp_pos[2];
	} else {
		float h = vm->distance * cosf(vm->elevation);
		vm->cam_pos[0] = vm->cam_tgt[0] + h * cosf(vm->azimuth);
		vm->cam_pos[1] = vm->cam_tgt[1] + h * sinf(vm->azimuth);
		vm->cam_pos[2] = vm->cam_tgt[2] + vm->distance * sinf(vm->elevation);
	}
}

void jjs_tick(jjs_vm *vm, float dt, const float actor_pos[3])
{
	vm->want_wave[0] = 0;
	if (vm->done) {
		jjs_camera(vm, actor_pos, dt);   /* keep framing after the script ends */
		return;
	}

	/* --- advance continuous animation every frame (independent of sequencing) --- */
	if (vm->spline_on) {
		vm->spline_t += dt;
		float u = vm->spline_t / vm->spline_dur;
		if (u >= 1.0f) { u = 1.0f; vm->spline_on = false; }
		bezier(vm->spline, vm->spline_n, u, vm->exp_pos);
	}
	if (vm->move_on) {
		float d[3] = { vm->move_dst[0] - vm->exp_tgt[0], vm->move_dst[1] - vm->exp_tgt[1],
		               vm->move_dst[2] - vm->exp_tgt[2] };
		float dist = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
		float step = vm->move_speed * dt;
		if (dist <= step || dist < 1e-4f) {
			vm->exp_tgt[0] = vm->move_dst[0]; vm->exp_tgt[1] = vm->move_dst[1]; vm->exp_tgt[2] = vm->move_dst[2];
			vm->move_on = false;
		} else {
			vm->exp_tgt[0] += d[0] / dist * step;
			vm->exp_tgt[1] += d[1] / dist * step;
			vm->exp_tgt[2] += d[2] / dist * step;
		}
	}
	/* distance glides toward the desired follow radius. The azimuth only
	 * auto-orbits in observe 2: the observe value is the CAMERA-MODE byte
	 * (update_script_camera 0x418c70 copies it into +0x28ab2d each frame) —
	 * mode 1 holds the yaw where it is (parked shot, e.g. behind John),
	 * mode 2 lets it run (the rotating crystal shot in Castle TimeBonus). */
	vm->distance += (vm->dist_want - vm->distance) * (1.0f - expf(-JJS_DIST_LERP * dt));
	if (vm->observe == 2)
		vm->azimuth += JJS_AUTO_ROT * dt;

	jjs_camera(vm, actor_pos, dt);   /* EVERY frame — waits/movetos below return early */

	/* --- sequencing: `wait` and an in-flight `moveto` hold the program counter --- */
	if (vm->wait_left > 0.0f) {
		vm->wait_left -= dt;
		if (vm->wait_left > 0.0f)
			return;
	}
	if (vm->move_on)
		return;

	/* run statements until one blocks (wait / moveto) or the script ends */
	for (int guard = 0; !vm->done && guard < JJS_MAX_STMT * 2; guard++) {
		if (vm->pc >= vm->n_stmt) { vm->done = true; break; }
		const char *st = vm->stmt[vm->pc];
		vm->pc++;
		if (jjs_exec(vm, st))
			break;
	}
	jjs_camera(vm, actor_pos, 0.0f);   /* refresh eye/mode from this frame's commands
	                                    * (dt already applied above — no double-lerp) */
}
