/* HUD/caption text primitives — see hud_text.h. Moved out of main.c (no logic change);
 * the atlas textures are now passed in rather than read from file-scope globals. */
#include "game/hud_text.h"

#include <string.h>
#include <math.h>

/* ---- NUMBERS.TGA bitmap font: tight per-glyph rects measured from the atlas.
 * yoff = glyph top offset within its 34px cell (so ':' and '.' sit right). ---- */
struct hud_glyph { char ch; short sx, sy, sw, sh, yoff; };
static const struct hud_glyph HUD_GLYPHS[] = {
	{ '0',   1,   5, 29, 34, 0 }, { '1',  37,   5, 21, 34, 0 },
	{ '2',  66,   5, 29, 34, 0 }, { '3',  98,   5, 29, 34, 0 },
	{ '4',   1,  47, 30, 34, 0 }, { '5',  33,  47, 29, 34, 0 },
	{ '6',  66,  47, 28, 34, 0 }, { '7',  99,  47, 28, 34, 0 },
	{ '8',   1,  90, 29, 34, 0 }, { '9',  34,  90, 29, 34, 0 },
	{ ':',  74,  95, 12, 25, 5 }, { '.', 106, 110, 12, 10, 20 },
};
#define HUD_GLYPH_H 34.0f

static const struct hud_glyph *hud_glyph(char c)
{
	for (size_t i = 0; i < sizeof HUD_GLYPHS / sizeof HUD_GLYPHS[0]; i++)
		if (HUD_GLYPHS[i].ch == c)
			return &HUD_GLYPHS[i];
	return NULL;
}

float hud_num_width(const char *s, float gh)
{
	float scale = gh / HUD_GLYPH_H, w = 0.0f, gap = gh * 0.12f;
	for (const char *p = s; *p; p++) {
		const struct hud_glyph *gl = hud_glyph(*p);
		if (gl)
			w += gl->sw * scale + gap;
		else if (*p == ' ')
			w += gh * 0.4f;
	}
	return w > 0.0f ? w - gap : 0.0f;
}

void hud_num(r_tex numbers, const char *s, float x, float y, float gh, r_color tint, int align)
{
	if (numbers < 0)
		return;
	float scale = gh / HUD_GLYPH_H, gap = gh * 0.12f;
	if (align == 1)
		x -= hud_num_width(s, gh);
	else if (align == 2)
		x -= hud_num_width(s, gh) * 0.5f;
	float pen = x;
	for (const char *p = s; *p; p++) {
		const struct hud_glyph *gl = hud_glyph(*p);
		if (!gl) {
			if (*p == ' ')
				pen += gh * 0.4f;
			continue;
		}
		r_draw_sprite(numbers, pen, y + gl->yoff * scale,
		              gl->sw * scale, gl->sh * scale,
		              gl->sx, gl->sy, gl->sw, gl->sh, tint);
		pen += gl->sw * scale + gap;
	}
}

void font2_text(r_tex font2, const char *s, float x, float y, float gh, r_color tint, int align)
{
	if (font2 < 0)
		return;
	float adv = gh * 0.58f;   /* horizontal advance (glyphs sit in wider cells) */
	int n = (int)strlen(s);
	if (align == 1)      x -= n * adv;
	else if (align == 2) x -= n * adv * 0.5f;
	for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
		if (*p > ' ') {   /* space + control chars (newlines) just advance */
			int col = *p & 15, row = (*p >> 4) & 15;
			r_draw_sprite(font2, x, y, gh, gh,
			              col * 16.0f, row * 16.0f, 16.0f, 16.0f, tint);
		}
		x += adv;
	}
}

/* the menus' warped look: each glyph bobs on its own sine (phase by index +
 * a per-row seed so rows bend differently), like the original's menu text. */
void font2_text_wave(r_tex font2, const char *s, float x, float y, float gh,
                     r_color tint, int align, float t, float seed)
{
	if (font2 < 0)
		return;
	float adv = gh * 0.58f;
	int n = (int)strlen(s);
	if (align == 1)      x -= n * adv;
	else if (align == 2) x -= n * adv * 0.5f;
	int i = 0;
	for (const unsigned char *p = (const unsigned char *)s; *p; p++, i++) {
		if (*p > ' ') {
			int col = *p & 15, row = (*p >> 4) & 15;
			float wy = y + sinf(t * 2.2f + (float)i * 0.55f + seed) * gh * 0.09f;
			r_draw_sprite(font2, x, wy, gh, gh,
			              col * 16.0f, row * 16.0f, 16.0f, 16.0f, tint);
		}
		x += adv;
	}
}

int text_line_count(const char *s)
{
	int lines = 1;
	for (const char *p = s; *p; p++)
		if (*p == '\n')
			lines++;
	return lines;
}

void font2_text_lines(r_tex font2, const char *s, float x, float top_y, float gh, float lh,
                      r_color tint, int align)
{
	char line[512];
	float y = top_y;
	const char *p = s;
	while (*p) {
		const char *nl = strchr(p, '\n');
		size_t len = nl ? (size_t)(nl - p) : strlen(p);
		if (len > 0 && p[len - 1] == '\r')   /* CRLF: drop the CR */
			len--;
		if (len >= sizeof line)
			len = sizeof line - 1;
		memcpy(line, p, len);
		line[len] = 0;
		font2_text(font2, line, x, y, gh, tint, align);
		y += lh;
		if (!nl)
			break;
		p = nl + 1;
	}
}
