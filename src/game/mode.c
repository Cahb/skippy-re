/* Game session setup, mode predicates and transitions (see game/mode.h). */
#include "game/mode.h"
#include "game/scene.h"        /* load_scene, load_scene_named, lvl, sim, cam, cur_scene_name */
#include "game/audio.h"        /* audio_music_want (CD-track background music) */
#include "formats/asset.h"
#include "formats/sav.h"

#include <stdlib.h>            /* getenv */
#include <stdio.h>            /* snprintf */
#include <string.h>            /* memset */

bool game_init(struct game *g, const char *base, const gam_manifest *man,
               int cur, bool menu)
{
	*g = (struct game){0};
	g->base = base;
	g->man = *man;
	g->cur = cur;
	g->hp = HP_START;
	g->want_intro = true;      /* the first level entry plays its intro too */
	g->intro_snd = -1;         /* 0 is a real sound handle — mark "no narration yet" */
	g->menu.scr = MS_MAIN;     /* the title menu (used when start_menu) */
	g->show_debug = true;
	g->mode = GM_SPLASH;
	g->start_menu = menu;

	/* the menu runs DemoLevelForest live under a looping .jjs camera; a debug start
	 * level (argv[2]) skips it. If the demo can't load, fall back to a normal start. */
	if (menu) {
		if (load_scene_named(base, "DemoLevelForest")) {
			char sp[1024];
			if (asset_resolve(base, "InstructionScripts\\DemoLevelForest.jjs", sp, sizeof sp))
				g->menu_vm_ok = jjs_load(sp, &g->menu_vm);
			float e[3] = { cam.pos.x, cam.pos.y, cam.pos.z };
			float l[3] = { cam.target.x, cam.target.y, cam.target.z };
			jjs_seed(&g->menu_vm, e, l);
		} else {
			g->start_menu = false;
		}
	}
	if (!g->start_menu && !load_scene(base, &g->man, cur))
		return false;
	/* background music: the title menu plays the "Main" CD track; a direct level
	 * start plays its world's track. */
	audio_music_want(base, g->start_menu ? "Main" : lvl.world);

	/* seed the follow cam + anim from the loaded scene */
	freecam_sync(&g->fc, cam);
	if (getenv("FREECAM"))
		g->fc.active = true;
	g->boom = (struct cam_smooth){ .fx = sim.p.fwd_x, .fy = sim.p.fwd_y, .pitch = 0.686f };
	g->anim = (struct player_anim){ .clip = ANI_IDLE2 };
	g->pick = (struct picker){ .kind = PICK_NONE };

	char p[1024];
	g->splash_tex = asset_resolve(base, "Bitmaps\\LOADING.bmp", p, sizeof p)
	                ? r_load_texture_file(p) : -1;
	g->load_tex = -1;
	return true;
}

/* ---- predicates ---- */

bool mode_scripted(const struct game *g)
{
	return g->mode == GM_MENU || g->mode == GM_INTRO;
}

bool gameplay_input(const struct game *g)
{
	return g->mode == GM_PLAYING && !g->fc.active && !g->anim.inspect;
}

/* the single freeze authority: what sim timestep this frame gets.
 *  - MENU / INTRO(playing): the scripted scene runs at real dt (ignores pause).
 *  - INTRO(ready) / DEATH / TIMEOUT / COMPLETE: world frozen.
 *  - PLAYING: real dt, honouring the debug pause / frame-step. */
float game_gdt(const struct game *g, float dt, bool step)
{
	switch (g->mode) {
	case GM_MENU:
		return dt;
	case GM_INTRO:
		return g->intro_vm.done ? 0.0f : dt;
	case GM_DEATH:
	case GM_TIMEOUT:
	case GM_COMPLETE:
	case GM_PAUSED:
		return 0.0f;
	default:  /* GM_PLAYING */
		return g->paused ? (step ? 1.0f / 60.0f : 0.0f) : dt;
	}
}

/* ---- transitions ---- */

/* load <world>.bmp for the current level and switch to the loading screen. */
void enter_loading(struct game *g)
{
	char rel[256], pth[1024];
	snprintf(rel, sizeof rel, "Bitmaps\\%s.bmp", lvl.world);
	g->load_tex = asset_resolve(g->base, rel, pth, sizeof pth)
	              ? r_load_texture_file(pth) : -1;
	g->mode = GM_LOADING;
	g->mode_t = 0.0f;
}

void level_enter_reset(struct game *g)
{
	g->lastsec_next = 10.0f;   /* covers levels that START under 10s on the clock */
	g->boom.fx = sim.p.fwd_x;
	g->boom.fy = sim.p.fwd_y;
	g->anim.clip = ANI_IDLE2;
	g->anim.frame = 0.0f;
	g->anim.idle_t = 0.0f;
}

void game_enter_level(struct game *g, int idx, bool play_intro)
{
	g->cur = idx;
	load_scene(g->base, &g->man, idx);
	audio_music_want(g->base, lvl.world);   /* world CD track (same world = no restart) */
	level_enter_reset(g);
	g->level_time = 0.0f;
	g->pick.kind = PICK_NONE;
	g->want_intro = play_intro;
	enter_loading(g);
}

/* the save name matches the manifest ("JJ.GAM" -> "jj", like the stock slots). A
 * player-entered name arrives with the menu system; until then every slot is jj's. */
#define SAV_PLAYER "jj"

static void sav_slot_path(const struct game *g, int slot, char *out, size_t n)
{
	snprintf(out, n, "%s/SavedGames/%s%d.sav", g->base, SAV_PLAYER, slot);
}

bool game_save_slot(struct game *g, int slot, const char *name, int level_idx)
{
	if (slot < 0 || slot >= SAV_SLOTS)
		return false;
	if (level_idx < 0)
		level_idx = 0;
	if (level_idx > g->man.num_levels - 1)
		level_idx = g->man.num_levels - 1;
	char p[1024];
	sav_slot_path(g, slot, p, sizeof p);
	sav_slot s;
	if (!sav_read(p, &s))              /* keep an existing slot's pad bytes verbatim */
		memset(&s, 0, sizeof s);
	snprintf(s.name, sizeof s.name, "%s", name && name[0] ? name : SAV_PLAYER);
	s.current_lvl = (uint8_t)level_idx;
	s.hearts_left = (uint8_t)(g->hp < 0 ? 0 : g->hp);
	s.total_score = (uint16_t)(g->score < 0 ? 0 : g->score > 0xffff ? 0xffff : g->score);
	return sav_write(p, &s);
}

bool game_load_slot(struct game *g, int slot)
{
	if (slot < 0 || slot >= SAV_SLOTS)
		return false;
	char p[1024];
	sav_slot_path(g, slot, p, sizeof p);
	sav_slot s;
	if (!sav_read(p, &s))
		return false;
	g->hp = s.hearts_left > 0 ? s.hearts_left : HP_START;
	g->score = s.total_score;
	audio_music_reset();               /* a load is a run boundary: track from the top */
	game_enter_level(g, s.current_lvl, true);
	return true;
}

void game_enter_menu(struct game *g)
{
	if (load_scene_named(g->base, "DemoLevelForest")) {
		char sp[1024];
		g->menu_vm_ok = false;
		if (asset_resolve(g->base, "InstructionScripts\\DemoLevelForest.jjs", sp, sizeof sp))
			g->menu_vm_ok = jjs_load(sp, &g->menu_vm);
		float e[3] = { cam.pos.x, cam.pos.y, cam.pos.z };
		float l[3] = { cam.target.x, cam.target.y, cam.target.z };
		jjs_seed(&g->menu_vm, e, l);
		freecam_sync(&g->fc, cam);
	}
	audio_music_want(g->base, "Main");
	g->menu = (struct menu){ .scr = MS_MAIN };
	g->mode = GM_MENU;
	g->mode_t = 0.0f;
	g->pick.kind = PICK_NONE;
}

void game_jump_level(struct game *g, int idx)
{
	g->cur = idx;
	load_scene(g->base, &g->man, idx);
	audio_music_want(g->base, lvl.world);
	level_enter_reset(g);
	g->level_time = 0.0f;
	g->pick.kind = PICK_NONE;
	g->mode = GM_PLAYING;
}
