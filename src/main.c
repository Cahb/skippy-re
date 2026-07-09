/* Skippy/Ka'roo — native rewrite: the game loop.
 *
 * Boot -> mode state machine (splash/menu/intro/playing/death/timeout/complete;
 * see game/mode.h). Each frame: read input, advance the world by the mode's
 * timestep, then build the camera and draw the scene + HUD. All per-level assets,
 * rendering, audio, camera math, HUD and FX live in the game/ modules; this file
 * is the loop that wires them together. World is Z-UP. */
#include "formats/gam.h"
#include "formats/jjm.h"
#include "formats/ani.h"
#include "formats/jjs.h"
#include "formats/asset.h"
#include "sim/sim.h"
#include "render/renderer.h"
#include "game/fx.h"
#include "game/fx_emit.h"
#include "game/camera.h"
#include "game/scene.h"
#include "game/render_scene.h"
#include "game/audio.h"
#include "game/hud.h"
#include "game/mode.h"
#include "game/options.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

#define WIN_W 1280
#define WIN_H 720

/* short label for a tile type (debug picker HUD) */
static const char *tile_mnemonic(int t)
{
	switch (t) {
	case TT_VOID:     return "void";
	case TT_FLOOR:    return "floor";
	case TT_GLUE:     return "glue";
	case TT_SPAWN:    return "spawn";
	case TT_EXIT:     return "exit";
	case TT_STAIR_PX: return "stair+X";
	case TT_STAIR_PY: return "stair+Y";
	case TT_STAIR_NX: return "stair-X";
	case TT_STAIR_NY: return "stair-Y";
	case TT_ELEVATOR: return "elevator";
	case TT_MOVER_Y:  return "mover+Y";
	case TT_MOVER_X:  return "mover+X";
	case TT_DESTRUCT: return "destruct";
	case TT_JUMPPAD:  return "jumppad";
	case TT_TELEPORT: return "teleport";
	case TT_SWITCH:   return "switch";
	case TT_BRIDGE_X: return "bridge+X";
	case TT_BRIDGE_Y: return "bridge+Y";
	case TT_PLANK:    return "plank";
	case TT_ICE:      return "ice";
	case TT_DECOR:    return "decoration";
	case TT_OBSTACLE: return "obstacle";
	default:          return "?";
	}
}

/* John's world position; the spirit rises out of frame during the death sequence. */
static r_vec3 player_world_pos(const struct game *g)
{
	r_vec3 w = { sim.p.rx, sim.p.ry, sim.p.rz };
	if (g->mode == GM_DEATH)
		w.z += g->mode_t * 2.5f;
	return w;
}

/* GM_LOADING is done: begin gameplay, or the level's how-to intro if one is armed. */
static void enter_playing_or_intro(struct game *g)
{
	g->mode = GM_PLAYING;
	g->mode_t = 0.0f;
	g->level_time = 0.0f;
	if (g->want_intro) {
		char rel[300], sp[1024];
		snprintf(rel, sizeof rel, "InstructionScripts\\%s.jjs", cur_scene_name);
		if (asset_resolve(g->base, rel, sp, sizeof sp) && jjs_load(sp, &g->intro_vm)) {
			g->mode = GM_INTRO;
			float e[3] = { cam.pos.x, cam.pos.y, cam.pos.z };
			float l[3] = { cam.target.x, cam.target.y, cam.target.z };
			jjs_seed(&g->intro_vm, e, l);
		}
	}
	g->want_intro = false;
}

/* the pre-gameplay shell (splash / loading): advance the phase timer, draw the
 * bitmap. Runs its own begin/end frame; the caller `continue`s afterward. */
static void shell_frame(struct game *g)
{
	if (g->mode == GM_SPLASH) {
		if (g->mode_t > 2.5f || r_any_key_pressed()) {
			if (g->start_menu) { g->mode = GM_MENU; g->mode_t = 0.0f; }
			else               enter_loading(g);
		}
	} else if (g->mode_t > 0.45f) {   /* GM_LOADING: brief fake delay */
		enter_playing_or_intro(g);
	}
	r_begin_frame((r_color){ 0, 0, 0, 255 });
	hud_draw_shell(g->mode == GM_SPLASH ? SCR_SPLASH : SCR_LOADING, g->splash_tex, g->load_tex);
	r_end_frame();
}

/* per-mode pre-tick logic: advance the scripted VM, or resolve an ENTER prompt.
 * Returns true if a level (re)load fired — the caller restarts the frame. */
static bool mode_update(struct game *g, float dt)
{
	switch (g->mode) {
	case GM_MENU:
	case GM_INTRO: {
		sim.protect_t = 1.0f;        /* spectator: invulnerable... */
		sim.enemy_freeze_t = 1.0f;   /* ...enemies hold still during the cinematic */
		jjs_vm *vm = (g->mode == GM_MENU) ? &g->menu_vm : &g->intro_vm;
		bool run = (g->mode == GM_MENU) ? g->menu_vm_ok : !g->intro_vm.done;
		if (run) {
			float actor[3] = { sim.p.rx, sim.p.ry, sim.p.rz };
			jjs_tick(vm, dt, actor);
			if (g->mode == GM_INTRO && vm->want_wave[0]) {   /* narration voice line */
				char vp[1024];
				if (asset_resolve(g->base, vm->want_wave, vp, sizeof vp)) {
					g->intro_snd = r_load_sound(vp);   /* remember it: ENTER-skip cuts it */
					r_play_sound(g->intro_snd);
				}
			}
		}
		if (g->mode == GM_MENU) {                    /* the real title menu */
			enum menu_action act = menu_update(&g->menu, g->base);
			if (act == MA_NEW_GAME) {
				sim.protect_t = 0.0f;
				sim.enemy_freeze_t = 0.0f;
				audio_music_reset();                 /* new run: world track from the top */
				g->score = 0;
				g->hp = HP_START;
				game_enter_level(g, 0, true);        /* a NEW game starts at level 1 —
				                                        g->cur may be mid-run after a
				                                        quit-to-menu */
				return true;
			}
			if (act >= MA_LOAD_SLOT0 && act < MA_LOAD_SLOT0 + SAV_SLOTS) {
				sim.protect_t = 0.0f;
				sim.enemy_freeze_t = 0.0f;
				if (game_load_slot(g, act - MA_LOAD_SLOT0))
					return true;
			}
			if (act == MA_EXIT)
				g->quit = true;
			return false;
		}
		if (r_key_pressed(R_KEY_ENTER)) {
			sim.protect_t = 0.0f;
			sim.enemy_freeze_t = 0.0f;
			g->mode = GM_PLAYING;                    /* intro: skip / confirm -> gameplay */
			g->mode_t = 0.0f;
			r_stop_sound(g->intro_snd);              /* don't let skipped narration ramble on */
			g->intro_snd = -1;
			level_enter_reset(g);
		}
		return false;
	}
	case GM_PAUSED: {
		enum menu_action act = menu_update(&g->menu, g->base);
		if (act == MA_RESUME) {
			g->mode = GM_PLAYING;
			g->mode_t = 0.0f;
			return false;
		}
		if (act == MA_NAME_DONE) {
			if (g->menu.name_for == NE_SAVE) {   /* pause-menu save slot named */
				game_save_slot(g, g->menu.save_slot, g->menu.edit, g->cur);
				menu_pop(&g->menu);              /* back to the slot list */
				menu_refresh_slots(&g->menu, g->base);
				return false;
			}
			/* qualified highscore committed */
			hsc_insert(&g->menu.records, g->menu.edit_len ? g->menu.edit : "jj",
			           (uint32_t)(g->score > 0 ? g->score : 0), g->cur + 1);
			char hp[1024];
			if (!asset_resolve(g->base, "Highscores\\jj.hsc", hp, sizeof hp))
				snprintf(hp, sizeof hp, "%s/highscores/jj.hsc", g->base);
			hsc_write(hp, &g->menu.records);
			g->menu.scr = MS_RECORDS;        /* show the table, then the flow continues */
			g->menu.depth = 0;
			g->menu.sel = 0;
			return false;
		}
		if (g->menu.scr == MS_NONE) {        /* the menu closed itself */
			if (g->gameover_entry) {         /* ...after the game-over records flow */
				g->gameover_entry = false;
				g->hp = HP_START;
				audio_music_reset();
				g->score = 0;
				game_enter_level(g, 0, true);
				return true;
			}
			g->mode = GM_PLAYING;
			g->mode_t = 0.0f;
			return false;
		}
		if (act >= MA_SAVE_SLOT0 && act < MA_SAVE_SLOT0 + SAV_SLOTS) {
			g->menu.name_for = NE_SAVE;          /* name the slot before writing it */
			g->menu.save_slot = act - MA_SAVE_SLOT0;
			g->menu.name_max = SAV_NAME;
			g->menu.edit_len = 0;
			g->menu.edit[0] = 0;
			menu_open(&g->menu, MS_NAME_ENTRY, g->base);
		}
		if (act >= MA_LOAD_SLOT0 && act < MA_LOAD_SLOT0 + SAV_SLOTS
		    && game_load_slot(g, act - MA_LOAD_SLOT0))
			return true;
		if (act == MA_QUIT_TO_MENU) {
			game_enter_menu(g);
			return true;
		}
		if (act == MA_EXIT)
			g->quit = true;
		return false;
	}
	case GM_DEATH:
		if (g->mode_t > 0.6f && r_key_pressed(R_KEY_ENTER)) {
			if (lvl.is_bonus) {            /* bonus levels: dying is harmless — the try is
			                                  simply over, move on to the next level */
				game_enter_level(g, g->cur + 1, true);
				return true;
			}
			g->hp -= 1;
			if (g->hp <= 0) {              /* last life: game over */
				menu_refresh_records(&g->menu, g->base);
				if (hsc_qualifies(&g->menu.records, (uint32_t)(g->score > 0 ? g->score : 0)) >= 0) {
					g->menu.scr = MS_NAME_ENTRY;   /* earn a spot: type the name first */
					g->menu.depth = 0;
					g->menu.edit_len = 0;
					g->menu.edit[0] = 0;
					g->gameover_entry = true;
					g->mode = GM_PAUSED;
					g->mode_t = 0.0f;
					return false;
				}
				g->hp = HP_START;          /* no record: straight to a fresh run */
				audio_music_reset();
				g->score = 0;
				game_enter_level(g, 0, true);
				return true;
			}
			sim_respawn_player(&sim);      /* HP left: respawn in place, no loading */
			level_enter_reset(g);
			g->level_time = 0.0f;          /* a fresh try gets a fresh level clock */
			g->mode = GM_PLAYING;
			g->mode_t = 0.0f;
		}
		return false;
	case GM_TIMEOUT:
		if (g->mode_t > 0.6f && r_key_pressed(R_KEY_ENTER)) {
			if (lvl.is_bonus) {            /* bonus level: the timed try is simply over,
			                                  move on to the next level (like bonus death) */
				game_enter_level(g, g->cur + 1, true);
				return true;
			}
			g->hp -= 1;
			if (g->hp <= 0) {              /* last life: game over (records flow, as DEATH) */
				menu_refresh_records(&g->menu, g->base);
				if (hsc_qualifies(&g->menu.records, (uint32_t)(g->score > 0 ? g->score : 0)) >= 0) {
					g->menu.scr = MS_NAME_ENTRY;
					g->menu.depth = 0;
					g->menu.edit_len = 0;
					g->menu.edit[0] = 0;
					g->gameover_entry = true;
					g->mode = GM_PAUSED;
					g->mode_t = 0.0f;
					return false;
				}
				g->hp = HP_START;
				audio_music_reset();
				g->score = 0;
				game_enter_level(g, 0, true);
				return true;
			}
			game_enter_level(g, g->cur, false);   /* restart the level */
			return true;
		}
		return false;
	case GM_COMPLETE:
		if (g->mode_t <= 0.4f)
			return false;
		if (g->menu.scr != MS_NONE) {          /* the Save flow over the score screen */
			enum menu_action act = menu_update(&g->menu, g->base);
			if (act >= MA_SAVE_SLOT0 && act < MA_SAVE_SLOT0 + SAV_SLOTS) {
				g->menu.name_for = NE_SAVE;
				g->menu.save_slot = act - MA_SAVE_SLOT0;
				g->menu.name_max = SAV_NAME;
				g->menu.edit_len = 0;
				g->menu.edit[0] = 0;
				menu_open(&g->menu, MS_NAME_ENTRY, g->base);
			} else if (act == MA_NAME_DONE) {
				/* the level on screen is BEATEN: the save must resume at the next */
				game_save_slot(g, g->menu.save_slot, g->menu.edit, g->cur + 1);
				g->menu = (struct menu){0};    /* saved: back to the score screen */
			}
			return false;
		}
		{
			int was = g->comp_sel;             /* Next / Save selector (gloves) */
			if (r_key_pressed(R_KEY_UP) || r_key_pressed(R_KEY_LEFT))    g->comp_sel = 0;
			if (r_key_pressed(R_KEY_DOWN) || r_key_pressed(R_KEY_RIGHT)) g->comp_sel = 1;
			if (g->comp_sel != was)
				r_play_sound(g_scene.snd_menu);
		}
		if (r_key_pressed(R_KEY_ENTER)) {
			if (g->comp_sel == 1) {            /* Save: pick a slot + name it, then return */
				g->menu = (struct menu){ .scr = MS_SAVE };
				menu_refresh_slots(&g->menu, g->base);
				return false;
			}
			game_enter_level(g, g->cur + 1, true);
			return true;
		}
		return false;
	default:   /* GM_PLAYING: gameplay transitions are detected post-tick. ESC opens
	              the pause menu HERE (not handle_keys) so the same key edge can't be
	              seen again by the GM_PAUSED handler in this very frame. */
		if (r_key_pressed(R_KEY_ESC)) {
			g->menu = (struct menu){ .scr = MS_PAUSE };
			g->mode = GM_PAUSED;
			g->mode_t = 0.0f;
		}
		return false;
	}
}

/* advance the global clock + level timer by the (frozen-aware) timestep. */
static void advance_clock(struct game *g, float gdt)
{
	g->t_accum += gdt;
	if (g->mode != GM_PLAYING)
		return;   /* the level clock only runs in play — an intro script animates
		           * the world (gdt flows) but must not burn the time limit */
	g->level_time += gdt;
	if (sim.time_bonus > 0.0f) {   /* Time pickup: rewind the clock */
		g->level_time -= sim.time_bonus;
		sim.time_bonus = 0.0f;
		if (g->level_time < 0.0f)
			g->level_time = 0.0f;
	}
}

/* post-tick (GM_PLAYING only): enter DEATH / TIMEOUT / COMPLETE when the sim says so;
 * also emit the final-10s clock ticks. */
static void check_transitions(struct game *g)
{
	if (!sim.p.alive) {
		g->mode = GM_DEATH;
		g->mode_t = 0.0f;
		g->death_x = sim.p.rx;   /* lock the camera on the death cell */
		g->death_y = sim.p.ry;
		g->death_z = sim.p.rz;
		g->death_a0 = atan2f(-g->boom.fy, -g->boom.fx);
		return;
	}
	if (lvl.time_limit > 0) {
		float rem = (float)lvl.time_limit - g->level_time;
		/* final 10s: one tick per integer second boundary. The original keeps a
		 * next-tick mark armed at 10.0 while above, plays when the remaining
		 * time crosses below it, and re-arms to floor(remaining) (RE game_tick
		 * 0x415415: skippy+0x65 double, reset via the >threshold branch). */
		if (rem > 11.0f) {   /* re-arm threshold is 11, not 10 (0x45d3c8): a time
		                      * bonus lifting the clock to 10.x must not re-arm
		                      * and duplicate the tick it already played */
			g->lastsec_next = 10.0f;
		} else if (rem < g->lastsec_next) {
			if (g_scene.snd_lastsec >= 0)
				r_play_sound(g_scene.snd_lastsec);
			g->lastsec_next = floorf(rem);
		}
		if (rem <= 0.0f) {
			g->mode = GM_TIMEOUT;
			g->mode_t = 0.0f;
			r_play_sound(g_scene.snd_timeout);
			return;
		}
	}
	if (!sim.p.moving && !sim.p.turning && !sim.p.falling
	    && lvl.tiles[sim.p.cx][sim.p.cy].type == TT_EXIT
	    && (int)sim.p.crystals >= (int)lvl.crystals_needed
	    && g->cur < g->man.num_levels - 1) {
		/* itemized tally per the original's formula; the run total advances here */
		score_compute(&g->summary, &sim, &lvl,
		              (float)lvl.time_limit - g->level_time, g->score);
		g->score = g->summary.total_score;
		g->mode = GM_COMPLETE;
		g->mode_t = 0.0f;
		g->comp_sel = 0;
		g->menu = (struct menu){0};
		r_play_sound(g_scene.snd_levelcomplete);   /* clapping on entering the open exit */
	}
}

/* TANK controls: W/S forward/back, A/D turn, SPACE drop bomb (Inverse debuff flips them). */
static void tank_input(void)
{
	bool inv = sim.inverse_t > 0.0f;
	if      (r_key_down(R_KEY_W)) (inv ? sim_back : sim_forward)(&sim);
	else if (r_key_down(R_KEY_S)) (inv ? sim_forward : sim_back)(&sim);
	if      (r_key_pressed(R_KEY_A)) sim_turn(&sim, inv ? +1 : -1);
	else if (r_key_pressed(R_KEY_D)) sim_turn(&sim, inv ? -1 : +1);
	if (r_key_pressed(R_KEY_SPACE)) sim_drop_bomb(&sim);
}

/* modifier / debug keys that work across the live modes (F1 freecam + F2 shot are
 * handled with the camera; F5-F7 with the animation). */
static void handle_keys(struct game *g)
{
	if (r_key_pressed(R_KEY_F3))  g->paused = !g->paused;
	if (r_key_pressed(R_KEY_F10)) g->show_debug = !g->show_debug;
	if (r_key_pressed(R_KEY_F11)) sim.p.crystals = (int)lvl.crystals_needed;   /* fill crystals */
	if (g->mode == GM_PLAYING) {   /* F8/F9 instant level jump (no loading / intro) */
		int nlv = g->cur;
		if (r_key_pressed(R_KEY_F8) && g->cur > 0)                       nlv = g->cur - 1;
		if (r_key_pressed(R_KEY_F9) && g->cur < g->man.num_levels - 1)   nlv = g->cur + 1;
		if (nlv != g->cur)
			game_jump_level(g, nlv);
	}
	if (g->fc.active && g->pick.kind == PICK_TILE && r_key_pressed(R_KEY_G))   /* warp John here */
		sim_warp(&sim, g->pick.x, g->pick.y);
	/* debug save/load (slot 0) until the menu wires the real slot screens:
	 * F12 = save, SHIFT+F12 = load. */
	if (r_key_pressed(R_KEY_F12)) {
		if (r_key_down(R_KEY_SHIFT))
			printf("[sav] load slot 0 -> %s\n", game_load_slot(g, 0) ? "ok" : "FAILED");
		else
			printf("[sav] save slot 0 -> %s\n", game_save_slot(g, 0, NULL, g->cur) ? "ok" : "FAILED");
	}
}

/* choose John's clip (F5 inspector overrides) and advance the frame. */
static void select_anim(struct game *g, float gdt)
{
	struct player_anim *a = &g->anim;
	if (r_key_pressed(R_KEY_F5))
		a->inspect = !a->inspect;
	if (a->inspect) {
		if (r_key_pressed(R_KEY_F6)) { a->clip = (a->clip + ANI_NUM_CLIPS - 1) % ANI_NUM_CLIPS; a->frame = 0; }
		if (r_key_pressed(R_KEY_F7)) { a->clip = (a->clip + 1) % ANI_NUM_CLIPS; a->frame = 0; }
	} else {
		int want;
		if (g->mode == GM_DEATH)      want = ANI_IDLE2;   /* static pose while ascending */
		else if (sim.p.glue_t > 0.0f) want = g_scene.john_anim.clips[ANI_GLUE].present ? ANI_GLUE : ANI_IDLE2;
		else if (sim.p.gliding)       want = g_scene.john_anim.clips[ANI_PARAGLIDE].present ? ANI_PARAGLIDE : ANI_FALL;
		else if (sim.p.launching)     want = g_scene.john_anim.clips[ANI_JUMP].present ? ANI_JUMP : ANI_FALL;   /* airborne off a jump-pad */
		else if (sim.p.falling)       want = ANI_FALL;
		else if (sim.p.sliding) {     /* pick by slide KIND: ice skate vs rutsche chute pose */
			int pref = sim.p.slide_ice ? ANI_ICE : ANI_SLIDE;
			int alt  = sim.p.slide_ice ? ANI_SLIDE : ANI_ICE;
			want = g_scene.john_anim.clips[pref].present ? pref
			     : g_scene.john_anim.clips[alt].present ? alt : ANI_WALK_FWD;
		}
		else if (sim.p.turning)       want = (sim.p.turn_dir > 0) ? ANI_TURN_RIGHT : ANI_TURN_LEFT;
		else if (sim.p.moving) {
			int mc = move_clip_for(&sim.p, &g_scene.john_anim);   /* stair clip on staircases */
			want = (mc == ANI_WALK_FWD && sim.speed_t > 0.0f && g_scene.john_anim.clips[ANI_SPEED_FWD].present)
			       ? ANI_SPEED_FWD : mc;
		} else {
			a->idle_t += gdt;
			want = (a->idle_t >= 5.0f) ? ANI_IDLE1 : ANI_IDLE2;
		}
		if (sim.p.moving || sim.p.turning || sim.p.falling || sim.p.sliding || sim.p.glue_t > 0.0f
		    || sim.p.launching)
			a->idle_t = 0.0f;
		if (want != a->clip) { a->clip = want; a->frame = 0.0f; }
	}

	ani_clip *c = &g_scene.john_anim.clips[a->clip];
	float clip_span = (c->present && c->count > 0) ? (float)c->count : 1.0f;
	bool synced = !a->inspect && c->present && c->count > 0 && (sim.p.moving || sim.p.turning);
	if (synced) {                        /* walk/turn play in lock-step with the hop progress */
		float prog = sim.p.turning ? sim.p.turn_t : sim.p.move_t;
		a->frame = prog * clip_span;
		if (a->frame > clip_span - 0.001f) a->frame = clip_span - 0.001f;
	} else {
		float fps = (c->fps > 0) ? c->fps : 10.0f;
		if (!a->inspect && a->clip == ANI_IDLE1)
			fps *= 2.0f;                 /* the idle dance plays faster */
		float adv = (a->inspect && g->paused)
		            ? (r_key_pressed(R_KEY_F4) ? 1.0f : 0.0f)
		            : fps * gdt;
		a->frame += adv;
		a->frame = fmodf(a->frame, clip_span);
		if (a->frame < 0.0f) a->frame += clip_span;
	}
}

/* build this frame's camera: follow/boom by default, freecam (F1) or the scripted
 * .jjs camera or the frozen death-cam depending on mode. */
static r_camera build_view(struct game *g, float dt)
{
	r_vec3 jw = player_world_pos(g);
	r_camera follow = follow_cam(&lvl, jw, sim.p.fwd_x, sim.p.fwd_y,
	                             &g->boom, dt, g->fc.active, g->mode == GM_DEATH);
	if (r_key_pressed(R_KEY_F1)) {
		g->fc.active = !g->fc.active;
		if (g->fc.active)
			freecam_sync(&g->fc, follow);
	}
	if (r_key_pressed(R_KEY_F2)) {
		char name[64];
		snprintf(name, sizeof name, "shot_%03d.png", g->shot_num++);
		r_screenshot(name);
		printf("screenshot -> %s\n", name);
	}
	if (g->fc.active)
		freecam_update(&g->fc, dt);

	r_camera view = g->fc.active ? freecam_view(&g->fc, follow.fovy) : follow;
	if (mode_scripted(g) && !g->fc.active && (g->mode == GM_MENU ? g->menu_vm_ok : true)) {
		const jjs_vm *vm = (g->mode == GM_MENU) ? &g->menu_vm : &g->intro_vm;
		view.pos    = (r_vec3){ vm->cam_pos[0], vm->cam_pos[1], vm->cam_pos[2] };
		view.target = (r_vec3){ vm->cam_tgt[0], vm->cam_tgt[1], vm->cam_tgt[2] };
		view.up     = (r_vec3){ 0, 0, 1 };
		view.fovy   = 50.0f;
	}
	if (g->mode == GM_DEATH && !g->fc.active)
		view = death_cam(g->death_x, g->death_y, g->death_z, g->death_a0);
	return view;
}

/* play this frame's sim events: sound + FX bursts + the obstacle-shatter they trigger. */
static void dispatch_events(struct game *g, r_camera view)
{
	for (int i = 0; i < sim.num_events; i++) {
		const sim_event *e = &sim.events[i];
		if (getenv("SNDDBG"))   /* event-stream tap: repro a sound bug, read the console */
			printf("[ev] t=%7.2f type=%2d at %.1f,%.1f,%.1f\n",
			       g->t_accum, e->type, e->x, e->y, e->z);
		audio_play_event(e, view, g->t_accum);
		if (e->type == SIM_EV_OBSTACLE) {   /* rock blown open: start its shatter */
			int ox = (int)e->x, oy = (int)e->y;
			if (ox >= 0 && oy >= 0 && ox < lvl.dim_x && oy < lvl.dim_y)
				g_scene.obstacle_shatter[ox][oy] = 0.6f;
		}
		fx_emit_event(e);
	}
	sim.num_events = 0;
}

/* F10 debug overlay: level/cell readout, controls hint, anim inspector + tile pick. */
static void draw_debug(const struct game *g, int sh)
{
	char dl[256];
	snprintf(dl, sizeof dl, "L%d %s  cell(%d,%d,z%d) rz=%.2f%s%s",
	         g->cur + 1, g->man.levels[g->cur], sim.p.cx, sim.p.cy,
	         lvl.tiles[sim.p.cx][sim.p.cy].z_pos, sim.p.rz,
	         sim.p.falling ? "  FALLING" : "", g->paused ? "  [PAUSED]" : "");
	r_draw_text(dl, 12, sh - 44, 18, (r_color){ 235, 235, 240, 255 });
	const sim_stats *st = &sim.stats;
	snprintf(dl, sizeof dl,
	         "STATS jump%u cry%u tp%u obs%u brg%u glu%u sld%u ice%u | pk par%u tim%u hp%u frz%u bmb%u spd%u inv%u slw%u prt%u | score %d",
	         st->jumps, st->crystals, st->teleports, st->obstacles, st->bridges,
	         st->glue, st->slides, st->ices,
	         st->collected[PU_PARAGLIDE], st->collected[PU_TIME], st->collected[PU_HEART],
	         st->collected[PU_FREEZE], st->collected[PU_BOMBS], st->collected[PU_SPEED],
	         st->collected[PU_INVERSE], st->collected[PU_SLOWDOWN], st->collected[PU_PROTECT],
	         g->score);
	r_draw_text(dl, 12, sh - 88, 15, (r_color){ 160, 200, 170, 255 });
	r_draw_text(g->fc.active
	            ? "FREECAM WASD/QE·RMB look·wheel·F1 exit·F2 shot·F3 pause·F4 step·F5 anim·F8/F9 lvl·F10 dbg·F11 cryst"
	            : "WASD move·F1 freecam·F2 shot·F3 pause·F5 anim·F8/F9 lvl·F10 dbg·F11 cryst·ESC menu",
	            12, sh - 22, 15, (r_color){ 160, 160, 170, 255 });
	if (g->anim.inspect) {
		ani_clip *c = &g_scene.john_anim.clips[g->anim.clip];
		char aline[256];
		snprintf(aline, sizeof aline,
		         "ANIM [%d] %s  frame %d/%d  mdl=%d  fps=%.0f%s  (F6/F7 clip, F4 step, F5 exit)",
		         g->anim.clip, ani_clip_name(g->anim.clip),
		         (c->present ? (int)g->anim.frame + 1 : 0), (c->present ? c->count : 0),
		         (c->present && c->count > 0) ? c->first + (int)g->anim.frame : 0,
		         c->fps, c->present ? "" : " (undefined)");
		r_draw_text(aline, 12, sh - 66, 15, (r_color){ 250, 220, 120, 255 });
	}
	if (g->pick.kind == PICK_TILE) {
		const jjm_tile *t = &lvl.tiles[g->pick.x][g->pick.y];
		char sel[160];
		snprintf(sel, sizeof sel,
		         "SEL tile(%d,%d) z=%d  type=%d(0x%02x) %s  clip=%d pickup=%d%s",
		         g->pick.x, g->pick.y, t->z_pos, t->type, t->type, tile_mnemonic(t->type),
		         t->clip_rule, t->pickup_type, g->fc.active ? "   [G = warp John here]" : "");
		r_draw_text(sel, 12, sh - 66, 15, (r_color){ 250, 240, 120, 255 });
	}
}

/* draw the 3D scene, then the mode-appropriate 2D overlay. */
static void render_scene_and_hud(struct game *g, r_camera view)
{
	r_begin_frame((r_color){ 28, 30, 38, 255 });
	struct render_frame rf = {
		.view = view, .t = g->t_accum, .jw = player_world_pos(g),
		.anim_clip = g->anim.clip, .anim_frame = g->anim.frame,
		.dying = g->mode == GM_DEATH, .death_t = g->mode_t,
	};
	render_scene_3d(g->base, &rf, &g->pick);

	int sw, sh;
	r_screen_size(&sw, &sh);
	float ps = sh / 720.0f;
	if (g->mode == GM_INTRO) {
		hud_draw_intro(sw, sh, ps, &g->intro_vm, g->intro_vm.done);
	} else if (g->mode == GM_MENU) {
		hud_draw_menu(sw, sh, ps, &g->menu, g->t_accum);
	} else {
		hud_draw(sw, sh, ps, g->cur, g->hp, g->level_time, g->t_accum);
		if (g->show_debug)
			draw_debug(g, sh);
		hud_draw_status(sw, sh, g->mode == GM_DEATH, g->mode_t,
		                g->mode == GM_COMPLETE, g->mode_t, g->mode == GM_TIMEOUT, g->hp,
		                &g->summary, g->comp_sel, g->t_accum);
		if (g->mode == GM_PAUSED
		    || (g->mode == GM_COMPLETE && g->menu.scr != MS_NONE)) {
			r_draw_rect(0, 0, (float)sw, (float)sh, (r_color){ 0, 0, 0, 110 });
			hud_draw_menu(sw, sh, ps, &g->menu, g->t_accum);
		}
	}
	r_end_frame();
}

int main(int argc, char **argv)
{
	const char *base = (argc > 1) ? argv[1] : "../game_root/SkippyAdventure";
	int cur = (argc > 2) ? atoi(argv[2]) : 0;
	srand((unsigned)time(NULL));   /* surprise-box rolls (sim rand()) */
	char p[1024];
	gam_manifest man;

	if (!asset_resolve(base, "JJ.GAM", p, sizeof p) || !gam_load(p, &man)) {
		fprintf(stderr, "gam load FAILED\n");
		return 1;
	}
	if (cur < 0 || cur >= man.num_levels)
		cur = 0;
	if (!r_init(WIN_W, WIN_H, "Skippy — Ka'roo")) {
		fprintf(stderr, "r_init FAILED\n");
		return 1;
	}
	r_audio_init();
	options_load(base);   /* volume knobs from rewrite.cfg (defaults if absent) */

	struct game g;
	if (!game_init(&g, base, &man, cur, argc <= 2))
		return 1;

	const char *shot = getenv("SHOT");
	int frame = 0;

	while (!r_should_close() && !g.quit) {
		float dt = r_frame_time();
		if (dt > 0.1f)
			dt = 0.1f;
		g.mode_t += dt;

		if (g.mode == GM_SPLASH || g.mode == GM_LOADING) {
			shell_frame(&g);
			continue;
		}

		handle_keys(&g);
		float gdt = game_gdt(&g, dt, r_key_pressed(R_KEY_F4));
		if (mode_update(&g, dt)) {        /* scripted tick / prompt ENTER; may reload the scene */
			r_poll_input();           /* the skipped EndDrawing would have refreshed the
			                             key edges — without this the ENTER that triggered
			                             the reload fires AGAIN in the next mode's menu */
			continue;
		}

		advance_clock(&g, gdt);
		fx_emit_world(gdt, g.mode == GM_DEATH);
		fx_update(gdt);
		for (int x = 0; x < lvl.dim_x; x++)   /* age out obstacle shatters */
			for (int y = 0; y < lvl.dim_y; y++)
				if (g_scene.obstacle_shatter[x][y] > 0.0f)
					g_scene.obstacle_shatter[x][y] -= gdt;

		if (gameplay_input(&g))
			tank_input();
		sim_tick(&sim, gdt);
		if (getenv("SNDDBG")) {           /* paraglide drain tap: log every charge change */
			static int pg_prev = -1;
			if (pg_prev >= 0 && sim.inv[PU_PARAGLIDE] != pg_prev)
				printf("[glide] charges %d -> %d (gliding=%d falling=%d cell=%d,%d rz=%.2f)\n",
				       pg_prev, sim.inv[PU_PARAGLIDE], sim.p.gliding, sim.p.falling,
				       sim.p.cx, sim.p.cy, sim.p.rz);
			pg_prev = sim.inv[PU_PARAGLIDE];
		}
		g.hp += sim.hp_gain;              /* hearts collected this frame */
		sim.hp_gain = 0;
		if (g.hp > HP_MAX)
			g.hp = HP_MAX;
		if (g.mode == GM_PLAYING)
			check_transitions(&g);

		select_anim(&g, gdt);
		r_camera view = build_view(&g, dt);
		audio_update_ambience(view);
		audio_update_loops(view, g.mode == GM_PLAYING);
		/* the CD track holds (position kept) while a .jjs intro script plays —
		 * narration and music don't talk over each other — and resumes in play. */
		r_track_set_paused(g.mode == GM_INTRO);
		r_track_update();                 /* pump the background-music stream */
		dispatch_events(&g, view);
		render_scene_and_hud(&g, view);

		if (shot && ++frame == 45) {      /* headless screenshot (SHOT=path) */
			r_screenshot(shot);
			printf("screenshot -> %s\n", shot);
		}
	}

	r_shutdown();
	return 0;
}
