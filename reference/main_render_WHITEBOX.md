# `main_render_func` — Whitebox Re-derivation (ASM-only)

**Goal:** re-map the function from the *raw binary* — not the IDA pseudo-C — and
say explicitly where IDA can and cannot be trusted. Everything below is derived
from `objdump` output + reading bytes out of `RUS_Karoo.exe` directly.

## Artifacts (in `claude/disasm/`)
| File | What |
|------|------|
| `full_text.asm` | full `.text` disassembly, Intel syntax (119,565 lines) |
| `main_render_func.asm` | just our function, `0x426f50`–`0x42cd58` (6,467 lines) |
| `iat_map.txt` | import-table map: IAT VMA → `DLL!Api` (152 entries) |

## Binary facts (objdump `-h`/`-p`)
- PE32, i386, ImageBase `0x00400000`, 4 sections.
- `.text`: VMA `0x401000`, size `0x5bbfa` (file off `0x1000`).
- `.rdata`: VMA `0x45d000` (file off `0x5d000`) — holds the IAT + float constants.
- `.data`: VMA `0x464000`; globals like `camera_pos_*` live at `0x46c4xx`.
- Imports: KERNEL32, USER32, GDI32, **WINMM, DSOUND, DINPUT, DDRAW**, ole32, Quartz.

## Function extent (derived, not assumed)
- Entry `0x426f50` (`push ebp`); called from `0x42d623`.
- The first *call target* at a higher address is `0x42cd60`, and bytes
  `0x42cd58`–`0x42cd5f` are `nop` padding → **function = `0x426f50`–`0x42cd58`,
  ~24 KB of code**. (objdump's linear sweep desyncs slightly near the tail where
  it walks into data/padding; doesn't affect the call/branch analysis.)

---

## What the ASM PROVES

### 1. The D3D call structure is faithful to IDA. ✓
Indirect calls resolve cleanly against the **`IDirect3DDevice3` vtable** (offset =
index × 4):

| vtable offset | index | method | call count in func |
|---|---|---|---|
| `[reg+0x58]` | 22 | **SetRenderState** | **61** |
| `[reg+0x64]` | 25 | **SetTransform** | 5 |
| `[reg+0x70]` | 28 | **DrawPrimitive** | 5 |
| `[reg+0x98]` | 38 | **SetTexture** | 18 |
| `[reg+0x24]` | 9  | BeginScene | 1 |
| `[reg+0x28]` | 10 | EndScene | (present) |

So "begin scene → set states → set transforms → draw primitives + bind textures
→ end scene" is real, in that order. The pipeline shape in the reconstruction
holds up.

### 2. IDA's `SetRenderState` flag-names are a DECOMPILER ARTIFACT. ✗ (don't trust)
Raw asm for the calls is just `push <imm>; push eax; call [ecx+0x58]`:
```
427866: push 0x34   ; state = 52
427868: push eax    ; value
42786b: call [ecx+0x58]   ; SetRenderState(52, value)
```
The state operand is a **plain integer** (`0x34` = 52). IDA printed it as
`D3DRENDERSTATE_RANGEFOGENABLE|D3DRENDERSTATE_TEXTUREPERSPECTIVE` purely because
`48 | 4 == 52` — it bit-decomposed an enum *value* into a fake OR of flag names.
**That is meaningless; ignore it.** Confirmed plain-integer states seen:

| dec | hex | values pushed | real meaning |
|---|---|---|---|
| 27 | 0x1b | 0/1 | `D3DRENDERSTATE_ALPHABLENDENABLE` ✓ |
| 28 | 0x1c | 0/1 | `D3DRENDERSTATE_FOGENABLE` ✓ |
| 29 | 0x1d | 0   | `D3DRENDERSTATE_SPECULARENABLE` ✓ |
| 19 | 0x13 | 2/5 | `D3DRENDERSTATE_SRCBLEND` ✓ |
| 20 | 0x14 | 2/6 | `D3DRENDERSTATE_DESTBLEND` ✓ |
| 14 | 0x0e | 0/1 | `D3DRENDERSTATE_ZWRITEENABLE` ✓ |
| 7  | 0x07 | 1   | `D3DRENDERSTATE_ZENABLE` ✓ |
| **52–57** | 0x34–0x39 | 0,1,3,8 | **NOT in the public DX5/6 enum** (it jumps 51→64). Small-int values (1/3/8) look like texture **filter/wrap** settings; treat as device/legacy texture-stage states. Exact semantics unconfirmed — but definitely *not* fog. |

Net: trust IDA's *named* states (fog/specular/alpha/blend/zwrite), distrust any
state it renders as an `A|B` OR — re-read the raw integer instead.

### 3. The "magic floats" are 1/screen-dimension constants. ✓ (proven from .rdata)
Read straight out of the constant pool:

| .rdata VMA | f32 value | = | refs |
|---|---|---|---|
| `0x45d4c4` | `0.0015625` | **1/640** | 13 |
| `0x45d4a8` | `0.025` | 1/40 | 6 |
| `0x45d4ac` | `0.033333335` | 1/30 | 6 |
| `0x45d4c0` | `0.0020833334` | 1/480 | — |

So HUD/UI positioning is exactly `pixel * (1/screenDim)` → normalized [0,1]
device coords. No exotic vertex math; the reconstruction's reading was right.

### 4. Win32/CRT usage confirmed by direct IAT references. ✓
Inside the function:
- `call ds:0x45d250` → **`WINMM.dll!timeGetTime`** (FPS-interval timer)
- `call ds:0x45d1b0` → **`USER32.dll!GetAsyncKeyState`** (the F1 / VK 0x70 check)

And the HUD format strings exist verbatim in the binary:
- `%02.0f:%02.0f;%d`  ← level timer (mm:ss**;**cc — note `;`, **not** `:` as the
  first IDA pass suggested; small correction)
- `%.1f fps`          ← FPS overlay
- `%d/%d`             ← crystals collected / needed
- `GAME: completed at level %d/%d`

### 5. The per-object draw workhorse. ✓
Direct call `0x4095f0` is invoked **13×** (the most of any target) and is itself a
~5.5 KB function (`0x4095fe`–`0x40afc8`). This is IDA's
`game_draw_dynamic_3d_object_in_world` — the routine every object category funnels
through. The vector-math helpers also resolve: `0x403770`/`0x403830` (cross),
`0x4037e0` (dot/len²), `0x407f70` (normalize), `0x403810` (dot) — confirming the
hand-rolled LookAt basis in the scripted-camera path.

---

## Verdict on the IDA dump
- **Control-flow & call structure:** faithful. The phase ordering, the D3D method
  sequence, the helper call graph, and the HUD contents all reproduce from asm.
- **Symbolic names (states/flags):** *not* faithful where it shows `FLAG_A|FLAG_B`
  on a scalar field — those are invented from bit-decomposition. Re-read the raw
  immediate.
- **Variable soup:** the ~600 locals are decompiler spill, not real structure.

So: **structure ✓, decoded constants ✓ (now from .rdata), enum labels ✗.**
The reconstruction in `main_render_reconstructed.cpp` stands, with the two
corrections folded in (timer separator `;`; render-states 52–57 are plain integer
texture/sampler states, not fog).

## Reproduce
```sh
objdump -d -M intel --no-show-raw-insn RUS_Karoo.exe > claude/disasm/full_text.asm
# states pushed before SetRenderState:
grep -B2 'call   DWORD PTR \[ecx+0x58\]' claude/disasm/main_render_func.asm
# read a float constant:
python3 -c "import struct;f=open('RUS_Karoo.exe','rb');f.seek(0x45d4c4-0x45d000+0x5d000);print(struct.unpack('<f',f.read(4)))"
```
