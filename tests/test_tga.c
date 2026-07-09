/* Loads a .tga, prints dims/bpp and a few sample pixels (sanity check). */
#include "formats/tga.h"

#include <stdio.h>

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <path.tga>\n", argv[0]); return 2; }

    tga_image img;
    if (!tga_load(argv[1], &img)) { fprintf(stderr, "FAILED to load %s\n", argv[1]); return 1; }

    printf("== %s ==\n", argv[1]);
    printf("size    : %dx%d\n", img.width, img.height);
    printf("src bpp : %d (%s)\n", img.src_bpp, img.has_alpha ? "RGBA" : "RGB");

    /* sample: top-left, center, bottom-right pixels (top-left origin) */
    int pts[3][2] = {{0,0}, {img.width/2, img.height/2}, {img.width-1, img.height-1}};
    const char *nm[3] = {"top-left", "center", "bot-right"};
    for (int i = 0; i < 3; i++) {
        int x = pts[i][0], y = pts[i][1];
        const uint8_t *p = img.rgba + ((size_t)y * img.width + x) * 4;
        printf("px %-9s (%d,%d): R=%3u G=%3u B=%3u A=%3u\n", nm[i], x, y, p[0], p[1], p[2], p[3]);
    }
    tga_free(&img);
    return 0;
}
