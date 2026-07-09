/* In-game menu system (see menu.h): the theme-panel screens driven by
 * UP/DOWN/LEFT/RIGHT/ENTER/ESC, modeled on the original menu_manager
 * (reference/menu_manager.md). Pure-menu screen moves happen here; anything
 * that changes GAME state is returned as an action for the mode layer. */
#include "game/menu.h"
#include "game/options.h"
#include "game/scene.h"        /* snd_menu (MenuUpDown.wav) */
#include "formats/sav.h"
#include "formats/hsc.h"
#include "formats/asset.h"
#include "render/renderer.h"

#include <stdio.h>
#include <string.h>

/* EN-only labels (the multilang pass will table these per language later). */
static const char *const MAIN_ITEMS[MENU_MAIN_N] = {
	"New Game", "Load", "Records", "Options", "Credits", "Quit",
};
static const char *const PAUSE_ITEMS[MENU_PAUSE_N] = {
	"Resume", "Save", "Load", "Options", "To Menu", "Quit",
};
static const char *const OPT_ITEMS[MENU_OPT_N] = {
	"Volume", "Music", "Effects", "Music on/off", "Back",
};

const char *menu_main_label(int i)  { return i >= 0 && i < MENU_MAIN_N ? MAIN_ITEMS[i] : ""; }
const char *menu_pause_label(int i) { return i >= 0 && i < MENU_PAUSE_N ? PAUSE_ITEMS[i] : ""; }
const char *menu_opt_label(int i)   { return i >= 0 && i < MENU_OPT_N ? OPT_ITEMS[i] : ""; }

static int screen_items(const struct menu *m)
{
	switch (m->scr) {
	case MS_MAIN:    return MENU_MAIN_N;
	case MS_PAUSE:   return MENU_PAUSE_N;
	case MS_OPTIONS: return MENU_OPT_N;
	case MS_LOAD:
	case MS_SAVE:    return SAV_SLOTS + 1;   /* 6 slots + back */
	default:         return 0;               /* records/credits/name: any-confirm screens */
	}
}

void menu_refresh_slots(struct menu *m, const char *base)
{
	for (int i = 0; i < SAV_SLOTS; i++) {
		char p[1024];
		snprintf(p, sizeof p, "%s/SavedGames/jj%d.sav", base, i);
		m->slot_ok[i] = sav_read(p, &m->slots[i]);
	}
}

void menu_refresh_records(struct menu *m, const char *base)
{
	char p[1024];
	if (!(asset_resolve(base, "Highscores\\jj.hsc", p, sizeof p) && hsc_read(p, &m->records)))
		hsc_defaults(&m->records);
}

void menu_open(struct menu *m, enum menu_screen scr, const char *base)
{
	if (scr == MS_LOAD || scr == MS_SAVE)
		menu_refresh_slots(m, base);
	if (scr == MS_RECORDS)
		menu_refresh_records(m, base);
	if (m->scr != scr && m->depth < (int)(sizeof m->back / sizeof m->back[0]))
		m->back[m->depth++] = m->scr;
	m->scr = scr;
	m->sel = 0;
}

void menu_pop(struct menu *m)
{
	m->scr = m->depth > 0 ? m->back[--m->depth] : MS_NONE;
	m->sel = 0;
}

/* LEFT/RIGHT on an options row (the original's option-adjuster semantics). */
static void opt_adjust(int row, int dir)
{
	int *v = row == 0 ? &g_opts.master : row == 1 ? &g_opts.music
	       : row == 2 ? &g_opts.sfx : NULL;
	if (v) {
		*v += dir * OPT_VOL_STEP;
		if (*v < 0) *v = 0;
		if (*v > OPT_VOL_MAX) *v = OPT_VOL_MAX;
	} else if (row == 3) {
		g_opts.music_on = !g_opts.music_on;
	}
	options_apply();
}

enum menu_action menu_update(struct menu *m, const char *base)
{
	int n = screen_items(m);
	if (n > 0) {
		int was = m->sel;
		if (r_key_pressed(R_KEY_UP))   m->sel = (m->sel + n - 1) % n;
		if (r_key_pressed(R_KEY_DOWN)) m->sel = (m->sel + 1) % n;
		if (m->sel != was)
			r_play_sound(g_scene.snd_menu);        /* MenuUpDown tick */
	}

	if (m->scr == MS_NAME_ENTRY) {         /* highscore or save-slot name: type + ENTER */
		int cap = m->name_max > 0 && m->name_max <= MENU_NAME_MAX ? m->name_max
		                                                          : MENU_NAME_MAX;
		int c;
		while ((c = r_char_pressed()) != 0)
			if (c >= 32 && c < 127 && m->edit_len < cap) {
				m->edit[m->edit_len++] = (char)c;
				m->edit[m->edit_len] = 0;
			}
		if (r_key_pressed(R_KEY_BACKSPACE) && m->edit_len > 0)
			m->edit[--m->edit_len] = 0;
		if (r_key_pressed(R_KEY_ENTER))
			return MA_NAME_DONE;           /* empty name allowed: mode layer defaults it */
		if (m->name_for == NE_SAVE && r_key_pressed(R_KEY_ESC))
			menu_pop(m);                  /* saving is optional; a record entry is not */
		return MA_NONE;
	}

	if (m->scr == MS_OPTIONS) {
		bool adj = false;
		if (r_key_pressed(R_KEY_LEFT))  { opt_adjust(m->sel, -1); adj = true; }
		if (r_key_pressed(R_KEY_RIGHT)) { opt_adjust(m->sel, +1); adj = true; }
		if (adj)
			r_play_sound(g_scene.snd_menu);
	}

	if (r_key_pressed(R_KEY_ESC)) {
		if (m->scr == MS_MAIN)
			return MA_NONE;                /* the title screen has nothing behind it */
		if (m->scr == MS_PAUSE)
			return MA_RESUME;
		if (m->scr == MS_OPTIONS)
			options_save(base);
		menu_pop(m);
		return MA_NONE;
	}

	if (!r_key_pressed(R_KEY_ENTER))
		return MA_NONE;

	switch (m->scr) {
	case MS_MAIN:
		switch (m->sel) {
		case 0: return MA_NEW_GAME;
		case 1: menu_open(m, MS_LOAD, base); return MA_NONE;
		case 2: menu_open(m, MS_RECORDS, base); return MA_NONE;
		case 3: menu_open(m, MS_OPTIONS, base); return MA_NONE;
		case 4: menu_open(m, MS_CREDITS, base); return MA_NONE;
		case 5: return MA_EXIT;
		}
		return MA_NONE;
	case MS_PAUSE:
		switch (m->sel) {
		case 0: return MA_RESUME;
		case 1: menu_open(m, MS_SAVE, base); return MA_NONE;
		case 2: menu_open(m, MS_LOAD, base); return MA_NONE;
		case 3: menu_open(m, MS_OPTIONS, base); return MA_NONE;
		case 4: return MA_QUIT_TO_MENU;
		case 5: return MA_EXIT;
		}
		return MA_NONE;
	case MS_OPTIONS:
		if (m->sel == MENU_OPT_N - 1) {    /* Назад */
			options_save(base);
			menu_pop(m);
		}
		return MA_NONE;
	case MS_LOAD:
		if (m->sel < SAV_SLOTS)
			return m->slot_ok[m->sel] ? (enum menu_action)(MA_LOAD_SLOT0 + m->sel) : MA_NONE;
		menu_pop(m);
		return MA_NONE;
	case MS_SAVE:
		if (m->sel < SAV_SLOTS)
			return (enum menu_action)(MA_SAVE_SLOT0 + m->sel);
		menu_pop(m);
		return MA_NONE;
	case MS_RECORDS:
	case MS_CREDITS:
		menu_pop(m);
		return MA_NONE;
	default:
		return MA_NONE;
	}
}
