/* Debug free-look camera (Z-up): WASD move, Q/E down/up, hold RMB to look, wheel to
 * dolly. Toggled with F1 in the game; seeded from the active look-at camera. */
#ifndef GAME_CAMERA_H
#define GAME_CAMERA_H

#include "render/renderer.h"
#include "formats/jjm.h"

typedef struct {
	r_vec3 eye;
	float  yaw, pitch;   /* radians */
	bool   active;
} freecam;

/* smoothed follow/boom camera state: eased facing (fx,fy) + boom pitch (rad). */
struct cam_smooth {
	float fx, fy;
	float pitch;
};

/* seed freecam yaw/pitch/eye from an existing look-at camera. */
void     freecam_sync(freecam *fc, r_camera cam);

/* build a look-at camera from the freecam's eye/orientation at the given fov. */
r_camera freecam_view(const freecam *fc, float fovy);

/* advance one frame of free-look input (move/look/dolly), dt seconds. */
void     freecam_update(freecam *fc, float dt);

/* Third-person boom/follow camera: eases facing (*cam_fx,*cam_fy) toward John's
 * facing and the boom pitch (*cam_pitch) toward overhead when a wall occludes the
 * eye. Skips those eases while freecam is active or John is dying, but always
 * returns the framed camera from the current state. jw = John's world pos. */
r_camera follow_cam(const jjm_level *l, r_vec3 jw, float fwd_x, float fwd_y,
                    struct cam_smooth *cs, float dt, bool freecam_active, bool dying);

/* Death camera: frozen ~60deg overview latched on the death cell, azimuth held at
 * the death-time facing a0 — the angel rises out of a static frame. */
r_camera death_cam(float dx, float dy, float dz, float a0);

#endif /* GAME_CAMERA_H */
