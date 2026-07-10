/* Enemy-AI wake/route regression vs original-game footage (re_g6_enemy_ai.md).
 *
 * Facts encoded (observed frame-by-frame in the original):
 *  - Forest\EnemyStart: the frog at (13,13) opens by hopping -Y: (13,12) then
 *    (13,11) — the backward search's fixed expansion order + LIFO ties pick the
 *    column route, not the row route.
 *  - Space\Bonus: NO robot moves while the player sits at spawn (the 50-pop budget
 *    never reaches them through the glue/ice/jump-pad conveyor). The robot at
 *    (5,8) wakes when the player stands on (7,8); the one at (10,5) wakes at
 *    (10,7).
 */
#include "formats/jjm.h"
#include "formats/asset.h"
#include "sim/sim.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void expect(int ok, const char *what)
{
	printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		fails++;
}

static jjm_level lvl;   /* file-scope: too big for the stack */
static sim_state sim;

static int load(const char *base, const char *rel)
{
	char p[1024];
	if (!asset_resolve(base, rel, p, sizeof p) || !jjm_load(p, &lvl)) {
		printf("  SKIP %s (not found)\n", rel);
		return 0;
	}
	sim_init(&sim, &lvl);
	return 1;
}

static sim_player *enemy_at(int x, int y)
{
	for (int i = 0; i < sim.num_enemies; i++)
		if (sim.enemies[i].home_x == x && sim.enemies[i].home_y == y)
			return &sim.enemies[i];
	return NULL;
}

/* pin the player to a cell (as if standing there) without touching enemy state */
static void player_at(int x, int y)
{
	sim.p.cx = x; sim.p.cy = y;
	sim.p.rx = (float)x; sim.p.ry = (float)y;
	sim.p.moving = sim.p.turning = sim.p.falling = false;
}

/* run `secs` of sim; true if enemy e commits any action (turn or hop) during it */
static int acts_within(sim_player *e, float secs)
{
	for (float t = 0.0f; t < secs; t += 0.016f) {
		sim_tick(&sim, 0.016f);
		if (e->turning || e->moving)
			return 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	const char *base = (argc > 1) ? argv[1] : "../game_root/SkippyAdventure";

	printf("Forest\\EnemyStart — opening route:\n");
	if (load(base, "Levels\\Forest\\EnemyStart.jjm")) {
		sim_player *frog = enemy_at(13, 13);
		expect(frog != NULL, "frog spawns at (13,13)");
		if (frog) {
			/* let it decide: it turns first (spawns facing -X), then hops */
			for (int i = 0; i < 400 && !frog->moving; i++)
				sim_tick(&sim, 0.016f);
			expect(frog->moving && frog->tx == 13 && frog->ty == 12,
			       "first hop is -Y onto (13,12)   [footage: column route]");
			for (int i = 0; i < 400 && !(frog->moving && frog->ty == 11); i++)
				sim_tick(&sim, 0.016f);
			expect(frog->moving && frog->tx == 13 && frog->ty == 11,
			       "second hop continues -Y onto (13,11)");
		}
	}

	printf("Space\\Bonus — conveyor-gated aggro:\n");
	if (load(base, "Levels\\Space\\Bonus.jjm")) {
		sim_player *r1 = enemy_at(5, 8), *r2 = enemy_at(10, 5);
		expect(r1 && r2, "robots spawn at (5,8) and (10,5)");
		if (r1 && r2) {
			int moved = 0;
			for (float t = 0.0f; t < 2.0f; t += 0.016f) {
				sim_tick(&sim, 0.016f);
				for (int i = 0; i < sim.num_enemies; i++)
					moved |= sim.enemies[i].turning || sim.enemies[i].moving;
			}
			expect(!moved, "no robot self-aggros with the player at spawn");

			sim_init(&sim, &lvl);                    /* fresh board per scenario */
			r1 = enemy_at(5, 8);
			player_at(7, 8);                         /* the observed wake tile (ice) */
			expect(acts_within(r1, 1.0f), "player on (7,8) wakes the (5,8) robot");

			sim_init(&sim, &lvl);
			r2 = enemy_at(10, 5);
			player_at(10, 7);                        /* the observed wake tile */
			expect(acts_within(r2, 1.0f), "player on (10,7) wakes the (10,5) robot");
		}
	}

	printf("Candy\\Candy01 — mover-track planning (wait for the ride):\n");
	if (load(base, "Levels\\Candy\\Candy01.jjm")) {
		/* (1,8) is a +Y mover whose track crosses void to (1,13): the track must be
		 * plannable so an enemy at (1,7) approaches, WAITS for the platform, boards,
		 * rides across, and steps off — never walking into the gap. */
		sim_player *e = &sim.enemies[0];
		expect(sim.num_enemies > 0, "level has an enemy to reposition");
		if (sim.num_enemies > 0) {
			e->cx = e->home_x = 1; e->cy = e->home_y = 7;
			e->rx = 1.0f; e->ry = 7.0f;
			e->moving = e->turning = e->falling = false;
			player_at(1, 13);
			expect(acts_within(e, 2.0f), "enemy at (1,7) plans across the mover gap");
			bool fell = false;
			float crossed = -1.0f;
			for (float t = 0.0f; t < 40.0f; t += 0.016f) {
				sim_tick(&sim, 0.016f);
				fell |= e->falling;
				if (crossed < 0.0f && e->cx == 1 && e->cy >= 12)
					crossed = t;
			}
			expect(!fell, "never hops into the open gap (waits for the platform)");
			expect(crossed >= 0.0f, "rides the platform across (reaches (1,>=12))");
		}

		/* boarding timing: a car that departs mid-hop must be REFUSED (a hop takes
		 * MOVE_DUR while the car crosses a full cell — boarding it = death) */
		sim_init(&sim, &lvl);
		e = &sim.enemies[0];
		sim_platform *pl = NULL;
		for (int i = 0; i < sim.num_platforms; i++)
			if (sim.platforms[i].hx == 1 && sim.platforms[i].hy == 8)
				pl = &sim.platforms[i];
		expect(pl != NULL, "found the (1,8) mover");
		if (pl && sim.num_enemies > 0) {
			e->cx = e->home_x = 1; e->cy = e->home_y = 7;
			e->rx = 1.0f; e->ry = 7.0f;
			e->moving = e->turning = e->falling = false;
			e->facing = DIR_PY;                  /* pre-faced: next ai act would be the hop */
			player_at(1, 13);
			pl->t = 0.05f; pl->phase = 1; pl->wait = 0.0f;   /* at home but DEPARTING */
			int hopped = 0;
			for (int i = 0; i < 3; i++) {        /* while it still looks aligned */
				sim_tick(&sim, 0.016f);
				hopped |= e->moving;
			}
			expect(!hopped, "refuses to board a departing car");
			pl->t = 0.0f; pl->phase = 0; pl->wait = 0.0f;    /* freshly parked at home */
			e->moving = e->turning = false;
			for (float t = 0.0f; t < 0.5f && !e->moving; t += 0.016f)
				sim_tick(&sim, 0.016f);
			expect(e->moving && e->tx == 1 && e->ty == 8,
			       "boards the freshly-parked car at (1,8)");
		}
	}

	printf(fails ? "test_ai: %d FAILURE(S)\n" : "test_ai: all ok\n", fails);
	return fails != 0;
}
