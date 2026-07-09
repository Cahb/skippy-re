/* Round-trip the real highscore files byte-identically + exercise defaults/insert.
 * Usage: test_hsc <Highscores dir> */
#include "formats/hsc.h"

#include <stdio.h>
#include <string.h>

static int roundtrip(const char *path)
{
    hsc_table t;
    if (!hsc_read(path, &t)) {
        printf("  %-44s SKIP (missing/short)\n", path);
        return 0;
    }
    const char *tmp = "build/hsc_rt.tmp";
    if (!hsc_write(tmp, &t)) {
        printf("  %-44s FAIL (re-encode write)\n", path);
        return 1;
    }
    FILE *a = fopen(path, "rb"), *b = fopen(tmp, "rb");
    unsigned char ba[HSC_RECORDS * HSC_REC_SIZE], bb[HSC_RECORDS * HSC_REC_SIZE];
    int ok = a && b && fread(ba, 1, sizeof ba, a) == sizeof ba
             && fread(bb, 1, sizeof bb, b) == sizeof bb
             && memcmp(ba, bb, sizeof ba) == 0;
    if (a) fclose(a);
    if (b) fclose(b);
    printf("  %-44s %s  top: '%s' %u @L%u\n", path, ok ? "OK " : "FAIL",
           t.rec[0].name, t.rec[0].score, t.rec[0].level);
    return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "../game_root/SkippyAdventure/highscores";
    static const char *FILES[] = { "JJ.HSC", "hui.hsc", "test.hsc" };
    int bad = 0;
    for (unsigned i = 0; i < sizeof FILES / sizeof FILES[0]; i++) {
        char p[1024];
        snprintf(p, sizeof p, "%s/%s", dir, FILES[i]);
        bad += roundtrip(p);
    }
    /* defaults + qualify/insert sanity */
    hsc_table t;
    hsc_defaults(&t);
    if (t.rec[0].score != 10770 || t.rec[9].score != 970
        || strcmp(t.rec[0].name, "Bernie Boulder") != 0) {
        printf("  defaults FAIL\n");
        bad++;
    }
    if (hsc_qualifies(&t, 970) != -1 || hsc_qualifies(&t, 10771) != 0
        || hsc_qualifies(&t, 5000) != 6) {
        printf("  qualify FAIL\n");
        bad++;
    }
    hsc_insert(&t, "test", 9999, 12);
    if (strcmp(t.rec[1].name, "test") != 0 || t.rec[1].score != 9999
        || t.rec[9].score != 1815) {   /* old #8 slid to the bottom, 970 fell off */
        printf("  insert FAIL\n");
        bad++;
    }
    printf("test_hsc: %s\n", bad ? "FAILED" : "passed");
    return bad ? 1 : 0;
}
