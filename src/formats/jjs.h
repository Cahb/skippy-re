/* .jjs instruction-script VM — cinematic camera sequences (menu backdrop + per-level
 * how-to intros). Text format, ';'-terminated statements, re-parsed as they run (see
 * reference / memory instruction-script-spec).
 *
 * CAMERA MODEL (RE'd from RUS_Karoo.exe — one follow-cam used by gameplay AND scripts):
 * the script never moves the eye directly. It layers modifiers on a single camera:
 *   - ORBIT mode (default): eye = target + Rot(azimuth, elevation) * dist. `observe N`
 *     picks which actor the target tracks; `distance N` is the orbit radius (glides);
 *     `gotoxyz`/`movetoxyz`/`setcamtargetxyz` move the look-at point. Azimuth auto-
 *     rotates (slow CCW) so the camera circles whatever it frames.
 *   - EXPLICIT mode: `setcamposxyz` / `splinexyz` give the eye an explicit position
 *     (a scripted fly-through) looking at the explicit/observed target. Used by the
 *     menu demo and the intro spline shots.
 * The VM tracks which mode is live per the last camera command and computes cam_pos +
 * cam_tgt (world coords); the host just applies them. */
#ifndef JJS_H
#define JJS_H

#include <stdbool.h>

#define JJS_MAX_STMT   512
#define JJS_STMT_LEN   512
#define JJS_MAX_CTRL   32     /* spline control points */
#define JJS_MAX_WAVE   16     /* preloaded wave slots */
#define JJS_WAVE_LEN   128

typedef struct {
	char  stmt[JJS_MAX_STMT][JJS_STMT_LEN];  /* raw statements */
	int   n_stmt;
	int   pc;                                /* program counter */
	int   loop_anchor;                       /* `fromhere` line (`again` jumps here) */
	bool  done;                              /* `break` reached (and not looping) */

	/* live camera outputs (host reads these) — WORLD coords, fully computed */
	float cam_pos[3];                        /* eye */
	float cam_tgt[3];                        /* look-at */
	char  text[JJS_STMT_LEN];                /* current caption ("" = hidden) */

	/* sequencing */
	float wait_left;                         /* `wait`: seconds remaining (only blocker) */

	/* spline: NON-blocking eye fly-through; animates over spline_dur while a `wait` holds */
	bool  spline_on;   float spline[JJS_MAX_CTRL][3]; int spline_n; float spline_dur, spline_t;
	/* moveto: BLOCKING look-at glide (no `wait` needed) */
	bool  move_on;     float move_dst[3], move_speed;

	/* camera model state */
	int   observe;                           /* observed actor index (0 = none) */
	bool  eye_explicit;                      /* eye is script-set (setcampos/spline) vs orbit */
	bool  tgt_explicit;                      /* target is script-set (setcamtarget/goto/moveto) vs observed actor */
	float exp_pos[3];                        /* explicit eye (setcampos / spline output) */
	float exp_tgt[3];                        /* explicit look-at (setcamtarget / goto / moveto) */
	float distance, dist_want;               /* orbit radius: current (glides) + desired */
	float azimuth, elevation;                /* orbit angles (radians) */

	/* audio: initwave fills a slot; playwave sets want_wave to that slot's path for one
	 * frame (host plays it, then clears). */
	char  wave[JJS_MAX_WAVE][JJS_WAVE_LEN];
	char  want_wave[JJS_WAVE_LEN];           /* "" = nothing to play this frame */
} jjs_vm;

/* parse a .jjs at an exact path. false on open/read failure. */
bool jjs_load(const char *path, jjs_vm *vm);

/* seed the orbit from an initial overview pose (eye + look-at, world coords) so intros
 * open from that framing and glide in. Call once after jjs_load, before ticking. */
void jjs_seed(jjs_vm *vm, const float eye[3], const float look[3]);

/* advance dt seconds: animates the camera and runs statements until one blocks, updating
 * cam_pos/cam_tgt/text/want_wave. `actor_pos` is the observed actor's world position (the
 * kangaroo) — the orbit/observe target tracks it; pass NULL to hold the last target.
 * Loops forever if the script uses `again`; stops (done=true) on `break`. */
void jjs_tick(jjs_vm *vm, float dt, const float actor_pos[3]);

#endif /* JJS_H */
