/* Game audio (see game/audio.h). Split out of main.c: SFX event dispatch + the
 * .leo positional ambience mix + the background-music (CD track) manager. */
#include "game/audio.h"
#include "game/scene.h"
#include "formats/cdt.h"
#include "formats/asset.h"

#include <math.h>
#include <stdio.h>

/* background music: the CD-audio analogue. JJ.CDT maps names -> track numbers;
 * the ripped tracks live at <base>/audio/TrackNN.wav. Loaded lazily once. */
static cdt_map g_cdt;
static int     g_cdt_state = 0;   /* 0 = not tried, 1 = loaded, -1 = absent */
static int     g_track_cur = 0;   /* track number currently playing (0 = none) */

void audio_music_reset(void)
{
	r_track_stop();
	g_track_cur = 0;
}

void audio_music_want(const char *base, const char *name)
{
	char p[1024];
	if (g_cdt_state == 0)
		g_cdt_state = (asset_resolve(base, "CDTracks\\JJ.CDT", p, sizeof p)
		               && cdt_load(p, &g_cdt)) ? 1 : -1;
	int tr = (g_cdt_state > 0 && name) ? cdt_track_for(&g_cdt, name) : 0;
	if (tr == g_track_cur)
		return;                   /* same-world reload: the track just keeps playing */
	g_track_cur = tr;
	if (tr <= 0) {
		r_track_stop();           /* track 0 (GameOver/Completed on this disc) = silence */
		return;
	}
	char rel[64];
	snprintf(rel, sizeof rel, "audio\\Track%02d.wav", tr);
	if (!(asset_resolve(base, rel, p, sizeof p) && r_track_play(p, true))) {
		r_track_stop();           /* rip missing: stay silent rather than loop the old world */
		g_track_cur = 0;
	}
}

/* pickup_type byte -> ADD number (0 = none/uses theme sound, e.g. crystal) */
static const int PT_TO_ADD[16] = {
	[PU_PARAGLIDE] = 1, [PU_SPEED] = 3, [PU_BOMBS] = 4, [PU_SLOWDOWN] = 5, [PU_HEART] = 6,
	[PU_TIME] = 7, [PU_FREEZE] = 8, [PU_PROTECT] = 9, [PU_INVERSE] = 10,
};

/* map a world sound position to (volume, pan) for the current camera: nearer =
 * louder, and pan left/right by which side of the view direction it's on. */
static void audio_pos(r_camera vc, float x, float y, float z, float *vol, float *pan)
{
	float dx = x - vc.pos.x, dy = y - vc.pos.y, dz = z - vc.pos.z;
	float dist = sqrtf(dx * dx + dy * dy + dz * dz);
	float v = 1.0f - dist / 26.0f;              /* fade to silence by ~26 units */
	*vol = v < 0.18f ? 0.18f : (v > 1.0f ? 1.0f : v);
	/* camera right vector (up = +Z): right = forward x up = (fy, -fx, 0) */
	float fx = vc.target.x - vc.pos.x, fy = vc.target.y - vc.pos.y;
	float rx = fy, ry = -fx, rl = sqrtf(rx * rx + ry * ry);
	float side = rl > 1e-4f ? ((x - vc.target.x) * rx + (y - vc.target.y) * ry) / rl : 0.0f;
	float pn = 0.5f + 0.4f * (side / 7.0f);     /* raylib pan is inverted: lower = right */
	*pan = pn < 0.0f ? 0.0f : (pn > 1.0f ? 1.0f : pn);
}

/* .leo positional ambience: seamlessly-looping Music streams, mixed by camera
 * distance. Unlike one-shot SFX (floored at 0.18 so they're always heard), ambience
 * fades fully to zero with distance so far-off sources don't muddy the mix. */
void audio_update_ambience(r_camera view)
{
	for (int i = 0; i < g_scene.leo_amb_n; i++) {
		r_vec3 ap = g_scene.leo_amb_pos[i];
		float dx = ap.x - view.pos.x, dy = ap.y - view.pos.y, dz = ap.z - view.pos.z;
		float dist = sqrtf(dx * dx + dy * dy + dz * dz);
		float vol = 1.0f - dist / 22.0f;                  /* linear fade to silence by ~22 units */
		vol = vol < 0.0f ? 0.0f : (vol > 1.0f ? 1.0f : vol);
		/* pan: +1 = source to the camera's right (Music pan is -1..1, 0 = centre) */
		float fwx = view.target.x - view.pos.x, fwy = view.target.y - view.pos.y;
		float rlx = fwy, rly = -fwx, rl = sqrtf(rlx * rlx + rly * rly);
		float side = rl > 1e-4f ? ((ap.x - view.target.x) * rlx + (ap.y - view.target.y) * rly) / rl : 0.0f;
		float pan = side / 7.0f;
		pan = pan < -1.0f ? -1.0f : (pan > 1.0f ? 1.0f : pan);
		r_music_update(g_scene.leo_amb_mus[i], vol, pan);
	}
}

/* mix one state loop: silent when off, else positional fade/pan like the .leo
 * ambience (fade to zero by ~22 units; pan +1 = camera-right). `centred` loops
 * (the player's own) play full volume, no pan. */
static void loop_mix(r_music m, bool on, bool centred, float x, float y, float z,
                     r_camera view)
{
	if (m < 0)
		return;
	if (!on) {
		r_music_update(m, 0.0f, 0.0f);
		return;
	}
	if (centred) {
		r_music_update(m, 1.0f, 0.0f);
		return;
	}
	float dx = x - view.pos.x, dy = y - view.pos.y, dz = z - view.pos.z;
	float dist = sqrtf(dx * dx + dy * dy + dz * dz);
	float vol = 1.0f - dist / 22.0f;
	vol = vol < 0.0f ? 0.0f : (vol > 1.0f ? 1.0f : vol);
	float fwx = view.target.x - view.pos.x, fwy = view.target.y - view.pos.y;
	float rlx = fwy, rly = -fwx, rl = sqrtf(rlx * rlx + rly * rly);
	float side = rl > 1e-4f ? ((x - view.target.x) * rlx + (y - view.target.y) * rly) / rl : 0.0f;
	float pan = side / 7.0f;
	pan = pan < -1.0f ? -1.0f : (pan > 1.0f ? 1.0f : pan);
	r_music_update(m, vol, pan);
}

/* the sustained loops, gated by live sim state each frame (the original starts
 * DSBPLAY_LOOPING buffers on the state transitions and Stop()s them on exit;
 * mixing by state every frame is equivalent and can't leak a stuck loop).
 * `active` = actually playing (not menu/pause/death/summary): everything mutes,
 * which is also the original's death rule for the player loops. Movers pick the
 * nearest travelling instance — the original gives each its own 3D buffer, but
 * per-mover streams aren't worth it for the 2-3 movers a level runs at once.
 * The bomb fuse is NOT a loop: bomb_tick re-Plays BombTick.wav every tick
 * (no-op while audible), sound_stop at detonation (RE 0x4028f0/0x4028fd). */
void audio_update_loops(r_camera view, bool active)
{
	bool p_ice = false, p_slide = false, p_glide = false;
	if (active && sim.p.alive) {
		p_ice   = sim.p.sliding && sim.p.slide_ice;
		p_slide = sim.p.sliding && !sim.p.slide_ice;
		p_glide = sim.p.gliding;
	}
	if (g_scene.mus_slide == g_scene.mus_ice) {   /* one-sound themes share the stream */
		loop_mix(g_scene.mus_ice, p_ice || p_slide, true, 0, 0, 0, view);
	} else {
		loop_mix(g_scene.mus_ice,   p_ice,   true, 0, 0, 0, view);
		loop_mix(g_scene.mus_slide, p_slide, true, 0, 0, 0, view);
	}
	loop_mix(g_scene.mus_glide, p_glide, true, 0, 0, 0, view);

	float ex = 0, ey = 0, ez = 0, best = -1.0f;
	for (int i = 0; active && i < sim.num_elevators; i++) {
		const sim_elevator *e = &sim.elevators[i];
		if (e->phase == 0)
			continue;
		float d = fabsf(e->cx + 0.5f - view.pos.x) + fabsf(e->cy + 0.5f - view.pos.y);
		if (best < 0.0f || d < best) {
			best = d; ex = e->cx + 0.5f; ey = e->cy + 0.5f; ez = e->z;
		}
	}
	loop_mix(g_scene.mus_elevator, best >= 0.0f, false, ex, ey, ez, view);

	best = -1.0f;
	for (int i = 0; active && i < sim.num_platforms; i++) {
		const sim_platform *pl = &sim.platforms[i];
		if (pl->phase == 0)
			continue;
		float px = pl->hx + 0.5f + pl->dx * pl->t, py = pl->hy + 0.5f + pl->dy * pl->t;
		float d = fabsf(px - view.pos.x) + fabsf(py - view.pos.y);
		if (best < 0.0f || d < best) {
			best = d; ex = px; ey = py; ez = (float)pl->hz;
		}
	}
	loop_mix(g_scene.mus_platform, best >= 0.0f, false, ex, ey, ez, view);

	best = -1.0f;
	for (int i = 0; active && i < sim.num_bridges; i++) {
		const sim_bridge *b = &sim.bridges[i];
		if (!b->animating)
			continue;
		float d = fabsf(b->ox + 0.5f - view.pos.x) + fabsf(b->oy + 0.5f - view.pos.y);
		if (best < 0.0f || d < best) {
			best = d; ex = b->ox + 0.5f; ey = b->oy + 0.5f; ez = (float)b->oz;
		}
	}
	loop_mix(g_scene.mus_bridge, best >= 0.0f, false, ex, ey, ez, view);

	/* bomb fuse retrigger: the nearest burning bomb keeps BombTick.wav going */
	if (active && g_scene.snd_bombtick >= 0) {
		best = -1.0f;
		for (int i = 0; i < sim.num_bombs; i++) {
			const sim_bomb *b = &sim.bombs[i];
			if (!b->alive || b->boomed)
				continue;
			float d = fabsf(b->rx - view.pos.x) + fabsf(b->ry - view.pos.y);
			if (best < 0.0f || d < best) {
				best = d; ex = b->rx; ey = b->ry; ez = b->rz;
			}
		}
		if (best >= 0.0f && !r_sound_playing(g_scene.snd_bombtick)) {
			float vol, pan;
			audio_pos(view, ex, ey, ez, &vol, &pan);
			r_play_sound_ex(g_scene.snd_bombtick, vol, pan);
		}
	}
}

void audio_play_event(const sim_event *e, r_camera view, float t)
{
	int addvar = ((int)t) % 3;   /* ADD variant A/B/C by whole-second time (per RE) */
	r_sound snd = -1;
	switch (e->type) {
	case SIM_EV_STEP:     snd = g_scene.snd_move;    break;
	case SIM_EV_CRYSTAL:  snd = g_scene.snd_crystal; break;   /* crystal uses the theme sound, not ADD */
	case SIM_EV_PICKUP: {
		int cx = (int)e->x, cy = (int)e->y;
		int pt = (cx >= 0 && cy >= 0 && cx < lvl.dim_x && cy < lvl.dim_y)
		         ? lvl.tiles[cx][cy].pickup_type : 0;
		int adn = (pt >= 0 && pt < 16) ? PT_TO_ADD[pt] : 0;
		snd = adn ? g_scene.add_snd[adn][addvar] : g_scene.snd_pickup;
		break;
	}
	case SIM_EV_SPLAT:
		r_stop_sound(g_scene.snd_fall);   /* the fall scream dies at impact (RE 0x4396e4) */
		snd = g_scene.snd_splat;
		break;
	case SIM_EV_FALL:     snd = g_scene.snd_fall;    break;
	case SIM_EV_CAUGHT:   snd = g_scene.snd_caught;  break;
	case SIM_EV_EXITOPEN: snd = g_scene.add_snd[2][addvar]; break;   /* ADD02 = "exit is open" */
	case SIM_EV_ESTEP:    snd = g_scene.snd_emove;   break;
	case SIM_EV_GLUE:     snd = g_scene.snd_glue;    break;
	case SIM_EV_SLIDE:    break;   /* the run is a state loop (audio_update_loops), no one-shot */
	case SIM_EV_BOMBDROP: break;   /* fuse = per-tick retrigger there too */
	case SIM_EV_EXPLODE:  snd = g_scene.snd_explode;  break;
	case SIM_EV_OBSTACLE: snd = g_scene.snd_obstacle; break;   /* rock blown open */
	case SIM_EV_ENEMYDIE: snd = g_scene.snd_ecaught; break;   /* enemy blast-death cry */
	case SIM_EV_JUMPPAD:  snd = g_scene.snd_jumppad; break;   /* launch (silent if theme = NONE) */
	case SIM_EV_ETHROW:   snd = g_scene.snd_ethrow;  break;   /* thrower hop (Kanone), positional */
	case SIM_EV_DESTRUCT: snd = g_scene.snd_destruct; break;  /* collapsing field -> hole */
	case SIM_EV_REGEN:    snd = g_scene.snd_regen;   break;   /* collapsing field regenerated */
	case SIM_EV_TELEPORT: snd = g_scene.snd_teleport; break;  /* teleporter warp */
	case SIM_EV_SWITCH:   snd = g_scene.snd_switch;  break;   /* bridge switch click */
	}
	if (e->type == SIM_EV_ESTEP || e->type == SIM_EV_ETHROW || e->type == SIM_EV_ENEMYDIE
	    || e->type == SIM_EV_OBSTACLE || e->type == SIM_EV_DESTRUCT
	    || e->type == SIM_EV_REGEN) {
		float vol, pan;
		audio_pos(view, e->x, e->y, e->z, &vol, &pan);
		r_play_sound_ex(snd, vol, pan);
	} else {
		r_play_sound(snd);   /* centred, full volume */
	}
}
