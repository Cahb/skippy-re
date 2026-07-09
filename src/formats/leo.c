#include "formats/leo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* parse "a,b,c" into out[3]; missing components stay 0 */
static void parse_vec3(const char *s, float *out)
{
	out[0] = out[1] = out[2] = 0.0f;
	int i = 0;
	const char *p = s;
	while (*p && i < 3) {
		out[i++] = (float)atof(p);
		const char *comma = strchr(p, ',');
		if (!comma)
			break;
		p = comma + 1;
	}
}

/* whitespace-split a NUL-terminated chunk into up to `max` tokens (in place) */
static int tokenize(char *chunk, char **tok, int max)
{
	int n = 0;
	char *p = chunk;
	while (*p && n < max) {
		while (*p && isspace((unsigned char)*p))
			*p++ = 0;
		if (!*p)
			break;
		tok[n++] = p;
		while (*p && !isspace((unsigned char)*p))
			p++;
	}
	return n;
}

bool leo_load(const char *path, leo_scene *out)
{
	memset(out, 0, sizeof *out);
	FILE *f = fopen(path, "rb");
	if (!f)
		return false;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (sz <= 0) {
		fclose(f);
		return false;
	}
	char *buf = malloc((size_t)sz + 1);
	if (!buf) {
		fclose(f);
		return false;
	}
	size_t got = fread(buf, 1, (size_t)sz, f);
	buf[got] = 0;
	fclose(f);

	/* strip // line comments */
	for (char *p = buf; *p; p++) {
		if (p[0] == '/' && p[1] == '/') {
			while (*p && *p != '\n')
				*p++ = ' ';
			if (!*p)
				break;
		}
	}

	/* entries are ';'-terminated */
	char *save = buf;
	for (char *semi; (semi = strchr(save, ';')) != NULL; save = semi + 1) {
		*semi = 0;
		char *tok[64];
		int n = tokenize(save, tok, 64);
		if (n < 2)
			continue;
		if (strcmp(tok[0], "Particle") == 0) {
			/* Particle  par  pos  off  src  dest  tex */
			if (out->num_particles < LEO_MAX_OBJECTS && n >= 3) {
				leo_particle *pp = &out->parts[out->num_particles++];
				snprintf(pp->par, sizeof pp->par, "%s", tok[1]);
				parse_vec3(tok[2], pp->pos);
				if (n >= 4)
					parse_vec3(tok[3], pp->off);
				if (n >= 6) {
					snprintf(pp->src, sizeof pp->src, "%s", tok[4]);
					snprintf(pp->dst, sizeof pp->dst, "%s", tok[5]);
				}
				if (n >= 7)
					snprintf(pp->tex, sizeof pp->tex, "%s", tok[6]);
			}
			continue;
		}
		if (strcmp(tok[0], "Sound") == 0) {
			/* Sound  wav  Y,X,Z  (positional ambience) */
			if (out->num_sounds < LEO_MAX_SOUNDS && n >= 3) {
				leo_sound *s = &out->sounds[out->num_sounds++];
				snprintf(s->wav, sizeof s->wav, "%s", tok[1]);
				parse_vec3(tok[2], s->pos);
			}
			continue;
		}
		if (strcmp(tok[0], "Model") != 0 || n < 4)
			continue;
		if (out->num >= LEO_MAX_OBJECTS)
			break;
		/* Model <mdl> <pos> <rot> [ANI <ani>] <src> <dst> <LIT|NOLIT|UNLIT> <tex>
		 *       [WRAP] [SPLINE_DYNAMIC <ms> <wp>...]. Fields after <rot> shift right by 2
		 * when the optional ANI pair is present, so walk tokens rather than fixed slots. */
		leo_object *o = &out->objs[out->num++];
		snprintf(o->model, sizeof o->model, "%s", tok[1]);
		parse_vec3(tok[2], o->pos);
		parse_vec3(tok[3], o->rot);
		int i = 4;
		if (i < n && strcmp(tok[i], "ANI") == 0 && i + 1 < n) {
			snprintf(o->ani, sizeof o->ani, "%s", tok[i + 1]);
			i += 2;
		}
		if (i < n) snprintf(o->src, sizeof o->src, "%s", tok[i]), i++;   /* src blend */
		if (i < n) snprintf(o->dst, sizeof o->dst, "%s", tok[i]), i++;   /* dest blend */
		if (i < n) {                                                     /* LIT|NOLIT|UNLIT */
			o->lit = strcmp(tok[i], "NOLIT") != 0 && strcmp(tok[i], "UNLIT") != 0;
			i++;
		}
		if (i < n) snprintf(o->tex, sizeof o->tex, "%s", tok[i]), i++;   /* texture */
		if (i < n && strcmp(tok[i], "WRAP") == 0) { o->wrap = true; i++; }
		if (i < n && strcmp(tok[i], "SPLINE_DYNAMIC") == 0 && i + 1 < n) {
			o->spline_ms = (float)atof(tok[i + 1]);
			i += 2;
			for (; i < n && o->spline_n < LEO_MAX_SPLINE; i++)
				parse_vec3(tok[i], o->spline[o->spline_n++]);
		}
	}

	free(buf);
	return true;
}
