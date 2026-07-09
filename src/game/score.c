/* End-of-level score (see score.h). Every constant and gate mirrors the RE'd
 * game_calc_summary_scores @ 0x41a760. */
#include "game/score.h"

const int SCORE_MULT[SC_ROWS] = { 5, 10, 50, 2, 5, 1 };

const char *const SCORE_LABEL[SC_ROWS] = {
	"crystals:", "extra crystals:", "destroyed enemies:",
	"time left:", "Sisyphus bonus:", "vitality:",
};

/* every collectible cell on the grid: crystals + the bonus/debuff pickup range —
 * the original sums the per-type counts in skippy_game_start_level into the WORD
 * the Sisyphus bonus is based on. */
static int total_pickups(const jjm_level *l)
{
	int n = 0;
	for (int x = 0; x < l->dim_x; x++)
		for (int y = 0; y < l->dim_y; y++) {
			int t = l->tiles[x][y].pickup_type;
			if (t == PU_CRYSTAL || (t >= PU_PARAGLIDE && t <= PU_PROTECT))
				n++;
		}
	return n;
}

void score_compute(score_summary *out, const sim_state *s, const jjm_level *l,
                   float time_left, int prev_total)
{
	const sim_stats *st = &s->stats;
	int needed = (int)l->crystals_needed;
	int coll = st->crystals;
	int coll_all = coll;
	for (int t = PU_PARAGLIDE; t <= PU_PROTECT; t++)
		coll_all += st->collected[t];
	int total = total_pickups(l);

	out->count[SC_CRYSTALS] = coll < needed ? coll : needed;
	out->count[SC_EXTRA]    = coll > needed ? coll - needed : 0;
	out->count[SC_ENEMIES]  = st->enemies;
	out->count[SC_TIME]     = time_left > 0.0f ? (int)time_left : 0;
	/* Sisyphus: the level's WHOLE pickup haul scores x5, but only on a 100% run */
	out->count[SC_SISYPHUS] = coll_all >= total ? total : 0;
	/* vitality: hearts collected — the strongest RE candidate for byte @0x170a64
	 * (the heart item's texture is literally ENERGY.TGA); flagged in the docs */
	out->count[SC_VITALITY] = st->collected[PU_HEART];

	out->level_score = 0;
	for (int i = 0; i < SC_ROWS; i++) {
		out->points[i] = out->count[i] * SCORE_MULT[i];
		out->level_score += out->points[i];
	}
	out->total_score = prev_total + out->level_score;
}
