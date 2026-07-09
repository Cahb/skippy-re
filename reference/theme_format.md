# .thm theme format (text config)

`themes\<world>.thm`, selected by the .jjm's `level_manager.world` field. Plain
UTF-8 text (German-authored). Parsed by `game_load_theme_gamefiles` @0x40c110 into
`skippy_theme_manager_obj` (global @0x46c890) -> themes[] slots. The parser is
~1400 lines but NOT worth dissecting — the .thm files ARE the schema.

## Load flow (small orchestrator: `load_level_assets` @0x426c50)
1. Builds `themes\<world>.thm` + a per-world loading screen `bitmaps\<world>.bmp`
   (`load_bitmap` -> `FUN_00425fc0` blit).
2. **Lazy**: `strcmp`s the new theme name vs `&skippy_theme_manager_obj` (its FIRST field =
   currently-loaded theme name, the cache key). Only on a world change does it call
   `game_load_theme_gamefiles(main, d3d, &theme_mgr, path, logger)` (0x40c110, the big parser).
3. `level_resources_load_level(d3d, &game_main_struct.offs_48b98, logger)` (0x420d38) loads the
   per-level resource block -> **confirms the ~1 MB unnamed submodule @god+0x48b98 = "LevelResources"**.
4. Positions the camera from `level_dim_x/Y` + `.rdata` consts; zeroes the `DAT_004e01a0[9]` anim-phase array.

## Grammar
```
ObjectType {
  Model <mesh.mdl> <anim.ani> [NoMoveStates ...] {
    [NoZWrite]
    Texture <tex.tga> {
      Condition Alive|Dead
      [SrcBlend One] [DestBlend One]
    }
    ... more textures (condition-gated) ...
  }
  ... more sub-meshes ...
}
```
An object can have multiple sub-meshes (e.g. player John = body k.mdl + head
kopf.mdl + wings fluegel.mdl), each with textures selected by Condition + blend.

## Environment section
- HUD / Menu / Edge / Pointer / Radar / bonus-circle textures (.tga, `alpha` flag)
- `HUDTextColors` + many `Menu*TextColors` = hex RGB pairs (fg, hi)
- `Fog  exp2|linear <density> <RRGGBBAA>`
- `Sky  textures\<world>\sky\<base>`  -> 6 faces `<base>_LF/UP/DN/FR/RT/BK.tga` (see render_skybox)
- `SideHeight <f>`
- `Sound <event> <wav>` (MoveJJ, MoveCatcher, MoveThrower, ...)

## Object types (each maps to a themes[] slot used by main_render_func)
John (player), Catcher (enemy), CatcherFX, Thrower, Plate, Switch, Exit, Glue,
DestructField(+FX), JumpPad, Teleporter, Slide, Stair, Side, Elevator, Platform,
Crystal(+FX), Ammunition, Surprise, Freeze, Speed(+FX), CollFX, Life, Time, Ice,
Bomb, Explosion, Protection(+FX), Paraglide.

These are exactly the object categories main_render_func draws via
skippy_theme_manager_obj's object array. **The authoritative name->slot map is now
extracted — see claude/theme_object_slots.md** (38 slots x 0x2ef0 bytes @ theme_mgr+0x104:
john=0, enemies catcher/fx+thrower/fx=1..4, paraglide=8, elevator=10, jumppad=15, bomb=22,
bridge=37, ...). Pulled from the parser's strcmp dispatch via local objdump (the function is
~5k lines / times out over MCP, so it was scanned as raw asm, not decompiled).
NOTE: this CORRECTS the old draw-site guess "themes[9]/[36]=Paraglide" — paraglide is slot 8.

## Sound events (see claude/sound_events.md)
The `Sound <event> <wav>` lines define the 26-event gameplay-action taxonomy
(MoveJJ/Catcher/Thrower, IceSliding/Sliding/Paragliding/JumpPad, Teleporter/
Elevator/Platform/Switch/Glue/Bridge, Destruct Start+Regen, Crystal, Splat*/Fall*
deaths, BombTick/Explosion*). Names = authoritative gameplay events; play via
sound_play(0x442900)/sound_stop(0x4429a0), gated per-effect by skippy_catcher.pad5[].

## Asset types pulled together by a theme
- .mdl (+ .ani) meshes  -> load_model @0x437bc0 (see mdl_format.md)
- .tga textures         -> load_texture @0x43f770
- .wav sounds, skybox tga set, HUD/menu bitmaps
