/* Game-specific FX emitters layered on the generic particle engine (game/fx.h):
 * per-frame world effects + per-event bursts. Reads the loaded scene (game/scene.h). */
#ifndef GAME_FX_EMIT_H
#define GAME_FX_EMIT_H

#include <stdbool.h>
#include "sim/sim.h"

/* emit this frame's world FX (exit fountain, .leo props, thruster, diver bubbles). */
void fx_emit_world(float dt, bool player_dying);

/* emit the FX for one sim event (pickup sparkle / explosion blobs). */
void fx_emit_event(const sim_event *e);

#endif /* GAME_FX_EMIT_H */
