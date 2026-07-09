/* .par ParticleSystem loader — see par.h + reference/par_format.md.
 * We parse by scanning for the generator/environment class-name strings; the
 * serialized fields sit immediately after each `name\0` (the engine reads them
 * straight from the file position after the class descriptor). Little-endian
 * floats (x86 -> x86-64, direct copy). */
#include "formats/par.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *slurp(const char *path, long *n)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	unsigned char *b = (sz > 0) ? malloc((size_t)sz) : NULL;
	if (b && fread(b, 1, (size_t)sz, f) != (size_t)sz) {
		free(b);
		b = NULL;
	}
	fclose(f);
	if (b) {
		/* the originals were serialized in Win32 TEXT mode and the shipping loader
		 * reads them back with fopen(...,"r"), collapsing CRLF -> LF. We open "rb",
		 * so undo the \r\n -> \n translation ourselves; otherwise every float whose
		 * low byte is 0x0a gets a stray 0x0d and the struct misaligns. */
		long w = 0;
		for (long r = 0; r < sz; r++) {
			if (b[r] == 0x0d && r + 1 < sz && b[r + 1] == 0x0a)
				continue;
			b[w++] = b[r];
		}
		*n = w;
	}
	return b;
}

/* offset just after `name`'s NUL (= start of that object's fields), or -1. */
static long find_class(const unsigned char *b, long n, const char *name, long from)
{
	long len = (long)strlen(name);
	for (long i = (from < 0 ? 0 : from); i + len < n; i++)
		if (memcmp(b + i, name, (size_t)len) == 0 && b[i + len] == 0)
			return i + len + 1;
	return -1;
}

static float rf(const unsigned char *b, long *o)
{
	float v;
	memcpy(&v, b + *o, 4);
	*o += 4;
	return v;
}

static void rv3(const unsigned char *b, long *o, float *d)
{
	memcpy(d, b + *o, 12);
	*o += 12;
}

bool par_load(const char *path, par_system *out)
{
	memset(out, 0, sizeof *out);
	long n = 0;
	unsigned char *b = slurp(path, &n);
	if (!b)
		return false;

	long o;
	/* XStdGenerator = StdGenerator + two trailing vec3s (pos offset, velocity bias).
	 * "StdGenerator" is a substring of "XStdGenerator", so the base scan below lands on
	 * the same fields for both; this flag just says whether to read the 24-byte tail. */
	bool is_x = find_class(b, n, "XStdGenerator", 0) >= 0;
	if ((o = find_class(b, n, "StdGenerator", 0)) >= 0) {
		/* Fixed 96-byte head (all plain LE floats once CRLF is undone; verified by RE):
		 *   u32 mode; vec3 aHi,aLo,bLo,bHi,cLo,cHi; f32 speed,speed_var,life,life_var,emit
		 * then a colour ramp: u32 count; { u8 rgb[3]; u8 pad; u32 weight } * count.
		 * Position range = mode 0 ? vecB(bLo,bHi) : vecA(aLo,aHi); velocity = a random
		 * direction in [cLo,cHi] normalized * speed. (XStdGenerator adds 24 bytes AFTER
		 * the ramp; harmless here — the environment scan finds gravity past them.) */
		if (o + 96 > n)
			goto fail;
		unsigned mode;
		memcpy(&mode, b + o, 4);
		o += 4;
		float aHi[3], aLo[3], bLo[3], bHi[3], cLo[3], cHi[3];
		rv3(b, &o, aHi); rv3(b, &o, aLo); rv3(b, &o, bLo);
		rv3(b, &o, bHi); rv3(b, &o, cLo); rv3(b, &o, cHi);
		float speed = rf(b, &o), speed_var = rf(b, &o);
		float life  = rf(b, &o), life_var  = rf(b, &o);
		float emit  = rf(b, &o);
		out->kind = PAR_GEN_STD;
		/* The spawn-position pair is (base, spread): the 2nd vec is an ADDITIVE extent,
		 * not a range max. Verified from real files — candle (Kerze) and torch (Fackel2)
		 * both carry extent (0,0,0), i.e. a point emitter; reading the pair as a [lo,hi]
		 * range smeared the flame from the torch top all the way down to the floor.
		 * Store [base, base+spread] so the emitter lerps within it (point when spread=0). */
		{
			const float *pbase = mode == 0 ? bLo : aLo;
			const float *pspread = mode == 0 ? bHi : aHi;
			for (int i = 0; i < 3; i++) {
				out->pos_lo[i] = pbase[i];
				out->pos_hi[i] = pbase[i] + pspread[i];
			}
		}
		memcpy(out->dir_lo, cLo, 12);
		memcpy(out->dir_hi, cHi, 12);
		out->speed_lo = speed;  out->speed_hi = speed + speed_var;
		out->life_lo  = life;   out->life_hi  = life + life_var;
		out->emit_rate = emit;
		/* colour palette (weighted random) */
		if (o + 4 <= n) {
			unsigned nc;
			memcpy(&nc, b + o, 4);
			o += 4;
			for (unsigned c = 0; c < nc && o + 8 <= n; c++, o += 8)
				if (out->num_colors < PAR_MAX_COLORS) {
					par_color *pc = &out->colors[out->num_colors++];
					/* ramp entries are D3DCOLOR (byte order B,G,R,pad) — read as R,G,B.
					 * Verified: torch/candle palettes are warm flame colours (gold/
					 * red-orange) only with byte0=B, byte2=R; RGB gave blue flames. */
					pc->rgb[0] = b[o + 2]; pc->rgb[1] = b[o + 1]; pc->rgb[2] = b[o];
					memcpy(&pc->weight, b + o + 4, 4);
				}
		}
		if (is_x && o + 24 <= n) {   /* XStd tail: pos_off folds into the spawn range, vel_off is a drift */
			float pos_off[3], vel_off[3];
			rv3(b, &o, pos_off);
			rv3(b, &o, vel_off);
			for (int i = 0; i < 3; i++) {
				out->pos_lo[i] += pos_off[i];
				out->pos_hi[i] += pos_off[i];
				out->vel_off[i] = vel_off[i];
			}
		}
	} else if ((o = find_class(b, n, "CylinderGenerator", 0)) >= 0) {
		/* file order: vec3 center, f32 radius, vec3 axis, vec3 dirLo, vec3 dirHi,
		 * f32 spdLo, f32 spdHi, f32 lifeLo, f32 lifeHi, f32 extra. */
		if (o + 12 + 4 + 3 * 12 + 5 * 4 > n)
			goto fail;
		out->kind = PAR_GEN_CYLINDER;
		rv3(b, &o, out->center);
		out->radius = rf(b, &o);
		rv3(b, &o, out->axis);
		rv3(b, &o, out->dir_lo);
		rv3(b, &o, out->dir_hi);
		out->speed_lo = rf(b, &o);
		out->speed_hi = rf(b, &o);
		out->life_lo = rf(b, &o);
		out->life_hi = rf(b, &o);
	} else {
		goto fail;   /* XStd/Point/Box not handled yet */
	}

	/* environment: GravityEnvironment stores a UNIT direction vec3 followed by a scalar
	 * MAGNITUDE at +12 (verified across every .par: flames/candles 0.3, bubbles 2, fountains
	 * 5-9, debris/crystal 7-10, and 9.81 = Earth g for the crumb bursts; 0 = weightless float
	 * for explosions/teleporters). Reading only the direction made every emitter gravity-1,
	 * which turned fountains into sky-jets and let debris hang. Scale dir by the magnitude.
	 * The FaceParticleSystem's base particle size (face_size @0x76) is right after the
	 * 64-byte environment block. */
	long es = find_class(b, n, "GravityEnvironment", o);
	if (es >= 0) {
		if (es + 16 <= n) {
			long e = es;
			rv3(b, &e, out->gravity);
			float gmag = rf(b, &e);
			for (int i = 0; i < 3; i++)
				out->gravity[i] *= gmag;
		}
		if (es + 64 + 4 <= n)
			memcpy(&out->face_size, b + es + 64, 4);
	}

	/* these files encode ranges as [base, extra] with extra often 0 (= fixed);
	 * clamp so hi >= lo and the emitter can lerp safely. */
	if (out->speed_hi < out->speed_lo) out->speed_hi = out->speed_lo;
	if (out->life_hi  < out->life_lo)  out->life_hi  = out->life_lo;

	out->valid = true;
	free(b);
	return true;
fail:
	free(b);
	return false;
}
