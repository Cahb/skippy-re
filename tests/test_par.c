/* validate the .par loader against a real file: prints the extracted emitter params. */
#include "formats/par.h"
#include <stdio.h>

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s <file.par>\n", argv[0]);
		return 2;
	}
	par_system p;
	if (!par_load(argv[1], &p)) {
		printf("FAILED to parse %s\n", argv[1]);
		return 1;
	}
	const char *kn = p.kind == PAR_GEN_STD ? "StdGenerator"
	               : p.kind == PAR_GEN_CYLINDER ? "CylinderGenerator" : "?";
	printf("%s  [%s]\n", argv[1], kn);
	if (p.kind == PAR_GEN_CYLINDER)
		printf("  center (%.3f,%.3f,%.3f)  axis (%.3f,%.3f,%.3f)  radius %.3f\n",
		       p.center[0], p.center[1], p.center[2], p.axis[0], p.axis[1], p.axis[2], p.radius);
	else
		printf("  pos_lo (%.3f,%.3f,%.3f)  pos_hi (%.3f,%.3f,%.3f)\n",
		       p.pos_lo[0], p.pos_lo[1], p.pos_lo[2], p.pos_hi[0], p.pos_hi[1], p.pos_hi[2]);
	printf("  dir_lo (%.3f,%.3f,%.3f)  dir_hi (%.3f,%.3f,%.3f)\n",
	       p.dir_lo[0], p.dir_lo[1], p.dir_lo[2], p.dir_hi[0], p.dir_hi[1], p.dir_hi[2]);
	printf("  speed [%.3f, %.3f]  life [%.3f, %.3f]  gravity (%.3f,%.3f,%.3f)  face_size %.3f\n",
	       p.speed_lo, p.speed_hi, p.life_lo, p.life_hi,
	       p.gravity[0], p.gravity[1], p.gravity[2], p.face_size);
	printf("  vel_off (%.3f,%.3f,%.3f)  emit_rate %.0f  colors %d:", p.vel_off[0], p.vel_off[1], p.vel_off[2],
	       p.emit_rate, p.num_colors);
	for (int i = 0; i < p.num_colors; i++)
		printf(" (%u,%u,%u)w%u", p.colors[i].rgb[0], p.colors[i].rgb[1], p.colors[i].rgb[2], p.colors[i].weight);
	printf("\n");
	return 0;
}
