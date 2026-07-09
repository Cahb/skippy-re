#define _POSIX_C_SOURCE 200809L
#include "formats/asset.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>   /* strcasecmp */
#include <dirent.h>

/* find a directory entry in `dir` matching `name` case-insensitively */
static bool ci_find(const char *dir, const char *name, char *found, size_t fsz) {
    DIR *d = opendir(dir);
    if (!d) return false;
    bool ok = false;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcasecmp(e->d_name, name) == 0) { snprintf(found, fsz, "%s", e->d_name); ok = true; break; }
    }
    closedir(d);
    return ok;
}

bool asset_resolve(const char *base, const char *rel, char *out, size_t outsz) {
    char work[1024];
    snprintf(work, sizeof work, "%s", rel);
    for (char *p = work; *p; p++) if (*p == '\\') *p = '/';   /* backslash -> slash */

    char cur[1024];
    snprintf(cur, sizeof cur, "%s", base);

    char *save = NULL;
    for (char *tok = strtok_r(work, "/", &save); tok; tok = strtok_r(NULL, "/", &save)) {
        if (!*tok || strcmp(tok, ".") == 0) continue;
        char found[512];
        if (!ci_find(cur, tok, found, sizeof found)) return false;
        size_t l = strlen(cur);
        snprintf(cur + l, sizeof cur - l, "/%s", found);
    }

    snprintf(out, outsz, "%s", cur);
    return true;
}
