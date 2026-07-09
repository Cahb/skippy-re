/* In-game menu system: theme-panel screens (main title / pause / options /
 * save-load slots / records / credits / highscore name entry) driven by
 * UP/DOWN/LEFT/RIGHT/ENTER/ESC. menu_update() navigates and returns an action
 * for the mode layer to execute — game-state transitions stay in mode.c. */
#ifndef GAME_MENU_H
#define GAME_MENU_H

#include "formats/sav.h"
#include "formats/hsc.h"

enum menu_screen {
	MS_NONE = 0, MS_MAIN, MS_PAUSE, MS_OPTIONS,
	MS_LOAD, MS_SAVE, MS_RECORDS, MS_CREDITS, MS_NAME_ENTRY,
};

enum menu_action {
	MA_NONE = 0,
	MA_NEW_GAME,
	MA_RESUME,
	MA_QUIT_TO_MENU,
	MA_EXIT,
	MA_NAME_DONE,                   /* name entry committed (menu.edit) */
	MA_LOAD_SLOT0,                  /* .. MA_LOAD_SLOT0 + SAV_SLOTS - 1 */
	MA_SAVE_SLOT0 = MA_LOAD_SLOT0 + SAV_SLOTS,
};

#define MENU_MAIN_N  6
#define MENU_PAUSE_N 6
#define MENU_OPT_N   5
#define MENU_NAME_MAX 20

/* what a committed MS_NAME_ENTRY names: a highscore row or a save slot. */
enum menu_name_for { NE_HIGHSCORE = 0, NE_SAVE };

struct menu {
	enum menu_screen scr;
	int  sel;
	enum menu_screen back[4];       /* back-target stack */
	int  depth;
	char edit[MENU_NAME_MAX + 1];   /* name-entry buffer */
	int  edit_len;
	int  name_max;                  /* typing cap (savename[10] vs highscore) */
	enum menu_name_for name_for;
	int  save_slot;                 /* the slot being named (NE_SAVE) */
	sav_slot  slots[SAV_SLOTS];     /* cached for the load/save screens */
	bool      slot_ok[SAV_SLOTS];
	hsc_table records;              /* cached for the records screen */
};

/* per-screen cp1251 labels (drawn by hud.c). */
const char *menu_main_label(int i);
const char *menu_pause_label(int i);
const char *menu_opt_label(int i);

void menu_open(struct menu *m, enum menu_screen scr, const char *base);
void menu_pop(struct menu *m);          /* back one screen (MS_NONE past the root) */
void menu_refresh_slots(struct menu *m, const char *base);
void menu_refresh_records(struct menu *m, const char *base);
enum menu_action menu_update(struct menu *m, const char *base);

#endif /* GAME_MENU_H */
