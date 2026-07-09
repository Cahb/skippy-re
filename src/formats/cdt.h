/* CDTracks/JJ.CDT: the CD-audio track map. One "<track#> <name>" per CRLF line,
 * e.g. "3<TAB>Forest" — names are theme worlds plus the Main/GameOver/Completed
 * states. Track 0 = no music (GameOver/Completed ship as 0 on this disc). */
#ifndef CDT_H
#define CDT_H

#include <stdbool.h>

#define CDT_MAX_ENTRIES 16
#define CDT_NAME 32

typedef struct {
    int num;
    struct { int track; char name[CDT_NAME]; } e[CDT_MAX_ENTRIES];
} cdt_map;

bool cdt_load(const char *path, cdt_map *out);

/* track number for a name (case-insensitive), or 0 = no music / unknown. */
int cdt_track_for(const cdt_map *c, const char *name);

#endif /* CDT_H */
