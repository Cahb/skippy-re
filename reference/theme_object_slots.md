# Theme object-slot map (authoritative, from the parser)

`skippy_theme_manager_obj` holds a **contiguous array of 38 object-theme blocks**, each
**0x2ef0 (12016) bytes**, starting at **theme_mgr + 0x104**. The slot index is a fixed
object enum; the theme parser (`game_load_theme_gamefiles` 0x40c110) dispatches each `.thm`
object keyword to its slot via a lowercased inline-strcmp chain
(`mov eax,[theme_mgr]; add eax, 0x104 + idx*0x2ef0`).

> Extraction W/A: the parser is ~5k decompiled lines (times out over MCP). Instead:
> `objdump -d` the function range locally + a python scan keyed on the
> `mov eax,[base]; add eax,<off>` store after each object-name strcmp. `slot = (off-0x104)/0x2ef0`.
> 37/38 confirmed by matching the English lowercase literal; slot 22 = bomb by structure
> (its keyword is localized to CP1251 "бомбить" in this RUS build).

## Slot → object (byte offset = 0x104 + slot*0x2ef0)
| slot | object | | slot | object | | slot | object |
|------|--------|-|------|--------|-|------|--------|
| 0 | john (player) | | 13 | destructfield | | 26 | speed |
| 1 | catcher | | 14 | destructfieldfx | | 27 | speedfx |
| 2 | catcherfx | | 15 | jumppad | | 28 | collfx |
| 3 | thrower | | 16 | slide | | 29 | life |
| 4 | throwerfx | | 17 | stair | | 30 | switch |
| 5 | plate | | 18 | teleporter | | 31 | time |
| 6 | side | | 19 | crystal | | 32 | ice |
| 7 | platform | | 20 | crystalfx | | 33 | obstacle |
| 8 | paraglide | | 21 | ammunition | | 34 | obstaclefx |
| 9 | paraglidefx | | 22 | **bomb** | | 35 | protection |
| 10 | elevator | | 23 | explosion | | 36 | protectionfx |
| 11 | exit | | 24 | surprise | | 37 | bridge |
| 12 | glue | | 25 | freeze | | | |

## Notes for the rewrite / render
- **Enemies = slots 1–4** (catcher, catcherfx, thrower, throwerfx) — matches the earlier
  draw-site observation "enemies themes[1..4]". Player = slot 0.
- Each slot's 0x2ef0-byte block holds that object's theme assets (model+anim+textures per the
  `.thm` grammar in theme_format.md); `main_render_func` draws an entity/tile by indexing here.
- CORRECTION to theme_format.md's speculative "player poses themes[9]/[36]=Paraglide":
  paraglide is **slot 8** (paraglidefx=9); slot 36 is actually protectionfx. The parser map above
  is authoritative (the draw-site guess was wrong).
- FX pairs (catcherfx, throwerfx, crystalfx, speedfx, paraglidefx, obstaclefx, protectionfx,
  destructfieldfx, collfx) = particle/effect companions to their base object.
- The `.thm` object keyword ORDER in the file ≠ this slot order (slots are the fixed engine enum,
  same lesson as the .ani clip slots).

## Inside a theme_object block (0x2ef0) — skeleton [partial, verified]
Each object block is an **array of ~8 sub-mesh/model-config slots, 0x5DD (1501) bytes each**
(`submesh_base = obj_base + idx*0x5DD`; 8*0x5DD = 0x2EE8, +8 trailer). The `.thm` `Model {…}`
sub-directive fills the next slot; `NoMoveStates` sets its free-run flag. Verified sub-mesh fields:
| submesh off | field | from |
|-------------|-------|------|
| +0x08 | active/valid flag (1) | Model handler |
| +0x0c | model handle | `load_model` result stored here |
| +0xc9 | **.ani clip table** (24 × 0x10 = 0x180) | `game_load_animation_file_gamefiles` writes here; == the "cfg+0xc9" the anim pass saw |
| +0x249 | geometry/model pointers (per anim pass) | |
| +0x5b1 | NoMoveStates free-run flag (1) | forces walk_forward loop off wallclock (anim pass "cfg+0x5b1") |
So every entity's anim clip table = `theme_mgr + 0x104 + obj*0x2ef0 + submesh*0x5DD + 0xc9`.
DONE (deeper): full field layout folded into the EXISTING referenced structs in
found_structs_ghidra.h — `skippy_theme_manager_theme_cfg_struct` (a sub-mesh, **0x5DD** — the
old 0x5DC was an off-by-one, corrected from parser ASM), `skippy_theme_manager_theme_struct`
(= object block, `cfg[8]`, 0x2ef0), with new element types `theme_texture`(0x3C, 8/cfg @+0x3cd)
and `theme_ani_clip`(0x10, 24/cfg @+0xc9). All reachable from `skippy_theme_manager_struct.themes[38]`. Sub-mesh carries type (Model/Field/Billboard/
ParticleSystem), model handle, anim_clips[24], transform (position/scale/rotate), the texture
array, and submesh FX flags (Lit/NoMoveStates/NoZWrite/NoShadow/Specular/RandomYAngle/Oscillate/
Pump). Each texture carries Condition, tga handle, Src/DestBlend (D3DBLEND), TextureAdress mode, and
an FX-anim type (flash/pulse/turn/wobble/environment/scroll) + up to 3 float params.
Caveats (comments in the header): submesh_count @obj+0x4 aliases submesh[0]'s head; Pump's last 2
floats overflow into the next slot's unused head — both benign.

## Still separate (not in this array)
- **Sound events** (MoveJJ … ExplosionCatcher) are parsed in a different section of the parser
  with their own slotting — see claude/sound_events.md (mapping TBD).
- **Environment** directives (HUD/Fog/Sky/menu colors/InverseControl/SideHeight…) store to
  discrete theme_mgr fields, not this object array.
