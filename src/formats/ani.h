/* .ani — animation clip table (e.g. Models/K.ANI, paired with K.mdl). Text, one
 * clip per line: `Name FirstFrame NumFrames FPS [Flag]`. The loader remaps each
 * Name to a FIXED 24-slot engine index (NOT file order), per reference/ani_format.md.
 * A clip's frames are mdl frames [first .. first+count) played at `fps` (fps 0 =
 * move-synced: driven by move/turn progress, not wall-clock). */
#ifndef ANI_H
#define ANI_H

#include <stdbool.h>

#define ANI_NUM_CLIPS 24

/* engine clip slots (index into ani_set.clips) */
enum {
	ANI_WALK_FWD = 0, ANI_WALK_BACK, ANI_SPEED_FWD, ANI_SPEED_BACK,
	ANI_SLOW_FWD, ANI_SLOW_BACK, ANI_CELEBRATION, ANI_JUMP,
	ANI_GLUE, ANI_GHOST, ANI_ICE, ANI_FALL,
	ANI_PARAGLIDE, ANI_SLIDE, ANI_IDLE1, ANI_IDLE2,
	ANI_FIELD_STAIR_UP, ANI_FIELD_STAIR_DOWN, ANI_STAIR_STAIR_UP, ANI_STAIR_STAIR_DOWN,
	ANI_STAIR_FIELD_UP, ANI_STAIR_FIELD_DOWN, ANI_TURN_LEFT, ANI_TURN_RIGHT,
};

typedef struct {
	int   first;    /* first mdl frame */
	int   count;    /* number of frames */
	float fps;      /* playback rate; 0 = move-synced */
	int   flag;
	bool  present;  /* had a definition in the file */
} ani_clip;

typedef struct {
	ani_clip clips[ANI_NUM_CLIPS];
} ani_set;

bool        ani_load(const char *path, ani_set *out);
const char *ani_clip_name(int idx);   /* engine name for a slot, or "?" */

#endif /* ANI_H */
