/* .hsc highscore table (see hsc.h). */
#include "formats/hsc.h"

#include <stdio.h>
#include <string.h>

#define HSC_KEY 75   /* 'K' — confirmed at the boot call site and the saver */

static void unpack(hsc_table *t, int i)
{
    memcpy(t->rec[i].name, t->raw[i], HSC_NAME);
    t->rec[i].name[HSC_NAME] = 0;
    const uint8_t *r = t->raw[i];
    t->rec[i].score = (uint32_t)r[50] | ((uint32_t)r[51] << 8)
                    | ((uint32_t)r[52] << 16) | ((uint32_t)r[53] << 24);
    t->rec[i].level = r[54];
}

static void pack(hsc_table *t, int i)
{
    uint8_t *r = t->raw[i];
    for (int k = 0; k < HSC_NAME; k++)
        r[k] = (uint8_t)t->rec[i].name[k];
    r[50] = (uint8_t)(t->rec[i].score);
    r[51] = (uint8_t)(t->rec[i].score >> 8);
    r[52] = (uint8_t)(t->rec[i].score >> 16);
    r[53] = (uint8_t)(t->rec[i].score >> 24);
    r[54] = t->rec[i].level;
}

bool hsc_read(const char *path, hsc_table *out)
{
    memset(out, 0, sizeof *out);
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    uint8_t enc[HSC_RECORDS * HSC_REC_SIZE];
    size_t n = fread(enc, 1, sizeof enc, f);
    fclose(f);
    if (n != sizeof enc)
        return false;
    for (int i = 0; i < HSC_RECORDS; i++) {
        for (int k = 0; k < HSC_REC_SIZE; k++)
            out->raw[i][k] = (uint8_t)(enc[i * HSC_REC_SIZE + k] - HSC_KEY);
        unpack(out, i);
    }
    return true;
}

bool hsc_write(const char *path, const hsc_table *t)
{
    hsc_table tmp = *t;
    uint8_t enc[HSC_RECORDS * HSC_REC_SIZE];
    for (int i = 0; i < HSC_RECORDS; i++) {
        pack(&tmp, i);
        for (int k = 0; k < HSC_REC_SIZE; k++)
            enc[i * HSC_REC_SIZE + k] = (uint8_t)(tmp.raw[i][k] + HSC_KEY);
    }
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    bool ok = fwrite(enc, 1, sizeof enc, f) == sizeof enc;
    fclose(f);
    return ok;
}

void hsc_defaults(hsc_table *t)
{
    /* verbatim from highscore_set_defaults (0x41ee30): "Bernie Boulder" x10 with
     * these exact hardcoded score/level pairs. */
    static const struct { uint32_t score; uint8_t level; } D[HSC_RECORDS] = {
        { 10770, 80 }, { 9785, 71 }, { 8750, 64 }, { 7840, 55 }, { 6120, 42 },
        { 5235, 33 },  { 3685, 20 }, { 2785, 15 }, { 1815, 11 }, { 970, 6 },
    };
    memset(t, 0, sizeof *t);
    for (int i = 0; i < HSC_RECORDS; i++) {
        snprintf(t->rec[i].name, sizeof t->rec[i].name, "Bernie Boulder");
        t->rec[i].score = D[i].score;
        t->rec[i].level = D[i].level;
        pack(t, i);
    }
}

int hsc_qualifies(const hsc_table *t, uint32_t score)
{
    for (int i = 0; i < HSC_RECORDS; i++)
        if (score > t->rec[i].score)
            return i;
    return -1;
}

void hsc_insert(hsc_table *t, const char *name, uint32_t score, int level)
{
    int at = hsc_qualifies(t, score);
    if (at < 0)
        return;
    for (int i = HSC_RECORDS - 1; i > at; i--) {
        t->rec[i] = t->rec[i - 1];
        memcpy(t->raw[i], t->raw[i - 1], HSC_REC_SIZE);
    }
    memset(t->raw[at], 0, HSC_REC_SIZE);
    memset(&t->rec[at], 0, sizeof t->rec[at]);
    snprintf(t->rec[at].name, sizeof t->rec[at].name, "%s", name);
    t->rec[at].score = score;
    t->rec[at].level = (uint8_t)level;
    pack(t, at);
}
