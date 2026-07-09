/* Game session + top-level mode state machine.
 *
 * The loop is in exactly one exclusive `game_mode` per frame (splash / loading /
 * menu / intro / playing / death / timeout / complete); freecam, pause, the anim
 * inspector and the debug overlay are orthogonal modifiers layered on top. All the
 * loop's cross-frame state lives in one `struct game`; the mode decides how time
 * flows (see game_gdt), who owns input (gameplay_input) and where the camera comes
 * from. Transitions are the handful of helpers below — not scattered flag pokes. */
#ifndef GAME_MODE_H
#define GAME_MODE_H

#include <stdbool.h>
#include "render/renderer.h"
#include "formats/gam.h"
#include "formats/jjs.h"
#include "game/camera.h"        /* freecam, struct cam_smooth */
#include "game/render_scene.h"  /* struct picker */
#include "game/score.h"         /* score_summary */
#include "game/menu.h"          /* struct menu */

#define HP_START 3      /* starting HP/lives */
#define HP_MAX   9      /* cap for heart pickups */

enum game_mode {
	GM_SPLASH,    /* splash bitmap, then -> MENU or LOADING */
	GM_LOADING,   /* themed loading bitmap, brief, then -> INTRO or PLAYING */
	GM_MENU,      /* live DemoLevelForest backdrop + scripted cam + menu UI (loops) */
	GM_INTRO,     /* per-level how-to cinematic; ENTER skips/confirms -> PLAYING */
	GM_PLAYING,   /* interactive tank-control gameplay */
	GM_DEATH,     /* spirit rises, world frozen, ENTER respawns / game-overs */
	GM_TIMEOUT,   /* clock hit 0, world frozen, ENTER restarts the level */
	GM_COMPLETE,  /* reached the exit, world frozen, ENTER proceeds */
	GM_PAUSED,    /* ESC menu over the frozen scene (pause/options/slots/records) */
};

/* John's on-screen animation state (grouped so a level entry resets it in one call). */
struct player_anim {
	int   clip;      /* current .ani clip */
	float frame;     /* current (fractional) frame */
	float idle_t;    /* seconds idle (drives the idle-dance switch) */
	bool  inspect;   /* F5 anim inspector active (modifier; persists across levels) */
};

/* everything the loop carries across frames. */
struct game {
	enum game_mode mode;
	float mode_t;               /* seconds in the current mode (splash/death/complete timer) */

	const char  *base;          /* asset root */
	gam_manifest man;           /* level manifest */
	int   cur;                  /* current level index */
	int   hp;
	int   score;                /* run total (updated from the summary on level complete) */
	score_summary summary;      /* the itemized level-complete tally (GM_COMPLETE overlay) */
	struct menu menu;           /* the theme-panel menu state (GM_MENU / GM_PAUSED) */
	int   comp_sel;             /* complete screen: 0 = Next, 1 = Save */
	bool  quit;                 /* menu Exit chosen: leave the main loop */
	bool  gameover_entry;       /* name-entry/records flow active; restart when it closes */
	float level_time;           /* seconds into the current level */
	float lastsec_next;         /* next LastSeconds tick fires when remaining < this
	                             * (armed to 10, then floor(remaining) — RE game_tick) */
	bool  want_intro;           /* the next loaded level should arm its intro */
	bool  start_menu;           /* splash exits into the live menu (vs straight to loading) */

	/* scripted cinematics */
	jjs_vm menu_vm, intro_vm;
	bool   menu_vm_ok;
	r_sound intro_snd;     /* the intro's narration wav — cut short if ENTER skips */

	/* orthogonal modifiers */
	freecam fc;
	bool    paused, show_debug;
	int     shot_num;           /* F2 screenshot counter */

	/* presentation carried across frames */
	struct cam_smooth  boom;
	struct player_anim anim;
	struct picker      pick;
	float t_accum;              /* global animation clock */

	/* death-cam anchor (locked when GM_DEATH is entered) */
	float death_x, death_y, death_z, death_a0;

	/* shell bitmaps */
	r_tex splash_tex, load_tex;
};

/* one-time setup: load the menu backdrop or the start level, seed camera/anim,
 * load the splash bitmap. Leaves the game in GM_SPLASH. false on hard load failure. */
bool game_init(struct game *g, const char *base, const gam_manifest *man,
               int cur, bool menu);

/* ---- predicates (derived facts, computed from the mode) ---- */
bool  mode_scripted(const struct game *g);          /* MENU || INTRO */
bool  gameplay_input(const struct game *g);         /* tank controls live this frame */
float game_gdt(const struct game *g, float dt, bool step);  /* the single freeze authority */

/* ---- transitions ---- */
/* switch to the themed loading screen for the current level. */
void enter_loading(struct game *g);
/* reset follow cam + player anim on entering a level (face John's facing, idle pose). */
void level_enter_reset(struct game *g);
/* full (re)load of level idx through the loading screen (menu-start, next, timeout, game-over). */
void game_enter_level(struct game *g, int idx, bool play_intro);
/* instant debug reload (F8/F9): no loading screen, no intro, stays GM_PLAYING. */
void game_jump_level(struct game *g, int idx);

/* savegame slots (SavedGames/jj<slot>.sav, original-compatible; see formats/sav.h).
 * `name` is the user-entered slot label (savename[10]); NULL/empty -> "jj".
 * `level_idx` is what the load should enter — the CURRENT level from the pause
 * menu, but the NEXT one when saving at the level-complete summary (that level
 * is beaten; loading must not replay it). Load enters the saved level with the
 * saved hearts/score (a run boundary). */
bool game_save_slot(struct game *g, int slot, const char *name, int level_idx);
bool game_load_slot(struct game *g, int slot);

/* return to the title menu (reload the demo backdrop + Main track). */
void game_enter_menu(struct game *g);

#endif /* GAME_MODE_H */
