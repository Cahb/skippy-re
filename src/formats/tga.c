#include "formats/tga.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* read one source pixel (BGR or BGRA) -> RGBA into dst[4] */
static void px_to_rgba(const uint8_t *src, int bpp, uint8_t *dst) {
    dst[0] = src[2];                    /* R <- B-order byte 2 */
    dst[1] = src[1];                    /* G */
    dst[2] = src[0];                    /* B <- byte 0 */
    dst[3] = (bpp == 32) ? src[3] : 255;/* A */
}

bool tga_load(const char *path, tga_image *out) {
    memset(out, 0, sizeof *out);

    FILE *f = fopen(path, "rb");
    if (!f) return false;

    uint8_t h[18];
    if (fread(h, 1, 18, f) != 18) { fclose(f); return false; }

    uint8_t id_len       = h[0];
    uint8_t cmap_type    = h[1];
    uint8_t image_type   = h[2];
    int     width        = h[12] | (h[13] << 8);
    int     height       = h[14] | (h[15] << 8);
    uint8_t bpp          = h[16];
    uint8_t descriptor   = h[17];
    bool    top_origin   = (descriptor & 0x20) != 0;

    /* only truecolor uncompressed(2)/RLE(10), 24/32 bpp, no color map */
    if (cmap_type != 0 || (image_type != 2 && image_type != 10) || (bpp != 24 && bpp != 32) ||
        width <= 0 || height <= 0 || width > 8192 || height > 8192) {
        fclose(f); return false;
    }
    if (id_len && fseek(f, id_len, SEEK_CUR) != 0) { fclose(f); return false; }

    int bytespp = bpp / 8;
    size_t npix = (size_t)width * height;
    uint8_t *rgba = malloc(npix * 4);
    if (!rgba) { fclose(f); return false; }

    bool ok = true;
    if (image_type == 2) {                       /* uncompressed */
        uint8_t px[4];
        for (size_t i = 0; i < npix && ok; i++) {
            if (fread(px, 1, bytespp, f) != (size_t)bytespp) { ok = false; break; }
            px_to_rgba(px, bpp, rgba + i * 4);
        }
    } else {                                     /* RLE (type 10) */
        size_t i = 0;
        while (i < npix && ok) {
            int hdr = fgetc(f);
            if (hdr < 0) { ok = false; break; }
            int count = (hdr & 0x7f) + 1;
            uint8_t px[4];
            if (hdr & 0x80) {                    /* run packet: 1 pixel repeated */
                if (fread(px, 1, bytespp, f) != (size_t)bytespp) { ok = false; break; }
                for (int k = 0; k < count && i < npix; k++, i++) px_to_rgba(px, bpp, rgba + i * 4);
            } else {                             /* raw packet: count distinct pixels */
                for (int k = 0; k < count && i < npix; k++, i++) {
                    if (fread(px, 1, bytespp, f) != (size_t)bytespp) { ok = false; break; }
                    px_to_rgba(px, bpp, rgba + i * 4);
                }
            }
        }
    }
    fclose(f);
    if (!ok) { free(rgba); return false; }

    /* TGA default origin is bottom-left; flip to top-left unless descriptor says top */
    if (!top_origin) {
        size_t row = (size_t)width * 4;
        uint8_t *tmp = malloc(row);
        if (tmp) {
            for (int y = 0; y < height / 2; y++) {
                uint8_t *a = rgba + (size_t)y * row;
                uint8_t *b = rgba + (size_t)(height - 1 - y) * row;
                memcpy(tmp, a, row); memcpy(a, b, row); memcpy(b, tmp, row);
            }
            free(tmp);
        }
    }

    out->width = width; out->height = height; out->src_bpp = bpp;
    out->has_alpha = (bpp == 32); out->rgba = rgba;
    return true;
}

void tga_free(tga_image *img) {
    if (!img) return;
    free(img->rgba);
    memset(img, 0, sizeof *img);
}
