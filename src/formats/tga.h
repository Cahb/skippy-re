/* .tga (Targa) texture loader. Decodes uncompressed (type 2) and RLE (type 10)
 * truecolor 24/32-bpp into top-left-origin RGBA8. The game's textures are TGA
 * (load_texture @0x43f770). Colormapped/grayscale types are rejected for now. */
#ifndef TGA_H
#define TGA_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int      width, height;
    int      src_bpp;   /* 24 or 32 (source), informational */
    bool     has_alpha; /* true if source was 32-bpp */
    uint8_t *rgba;      /* width*height*4, top-left origin, R,G,B,A */
} tga_image;

bool tga_load(const char *path, tga_image *out);
void tga_free(tga_image *img);

#endif /* TGA_H */
