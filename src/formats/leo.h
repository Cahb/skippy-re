/* .leo — "Level Extra Objects": per-level decoration placement (bushes, crates,
 * beehive, supporting plattes, ...), separate from the .jjm tilemap. ASCII text.
 *   Level3DExtraObjects\<world>\<level>.leo
 * Each entry (';'-terminated) is one of:
 *   Model     <mdl>  Y,X,Z  rx,ry,rz  <srcblend> <destblend> <LIT|NOLIT>  <tga>;
 *   Particle  <par>  Y,X,Z  rx,ry,rz  <srcblend> <destblend>              <tga>;
 *   Sound     <wav>  Y,X,Z;
 * Coordinates are (gridY, gridX, height) — first two SWAPPED vs our tiles[x][y]
 * indexing (see reference/coordinate model). This loader keeps Model entries;
 * Particle/Sound are counted but not stored (FX/audio come later). */
#ifndef LEO_H
#define LEO_H

#include <stdbool.h>

#define LEO_PATH        128
#define LEO_MAX_OBJECTS 128
#define LEO_MAX_SOUNDS  32
#define LEO_MAX_SPLINE  32   /* SPLINE_DYNAMIC waypoints */

typedef struct {
	char  model[LEO_PATH];   /* .mdl path as written */
	char  tex[LEO_PATH];     /* texture path ("" if none) */
	char  ani[LEO_PATH];     /* optional "ANI <path>" (animated props: flags, fish, ...) — "" if none */
	float pos[3];            /* AS WRITTEN: [0]=gridY, [1]=gridX, [2]=height */
	float rot[3];            /* rx,ry,rz radians (rz = yaw about world up Z) */
	char  src[16], dst[16];  /* D3DBLEND keywords (NONE/SRCALPHA/INVSRCALPHA/ONE/ZERO) */
	bool  lit;               /* LIT vs NOLIT/UNLIT */
	bool  wrap;              /* trailing WRAP token = texture-repeat addressing */
	/* optional SPLINE_DYNAMIC <ms> <wp>... : closed-loop motion path (submarine/fish/ray) */
	int   spline_n;          /* waypoint count (0 = static) */
	float spline_ms;         /* loop duration in ms */
	float spline[LEO_MAX_SPLINE][3];  /* waypoints, AS WRITTEN (Y,X,Z) */
} leo_object;

typedef struct {
	char  wav[LEO_PATH];     /* .wav path as written */
	float pos[3];            /* AS WRITTEN: [0]=gridY, [1]=gridX, [2]=height */
} leo_sound;

typedef struct {
	char  par[LEO_PATH];     /* .par ParticleSystem path */
	char  tex[LEO_PATH];     /* particle texture (e.g. blow.tga) */
	float pos[3];            /* AS WRITTEN: [0]=gridY, [1]=gridX, [2]=height */
	float off[3];            /* 2nd triple: emit offset (e.g. 0,0,0.78 for the fountain) */
	char  src[16], dst[16];  /* blend keywords; "ZERO"/"ZERO" = invisible (audio-only, e.g. bees) */
} leo_particle;

typedef struct {
	int          num;
	leo_object   objs[LEO_MAX_OBJECTS];
	int          num_particles;               /* stored particle emitters */
	leo_particle parts[LEO_MAX_OBJECTS];
	int          num_sounds;                  /* .leo Sound entries (positional ambience) */
	leo_sound    sounds[LEO_MAX_SOUNDS];
} leo_scene;

bool leo_load(const char *path, leo_scene *out);

#endif /* LEO_H */
