/* Load-once asset caches + theme loaders — see assets.h. Moved verbatim out of main.c
 * (no logic change); caches are now file-static here, cleared via assets_reset_caches(). */
#include "game/assets.h"

#include "formats/mdl.h"
#include "formats/tga.h"
#include "formats/asset.h"
#include "formats/leo.h"   /* LEO_PATH */

#include <string.h>
#include <strings.h>   /* strcasecmp */
#include <stdio.h>
#include <stdint.h>

/* tiny load-once caches (the .leo reuses platte/bush meshes many times) */
static char   g_mcache_path[64][LEO_PATH];
static r_mesh g_mcache[64];
static r_vec3 g_mcache_bmin[64], g_mcache_bmax[64];   /* frame-0 model bbox */
static int    g_mcache_n;
static char   g_tcache_path[64][LEO_PATH];
static r_tex  g_tcache[64];
static int    g_tcache_n;
/* anim-mesh cache (dedup by .mdl path so N identical props share one upload) */
static char   g_amc_path[16][LEO_PATH];
static r_mesh g_amc_mesh[16];
static int    g_amc_nf[16], g_amc_n;

void assets_reset_caches(void)
{
	g_mcache_n = g_tcache_n = g_amc_n = 0;
}

r_mesh mesh_cached(const char *base, const char *rel)
{
	for (int i = 0; i < g_mcache_n; i++)
		if (strcmp(g_mcache_path[i], rel) == 0)
			return g_mcache[i];
	r_mesh m = -1;
	r_vec3 bmn = {0}, bmx = {0};
	char pth[1024];
	mdl_model mdl;
	if (asset_resolve(base, rel, pth, sizeof pth) && mdl_load(pth, &mdl)) {
		m = r_upload_mesh(mdl_frame(&mdl, 0), (int)mdl.num_verts);
		bmn = (r_vec3){ mdl.frame_hdrs[0].bbox_min[0], mdl.frame_hdrs[0].bbox_min[1], mdl.frame_hdrs[0].bbox_min[2] };
		bmx = (r_vec3){ mdl.frame_hdrs[0].bbox_max[0], mdl.frame_hdrs[0].bbox_max[1], mdl.frame_hdrs[0].bbox_max[2] };
		mdl_free(&mdl);
	}
	if (g_mcache_n < 64) {
		snprintf(g_mcache_path[g_mcache_n], LEO_PATH, "%s", rel);
		g_mcache_bmin[g_mcache_n] = bmn;
		g_mcache_bmax[g_mcache_n] = bmx;
		g_mcache[g_mcache_n++] = m;
	}
	return m;
}

/* look up a cached mesh's frame-0 bbox by path (must be mesh_cached first) */
bool mesh_bbox(const char *rel, r_vec3 *mn, r_vec3 *mx)
{
	for (int i = 0; i < g_mcache_n; i++)
		if (strcmp(g_mcache_path[i], rel) == 0) {
			*mn = g_mcache_bmin[i];
			*mx = g_mcache_bmax[i];
			return true;
		}
	return false;
}

r_tex tex_cached(const char *base, const char *rel)
{
	if (!rel[0])
		return -1;
	for (int i = 0; i < g_tcache_n; i++)
		if (strcmp(g_tcache_path[i], rel) == 0)
			return g_tcache[i];
	r_tex t = -1;
	char pth[1024];
	tga_image im;
	if (asset_resolve(base, rel, pth, sizeof pth) && tga_load(pth, &im)) {
		t = r_load_texture_rgba(im.rgba, im.width, im.height);
		tga_free(&im);
	}
	if (g_tcache_n < 64) {
		snprintf(g_tcache_path[g_tcache_n], LEO_PATH, "%s", rel);
		g_tcache[g_tcache_n++] = t;
	}
	return t;
}

/* one-shot (uncached) texture load: resolve `rel` under base, upload, free. NULL/"" -> -1.
 * For the many one-off HUD/theme textures that don't need the mesh/tex path caches. */
r_tex load_tex_rel(const char *base, const char *rel)
{
	if (!rel || !rel[0])
		return -1;
	char p[1024];
	tga_image im;
	if (!(asset_resolve(base, rel, p, sizeof p) && tga_load(p, &im)))
		return -1;
	r_tex t = r_load_texture_rgba(im.rgba, im.width, im.height);
	tga_free(&im);
	return t;
}

/* like load_tex_rel but sets alpha = luminance first (black-bg additive sprites/glows). */
r_tex load_tex_rel_alpha(const char *base, const char *rel)
{
	if (!rel || !rel[0])
		return -1;
	char p[1024];
	tga_image im;
	if (!(asset_resolve(base, rel, p, sizeof p) && tga_load(p, &im)))
		return -1;
	/* Respect a real alpha channel (32-bpp TGAs, e.g. the teleporter's RAHMEN frame:
	 * opaque vines + transparent centre = a circular mask). Only 24-bpp textures with no
	 * alpha get a synthesized luminance key (additive glows: star/flare on black). */
	if (!im.has_alpha)
		alpha_from_luma(im.rgba, im.width, im.height);
	r_tex t = r_load_texture_rgba(im.rgba, im.width, im.height);
	tga_free(&im);
	return t;
}

/* load the first Model mesh (frame 0) + first texture of theme object `slot` */
void load_slot_model(const char *b, const thm_theme *th, int slot, r_mesh *mesh, r_tex *tex)
{
	*mesh = -1;
	*tex = -1;
	char path[1024];
	tga_image im;
	for (int o = 0; o < th->num_objects; o++) {
		if (th->objects[o].slot != slot)
			continue;
		const thm_object *ob = &th->objects[o];
		for (int mi = 0; mi < ob->num_meshes; mi++) {
			const thm_mesh *tm = &ob->meshes[mi];
			if (!tm->mesh[0])
				continue;
			mdl_model mdl;
			if (asset_resolve(b, tm->mesh, path, sizeof path) && mdl_load(path, &mdl)) {
				*mesh = r_upload_mesh(mdl_frame(&mdl, 0), (int)mdl.num_verts);
				mdl_free(&mdl);
			}
			if (tm->num_tex > 0 && tm->tex[0].tga[0]
			    && asset_resolve(b, tm->tex[0].tga, path, sizeof path) && tga_load(path, &im)) {
				*tex = r_load_texture_rgba(im.rgba, im.width, im.height);
				tga_free(&im);
			}
			return;
		}
		return;
	}
}

r_mesh leo_anim_mesh(const char *b, const char *rel, int *nframes)
{
	for (int i = 0; i < g_amc_n; i++)
		if (strcmp(g_amc_path[i], rel) == 0) { *nframes = g_amc_nf[i]; return g_amc_mesh[i]; }
	r_mesh m = -1; int nf = 0;
	char pth[1024];
	mdl_model mdl;
	if (asset_resolve(b, rel, pth, sizeof pth) && mdl_load(pth, &mdl)) {
		m = r_upload_anim(mdl.verts, (int)mdl.num_verts, mdl.num_frames);
		nf = (int)mdl.num_frames;
		mdl_free(&mdl);
	}
	if (g_amc_n < 16) {
		snprintf(g_amc_path[g_amc_n], LEO_PATH, "%s", rel);
		g_amc_mesh[g_amc_n] = m; g_amc_nf[g_amc_n] = nf; g_amc_n++;
	}
	*nframes = nf;
	return m;
}

/* additive (SrcBlend One) glow textures of a theme object slot, in order.
 * The exit (slot 11) lists them as gl01 (X-corner dots) ... gl02 (circle ring),
 * so *first = the corner glow and *last = the ring (NULL if only one/none). */
void thm_slot_glow_texes(const thm_theme *th, int slot, const char **first, const char **last)
{
	*first = *last = NULL;
	for (int o = 0; o < th->num_objects; o++) {
		if (th->objects[o].slot != slot)
			continue;
		for (int m = 0; m < th->objects[o].num_meshes; m++)
			for (int t = 0; t < th->objects[o].meshes[m].num_tex; t++) {
				const thm_texture *tx = &th->objects[o].meshes[m].tex[t];
				if (strcasecmp(tx->src_blend, "one") != 0 || !tx->tga[0])
					continue;
				if (!*first)
					*first = tx->tga;
				else if (strcmp(tx->tga, *first) != 0)
					*last = tx->tga;   /* a DISTINCT later additive = the ring */
			}
	}
}

/* give a black-background sprite (flare/star glow) an alpha = luminance, so its
 * black surround is transparent — no visible quad box, in any blend mode. */
void alpha_from_luma(uint8_t *rgba, int w, int h)
{
	for (int i = 0; i < w * h; i++) {
		uint8_t *px = rgba + i * 4;
		int l = px[0] > px[1] ? px[0] : px[1];
		if (px[2] > l) l = px[2];
		px[3] = (uint8_t)l;
	}
}

/* Load a particle texture the way the engine does: RGB from `rel` (e.g. RED.TGA,
 * Blow.tga) with its alpha taken from the separate companion map `<stem>_a.tga`
 * (or `<stem>a.tga`) — the game ships a paired grayscale alpha per sprite. If no
 * companion exists, fall back to alpha=luminance (black-bg additive glow). -1 on
 * failure. */
r_tex load_particle_tex(const char *base, const char *rel)
{
	char p[1024];
	tga_image im;
	if (!(asset_resolve(base, rel, p, sizeof p) && tga_load(p, &im)))
		return -1;

	const char *dot = strrchr(rel, '.');
	int stem = dot ? (int)(dot - rel) : (int)strlen(rel);
	const char *suf[] = { "_a.tga", "a.tga" };
	bool got_alpha = im.has_alpha;   /* a real 32-bpp alpha channel wins over any synthesis */
	for (int s = 0; s < 2 && !got_alpha; s++) {
		char cand[1024];
		tga_image am;
		snprintf(cand, sizeof cand, "%.*s%s", stem, rel, suf[s]);
		if (asset_resolve(base, cand, p, sizeof p) && tga_load(p, &am)) {
			if (am.width == im.width && am.height == im.height) {
				/* the companion carries the mask in its ALPHA channel (RGB left
				 * white, e.g. Blow_a); a grayscale companion would use luminance. */
				for (int i = 0; i < im.width * im.height; i++)
					im.rgba[i * 4 + 3] = am.has_alpha ? am.rgba[i * 4 + 3] : am.rgba[i * 4];
				got_alpha = true;
			}
			tga_free(&am);
		}
	}
	if (!got_alpha)
		alpha_from_luma(im.rgba, im.width, im.height);

	r_tex t = r_load_texture_rgba(im.rgba, im.width, im.height);
	tga_free(&im);
	return t;
}

/* dominant colour of an image (average of its non-dark pixels, boosted so the
 * brightest channel hits 255) — a vivid theme tint for the gem glow + sparks:
 * green in Forest, whatever the Castle/etc. gem texture is. `fallback` on empty. */
r_color dominant_color(const tga_image *im, r_color fallback)
{
	long sr = 0, sg = 0, sb = 0, cnt = 0;
	for (int i = 0; i < im->width * im->height; i++) {
		const uint8_t *px = im->rgba + i * 4;
		if (px[0] + px[1] + px[2] < 60)   /* skip near-black outline/background */
			continue;
		sr += px[0]; sg += px[1]; sb += px[2]; cnt++;
	}
	if (!cnt)
		return fallback;
	float ar = (float)sr / cnt, ag = (float)sg / cnt, ab = (float)sb / cnt;
	float m = ar > ag ? ar : ag;
	if (ab > m) m = ab;
	if (m < 1.0f)
		return fallback;
	float k = 255.0f / m;
	return (r_color){ (uint8_t)(ar * k), (uint8_t)(ag * k), (uint8_t)(ab * k), 255 };
}

/* load the .wav mapped to a thm "Sound<event>" entry (case-insensitive), or -1. */
r_sound load_theme_sound(const char *base, const thm_theme *th, const char *event)
{
	for (int i = 0; i < th->num_sounds; i++)
		if (strcasecmp(th->sounds[i].event, event) == 0 && th->sounds[i].wav[0]) {
			char pth[1024];
			if (asset_resolve(base, th->sounds[i].wav, pth, sizeof pth))
				return r_load_sound(pth);
			return -1;
		}
	return -1;
}

/* same lookup, loaded as a seamlessly-looping stream (sustained state loops:
 * slides, movers — the original plays these DSBPLAY_LOOPING). */
r_music load_theme_music(const char *base, const thm_theme *th, const char *event)
{
	for (int i = 0; i < th->num_sounds; i++)
		if (strcasecmp(th->sounds[i].event, event) == 0 && th->sounds[i].wav[0]) {
			char pth[1024];
			if (asset_resolve(base, th->sounds[i].wav, pth, sizeof pth))
				return r_load_music(pth);
			return -1;
		}
	return -1;
}
