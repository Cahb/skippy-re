/* Game HUD (see game/hud.h). Split out of main.c: the in-play overlay + the
 * end-of-attempt prompts. The intro/menu overlays and the F10 debug overlay stay
 * in the game loop (they gate the frame / read dev-only loop state). */
#include "game/hud.h"
#include "game/scene.h"
#include "game/hud_text.h"
#include "game/options.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

static void draw_gloves(const char *s, float cx, float y, float gh, float ps);   /* menu helpers (below) */

void hud_draw(int sw, int sh, float ps, int cur, int hp, float level_time, float t_accum)
{
	r_color white = { 245, 245, 250, 255 };
	float lp = 256.0f * ps;              /* corner panel drawn size */

	/* corner leaf panels (icons + Уровень:/Жизненная сила labels baked in).
	 * Left blob is already top-left in the atlas. Right blob lives at atlas
	 * ~(63,128)-(255,236), so shift the sheet up/right to hug the corner:
	 * atlas point (ax,ay) maps to screen (rdx + ax*ps, rdy + ay*ps). */
	float rdx = sw - 5 * ps - 255 * ps;
	float rdy = 6 * ps - 128 * ps;
	if (g_scene.hud_left >= 0)
		r_draw_sprite(g_scene.hud_left, 0, 0, lp, lp, 0, 0, 256, 256, white);
	if (g_scene.hud_right >= 0)
		r_draw_sprite(g_scene.hud_right, rdx, rdy, lp, lp, 0, 0, 256, 256, white);

	/* small counters use Font2 (which has '/', digits, 'x'); only the big timer
	 * uses NUMBERS.TGA. */
	{
		char s[16];
		snprintf(s, sizeof s, "%d/%u", sim.p.crystals, lvl.crystals_needed);
		font2_text(g_scene.font2_tex, s, 56 * ps, 8 * ps, 26 * ps, white, 0);       /* crystals x/y */
		snprintf(s, sizeof s, "x%d", sim.inv[PU_BOMBS]);
		font2_text(g_scene.font2_tex, s, 68 * ps, 82 * ps, 24 * ps, white, 0);      /* bombs */
		snprintf(s, sizeof s, "x%d", sim.inv[PU_PARAGLIDE]);
		font2_text(g_scene.font2_tex, s, 148 * ps, 40 * ps, 24 * ps, white, 0);     /* paraglide */
	}

	/* central countdown timer MM:SS.t (reddens + pulses under 10s) */
	{
		float rem = (float)lvl.time_limit - level_time;
		if (rem < 0.0f)
			rem = 0.0f;
		char t[40];
		snprintf(t, sizeof t, "%02d:%02d.%d",
		         (int)rem / 60, (int)rem % 60, (int)(rem * 10) % 10);
		r_color tc = white;
		if (rem <= 10.0f) {
			tc = (r_color){ 235, 70, 60, 255 };
			if (fmodf(t_accum, 0.6f) < 0.3f)
				tc.a = 130;
		}
		hud_num(g_scene.hud_numbers, t, sw * 0.5f, 6 * ps, 46 * ps, tc, 2);
	}

	/* lives (beside heart, atlas ~(89,141)) + level number (under Уровень:,
	 * atlas ~(215,175)) — positioned in atlas space via rdx/rdy. */
	{
		char n[16];
		snprintf(n, sizeof n, "x%d", hp);
		font2_text(g_scene.font2_tex, n, rdx + 120 * ps, rdy + 133 * ps, 24 * ps, white, 0);   /* lives */
		snprintf(n, sizeof n, "%d", cur + 1);
		font2_text(g_scene.font2_tex, n, rdx + 214 * ps, rdy + 168 * ps, 24 * ps, white, 0);   /* level */
	}

	/* active bonus/debuff timers: the theme's ring icons + remaining seconds, a
	 * column down the right edge between the corner panel and the radar. Only
	 * running timers draw; the icon blinks (~4Hz) through its last 2 seconds. */
	{
		const float *bt[THM_BONUS_N] = {
			[THM_BONUS_FREEZE]  = &sim.enemy_freeze_t,
			[THM_BONUS_INVERSE] = &sim.inverse_t,
			[THM_BONUS_PROTECT] = &sim.protect_t,
			[THM_BONUS_SLOW]    = &sim.slow_t,
			[THM_BONUS_SPEED]   = &sim.speed_t,
		};
		float bs = 36.0f * ps;                       /* icon size */
		float bx = sw - bs - 8.0f * ps;              /* right-edge anchor */
		float by = 124.0f * ps;                      /* below the corner panel */
		for (int i = 0; i < THM_BONUS_N; i++) {
			float tleft = *bt[i];
			if (tleft <= 0.0f)
				continue;
			bool hide = tleft < 2.0f && fmodf(t_accum, 0.25f) < 0.125f;
			if (!hide && g_scene.bonus_tex[i] >= 0)
				r_draw_sprite(g_scene.bonus_tex[i], bx, by, bs, bs,
				              0, 0, g_scene.bonus_w[i], g_scene.bonus_h[i], white);
			char b[8];
			snprintf(b, sizeof b, "%d", (int)ceilf(tleft));
			font2_text(g_scene.font2_tex, b, bx - 6.0f * ps, by + bs * 0.28f,
			           22 * ps, white, 1);           /* seconds, right-aligned at the icon */
			by += bs + 6.0f * ps;
		}
	}

	/* radar (bottom-right): crystals green, exit cyan, enemies red, John white */
	if (g_scene.hud_radar >= 0) {
		float rs = 148 * ps, rx = sw - rs - 6 * ps, ry = sh - rs - 6 * ps;
		float ccx = rx + rs * 0.5f, ccy = ry + rs * 0.5f, rad = rs * 0.36f;
		float range = 14.0f;
		r_draw_sprite(g_scene.hud_radar, rx, ry, rs, rs, 0, 0, 128, 128, white);
		for (int x = 0; x < lvl.dim_x; x++)
			for (int y = 0; y < lvl.dim_y; y++) {
				const jjm_tile *t = &lvl.tiles[x][y];
				r_color bc;
				if (t->pickup_type == PU_CRYSTAL && !sim.picked[x][y])
					bc = (r_color){ 90, 230, 110, 255 };
				else if (t->type == TT_EXIT)
					bc = (r_color){ 90, 210, 235, 255 };
				else
					continue;
				float dx = (x + 0.5f) - sim.p.rx, dy = (y + 0.5f) - sim.p.ry;
				if (fabsf(dx) > range || fabsf(dy) > range)
					continue;
				r_draw_circle(ccx + dx / range * rad, ccy + dy / range * rad, 2.4f * ps, bc);
			}
		for (int i = 0; i < sim.num_enemies; i++) {
			float dx = sim.enemies[i].rx - sim.p.rx, dy = sim.enemies[i].ry - sim.p.ry;
			if (fabsf(dx) > range || fabsf(dy) > range)
				continue;
			r_draw_circle(ccx + dx / range * rad, ccy + dy / range * rad, 3.0f * ps,
			              (r_color){ 235, 70, 60, 255 });
		}
		r_draw_circle(ccx, ccy, 3.2f * ps, white);
	}
}

void hud_draw_status(int sw, int sh, bool dying, float death_t,
                     bool level_done, float done_t, bool timed_out, int hp,
                     const score_summary *sum, int comp_sel, float t)
{
	/* death overlay: dim the frozen scene, then the angel prompt */
	if (dying && death_t > 0.6f) {
		r_draw_rect(0, 0, (float)sw, (float)sh, (r_color){ 0, 0, 0, 120 });
		const char *msg = (hp > 1) ? "Press ENTER to continue"
		                           : "Press ENTER to restart from level 1";
		int p1 = 40, p2 = 22;
		int w1 = (int)(strlen("YOU DIED") * p1 * 0.5f);
		int w2 = (int)(strlen(msg) * p2 * 0.5f);
		r_draw_text("YOU DIED", sw / 2 - w1 / 2, sh / 2 - 60, p1, (r_color){ 245, 90, 80, 255 });
		r_draw_text(msg, sw / 2 - w2 / 2, sh / 2 + 4, p2, (r_color){ 235, 235, 240, 255 });
	}
	/* level-complete overlay: the itemized score tally (per the original's
	 * summary screen: label, count x mult = points, level + running totals). */
	if (level_done && done_t > 0.4f) {
		float ps = sh / 720.0f;
		r_color grn = { 120, 235, 190, 255 };
		r_draw_rect(0, 0, (float)sw, (float)sh, (r_color){ 0, 0, 0, 120 });
		const char *ttl = lvl.is_bonus ? "BONUS CLEARED" : "LEVEL COMPLETED";
		int p1 = 40;
		int w1 = (int)(strlen(ttl) * p1 * 0.5f);
		r_draw_text(ttl, sw / 2 - w1 / 2, (int)(sh * 0.10f), p1, (r_color){ 235, 150, 60, 255 });
		if (sum) {
			float gh = 26 * ps, lh = 38 * ps, ty = sh * 0.26f;
			char b[32];
			for (int i = 0; i < SC_ROWS; i++, ty += lh) {
				font2_text(g_scene.font2_tex, SCORE_LABEL[i], sw * 0.14f, ty, gh, grn, 0);
				snprintf(b, sizeof b, "%d x %d =", sum->count[i], SCORE_MULT[i]);
				font2_text(g_scene.font2_tex, b, sw * 0.68f, ty, gh, grn, 1);
				snprintf(b, sizeof b, "%d", sum->points[i]);
				font2_text(g_scene.font2_tex, b, sw * 0.86f, ty, gh, grn, 1);
			}
			font2_text(g_scene.font2_tex, "level score:", sw * 0.14f, ty, gh, grn, 0);
			snprintf(b, sizeof b, "%d", sum->level_score);
			font2_text(g_scene.font2_tex, b, sw * 0.86f, ty, gh, grn, 1);
			ty += lh * 1.3f;
			font2_text(g_scene.font2_tex, "total score:", sw * 0.14f, ty, gh, grn, 0);
			snprintf(b, sizeof b, "%d", sum->total_score);
			font2_text(g_scene.font2_tex, b, sw * 0.86f, ty, gh, grn, 1);
		}
		/* Next / Save selector (the original's gloved pair under the tally) */
		{
			static const char *const OPT[2] = {
				"Next",
				"Save",
			};
			float gh = 28 * ps;
			for (int i = 0; i < 2; i++) {
				float y = sh * (0.80f + 0.065f * i);
				r_color c = i == comp_sel ? (r_color){ 255, 210, 90, 255 }
				                          : (r_color){ 235, 235, 240, 255 };
				font2_text_wave(g_scene.font2_tex, OPT[i], sw * 0.5f, y, gh, c, 2,
				                t, y * 0.05f);
				if (i == comp_sel)
					draw_gloves(OPT[i], sw * 0.5f, y, gh, ps);
			}
		}
	}
	/* timeout overlay: no angel — just the prompt to restart the level */
	if (timed_out && death_t > 0.6f) {
		r_draw_rect(0, 0, (float)sw, (float)sh, (r_color){ 0, 0, 0, 120 });
		const char *msg = (hp > 1) ? "Press ENTER to restart the level"
		                           : "Press ENTER to restart from level 1";
		int p1 = 40, p2 = 22;
		int w1 = (int)(strlen("TIME UP") * p1 * 0.5f);
		int w2 = (int)(strlen(msg) * p2 * 0.5f);
		r_draw_text("TIME UP", sw / 2 - w1 / 2, sh / 2 - 60, p1, (r_color){ 245, 90, 80, 255 });
		r_draw_text(msg, sw / 2 - w2 / 2, sh / 2 + 4, p2, (r_color){ 235, 235, 240, 255 });
	}
}

/* pre-gameplay shell: splash bitmap + "press any key", loading bitmap, or the
 * fallback text menu (used only if the live 3D menu backdrop failed to load). */
void hud_draw_shell(enum game_screen screen, r_tex splash_tex, r_tex load_tex)
{
	int sw, sh;
	r_screen_size(&sw, &sh);
	r_color wht = { 245, 245, 250, 255 };
	if (screen == SCR_SPLASH && splash_tex >= 0)
		r_draw_fullscreen(splash_tex, wht);
	else if (screen == SCR_LOADING && load_tex >= 0)
		r_draw_fullscreen(load_tex, wht);
	if (screen == SCR_SPLASH) {
		const char *s = "press any key";
		r_draw_text(s, sw / 2 - (int)(strlen(s) * 18 * 0.25f), sh - 46, 18,
		            (r_color){ 230, 230, 235, 220 });
	} else if (screen == SCR_MENU) {
		const char *t1 = "SKIPPY", *t2 = "Press ENTER to start";
		r_draw_text(t1, sw / 2 - (int)(strlen(t1) * 52 * 0.25f), sh / 2 - 72, 52, wht);
		r_draw_text(t2, sw / 2 - (int)(strlen(t2) * 24 * 0.25f), sh / 2 + 12, 24,
		            (r_color){ 210, 210, 220, 255 });
	}
}

/* menu overlay: the theme's rounded panel + the Russian menu items, over the live
 * DemoLevelForest backdrop. */
/* 0xRRGGBB theme color -> r_color, with a fallback when the theme left it unset. */
static r_color mc(unsigned rgb, r_color fb)
{
	if (rgb == 0)
		return fb;
	return (r_color){ (uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb, 255 };
}

/* the SELECTOR.TGA boxing gloves flanking a row `half` wide around cx, almost
 * touching it (left punches right; the right one mirrors via negative src width). */
static void draw_gloves_span(float cx, float half, float y, float gh, float ps)
{
	if (g_scene.menu_selector < 0)
		return;
	float gs = gh * 1.25f;
	float gap = 8.0f * ps;
	float gy = y + gh * 0.5f - gs * 0.5f;
	float w = (float)g_scene.menu_selector_w, h = (float)g_scene.menu_selector_h;
	r_color white = { 255, 255, 255, 255 };
	r_draw_sprite(g_scene.menu_selector, cx - half - gap - gs, gy, gs, gs, 0, 0, w, h, white);
	r_draw_sprite(g_scene.menu_selector, cx + half + gap, gy, gs, gs, w, 0, -w, h, white);
}

static void draw_gloves(const char *s, float cx, float y, float gh, float ps)
{
	draw_gloves_span(cx, (float)strlen(s) * gh * 0.58f * 0.5f, y, gh, ps);
}

/* one selectable row: theme normal/highlight pair (fallback white/gold), text
 * bobbing per-glyph like the original's menus; the gloves mark the hot row. */
static void menu_row(const char *s, float x, float y, float gh, const unsigned col[2],
                     bool hot, int align, float t, float ps)
{
	r_color nc = mc(col ? col[0] : 0, (r_color){ 235, 235, 240, 255 });
	r_color hc = mc(col ? col[1] : 0, (r_color){ 255, 210, 90, 255 });
	font2_text_wave(g_scene.font2_tex, s, x, y, gh, hot ? hc : nc, align, t, y * 0.05f);
	if (hot && align == 2)
		draw_gloves(s, x, y, gh, ps);
}

void hud_draw_menu(int sw, int sh, float ps, const struct menu *m, float t)
{
	r_color white = { 245, 245, 250, 255 };
	float pw = 460.0f * ps;
	if (g_scene.menu_panel >= 0)
		r_draw_sprite(g_scene.menu_panel, sw * 0.5f - pw * 0.5f, sh * 0.5f - pw * 0.5f,
		              pw, pw, 0, 0, (float)g_scene.menu_panel_w, (float)g_scene.menu_panel_h, white);
	float cx = sw * 0.5f, gh = 30.0f * ps, step = 40.0f * ps;
	float gy = sh * 0.5f - pw * 0.31f;
	char b[96];

	switch (m->scr) {
	case MS_MAIN:
		font2_text_wave(g_scene.font2_tex, "Menu", cx, gy, gh * 1.3f,
		                (r_color){ 255, 210, 90, 255 }, 2, t, 0.0f);
		for (int i = 0; i < MENU_MAIN_N; i++)
			menu_row(menu_main_label(i), cx, gy + gh * 1.8f + i * step, gh,
			         g_scene.th_menu_color[THM_MC_NEWGAME + i], m->sel == i, 2, t, ps);
		break;
	case MS_PAUSE:
		font2_text_wave(g_scene.font2_tex, "Paused", cx, gy, gh * 1.3f,
		                (r_color){ 255, 210, 90, 255 }, 2, t, 0.0f);
		for (int i = 0; i < MENU_PAUSE_N; i++)
			menu_row(menu_pause_label(i), cx, gy + gh * 1.8f + i * step, gh, NULL,
			         m->sel == i, 2, t, ps);
		break;
	case MS_OPTIONS:
		font2_text(g_scene.font2_tex, "\xCE\xEF\xF6\xE8\xE8" /* Опции */, cx, gy, gh * 1.3f,
		           (r_color){ 255, 210, 90, 255 }, 2);
		for (int i = 0; i < MENU_OPT_N; i++) {
			float y = gy + gh * 1.8f + i * step;
			menu_row(menu_opt_label(i), cx - 12 * ps, y, gh, NULL, m->sel == i, 1, t, ps);
			if (i <= 2) {
				int v = i == 0 ? g_opts.master : i == 1 ? g_opts.music : g_opts.sfx;
				snprintf(b, sizeof b, "< %2d >", v);
				menu_row(b, cx + 16 * ps, y, gh, NULL, m->sel == i, 0, t, ps);
			} else if (i == 3) {
				menu_row(g_opts.music_on ? "on" : "off",
				         cx + 16 * ps, y, gh, NULL, m->sel == i, 0, t, ps);
			}
		}
		/* the rows aren't centred strings, so span the gloves across the column */
		draw_gloves_span(cx, 190.0f * ps, gy + gh * 1.8f + m->sel * step, gh, ps);
		break;
	case MS_LOAD:
	case MS_SAVE: {
		const unsigned *ec = g_scene.th_menu_color[m->scr == MS_LOAD ? THM_MC_LOAD_ENTRIES
		                                                             : THM_MC_SAVE_ENTRIES];
		font2_text(g_scene.font2_tex,
		           m->scr == MS_LOAD ? "Load" : "Save",
		           cx, gy, gh * 1.3f, (r_color){ 255, 210, 90, 255 }, 2);
		for (int i = 0; i < SAV_SLOTS; i++) {
			if (m->slot_ok[i])
				snprintf(b, sizeof b, "%d. %-10s  L%-3u  %u", i + 1,
				         m->slots[i].name, m->slots[i].current_lvl + 1u,
				         m->slots[i].total_score);
			else
				snprintf(b, sizeof b, "%d. - empty -", i + 1);
			menu_row(b, cx, gy + gh * 1.8f + i * (step * 0.85f), gh * 0.9f, ec,
			         m->sel == i, 2, t, ps);
		}
		menu_row("Back", cx,
		         gy + gh * 1.8f + SAV_SLOTS * (step * 0.85f), gh, NULL,
		         m->sel == SAV_SLOTS, 2, t, ps);
		break;
	}
	case MS_RECORDS: {
		const unsigned *ec = g_scene.th_menu_color[THM_MC_HS_ENTRIES];
		font2_text(g_scene.font2_tex, "Records", cx, gy,
		           gh * 1.3f, (r_color){ 255, 210, 90, 255 }, 2);
		for (int i = 0; i < HSC_RECORDS; i++) {
			snprintf(b, sizeof b, "%2d. %-16s %3u  %6u", i + 1,
			         m->records.rec[i].name[0] ? m->records.rec[i].name : "-",
			         m->records.rec[i].level, m->records.rec[i].score);
			menu_row(b, cx, gy + gh * 1.7f + i * (step * 0.75f), gh * 0.82f, ec, false, 2, t, ps);
		}
		break;
	}
	case MS_CREDITS:
		font2_text(g_scene.font2_tex, "Credits", cx, gy, gh * 1.3f,
		           (r_color){ 255, 210, 90, 255 }, 2);
		font2_text(g_scene.font2_tex, "Skippy Ka'roo", cx, gy + gh * 2.2f, gh, white, 2);
		font2_text(g_scene.font2_tex, "native rewrite", cx, gy + gh * 3.6f, gh * 0.85f, white, 2);
		break;
	case MS_NAME_ENTRY:
		font2_text(g_scene.font2_tex,
		           "New record!",
		           cx, gy + gh, gh * 1.3f, (r_color){ 255, 210, 90, 255 }, 2);
		snprintf(b, sizeof b, "%s_", m->edit);
		font2_text(g_scene.font2_tex, b, cx, gy + gh * 3.4f, gh * 1.1f, white, 2);
		break;
	default:
		break;
	}
}

/* level-intro overlay: the narration caption in a bottom banner + a skip/ready hint. */
void hud_draw_intro(int sw, int sh, float ps, const jjs_vm *vm, bool ready)
{
	if (vm->text[0]) {
		float gh = 26.0f * ps, lh = 30.0f * ps;
		int lines = text_line_count(vm->text);
		float pad = 14.0f * ps;
		float bh = lines * lh + pad * 2.0f;          /* banner grows to fit the lines */
		r_draw_rect(0, sh - bh, (float)sw, bh, (r_color){ 30, 20, 10, 170 });
		font2_text_lines(g_scene.font2_tex, vm->text, sw * 0.5f, sh - bh + pad, gh, lh,
		                 (r_color){ 245, 225, 130, 255 }, 2);
	}
	const char *hint = ready ? "Press ENTER to play" : "Press ENTER to skip";
	r_draw_text(hint, sw - (int)(strlen(hint) * 16 * 0.28f) - 14, 12, 16,
	            (r_color){ 220, 220, 225, 200 });
}
