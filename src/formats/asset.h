/* Asset path resolver: turn a Windows-relative asset path from the game data
 * (e.g. "models\\k.mdl", "textures\\forest\\energy.tga") into a real path under
 * the game base dir, case-insensitively (data says "models\\k.mdl"; disk has
 * "Models/K.MDL"). Backslash->slash, per-component case-insensitive dir walk. */
#ifndef ASSET_H
#define ASSET_H

#include <stdbool.h>
#include <stddef.h>

/* Resolve `rel` under `base`; write the real filesystem path to out.
 * Returns false if any path component doesn't exist. */
bool asset_resolve(const char *base, const char *rel, char *out, size_t outsz);

#endif /* ASSET_H */
