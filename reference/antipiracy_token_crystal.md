# Anti-piracy: the "token crystal"

A hidden copy-protection in `skippy_game_start_level` (0x416420). If the original
CD isn't in the drive, the game silently removes one crystal from levels past #5,
so you can never collect the required count and get stuck — no warning, no nag.

## Mechanism (from the decompile)
```c
if (cdm_check_original_cd_in_drive() == 0        // no CD
    && this[0xc] == 0                            // real build (not the exempt/demo flag)
    && this->current_level(0x173583) > 4) {      // only levels 6+ (early levels play fine)
    x = player_start_x (0x17531c); y = player_start_y (0x17531d);
    if (find_cell_of_type(&x, &y, 0x14)) {       // locate the 'token' crystal near start
        tiles[x][y].pickup (@ +0x2ab72c) = 0;    // REMOVE it
        log("GAME: CD is not in drive! Crystal at %d,%d token!", x, y);
    }
}
```
The developer's own log string calls it a **"token"** crystal.

## Why it was hard to beat at the time
- No visible message — the level just becomes uncompletable.
- Forums suggested lowering the required count (`crystals_needed` @ level_manager
  +0x2ab723), which *works around* it but isn't the real fix.
- The **correct** fix: put the crystal back — set that cell's `pickup_type` byte
  (`tiles[x][y]` +3) to `1`. Nobody knew where it was removed.

## Related
- `cdm_check_original_cd_in_drive` (0x403420) — the CD presence check.
- `find_cell_of_type` (0x41b810) — grid search for a cell (also used by enemy AI seek).
- crystal count `0x42252`, needed `0x2ab723`, tile pickup byte `tiles[x][y]+3`.
