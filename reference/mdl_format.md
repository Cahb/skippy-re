# .mdl model format — load_model @ 0x437bc0

Player and enemies are vertex models (.mdl). Confirmed vs found_structs
skippy_mdl_struct/skippy_mdl_verticy_struct; richer than AssetParser::LoadModel
(which only read a single frame).

## File layout
```
[0..1]   num_frames  (WORD)   -> mdl.num_of_anims (+0x10)
[2..5]   num_verts   (DWORD)  -> mdl.num_of_verticies (+0x08)
[6..?]   (rest of 30-byte header — not consumed by fields here)
per frame f in 0..num_frames:
   24 bytes (6 dwords)  -> anim_hdrs[f]                  (anim header / bbox / transform?)
   num_verts * 40 bytes -> anim_buf[f*num_verts + v]     (this frame's vertices)
<trailing>  path string -> strdup -> mdl.mdl_name (+0x12)
```
Vertex = 40 bytes (0x28) = 10 floats:
`{x, y, z, nx, ny, nz, u, v, unk1, unk2}` — last two floats are the previously
"missing" fields (likely a 2nd UV or blend/tangent data).

## skippy_mdl_struct offsets (confirmed)
| off | field |
|-----|-------|
| 0x00 | vtable |
| 0x04 | anim_buf  (frames*verts*40) — all animation frames |
| 0x08 | num_of_verticies (DWORD) |
| 0x0c | anim_hdrs (frames*24) |
| 0x10 | num_of_anims (WORD = frame count) |
| 0x12 | mdl_name (char*) |
| 0x76 | main_mdl_buf (verts*40) — single working frame |

## Notes
- AssetParser::LoadModel is oversimplified: it ignores the frame count and the
  24-byte per-frame headers. To parse real multi-frame models, follow the layout
  above.
- GUI retype: load_model → Custom Storage → this → skippy_mdl_struct *
