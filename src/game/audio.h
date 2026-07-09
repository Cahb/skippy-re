/* Game audio: positional SFX dispatch for a frame's sim events + the .leo
 * positional ambience mix. Reads the loaded scene's sound handles (game/scene.h). */
#ifndef GAME_AUDIO_H
#define GAME_AUDIO_H

#include "render/renderer.h"
#include "sim/sim.h"

/* mix each .leo ambience stream by distance/pan from the current camera. */
void audio_update_ambience(r_camera view);

/* gate the sustained state loops (slide/ice/paraglide runs, mover travel hums)
 * by live sim state, and keep the bomb-fuse retrigger fed. Call once per frame;
 * `active` = gameplay running (false mutes everything: menu/pause/death). */
void audio_update_loops(r_camera view, bool active);

/* request background music by CDT name ("Main", a theme world, "GameOver", ...):
 * resolves the name through CDTracks/JJ.CDT to audio/TrackNN.wav and switches the
 * streamed track. Same track = keeps playing (level restarts don't hiccup); track
 * 0 / unknown name / missing file = silence. */
void audio_music_want(const char *base, const char *name);

/* forget the current track so the NEXT audio_music_want restarts it from the top.
 * Call on run boundaries (new game, game over -> level 1, loading a save); plain
 * level-to-level advances keep the track's position instead. */
void audio_music_reset(void);

/* play the sound for one sim event: player/announce sounds centred, enemy/field
 * sounds positional from the event's world pos. `t` is the global clock (picks the
 * ADD A/B/C variant). No-op for events with no mapped sound. */
void audio_play_event(const sim_event *e, r_camera view, float t);

#endif /* GAME_AUDIO_H */
