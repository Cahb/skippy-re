/* Player options (see options.h). */
#include "game/options.h"
#include "render/renderer.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

game_options g_opts = { .master = 70, .music = 60, .sfx = 70, .music_on = true };

static void cfg_path(const char *base, char *out, size_t n)
{
	snprintf(out, n, "%s/rewrite.cfg", base);
}

void options_apply(void)
{
	r_set_volume(R_VOL_MASTER, (float)g_opts.master / OPT_VOL_MAX);
	r_set_volume(R_VOL_MUSIC, g_opts.music_on ? (float)g_opts.music / OPT_VOL_MAX : 0.0f);
	r_set_volume(R_VOL_SFX, (float)g_opts.sfx / OPT_VOL_MAX);
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void options_load(const char *base)
{
	char p[1024], line[128];
	cfg_path(base, p, sizeof p);
	FILE *f = fopen(p, "r");
	if (f) {
		while (fgets(line, sizeof line, f)) {
			char key[32];
			int v;
			if (sscanf(line, "%31[^=]=%d", key, &v) != 2)
				continue;
			if      (!strcmp(key, "vol_master")) g_opts.master = clampi(v, 0, OPT_VOL_MAX);
			else if (!strcmp(key, "vol_music"))  g_opts.music = clampi(v, 0, OPT_VOL_MAX);
			else if (!strcmp(key, "vol_sfx"))    g_opts.sfx = clampi(v, 0, OPT_VOL_MAX);
			else if (!strcmp(key, "music_on"))   g_opts.music_on = v != 0;
		}
		fclose(f);
	}
	options_apply();
}

void options_save(const char *base)
{
	char p[1024];
	cfg_path(base, p, sizeof p);
	FILE *f = fopen(p, "w");
	if (!f)
		return;
	fprintf(f, "vol_master=%d\nvol_music=%d\nvol_sfx=%d\nmusic_on=%d\n",
	        g_opts.master, g_opts.music, g_opts.sfx, g_opts.music_on ? 1 : 0);
	fclose(f);
}
