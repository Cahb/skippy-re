/* End-of-level score, exactly per the RE'd game_calc_summary_scores (0x41a760) —
 * row table + provenance in reference/savegame_highscore_format.md. Multipliers
 * and gating are the original's; nothing invented. */
#ifndef GAME_SCORE_H
#define GAME_SCORE_H

#include "sim/sim.h"
#include "formats/jjm.h"

enum {
	SC_CRYSTALS = 0,   /* min(collected, needed)      x 5  */
	SC_EXTRA,          /* max(collected - needed, 0)  x 10 */
	SC_ENEMIES,        /* enemies destroyed           x 50 */
	SC_TIME,           /* whole seconds left          x 2  */
	SC_SISYPHUS,       /* total pickups on the level  x 5, iff 100% collected */
	SC_VITALITY,       /* hearts collected            x 1  (identity: best RE candidate) */
	SC_ROWS
};

extern const int         SCORE_MULT[SC_ROWS];    /* 5, 10, 50, 2, 5, 1 */
extern const char *const SCORE_LABEL[SC_ROWS];

typedef struct {
	int count[SC_ROWS];
	int points[SC_ROWS];
	int level_score;   /* sum of the rows */
	int total_score;   /* prev_total + level_score */
} score_summary;

void score_compute(score_summary *out, const sim_state *s, const jjm_level *l,
                   float time_left, int prev_total);

#endif /* GAME_SCORE_H */
