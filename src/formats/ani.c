#include "formats/ani.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* engine slot order (index = enum above); Name is matched case-insensitively */
static const char *SLOT_NAMES[ANI_NUM_CLIPS] = {
	"walk_forward", "walk_backward", "speed_forward", "speed_backward",
	"slow_forward", "slow_backward", "celebration", "jump",
	"glue", "ghost", "ice", "fall",
	"paraglide", "slide", "idle1", "idle2",
	"field_stair_up", "field_stair_down", "stair_stair_up", "stair_stair_down",
	"stair_field_up", "stair_field_down", "turn_left", "turn_right",
};

const char *ani_clip_name(int idx)
{
	return (idx >= 0 && idx < ANI_NUM_CLIPS) ? SLOT_NAMES[idx] : "?";
}

static int ieq(const char *a, const char *b)
{
	for (; *a && *b; a++, b++)
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
			return 0;
	return *a == *b;
}

static int slot_for(const char *name)
{
	for (int i = 0; i < ANI_NUM_CLIPS; i++)
		if (ieq(name, SLOT_NAMES[i]))
			return i;
	return -1;
}

bool ani_load(const char *path, ani_set *out)
{
	memset(out, 0, sizeof *out);
	FILE *f = fopen(path, "rb");
	if (!f)
		return false;

	char line[256];
	while (fgets(line, sizeof line, f)) {
		char *cm = strstr(line, "//");
		if (cm)
			*cm = 0;
		char name[64];
		int first = 0, count = 0, flag = 0;
		double fps = 0;
		int n = sscanf(line, "%63s %d %d %lf %d", name, &first, &count, &fps, &flag);
		if (n < 3)                       /* need at least Name First Num */
			continue;
		int slot = slot_for(name);
		if (slot < 0)
			continue;
		out->clips[slot] = (ani_clip){ first, count, (float)fps, flag, true };
	}
	fclose(f);
	return true;
}
