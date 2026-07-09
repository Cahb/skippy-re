/* .CDT CD-track map parser (see cdt.h). Plain ASCII, CRLF line ends. */
#include "formats/cdt.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

static int ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
    return *a == *b;
}

bool cdt_load(const char *path, cdt_map *out)
{
    memset(out, 0, sizeof *out);
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    char line[128];
    while (fgets(line, sizeof line, f) && out->num < CDT_MAX_ENTRIES) {
        int track;
        char name[CDT_NAME];
        if (sscanf(line, "%d %31s", &track, name) != 2)
            continue;
        out->e[out->num].track = track;
        snprintf(out->e[out->num].name, CDT_NAME, "%s", name);
        out->num++;
    }
    fclose(f);
    return out->num > 0;
}

int cdt_track_for(const cdt_map *c, const char *name)
{
    for (int i = 0; i < c->num; i++)
        if (ieq(c->e[i].name, name))
            return c->e[i].track;
    return 0;
}
