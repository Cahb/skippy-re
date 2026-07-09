/* Game HUD: the in-play overlay (corner panels, crystal/bomb/paraglide counters,
 * countdown timer, lives/level, radar) and the death/complete/timeout prompts.
 * Reads the loaded scene + sim (game/scene.h). */
#ifndef GAME_HUD_H
#define GAME_HUD_H

#include <stdbool.h>
#include "render/renderer.h"
#include "formats/jjs.h"
#include "game/score.h"
#include "game/menu.h"

/* shell flow: intro splash -> minimal text menu -> themed loading -> play. The loop
 * owns the transitions; hud.c owns the drawing. */
enum game_screen { SCR_SPLASH, SCR_MENU, SCR_LOADING, SCR_PLAYING };

/* the in-play HUD: corner panels, counters, timer, lives/level and radar. */
void hud_draw(int sw, int sh, float ps, int cur, int hp, float level_time, float t_accum);

/* full-screen end-of-attempt prompts (death / level-complete / timeout). `sum` is
 * the itemized tally shown on the level-complete screen. */
void hud_draw_status(int sw, int sh, bool dying, float death_t,
                     bool level_done, float done_t, bool timed_out, int hp,
                     const score_summary *sum, int comp_sel, float t);

/* pre-gameplay shell screen (splash bitmap / loading bitmap / fallback text menu). */
void hud_draw_shell(enum game_screen screen, r_tex splash_tex, r_tex load_tex);

/* the theme-panel menu overlay (title / pause / options / slots / records /
 * credits / name entry), rendered per the menu struct's current screen. */
void hud_draw_menu(int sw, int sh, float ps, const struct menu *m, float t);

/* the level-intro narration overlay drawn over the live 3D scene. */
void hud_draw_intro(int sw, int sh, float ps, const jjs_vm *vm, bool ready);

#endif /* GAME_HUD_H */
