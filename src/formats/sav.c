/* .sav savegame slots (see sav.h). */
#include "formats/sav.h"

#include <stdio.h>
#include <string.h>

bool sav_read(const char *path, sav_slot *out)
{
    memset(out, 0, sizeof *out);
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    uint8_t enc[SAV_SIZE];
    size_t n = fread(enc, 1, SAV_SIZE, f);
    fclose(f);
    if (n != SAV_SIZE)
        return false;
    for (int i = 0; i < SAV_SIZE; i++)
        out->raw[i] = (uint8_t)(enc[i] - 55);
    memcpy(out->name, out->raw, SAV_NAME);
    out->name[SAV_NAME] = 0;
    out->current_lvl = out->raw[14];
    out->hearts_left = out->raw[15];
    out->total_score = (uint16_t)(out->raw[16] | (out->raw[17] << 8));
    return true;
}

bool sav_write(const char *path, const sav_slot *s)
{
    uint8_t img[SAV_SIZE];
    memcpy(img, s->raw, SAV_SIZE);          /* pad bytes carried over verbatim */
    for (int i = 0; i < SAV_NAME; i++)
        img[i] = (uint8_t)s->name[i];
    img[14] = s->current_lvl;
    img[15] = s->hearts_left;
    img[16] = (uint8_t)(s->total_score & 0xff);
    img[17] = (uint8_t)(s->total_score >> 8);
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    uint8_t enc[SAV_SIZE];
    for (int i = 0; i < SAV_SIZE; i++)
        enc[i] = (uint8_t)(img[i] + 55);
    bool ok = fwrite(enc, 1, SAV_SIZE, f) == SAV_SIZE;
    fclose(f);
    return ok;
}
