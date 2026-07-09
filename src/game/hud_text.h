/* HUD/caption text primitives: the NUMBERS.TGA bitmap digit font (timer/counters) and
 * the Font2.tga 16x16 byte-indexed glyph atlas (captions/menu). The atlas textures are
 * per-level assets, so the caller passes them in. align: 0=left, 1=right, 2=centre. */
#ifndef GAME_HUD_TEXT_H
#define GAME_HUD_TEXT_H

#include "render/renderer.h"

/* width in px of numeric string `s` at glyph height gh (matches hud_num's advance). */
float hud_num_width(const char *s, float gh);

/* draw a numeric string from the NUMBERS.TGA atlas `numbers` at glyph height gh. */
void  hud_num(r_tex numbers, const char *s, float x, float y, float gh, r_color tint, int align);

/* draw a byte string in the Font2 atlas `font2` (codepage-agnostic). */
void  font2_text(r_tex font2, const char *s, float x, float y, float gh, r_color tint, int align);

/* menu variant: per-glyph sine bob (the original's warped menu text). */
void  font2_text_wave(r_tex font2, const char *s, float x, float y, float gh,
                      r_color tint, int align, float t, float seed);

/* draw a multi-line caption: each '\n'-separated line is one row, stacked from top_y by lh. */
void  font2_text_lines(r_tex font2, const char *s, float x, float top_y, float gh, float lh,
                       r_color tint, int align);

/* number of '\n'-separated lines in s (>=1). */
int   text_line_count(const char *s);

#endif /* GAME_HUD_TEXT_H */
