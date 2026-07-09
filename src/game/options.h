/* Player options: the original's three volume knobs (0..90 in steps of 10, per
 * the menu_manager RE) + a music toggle (the CD-audio toggle analogue).
 * Persisted as flat key=value lines in <base>/rewrite.cfg. */
#ifndef GAME_OPTIONS_H
#define GAME_OPTIONS_H

#include <stdbool.h>

#define OPT_VOL_MAX  90
#define OPT_VOL_STEP 10

typedef struct {
	int  master, music, sfx;   /* 0..90 */
	bool music_on;
} game_options;

extern game_options g_opts;

void options_load(const char *base);   /* read rewrite.cfg (defaults if absent) + apply */
void options_save(const char *base);
void options_apply(void);              /* push the knobs into r_set_volume */

#endif /* GAME_OPTIONS_H */
