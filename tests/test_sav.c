/* Round-trip the real SavedGames slots: decode each jj<N>.sav, re-encode it,
 * assert byte-identical, print the decoded fields. Usage: test_sav <SavedGames dir> */
#include "formats/sav.h"

#include <stdio.h>
#include <string.h>

static int roundtrip(const char *path)
{
    sav_slot s;
    if (!sav_read(path, &s)) {
        printf("  %-40s SKIP (missing/short)\n", path);
        return 0;
    }
    const char *tmp = "build/sav_rt.tmp";
    if (!sav_write(tmp, &s)) {
        printf("  %-40s FAIL (re-encode write)\n", path);
        return 1;
    }
    FILE *a = fopen(path, "rb"), *b = fopen(tmp, "rb");
    unsigned char ba[SAV_SIZE], bb[SAV_SIZE];
    int ok = a && b && fread(ba, 1, SAV_SIZE, a) == SAV_SIZE
             && fread(bb, 1, SAV_SIZE, b) == SAV_SIZE
             && memcmp(ba, bb, SAV_SIZE) == 0;
    if (a) fclose(a);
    if (b) fclose(b);
    printf("  %-40s %s  name='%s' lvl=%u hearts=%u score=%u\n",
           path, ok ? "OK " : "FAIL", s.name, s.current_lvl, s.hearts_left,
           s.total_score);
    return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "../game_root/SkippyAdventure/SavedGames";
    int bad = 0;
    for (int i = 0; i < SAV_SLOTS; i++) {
        char p[1024];
        snprintf(p, sizeof p, "%s/jj%d.sav", dir, i);
        bad += roundtrip(p);
    }
    printf("test_sav: %s\n", bad ? "FAILED" : "passed");
    return bad ? 1 : 0;
}
