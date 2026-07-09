/* .par — ParticleSystem loader (see reference/par_format.md). The originals are
 * serialized C++ object graphs (system -> generator + environment). We don't
 * replicate the engine's internal sample tables; we extract the emitter-relevant
 * ranges and feed them to our own particle pool. Parsed by scanning for the
 * class-name strings — the reader fields sit immediately after each `name\0`. */
#ifndef PAR_H
#define PAR_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { PAR_GEN_NONE = 0, PAR_GEN_STD, PAR_GEN_CYLINDER } par_gen_kind;

#define PAR_MAX_COLORS 8

/* one entry of a StdGenerator's weighted-random colour palette (the "ramp"). */
typedef struct { uint8_t rgb[3]; uint32_t weight; } par_color;

typedef struct {
	par_gen_kind kind;
	/* spawn position: STD = offset range [lo,hi]; CYLINDER = center + radius about axis */
	float pos_lo[3], pos_hi[3];
	float center[3], axis[3], radius;
	/* initial velocity: random direction in [dir_lo,dir_hi], magnitude in [speed_lo,speed_hi] */
	float dir_lo[3], dir_hi[3];
	float speed_lo, speed_hi;
	float life_lo, life_hi;    /* particle lifetime range (seconds) */
	float gravity[3];          /* from the environment (GravityEnvironment) */
	float vel_off[3];          /* XStdGenerator: constant velocity bias added to every particle */
	float face_size;           /* FaceParticleSystem base particle size (@0x76) */
	float emit_rate;           /* StdGenerator @0x10: emission rate / particle budget */
	int   num_colors;          /* StdGenerator colour palette (weighted random) */
	par_color colors[PAR_MAX_COLORS];
	bool  valid;
} par_system;

/* parse a .par into `out`. Returns false (and out->valid=false) on failure. */
bool par_load(const char *path, par_system *out);

#endif /* PAR_H */
