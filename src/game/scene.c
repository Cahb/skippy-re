/* Per-level scene state + loaders (see game/scene.h). Split out of main.c: the
 * render loop and input stay there; everything that (re)builds a level lives here. */
#include "game/scene.h"
#include "game/assets.h"
#include "game/fx.h"
#include "formats/mdl.h"
#include "formats/tga.h"
#include "formats/asset.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strcasecmp */
#include <math.h>

/* theme object slot indices ("Object <slot>" in the .thm) referenced per level */
enum {
    THM_SLOT_JOHN      = 0,
    THM_SLOT_CATCHER   = 1,
    THM_SLOT_THROWER   = 3,
    THM_SLOT_PLATE     = 5,
    THM_SLOT_SIDE      = 6,
    THM_SLOT_PLATFORM  = 7,
    THM_SLOT_PARAGLIDE = 8,
    THM_SLOT_ELEVATOR  = 10,
    THM_SLOT_EXIT      = 11,
    THM_SLOT_GLUE      = 12,
    THM_SLOT_DESTRUCT  = 13,
    THM_SLOT_JUMPPAD   = 15,
    THM_SLOT_SLIDE     = 16,
    THM_SLOT_STAIR     = 17,
    THM_SLOT_TELEPORTER= 18,
    THM_SLOT_CRYSTAL   = 19,
    THM_SLOT_CRYSTALFX = 20,
    THM_SLOT_BOMB      = 22,
    THM_SLOT_OBSTACLE  = 33,
};

/* ---- scene + level/sim/camera state (declared extern in game/scene.h) ---- */
struct scene g_scene = { .crystal_color = { 130, 255, 170, 255 } };
jjm_level    lvl;                    /* current grid */
sim_state    sim;                    /* gameplay */
r_camera     cam;                    /* freecam seed (grid overview) */
char         cur_scene_name[128];    /* level whose assets are loaded (for the .leo path etc.) */

/* (re)load the theme's Sound<event> handles + the global clock wavs + the ADD bank. */
static void load_sounds(const char *b, const thm_theme *th)
{
	char p[1024];
	r_unload_sounds();
	r_unload_music();   /* drop the previous level's .leo ambience streams */
	g_scene.snd_move    = load_theme_sound(b, th, "MoveJJ");
	g_scene.snd_crystal = load_theme_sound(b, th, "Crystal");
	g_scene.snd_pickup  = g_scene.snd_crystal;                       /* bonus pickups reuse the chime for now */
	g_scene.snd_splat   = load_theme_sound(b, th, "SplatJJ");
	g_scene.snd_fall    = load_theme_sound(b, th, "FallJJ");
	g_scene.snd_caught  = g_scene.snd_splat;                          /* no distinct "caught" event in the thm */
	g_scene.snd_emove   = load_theme_sound(b, th, "MoveCatcher");
	g_scene.snd_glue    = load_theme_sound(b, th, "Glue");
	/* sustained loops (original: DSBPLAY_LOOPING): slide/ice/paraglide runs +
	 * the mover travel hums. Gapless streams, gated by state in audio_update_loops. */
	g_scene.mus_ice   = load_theme_music(b, th, "MoveIceSliding");
	g_scene.mus_slide = load_theme_music(b, th, "MoveSliding");       /* rutsche chute (Rutsche.wav) */
	if (g_scene.mus_ice < 0)     /* themes with only the chute sound (Egypt: Ice=NONE) */
		g_scene.mus_ice = g_scene.mus_slide;
	if (g_scene.mus_slide < 0)   /* and vice versa: no chute sound -> reuse the ice slip */
		g_scene.mus_slide = g_scene.mus_ice;
	g_scene.mus_glide    = load_theme_music(b, th, "MoveParagliding");
	g_scene.mus_elevator = load_theme_music(b, th, "Elevator");       /* Space/Egypt only */
	g_scene.mus_platform = load_theme_music(b, th, "Platform");
	g_scene.mus_bridge   = load_theme_music(b, th, "Bridge");         /* Space only */
	g_scene.snd_bombtick = load_theme_sound(b, th, "BombTick");
	g_scene.snd_explode  = load_theme_sound(b, th, "ExplosionBomb");
	g_scene.snd_destruct = load_theme_sound(b, th, "DestructStart");     /* DestructField collapse */
	g_scene.snd_regen    = load_theme_sound(b, th, "DestructRegen");     /* DestructField regenerate */
	g_scene.snd_obstacle = load_theme_sound(b, th, "Obstacle");          /* 0x17 obstacle blown open */
	g_scene.snd_ecaught  = load_theme_sound(b, th, "ExplosionCatcher");  /* enemy killed by a blast */
	g_scene.snd_jumppad  = load_theme_sound(b, th, "MoveJumpPad");       /* NONE in some themes (silent) */
	g_scene.snd_ethrow   = load_theme_sound(b, th, "MoveThrower");       /* distinct from MoveCatcher */
	if (g_scene.snd_ethrow < 0) g_scene.snd_ethrow = g_scene.snd_emove;                     /* fall back to the catcher hop */
	g_scene.snd_teleport = load_theme_sound(b, th, "Teleporter");        /* teleporter warp */
	g_scene.snd_menu = asset_resolve(b, "Waves\\MenuUpDown.wav", p, sizeof p)
	                   ? r_load_sound(p) : -1;                           /* menu up/down tick */
	g_scene.snd_switch = load_theme_sound(b, th, "Switch");              /* bridge switch click */
	/* global (non-theme) clock wavs — reloaded here since r_unload_sounds wiped them */
	g_scene.snd_timeout = asset_resolve(b, "waves\\TimeOut.wav", p, sizeof p) ? r_load_sound(p) : -1;
	g_scene.snd_lastsec = asset_resolve(b, "waves\\LastSeconds.wav", p, sizeof p) ? r_load_sound(p) : -1;
	g_scene.snd_levelcomplete = asset_resolve(b, "waves\\LevelCompleted.wav", p, sizeof p) ? r_load_sound(p) : -1;
	/* global ADD bank ADD01..ADD10, variants A/B/C (pickup + exit-open announces) */
	for (int nn = 1; nn <= 10; nn++)
		for (int v = 0; v < 3; v++) {
			char wav[64];
			snprintf(wav, sizeof wav, "waves\\ADD%02d%c.wav", nn, 'A' + v);
			g_scene.add_snd[nn][v] = asset_resolve(b, wav, p, sizeof p) ? r_load_sound(p) : -1;
		}
	/* NB (verified by RE of theme_parse_sound_line): the engine has NO "exit
	 * open / all crystals" sound event. Collecting the last crystal just plays
	 * Crystal.wav; LevelCompleted.wav (clapping) is for entering the exit — which
	 * needs a level-complete transition we don't have yet. So SIM_EV_EXITOPEN is
	 * kept as a game-moment marker but plays nothing for now. */
	g_scene.snd_exit = -1;
}

/* resolve a slot's Field texture STACK into draw-ready layers, in declaration
 * order: base first, overlays after, each with its own blend (opaque / SrcAlpha
 * / One-One additive), state Condition and UV animation. THE generic tile-top
 * loader: what the theme declares is what renders — no cherry-picking single
 * layers per heuristic (that lost Candy's switch frame, Space's second ice
 * glow, Egypt's teleporter wobble layer, the platform pane, ...). */
static void load_field_stack(const char *base, const thm_theme *th, int slot,
                             struct field_stack *out)
{
	memset(out, 0, sizeof *out);
	for (int o = 0; o < th->num_objects; o++) {
		if (th->objects[o].slot != slot)
			continue;
		for (int m = 0; m < th->objects[o].num_meshes; m++) {
			const thm_mesh *me = &th->objects[o].meshes[m];
			if (!me->is_field)
				continue;
			for (int ti = 0; ti < me->num_tex
			     && out->n < (int)(sizeof out->l / sizeof out->l[0]); ti++) {
				const thm_texture *tx = &me->tex[ti];
				if (!tx->tga[0])
					continue;
				struct field_layer *L = &out->l[out->n];
				L->tex = load_tex_rel_alpha(base, tx->tga);
				if (L->tex < 0)
					continue;
				L->blend = strcasecmp(tx->src_blend, "one") == 0 ? FL_ADD
				         : (tx->alpha || strcasecmp(tx->src_blend, "srcalpha") == 0)
				           ? FL_ALPHA : FL_OPAQUE;
				L->cond = (unsigned char)tx->condition;
				L->turn = tx->turn;
				L->scroll[0] = tx->scroll[0];
				L->scroll[1] = tx->scroll[1];
				L->pulse = tx->pulse;
				memcpy(L->wobble, tx->wobble, sizeof L->wobble);
				memcpy(L->flash, tx->flash, sizeof L->flash);
				if (tx->wrap || L->turn != 0.0f || L->scroll[0] != 0.0f
				    || L->scroll[1] != 0.0f || L->wobble[1] != 0.0f || L->wobble[2] != 0.0f)
					r_texture_repeat(L->tex);   /* animated/tiling UVs sample past 0..1 */
				out->n++;
			}
			if (out->n)
				break;                       /* the slot's first Field is the tile top */
		}
		break;
	}
}

static void load_tiles(const char *base, thm_theme *th)
{
	char rel[512];

	/* tile textures + thickness */
	g_scene.tex_plate = g_scene.tex_side = g_scene.tex_exit = -1;
	const char *plate_rel = thm_slot_tex(th, THM_SLOT_PLATE);
	const char *side_rel  = thm_slot_tex(th, THM_SLOT_SIDE);
	const char *exit_rel  = thm_slot_tex(th, THM_SLOT_EXIT);
	g_scene.tex_plate = load_tex_rel(base, plate_rel);
	/* A theme-Alpha Side (Water SIDE64 = a full-height vine curtain with a transparent
	 * background) is drawn full-texture + alpha-cut on the sides; the default (Forest/Castle
	 * SIDE64 = a plank strip in the bottom band, black elsewhere) stays opaque + banded. */
	g_scene.side_alpha = false;
	for (int o = 0; o < th->num_objects; o++)
		if (th->objects[o].slot == THM_SLOT_SIDE) {
			const thm_object *ob = &th->objects[o];
			if (ob->num_meshes > 0 && ob->meshes[0].num_tex > 0)
				g_scene.side_alpha = ob->meshes[0].tex[0].alpha
				    || strcasecmp(ob->meshes[0].tex[0].src_blend, "srcalpha") == 0;
			break;
		}
	g_scene.tex_side  = g_scene.side_alpha ? load_tex_rel_alpha(base, side_rel) : load_tex_rel(base, side_rel);
	g_scene.tex_exit  = load_tex_rel(base, exit_rel);
	g_scene.thk = (th->side_height > 0.0f) ? th->side_height : 0.1875f;

	/* generic Field-layer stacks. Every special tile top is the theme's OWN
	 * ordered texture stack, resolved once here and drawn per-layer across the
	 * three passes (opaque slab / alpha overlays / additive glows) by
	 * render_scene. Floor tiles default to the plate. Glue stays on its own
	 * inscribed path (goo semantics), destruct on its cover path. */
	memset(g_scene.tile_field, 0, sizeof g_scene.tile_field);
	for (int i = 0; i < 64; i++)
		g_scene.tile_obj[i].n = 0;
	static const struct { int type, slot; } FMAP[] = {
		{ TT_EXIT, THM_SLOT_EXIT },      /* base + 2 Flash glows + Active ring */
		{ TT_ICE, 32 },                  /* base + glow pulses / alpha sheen */
		{ TT_SWITCH, 30 },               /* button (state) + shadow + frame */
		{ TT_PLANK, 37 },                /* bridge pane: additive (Space) / alpha (Candy) */
		{ TT_JUMPPAD, THM_SLOT_JUMPPAD },/* top + pulsing glow */
		{ TT_TELEPORT, THM_SLOT_TELEPORTER }, /* spinning swirl stack */
	};
	for (size_t i = 0; i < sizeof FMAP / sizeof FMAP[0]; i++)
		load_field_stack(base, th, FMAP[i].slot, &g_scene.tile_field[FMAP[i].type]);
	/* mover top panes (the walkable grid on elevators/platforms — Space, Forest) */
	load_field_stack(base, th, THM_SLOT_ELEVATOR, &g_scene.mover_field[0]);
	load_field_stack(base, th, THM_SLOT_PLATFORM, &g_scene.mover_field[1]);

	/* skybox: <base>_{FR,BK,LF,RT,UP,DN}.tga */
	for (int i = 0; i < 6; i++)
		g_scene.sky[i] = -1;
	if (th->sky_base[0]) {
		static const char *suf[6] = { "_FR", "_BK", "_LF", "_RT", "_UP", "_DN" };
		for (int i = 0; i < 6; i++) {
			snprintf(rel, sizeof rel, "%s%s.tga", th->sky_base, suf[i]);
			g_scene.sky[i] = load_tex_rel(base, rel);
		}
	}
}

static void load_hud(const char *base, thm_theme *th)
{
	char p[1024], rel[512];
	tga_image im;

	/* HUD art: shared digit atlas, per-theme radar disc, and the two corner
	 * panels (HUD_ALL packs them diagonally, so split on the anti-diagonal). */
	g_scene.hud_left = g_scene.hud_right = g_scene.hud_numbers = g_scene.hud_radar = -1;
	if (asset_resolve(base, "Textures\\NUMBERS.TGA", p, sizeof p) && tga_load(p, &im)) {
		g_scene.hud_numbers = r_load_texture_rgba(im.rgba, im.width, im.height);
		tga_free(&im);
	}
	/* Font2: caption/menu glyph atlas — RGB is all-white, the glyph shapes live in the
	 * 32-bit ALPHA channel, so keep it as-is (tint colours the white; alpha cuts it). */
	g_scene.font2_tex = -1;
	if (asset_resolve(base, "Textures\\Font2.tga", p, sizeof p) && tga_load(p, &im)) {
		g_scene.font2_tex = r_load_texture_rgba(im.rgba, im.width, im.height);
		tga_free(&im);
	}
	/* menu panel: the theme's own Menu.tga (rounded shape baked into its alpha) */
	g_scene.menu_panel = -1;
	snprintf(rel, sizeof rel, "Textures\\%s\\Menu.tga", lvl.world);
	if (asset_resolve(base, rel, p, sizeof p) && tga_load(p, &im)) {
		g_scene.menu_panel = r_load_texture_rgba(im.rgba, im.width, im.height);
		g_scene.menu_panel_w = im.width; g_scene.menu_panel_h = im.height;
		tga_free(&im);
	}
	if (th->radar_tex[0] && asset_resolve(base, th->radar_tex, p, sizeof p) && tga_load(p, &im)) {
		g_scene.hud_radar = r_load_texture_rgba(im.rgba, im.width, im.height);
		tga_free(&im);
	}
	memcpy(g_scene.th_menu_color, th->menu_color, sizeof g_scene.th_menu_color);

	/* SELECTOR.TGA: the boxing-glove pair flanking the selected menu row */
	g_scene.menu_selector = -1;
	g_scene.menu_selector_w = g_scene.menu_selector_h = 0;
	if (asset_resolve(base, "Textures\\SELECTOR.TGA", p, sizeof p) && tga_load(p, &im)) {
		g_scene.menu_selector = r_load_texture_rgba(im.rgba, im.width, im.height);
		g_scene.menu_selector_w = im.width;
		g_scene.menu_selector_h = im.height;
		tga_free(&im);
	}

	/* bonus-timer ring icons (env Freeze/InverseControl/Protection/Slowdwon/Speed).
	 * Every stock theme declares all five (Castle borrows Space's files); a theme
	 * that declares none falls back to Forest's set so a custom level granting a
	 * bonus there still shows its timer (never crash, never invent art). */
	static const char *BONUS_FALLBACK[THM_BONUS_N] = {
		"Textures\\Forest\\BonusFreezeKreis.tga", "Textures\\Forest\\BonusInvKreis.tga",
		"Textures\\Forest\\BonusProtectionKreis.tga", "Textures\\Forest\\BonusSlowKreis.tga",
		"Textures\\Forest\\BonusSpeedKreis.tga",
	};
	for (int i = 0; i < THM_BONUS_N; i++) {
		g_scene.bonus_tex[i] = -1;
		g_scene.bonus_w[i] = g_scene.bonus_h[i] = 0;
		const char *br = th->bonus_tex[i][0] ? th->bonus_tex[i] : BONUS_FALLBACK[i];
		if (asset_resolve(base, br, p, sizeof p) && tga_load(p, &im)) {
			g_scene.bonus_tex[i] = r_load_texture_rgba(im.rgba, im.width, im.height);
			g_scene.bonus_w[i] = im.width;
			g_scene.bonus_h[i] = im.height;
			tga_free(&im);
		}
	}

	if (th->hud_tex[0] && asset_resolve(base, th->hud_tex, p, sizeof p) && tga_load(p, &im)) {
		int n = im.width * im.height;
		uint8_t *L = malloc((size_t)n * 4), *R = malloc((size_t)n * 4);
		if (L && R) {
			memcpy(L, im.rgba, (size_t)n * 4);
			memcpy(R, im.rgba, (size_t)n * 4);
			/* the two leaf blobs occupy disjoint Y ranges across every theme: the
			 * upper-left blob ends by y127, the lower-right blob starts at y128 —
			 * so a horizontal cut at y=128 separates them cleanly. */
			for (int y = 0; y < im.height; y++)
				for (int x = 0; x < im.width; x++) {
					int a = (y * im.width + x) * 4 + 3;
					if (y >= 128)
						L[a] = 0;   /* left panel keeps only the upper blob */
					else
						R[a] = 0;   /* right panel keeps only the lower blob */
				}
			g_scene.hud_left  = r_load_texture_rgba(L, im.width, im.height);
			g_scene.hud_right = r_load_texture_rgba(R, im.width, im.height);
		}
		free(L);
		free(R);
		tga_free(&im);
	}
}

static void load_fx(const char *base, thm_theme *th)
{
	/* FX textures used by the event bursts (the continuous world emitters load
	 * their own .par sprites in load_emitters; the exit glow now renders from the
	 * exit's own Field-layer stack): star sparkle, flare fallback, explosion blob. */
	g_scene.fx_star = g_scene.fx_flare = -1;
	fx_reset();
	g_scene.fx_star  = load_tex_rel_alpha(base, "Textures\\Star3_32.tga");
	g_scene.fx_flare = load_tex_rel_alpha(base, "Textures\\flare01_64x64.tga");
	g_scene.fx_blow  = load_tex_rel_alpha(base, "Textures\\Blow.tga");   /* explosion puffs */

	/* CrystalFX: a notMovable spark bound to the gem — drawn attached at the gem's
	 * animated transform (render_scene), not a world emitter. Pull its sprite + a
	 * representative colour + size + spawn height straight from the theme .par. */
	g_scene.crystal_fx_tex = -1;
	g_scene.crystal_fx_col = (r_color){ 255, 255, 255, 255 };
	g_scene.crystal_fx_size = 0.30f;
	g_scene.crystal_fx_up = 0.50f;
	for (int o = 0; o < th->num_objects; o++) {
		if (th->objects[o].slot != THM_SLOT_CRYSTALFX || th->objects[o].num_particles == 0)
			continue;
		thm_particle *pp = &th->objects[o].particles[0];
		char cp[1024];
		par_system ps;
		if (asset_resolve(base, pp->par, cp, sizeof cp) && par_load(cp, &ps)) {
			g_scene.crystal_fx_tex = pp->tex[0] ? load_particle_tex(base, pp->tex) : g_scene.fx_star;
			if (g_scene.crystal_fx_tex < 0)
				g_scene.crystal_fx_tex = g_scene.fx_star;
			/* weighted-mean colour across the .par ramp */
			unsigned long cr = 0, cg = 0, cb = 0, cw = 0;
			for (int c = 0; c < ps.num_colors; c++) {
				unsigned wt = ps.colors[c].weight ? ps.colors[c].weight : 1;
				cr += (unsigned long)ps.colors[c].rgb[0] * wt;
				cg += (unsigned long)ps.colors[c].rgb[1] * wt;
				cb += (unsigned long)ps.colors[c].rgb[2] * wt;
				cw += wt;
			}
			if (cw)
				g_scene.crystal_fx_col = (r_color){ (unsigned char)(cr / cw),
				                                    (unsigned char)(cg / cw),
				                                    (unsigned char)(cb / cw), 255 };
			if (ps.face_size > 0.05f)
				g_scene.crystal_fx_size = ps.face_size;
			g_scene.crystal_fx_up = ps.pos_lo[1];   /* .par spawn height; refined to the gem's mesh centre in load_tile_objects */
		}
		break;
	}

	/* DestructFieldFX (slot 14): a one-shot crumb burst emitted when a step-and-break
	 * tile (0x0d) collapses. Decode the theme .par + its sprite/blend; fired on
	 * SIM_EV_DESTRUCT in fx_emit_event via par_burst. */
	g_scene.destruct_fx_tex = -1;
	g_scene.destruct_fx_blend = R_BLEND_ALPHA;
	g_scene.destruct_fx_par.valid = false;
	for (int o = 0; o < th->num_objects; o++) {
		if (th->objects[o].slot != 14 || th->objects[o].num_particles == 0)
			continue;
		thm_particle *pp = &th->objects[o].particles[0];
		char dp[1024];
		if (asset_resolve(base, pp->par, dp, sizeof dp) && par_load(dp, &g_scene.destruct_fx_par)) {
			g_scene.destruct_fx_tex = pp->tex[0] ? load_particle_tex(base, pp->tex) : g_scene.fx_star;
			/* debris crumbs (kruemel) are SrcAlpha/InvSrcAlpha — a fading solid speck,
			 * not an additive glow. (The .thm particle blend isn't parsed; crumbs = alpha.) */
			g_scene.destruct_fx_blend = R_BLEND_ALPHA;
		}
		break;
	}
}

/* distill a mesh's theme transform/anim modifiers into the render-time mesh_anim. */
static struct mesh_anim mesh_anim_of(const thm_mesh *m)
{
	return (struct mesh_anim){
		.random_yaw = m->random_yaw,
		.rotate_z   = m->rotate[1],   /* theme Y-up spin -> our yaw about Z */
		.oscillate  = m->oscillate,
		.osc_random = m->oscillate_random,
		.osc        = { m->osc[0], m->osc[1], m->osc[2] },
		.pump       = m->pump,
		.pump_p     = { m->pump_p[0], m->pump_p[1] },
	};
}

/* Load EVERY Model sub-mesh of a theme object (mesh + tex[0] + transform/anim +
 * additive flag), ready for the generic draw_obj. Fields (no .mdl) are skipped. */
static void load_obj(const char *base, const thm_object *ob, struct obj_render *o)
{
	char p[1024];
	tga_image im;
	o->n = 0;
	o->nbb = 0;
	for (int mi = 0; mi < ob->num_meshes && o->n < THM_MAX_MESHES; mi++) {
		const thm_mesh *tm = &ob->meshes[mi];
		if (!tm->mesh[0])
			continue;                          /* Field (engine quad), not a .mdl */
		mdl_model mdl;
		if (!(asset_resolve(base, tm->mesh, p, sizeof p) && mdl_load(p, &mdl)))
			continue;
		struct obj_submesh *sm = &o->sm[o->n++];
		sm->mesh = r_upload_anim(mdl.verts, (int)mdl.num_verts, mdl.num_frames);
		sm->frames = (int)mdl.num_frames;
		mdl_free(&mdl);
		/* looping vertex animation: a Model that declares an .ani (e.g. the Egypt
		 * flying carpet, tepisch.ani) flutters continuously. Default = loop all
		 * frames at 12fps; the first present .ani clip refines the range/fps. */
		sm->afirst = 0; sm->acount = 1; sm->afps = 12.0f;
		if (tm->anim[0] && sm->frames > 1) {
			sm->acount = sm->frames;
			ani_set as;
			if (asset_resolve(base, tm->anim, p, sizeof p) && ani_load(p, &as))
				for (int c = 0; c < ANI_NUM_CLIPS; c++)
					if (as.clips[c].present && as.clips[c].count > 0) {
						sm->afirst = as.clips[c].first;
						sm->acount = as.clips[c].count;
						sm->afps   = as.clips[c].fps > 0 ? as.clips[c].fps : 12.0f;
						break;
					}
		}
		sm->tex = -1;
		if (tm->num_tex > 0 && tm->tex[0].tga[0]
		    && asset_resolve(base, tm->tex[0].tga, p, sizeof p) && tga_load(p, &im)) {
			sm->tex = r_load_texture_rgba(im.rgba, im.width, im.height);
			tga_free(&im);
		}
		sm->anim = mesh_anim_of(tm);
		sm->pos = (r_vec3){ tm->pos[0], -tm->pos[2], tm->pos[1] };   /* theme Y-up -> world */
		sm->additive = tm->num_tex > 0 && strcasecmp(tm->tex[0].src_blend, "one") == 0;
		sm->alpha = tm->num_tex > 0 && !sm->additive
		            && (tm->tex[0].alpha || strcasecmp(tm->tex[0].src_blend, "srcalpha") == 0);
		/* Environment reflection layer (e.g. reflect_stark32): a 2nd texture flagged
		 * Environment, drawn additively sphere-mapped over the mesh for a metallic shine. */
		sm->env_tex = -1;
		for (int ti = 0; ti < tm->num_tex; ti++)
			if (tm->tex[ti].environment && tm->tex[ti].tga[0]) {
				sm->env_tex = load_particle_tex(base, tm->tex[ti].tga);
				break;
			}
	}
	/* Billboard glow sprites (additive), e.g. the Time bonus flare. */
	for (int b = 0; b < ob->num_billboards && o->nbb < THM_MAX_MESHES; b++) {
		const thm_billboard *tb = &ob->billboards[b];
		r_tex t = tb->tex[0] ? load_particle_tex(base, tb->tex) : -1;
		if (t < 0)
			continue;
		o->bb[o->nbb].tex  = t;
		o->bb[o->nbb].size = tb->size > 0.0f ? tb->size : 0.5f;
		o->bb[o->nbb].pos  = (r_vec3){ tb->pos[0], -tb->pos[2], tb->pos[1] };   /* Y-up -> world */
		o->nbb++;
	}
}

/* load the object occupying `slot` (all its sub-meshes) into `o`; empty if absent. */
static void load_slot_obj(const char *base, const thm_theme *th, int slot, struct obj_render *o)
{
	o->n = 0;
	for (int i = 0; i < th->num_objects; i++)
		if (th->objects[i].slot == slot) {
			load_obj(base, &th->objects[i], o);
			return;
		}
}

/* crystal gem (theme slot 19; the glow Billboard is skipped by the parser) */
static void load_crystal_obj(const char *base, thm_theme *th)
{
	char p[1024];
	tga_image im;

	g_scene.crystal_mesh = -1;
	g_scene.crystal_tex = -1;
	g_scene.crystal_anim = (struct mesh_anim){ 0 };
	for (int o = 0; o < th->num_objects; o++) {
		if (th->objects[o].slot != THM_SLOT_CRYSTAL)
			continue;
		thm_object *ob = &th->objects[o];
		for (int mi = 0; mi < ob->num_meshes; mi++) {
			thm_mesh *tm = &ob->meshes[mi];
			if (!tm->mesh[0])
				continue;
			g_scene.crystal_anim = mesh_anim_of(tm);   /* gem spin/bob from the theme */
			mdl_model mdl;
			if (asset_resolve(base, tm->mesh, p, sizeof p) && mdl_load(p, &mdl)) {
				g_scene.crystal_mesh = r_upload_mesh(mdl_frame(&mdl, 0), (int)mdl.num_verts);
				/* centre the bound CrystalFX spark on the gem's geometry rather than the
				 * .par spawn height (which sits a touch high vs how we place the gem). Use
				 * the true vertex Z bbox — the frame header pads a pivot at Z=0. */
				const mdl_vertex *fv = mdl_frame(&mdl, 0);
				float zmn = 1e9f, zmx = -1e9f;
				for (uint32_t v = 0; v < mdl.num_verts; v++) {
					if (fv[v].z < zmn) zmn = fv[v].z;
					if (fv[v].z > zmx) zmx = fv[v].z;
				}
				if (mdl.num_verts)
					g_scene.crystal_fx_up = (zmn + zmx) * 0.5f;
				mdl_free(&mdl);
			}
			if (tm->num_tex > 0 && tm->tex[0].tga[0] && asset_resolve(base, tm->tex[0].tga, p, sizeof p) && tga_load(p, &im)) { g_scene.crystal_tex = r_load_texture_rgba(im.rgba, im.width, im.height); g_scene.crystal_color = dominant_color(&im, g_scene.crystal_color); tga_free(&im); }
			break;
		}
		break;
	}
}

/* glue overlay mesh (theme slot 12 "Glue" Model sub-mesh, e.g. Castle
 * spinnweben webs); the Field is the floor, the Model is the overlay. */
static void load_glue_obj(const char *base, thm_theme *th)
{
	char p[1024];
	tga_image im;

	g_scene.glue_mesh = -1;
	g_scene.glue_tex = -1;
	for (int o = 0; o < th->num_objects; o++) {
		if (th->objects[o].slot != THM_SLOT_GLUE)
			continue;
		thm_object *ob = &th->objects[o];
		for (int mi = 0; mi < ob->num_meshes; mi++) {
			thm_mesh *tm = &ob->meshes[mi];
			if (!tm->mesh[0])           /* skip the Field; want the .mdl Model */
				continue;
			mdl_model mdl;
			if (asset_resolve(base, tm->mesh, p, sizeof p) && mdl_load(p, &mdl)) { g_scene.glue_mesh = r_upload_mesh(mdl_frame(&mdl, 0), (int)mdl.num_verts); mdl_free(&mdl); }
			if (tm->num_tex > 0 && tm->tex[0].tga[0] && asset_resolve(base, tm->tex[0].tga, p, sizeof p) && tga_load(p, &im)) { g_scene.glue_tex = r_load_texture_rgba(im.rgba, im.width, im.height); r_texture_repeat(g_scene.glue_tex); tga_free(&im); }
			break;
		}
		break;
	}
	/* goo themes (Forest/Candy/Space) have no Model — the glue is the Field's stack
	 * of textures. The animated goo base is the one carrying a Wobble (Forest kleb,
	 * Space glibber, Candy klebe64) — identify it by that, NOT by Condition, since
	 * Candy's goo is itself Condition InActive. The remaining overlays split by state:
	 *   Condition InActive = fresh pad, never stepped (frame)  -> glue_fresh_tex
	 *   Condition Active    = stepped/spent (closed grid)      -> glue_spent_tex */
	g_scene.glue_fresh_tex = -1;
	g_scene.glue_spent_tex = -1;
	if (g_scene.glue_mesh < 0) {
		for (int o = 0; o < th->num_objects; o++) {
			if (th->objects[o].slot != THM_SLOT_GLUE)
				continue;
			for (int mi = 0; mi < th->objects[o].num_meshes; mi++) {
				thm_mesh *fm = &th->objects[o].meshes[mi];
				if (!fm->is_field)
					continue;
				for (int ti = 0; ti < fm->num_tex; ti++) {
					if (!fm->tex[ti].tga[0]
					    || !asset_resolve(base, fm->tex[ti].tga, p, sizeof p)
					    || !tga_load(p, &im))
						continue;
					r_tex t = r_load_texture_rgba(im.rgba, im.width, im.height);
					r_texture_repeat(t);
					tga_free(&im);
					bool has_wobble = fm->tex[ti].wobble[0] > 0.0f
					               || fm->tex[ti].wobble[1] > 0.0f
					               || fm->tex[ti].wobble[2] > 0.0f;
					if (has_wobble) {                            /* the Wobble texture = goo base */
						g_scene.glue_tex = t;
						memcpy(g_scene.glue_wobble, fm->tex[ti].wobble, sizeof g_scene.glue_wobble);
					} else switch (fm->tex[ti].condition) {
					case THM_COND_INACTIVE: g_scene.glue_fresh_tex = t; break;  /* fresh frame */
					case THM_COND_ACTIVE:   g_scene.glue_spent_tex = t; break;  /* spent grid */
					default: if (g_scene.glue_tex < 0)           /* unconditioned base fallback */
						         g_scene.glue_tex = t;
					         break;
					}
				}
			}
			break;
		}
	}
}

static void load_destruct_field_tex(const char *base, thm_theme *th)
{
	char p[1024];
	tga_image im;

	g_scene.destruct_tex = -1;
	for (int o = 0; o < th->num_objects; o++) {
		if (th->objects[o].slot != THM_SLOT_DESTRUCT)
			continue;
		for (int mi = 0; mi < th->objects[o].num_meshes; mi++) {
			thm_mesh *fm = &th->objects[o].meshes[mi];
			if (!fm->is_field || fm->num_tex == 0)
				continue;
			int ti = 0;                                  /* prefer InActive (intact) if present */
			for (int k = 0; k < fm->num_tex; k++)
				if (fm->tex[k].condition == THM_COND_INACTIVE) { ti = k; break; }
			if (fm->tex[ti].tga[0] && asset_resolve(base, fm->tex[ti].tga, p, sizeof p) && tga_load(p, &im)) {
				g_scene.destruct_tex = r_load_texture_rgba(im.rgba, im.width, im.height);
				tga_free(&im);
			}
			break;
		}
		break;
	}
}

static void load_tile_objects(const char *base, thm_theme *th)
{
	load_crystal_obj(base, th);

	/* stair (theme slot 17 "Stair") for tiles 5-8: ALL sub-meshes — Treppe.mdl plus
	 * e.g. Castle Treppe_Kerzen.mdl candle holders (previously dropped). */
	load_slot_obj(base, th, THM_SLOT_STAIR, &g_scene.stair_obj);

	load_glue_obj(base, th);

	/* DestructField (slot 13) is polymorphic: a Model (e.g. Castle castle_destroy.mdl) OR a
	 * Field grid (Forest/Space, alpha, Condition InActive). Load both; render picks whichever
	 * the theme provided. */
	load_slot_obj(base, th, THM_SLOT_DESTRUCT, &g_scene.destruct_obj);
	/* teleporter Model sub-meshes (Egypt/Candy ring + mesh) drawn at each teleporter tile */
	load_slot_obj(base, th, THM_SLOT_TELEPORTER, &g_scene.tile_obj[TT_TELEPORT]);
	/* slide chute mesh (rutsche): the Slide object's Model(s) drawn at each 0x10 tile
	 * (Candy/Egypt/Space; env/Lit/Pulse handled by draw_obj). */
	load_slot_obj(base, th, THM_SLOT_SLIDE, &g_scene.tile_obj[TT_SLIDE]);

	load_destruct_field_tex(base, th);
}

/* catcher enemy (theme slot 1, e.g. Forest frosch/frog): all frames + .ani */
static void load_catcher(const char *base, thm_theme *th)
{
	char p[1024];
	tga_image im;

	g_scene.catcher_mesh = -1;
	g_scene.catcher_tex = -1;
	g_scene.catcher_frames = 1;
	memset(&g_scene.catcher_anim, 0, sizeof g_scene.catcher_anim);
	for (int o = 0; o < th->num_objects; o++) {
		if (th->objects[o].slot != THM_SLOT_CATCHER)
			continue;
		thm_object *ob = &th->objects[o];
		for (int mi = 0; mi < ob->num_meshes; mi++) {
			thm_mesh *tm = &ob->meshes[mi];
			if (!tm->mesh[0])
				continue;
			mdl_model mdl;
			if (asset_resolve(base, tm->mesh, p, sizeof p) && mdl_load(p, &mdl)) {
				g_scene.catcher_mesh = r_upload_anim(mdl.verts, (int)mdl.num_verts, mdl.num_frames);
				g_scene.catcher_frames = mdl.num_frames;
				mdl_free(&mdl);
			}
			int ci = -1;
			for (int ti = 0; ti < tm->num_tex; ti++)
				if (tm->tex[ti].condition == THM_COND_NONE
				    || tm->tex[ti].condition == THM_COND_ALIVE) { ci = ti; break; }
			if (ci >= 0 && tm->tex[ci].tga[0] && asset_resolve(base, tm->tex[ci].tga, p, sizeof p) && tga_load(p, &im)) {
				g_scene.catcher_tex = r_load_texture_rgba(im.rgba, im.width, im.height);
				tga_free(&im);
			}
			if (tm->anim[0] && asset_resolve(base, tm->anim, p, sizeof p))
				ani_load(p, &g_scene.catcher_anim);
			break;
		}
		break;
	}
}

/* thrower enemy (theme slot 3): N sub-meshes (Castle cannon = base+barrel; Space robot
 * = body + light-rays + dome), animated by the shared .ani — it hops like a catcher. */
static void load_thrower(const char *base, thm_theme *th)
{
	char p[1024];
	tga_image im;

	g_scene.thrower_nmesh = 0;
	g_scene.thrower_frames = 1;
	g_scene.thrower_off_z = 0.0f;
	memset(&g_scene.thrower_anim, 0, sizeof g_scene.thrower_anim);
	for (int o = 0; o < th->num_objects; o++) {
		if (th->objects[o].slot != THM_SLOT_THROWER)
			continue;
		thm_object *ob = &th->objects[o];
		/* float offset ("Position 0 1.0 0" -> Y is model up -> our world Z); the engine
		 * exhaust ParticleSystem is built generically by load_emitters (triebwerk.par). */
		g_scene.thrower_off_z = ob->num_meshes > 0 ? ob->meshes[0].pos[1] : 0.0f;
		for (int mi = 0; mi < ob->num_meshes && g_scene.thrower_nmesh < THROWER_MAX_MESH; mi++) {
			thm_mesh *tm = &ob->meshes[mi];
			if (!tm->mesh[0])
				continue;
			mdl_model mdl;
			r_mesh m = -1;
			if (asset_resolve(base, tm->mesh, p, sizeof p) && mdl_load(p, &mdl)) {
				m = r_upload_anim(mdl.verts, (int)mdl.num_verts, mdl.num_frames);
				if (g_scene.thrower_nmesh == 0) g_scene.thrower_frames = mdl.num_frames;
				mdl_free(&mdl);
			}
			r_tex mt = -1;
			if (tm->num_tex > 0 && tm->tex[0].tga[0]
			    && asset_resolve(base, tm->tex[0].tga, p, sizeof p) && tga_load(p, &im)) {
				mt = r_load_texture_rgba(im.rgba, im.width, im.height);
				tga_free(&im);
			}
			int idx = g_scene.thrower_nmesh++;
			g_scene.thrower_mesh[idx] = m;
			g_scene.thrower_mtex[idx] = mt;
			g_scene.thrower_madd[idx] = tm->num_tex > 0 && strcasecmp(tm->tex[0].src_blend, "one") == 0;
			if (!g_scene.thrower_anim.clips[ANI_WALK_FWD].present && tm->anim[0]
			    && asset_resolve(base, tm->anim, p, sizeof p))
				ani_load(p, &g_scene.thrower_anim);
		}
		break;
	}
}

/* bonus pickups: pickup byte -> theme slot (from level names + slot map). Loaded as
 * full objects (all sub-meshes + theme spin/bob/pump), e.g. speed's two counter-
 * rotating parts, Life's Pump throb. */
static void load_pickups(const char *base, thm_theme *th)
{
	for (int i = 0; i < 16; i++) g_scene.pickup_obj[i].n = 0;
	static const struct { int byte, slot; } BONUS[] = {
		{ PU_PARAGLIDE, 8 },  /* paraglide */
		{ PU_TIME, 31 },      /* time      */
		{ PU_HEART, 29 },     /* heart/life */
		{ PU_FREEZE, 25 },    /* freeze    */
		{ PU_BOMBS, 21 },     /* ammunition BOX (slot 21), not the bomb model (22) */
		{ PU_SPEED, 26 },     /* speed     */
		{ PU_PROTECT, 35 },   /* protection */
	};
	for (size_t i = 0; i < sizeof BONUS / sizeof BONUS[0]; i++)
		load_slot_obj(base, th, BONUS[i].slot, &g_scene.pickup_obj[BONUS[i].byte]);
	g_scene.surprise_obj.n = 0;
	load_slot_obj(base, th, 24, &g_scene.surprise_obj);   /* surprise box (pickup 255) */
}

static void load_bomb(const char *base, thm_theme *th)
{
	char p[1024];
	tga_image im;

	load_slot_model(base, th, THM_SLOT_BOMB, &g_scene.bomb_mesh, &g_scene.bomb_tex);   /* live bomb model */
	/* Bomb's optional 2nd mesh = an additive glow shell (Space bomb01_gl "lasery" halo) */
	g_scene.bomb_glow_mesh = -1;
	g_scene.bomb_glow_tex = -1;
	for (int o = 0; o < th->num_objects; o++) {
		if (th->objects[o].slot != THM_SLOT_BOMB || th->objects[o].num_meshes < 2)
			continue;
		thm_mesh *tm = &th->objects[o].meshes[1];
		mdl_model mdl;
		if (tm->mesh[0] && asset_resolve(base, tm->mesh, p, sizeof p) && mdl_load(p, &mdl)) {
			g_scene.bomb_glow_mesh = r_upload_mesh(mdl_frame(&mdl, 0), (int)mdl.num_verts);
			mdl_free(&mdl);
		}
		if (tm->num_tex > 0 && tm->tex[0].tga[0]
		    && asset_resolve(base, tm->tex[0].tga, p, sizeof p) && tga_load(p, &im)) {
			g_scene.bomb_glow_tex = r_load_texture_rgba(im.rgba, im.width, im.height);
			tga_free(&im);
		}
		break;
	}
}

static void load_actors(const char *base, thm_theme *th)
{
	load_catcher(base, th);
	load_thrower(base, th);
	load_pickups(base, th);
	load_bomb(base, th);
	load_slot_obj(base, th, THM_SLOT_OBSTACLE, &g_scene.obstacle_obj); /* destructible rock (all sub-meshes) */
	memset(g_scene.obstacle_shatter, 0, sizeof g_scene.obstacle_shatter);
	load_slot_obj(base, th, THM_SLOT_PLATFORM, &g_scene.platform_obj);  /* horizontal mover */
	load_slot_obj(base, th, THM_SLOT_ELEVATOR, &g_scene.elevator_obj);  /* vertical mover: frame
	                                        (its walkable top pane = mover_field, load_tiles) */
	load_slot_obj(base, th, THM_SLOT_JUMPPAD,  &g_scene.jumppad_obj);   /* launch pad */
}

static void load_leo_props(const char *base)
{
	char p[1024], rel[512];

	/* extra 3D objects (.leo), pre-loaded into the caches */
	g_scene.leo.num = g_scene.leo.num_particles = g_scene.leo.num_sounds = 0;
	snprintf(rel, sizeof rel, "Level3DExtraObjects\\%s.leo", cur_scene_name);
	if (asset_resolve(base, rel, p, sizeof p))
		leo_load(p, &g_scene.leo);
	/* .leo anim-mesh cache was cleared by assets_reset_caches() at scene start */
	for (int i = 0; i < g_scene.leo.num; i++) {
		mesh_cached(base, g_scene.leo.objs[i].model);
		tex_cached(base, g_scene.leo.objs[i].tex);
		/* animated prop: upload all mesh frames + pick the loop clip from its .ani */
		g_scene.leo_amesh[i] = -1;
		if (g_scene.leo.objs[i].ani[0]) {
			int nf = 0;
			r_mesh am = leo_anim_mesh(base, g_scene.leo.objs[i].model, &nf);
			if (am >= 0 && nf > 1) {
				g_scene.leo_amesh[i] = am;
				g_scene.leo_afirst[i] = 0; g_scene.leo_acount[i] = nf; g_scene.leo_afps[i] = 12.0f;  /* fallback: loop all */
				ani_set as;
				if (asset_resolve(base, g_scene.leo.objs[i].ani, p, sizeof p) && ani_load(p, &as))
					for (int c = 0; c < ANI_NUM_CLIPS; c++)
						if (as.clips[c].present && as.clips[c].count > 0) {
							g_scene.leo_afirst[i] = as.clips[c].first;
							g_scene.leo_acount[i] = as.clips[c].count;
							g_scene.leo_afps[i]   = as.clips[c].fps > 0 ? as.clips[c].fps : 12.0f;
							break;   /* first present clip = the prop's loop */
						}
			}
		}
	}
	/* (.leo Particle props are turned into generic emitters in load_emitters,
	 * which reads g_scene.leo.parts[] parsed above.) */
	/* .leo Sound props: positional looping ambience (bee buzz, bird, cuckoo, castle
	 * fountain). Streamed as seamlessly-looping Music (no re-trigger click); mixed by
	 * camera distance each frame (r_music_update). */
	g_scene.leo_amb_n = 0;
	for (int i = 0; i < g_scene.leo.num_sounds && g_scene.leo_amb_n < LEO_MAX_SOUNDS; i++) {
		leo_sound *ls = &g_scene.leo.sounds[i];
		r_music h;
		if (!asset_resolve(base, ls->wav, p, sizeof p) || (h = r_load_music(p)) < 0)
			continue;
		g_scene.leo_amb_mus[g_scene.leo_amb_n] = h;
		g_scene.leo_amb_pos[g_scene.leo_amb_n] = (r_vec3){ ls->pos[1] + 0.5f, ls->pos[0] + 0.5f, ls->pos[2] };
		g_scene.leo_amb_n++;
	}
}

/* first theme object occupying `slot`, or NULL. */
static const thm_object *obj_for_slot(const thm_theme *th, int slot)
{
	for (int o = 0; o < th->num_objects; o++)
		if (th->objects[o].slot == slot)
			return &th->objects[o];
	return NULL;
}

/* register one placed (moving, world-space) emitter: decode its .par + load its
 * sprite, then push it. */
static void add_emitter(const char *base, const char *par_rel, const char *tex_rel,
                        int blend, int anchor, int ref, int rx, int ry,
                        r_vec3 pos, float yaw)
{
	if (g_scene.num_emitters >= FX_EMIT_MAX) {
		fprintf(stderr, "[fx] emitter list full (%d); dropping %s\n", FX_EMIT_MAX, par_rel);
		return;
	}
	char p[1024];
	par_system ps;
	if (!(asset_resolve(base, par_rel, p, sizeof p) && par_load(p, &ps)))
		return;   /* unresolved / undecodable .par (e.g. an XStd variant) -> silently skip */
	r_tex t = (tex_rel && tex_rel[0]) ? load_particle_tex(base, tex_rel) : g_scene.fx_flare;
	if (t < 0)
		t = g_scene.fx_flare;
	struct fx_emitter *em = &g_scene.emitters[g_scene.num_emitters++];
	em->par = ps;
	em->tex = t;
	em->blend = (unsigned char)blend;
	em->anchor = (unsigned char)anchor;
	em->ref = (short)ref;
	em->rx = (short)rx;
	em->ry = (short)ry;
	em->pos = pos;
	em->yaw_deg = yaw;
	em->accum = 0.0f;
	em->sz0 = em->sz1 = 0.0f;   /* no size override (slots are reused across levels) */
}

/* Stair candles/torches: each Stair particle system at every stair tile, rotated
 * to the tile facing (the candle offset lives in the .par's own spawn range). */
static void add_stair_emitters(const char *base, const thm_theme *th)
{
	const thm_object *stair = obj_for_slot(th, THM_SLOT_STAIR);
	if (stair && stair->num_particles > 0)
		for (int x = 0; x < lvl.dim_x; x++)
			for (int y = 0; y < lvl.dim_y; y++) {
				int tt = lvl.tiles[x][y].type;
				if (tt < TT_STAIR_LO || tt > TT_STAIR_HI)
					continue;
				float yaw = (float)(tt - TT_STAIR_LO) * 90.0f;
				r_vec3 pos = { x + 0.5f, y + 0.5f, (float)lvl.tiles[x][y].z_pos };
				for (int q = 0; q < stair->num_particles; q++)
					add_emitter(base, stair->particles[q].par, stair->particles[q].tex,
					            R_BLEND_ADD, FXA_STATIC, 0, x, y, pos, yaw);
			}
}

/* Exit fountain: the Exit object's particle systems at each exit tile, lit only
 * once the exit opens (all crystals collected). */
static void add_exit_emitters(const char *base, const thm_theme *th)
{
	const thm_object *exit = obj_for_slot(th, THM_SLOT_EXIT);
	if (exit && exit->num_particles > 0)
		for (int x = 0; x < lvl.dim_x; x++)
			for (int y = 0; y < lvl.dim_y; y++) {
				if (lvl.tiles[x][y].type != TT_EXIT)
					continue;
				r_vec3 pos = { x + 0.5f, y + 0.5f, (float)lvl.tiles[x][y].z_pos + 0.06f };
				for (int q = 0; q < exit->num_particles; q++)
					add_emitter(base, exit->particles[q].par, exit->particles[q].tex,
					            R_BLEND_ADD, FXA_EXIT, 0, x, y, pos, 0.0f);
			}
}

/* Teleporter shimmer: at every teleporter tile. */
static void add_teleporter_emitters(const char *base, const thm_theme *th)
{
	const thm_object *tele = obj_for_slot(th, THM_SLOT_TELEPORTER);
	if (tele && tele->num_particles > 0)
		for (int x = 0; x < lvl.dim_x; x++)
			for (int y = 0; y < lvl.dim_y; y++) {
				if (lvl.tiles[x][y].type != TT_TELEPORT)
					continue;
				r_vec3 pos = { x + 0.5f, y + 0.5f, (float)lvl.tiles[x][y].z_pos + 0.06f };
				for (int q = 0; q < tele->num_particles; q++)
					add_emitter(base, tele->particles[q].par, tele->particles[q].tex,
					            R_BLEND_ADD, FXA_STATIC, 0, x, y, pos, 0.0f);
			}
}

/* John's diver-helmet bubbles (Water helmblubber): follows the player. Alpha
 * blend — bubbles are masked/translucent, not additive glow. */
static void add_john_emitters(const char *base, const thm_theme *th)
{
	const thm_object *john = obj_for_slot(th, THM_SLOT_JOHN);
	if (john)
		for (int q = 0; q < john->num_particles; q++)
			add_emitter(base, john->particles[q].par, john->particles[q].tex,
			            R_BLEND_ALPHA, FXA_PLAYER, 0, -1, -1, (r_vec3){ 0, 0, 0 }, 0.0f);
}

/* Thrower engine exhaust (triebwerk): under each thrower enemy (mesh float offset). */
static void add_thrower_emitters(const char *base, const thm_theme *th)
{
	const thm_object *thr = obj_for_slot(th, THM_SLOT_THROWER);
	if (thr && thr->num_particles > 0)
		for (int i = 0; i < sim.num_enemies; i++) {
			if (!sim.enemies[i].is_thrower)
				continue;
			for (int q = 0; q < thr->num_particles; q++)
				add_emitter(base, thr->particles[q].par, thr->particles[q].tex,
				            R_BLEND_ADD, FXA_ENEMY, i, -1, -1,
				            (r_vec3){ 0, 0, g_scene.thrower_off_z }, 0.0f);
		}
}

/* .leo-placed particle props (fountains, bee swarms): world-anchored, blend from
 * the .leo ("One/One" = additive glow, else masked alpha). */
static void add_leo_emitters(const char *base)
{
	for (int i = 0; i < g_scene.leo.num_particles; i++) {
		leo_particle *lp = &g_scene.leo.parts[i];
		int blend = (strcasecmp(lp->src, "one") == 0) ? R_BLEND_ADD : R_BLEND_ALPHA;
		r_vec3 pos = { lp->pos[1] + 0.5f + lp->off[1], lp->pos[0] + 0.5f + lp->off[0],
		               lp->pos[2] + lp->off[2] };
		int before = g_scene.num_emitters;
		add_emitter(base, lp->par, lp->tex, blend, FXA_STATIC, 0, -1, -1, pos, 0.0f);
		/* masked .leo swarms = the bees: their sprite data is RED 2x2, but they read
		 * as insects only as tiny BLACK diamonds (the pre-.par manual quirk) — blacken
		 * this emitter's private colour ramp. Additive .leo props (fountains) keep
		 * their .par colours. */
		if (blend == R_BLEND_ALPHA && g_scene.num_emitters > before) {
			struct fx_emitter *bem = &g_scene.emitters[before];
			for (int c = 0; c < bem->par.num_colors; c++)
				bem->par.colors[c].rgb[0] = bem->par.colors[c].rgb[1] =
				bem->par.colors[c].rgb[2] = 0;
			bem->sz0 = 0.020f;   /* the 8981214 tuning: "only reads as insects when
			                      * it's small and busy" — spawn 0.020, die 0.013 */
			bem->sz1 = 0.013f;
			/* mill AROUND the hive: the .par's dir range is an all-positive octant,
			 * which read as a constant up/side drift — the swarm slid ~half a tile
			 * above the korb. Symmetrize the drift (the old quirk's +-0.25/+-0.15). */
			bem->par.dir_lo[0] = -1.0f; bem->par.dir_hi[0] = 1.0f;
			bem->par.dir_lo[1] = -0.6f; bem->par.dir_hi[1] = 0.6f;
			bem->par.dir_lo[2] = -1.0f; bem->par.dir_hi[2] = 1.0f;
			bem->par.speed_lo = 0.25f;
			bem->par.speed_hi = 0.35f;
		}
	}
}

/* Build the per-level particle-emitter list from EVERY theme/leo particle system,
 * anchored to the instances that carry it: crystal sparkles at each gem, stair
 * candles/torches at each stair (rotated to facing), the exit fountain, teleporter
 * shimmer, John's helmet bubbles, thrower exhaust, and .leo fountains/swarms. Theme
 * FX are additive (One/One) — the theme's own blend keywords are skipped by the
 * parser; .leo props carry their own blend. Called after sim_init (enemies known). */
static void load_emitters(const char *base, const thm_theme *th)
{
	g_scene.num_emitters = 0;

	/* (CrystalFX is a notMovable spark bound to the animated gem — loaded in load_fx
	 * and drawn attached at the gem transform in render_scene, not a world emitter.) */

	add_stair_emitters(base, th);
	add_exit_emitters(base, th);
	add_teleporter_emitters(base, th);
	add_john_emitters(base, th);
	add_thrower_emitters(base, th);
	add_leo_emitters(base);
}

static void load_john(const char *base, thm_theme *th)
{
	char p[1024];
	tga_image im;

	/* John (theme slot 0): each sub-mesh uploaded once; the alive-condition tex
	 * (0/4) builds the live body, the Dead-condition tex (3) builds the angel
	 * (k+kopf on grey.tga, fluegel on fluegel64.tga — all additive). fluegel is
	 * dead-only so it appears only when the death sequence renders john_dead. */
	g_scene.john_n = 0;
	g_scene.john_dead_n = 0;
	g_scene.john_glide_n = 0;
	g_scene.john_frames = 1;
	char john_ani[THM_PATH] = {0};
	g_scene.jmin = (r_vec3){ 1e9f, 1e9f, 1e9f };
	g_scene.jmax = (r_vec3){ -1e9f, -1e9f, -1e9f };
	for (int o = 0; o < th->num_objects && g_scene.john_n == 0; o++) {
		if (th->objects[o].slot != THM_SLOT_JOHN)
			continue;
		thm_object *ob = &th->objects[o];
		for (int mi = 0; mi < ob->num_meshes; mi++) {
			thm_mesh *tm = &ob->meshes[mi];
			if (!tm->mesh[0])
				continue;
			int alive_ti = -1, dead_ti = -1, glide_ti = -1;
			for (int ti = 0; ti < tm->num_tex; ti++) {
				int cond = tm->tex[ti].condition;
				if ((cond == THM_COND_NONE || cond == THM_COND_ALIVE) && alive_ti < 0) alive_ti = ti;
				if (cond == THM_COND_DEAD && dead_ti < 0) dead_ti = ti;
				if (cond == THM_COND_PARAGLIDE && glide_ti < 0) glide_ti = ti;
			}
			mdl_model mdl;
			if (!(asset_resolve(base, tm->mesh, p, sizeof p) && mdl_load(p, &mdl))) {
				fprintf(stderr, "  John mesh MISS: %s\n", tm->mesh);
				continue;
			}
			r_mesh mh = r_upload_anim(mdl.verts, (int)mdl.num_verts, mdl.num_frames);
			int nf = (int)mdl.num_frames;
			r_vec3 bmn = { mdl.frame_hdrs[0].bbox_min[0], mdl.frame_hdrs[0].bbox_min[1], mdl.frame_hdrs[0].bbox_min[2] };
			r_vec3 bmx = { mdl.frame_hdrs[0].bbox_max[0], mdl.frame_hdrs[0].bbox_max[1], mdl.frame_hdrs[0].bbox_max[2] };
			if (!john_ani[0] && tm->anim[0])
				snprintf(john_ani, sizeof john_ani, "%s", tm->anim);
			mdl_free(&mdl);
			/* alive body mesh (drives frame count, bbox, .ani) */
			if (tm->num_tex == 0 || alive_ti >= 0) {
				draw_mesh dm = { .mesh = mh, .tex = -1, .env_tex = -1, .bmin = bmn, .bmax = bmx };
				if (alive_ti >= 0 && tm->tex[alive_ti].tga[0]
				    && asset_resolve(base, tm->tex[alive_ti].tga, p, sizeof p) && tga_load(p, &im)) {
					dm.tex = r_load_texture_rgba(im.rgba, im.width, im.height);
					tga_free(&im);
				}
				/* Environment reflection (e.g. Castle knight's ruestung/ritterhelm shine) */
				for (int ti = 0; ti < tm->num_tex; ti++)
					if (tm->tex[ti].environment && tm->tex[ti].tga[0]) {
						dm.env_tex = load_particle_tex(base, tm->tex[ti].tga);
						break;
					}
				g_scene.john_frames = nf;
				g_scene.jmin.x = fminf(g_scene.jmin.x, bmn.x); g_scene.jmin.y = fminf(g_scene.jmin.y, bmn.y); g_scene.jmin.z = fminf(g_scene.jmin.z, bmn.z);
				g_scene.jmax.x = fmaxf(g_scene.jmax.x, bmx.x); g_scene.jmax.y = fmaxf(g_scene.jmax.y, bmx.y); g_scene.jmax.z = fmaxf(g_scene.jmax.z, bmx.z);
				g_scene.john[g_scene.john_n++] = dm;
			}
			/* dead/angel mesh (shares the uploaded handle; rendered additively).
			 * GHOST clip is the death pose: body -> k.ani GHOST frame (arms-up
			 * float), fluegel wings -> its own frame 0. */
			if (dead_ti >= 0) {
				draw_mesh dd = { .mesh = mh, .tex = -1, .env_tex = -1, .bmin = bmn, .bmax = bmx };
				if (tm->tex[dead_ti].tga[0]
				    && asset_resolve(base, tm->tex[dead_ti].tga, p, sizeof p) && tga_load(p, &im)) {
					dd.tex = r_load_texture_rgba(im.rgba, im.width, im.height);
					tga_free(&im);
				}
				g_scene.john_dead_wings[g_scene.john_dead_n] = (strstr(tm->mesh, "fluegel") != NULL);
				g_scene.john_dead_nf[g_scene.john_dead_n] = nf;
				g_scene.john_dead[g_scene.john_dead_n++] = dd;
			}
			/* Condition-Paraglide chute submesh (Candy schirm3 / Egypt Fallschirm /
			 * Space): shares the uploaded handle; k.ani-animated so it tracks the
			 * body pose (stowed on the back, opening in the PARAGLIDE clip). */
			if (glide_ti >= 0
			    && g_scene.john_glide_n < (int)(sizeof g_scene.john_glide
			                                    / sizeof g_scene.john_glide[0])) {
				draw_mesh dg = { .mesh = mh, .tex = -1, .env_tex = -1,
				                 .bmin = bmn, .bmax = bmx };
				if (tm->tex[glide_ti].tga[0]
				    && asset_resolve(base, tm->tex[glide_ti].tga, p, sizeof p)
				    && tga_load(p, &im)) {
					dg.tex = r_load_texture_rgba(im.rgba, im.width, im.height);
					tga_free(&im);
				}
				g_scene.john_glide[g_scene.john_glide_n++] = dg;
			}
		}
	}
	g_scene.jscale = 1.0f;

	memset(&g_scene.john_anim, 0, sizeof g_scene.john_anim);
	if (john_ani[0] && asset_resolve(base, john_ani, p, sizeof p))
		ani_load(p, &g_scene.john_anim);
}

bool load_scene_named(const char *base, const char *name)
{
	char p[1024], rel[512];
	thm_theme th;

	r_reset();                     /* free previous level's meshes/textures */
	assets_reset_caches();         /* invalidate .leo asset caches */
	snprintf(cur_scene_name, sizeof cur_scene_name, "%s", name);

	snprintf(rel, sizeof rel, "Levels\\%s.jjm", name);
	if (!asset_resolve(base, rel, p, sizeof p) || !jjm_load(p, &lvl)) {
		fprintf(stderr, "jjm load FAILED (%s)\n", rel);
		return false;
	}
	snprintf(rel, sizeof rel, "Themes\\%s.thm", lvl.world);
	if (!asset_resolve(base, rel, p, sizeof p) || !thm_load(p, &th)) {
		fprintf(stderr, "thm load FAILED (%s)\n", lvl.world);
		return false;
	}
	printf("scene = %s | grid %ux%u | world=%s | crystals=%u | bonus=%u\n",
	       name, lvl.dim_x, lvl.dim_y, lvl.world, lvl.crystals_needed, lvl.is_bonus);

	int max_z = 1;
	for (int x = 0; x < lvl.dim_x; x++)
		for (int y = 0; y < lvl.dim_y; y++)
			if (lvl.tiles[x][y].z_pos > max_z)
				max_z = lvl.tiles[x][y].z_pos;

	load_tiles(base, &th);
	load_hud(base, &th);
	load_sounds(base, &th);   /* theme Sound<event> map + global clock/ADD wavs */
	load_fx(base, &th);
	load_tile_objects(base, &th);   /* crystal / stair / glue */
	load_actors(base, &th);         /* enemies, pickups, bombs, movers, obstacles */
	load_leo_props(base);           /* .leo extra objects + particle/sound props */
	sim_init(&sim, &lvl);
	load_emitters(base, &th);       /* .par particle emitters (needs the grid + enemies) */
	load_john(base, &th);

	/* freecam seed: overview framed from the grid dims */
	float cx = lvl.dim_x * 0.5f, cy = lvl.dim_y * 0.5f;
	float span = (float)(lvl.dim_x > lvl.dim_y ? lvl.dim_x : lvl.dim_y);
	cam = (r_camera){ .pos = { cx, cy - span * 0.9f, max_z + span * 0.75f },
	                  .target = { cx, cy, (float)max_z * 0.4f }, .up = { 0, 0, 1 }, .fovy = 50.0f };
	return true;
}


bool load_scene(const char *base, const gam_manifest *g, int idx)
{
	if (idx < 0 || idx >= g->num_levels)
		idx = 0;
	return load_scene_named(base, g->levels[idx]);
}
