# DirectDraw4 + Direct3D3 init — `directdraw_d3d_init` @ 0x412740

The exclusive-fullscreen graphics bring-up. This is the exact sequence the
`ddraw_proxy` must satisfy. Called from the window/entry setup (`FUN_0042d100`)
as `directdraw_d3d_init(this=direct3d_interface_obj, hWnd, cmdline)`.

German error strings (each routed through `report_init_error` @0x412f80, which
aborts init) pinpoint every step.

## Sequence
1. `DirectDrawCreate()` (retried once). Fail → "Fehler beim Anlegen des DirectDraw" (error creating DirectDraw).
2. `QueryInterface(IID_IDirectDraw4 @0x45d768)` → stored at **this+0xb8**. Fail → "DirectDraw4-Schnittstelle nicht gefunden".
3. `Release()` the original IDirectDraw1.
4. `IDirectDraw4::SetCooperativeLevel(hWnd, 0x811)` — `0x811 = DDSCL_EXCLUSIVE|DDSCL_FULLSCREEN|DDSCL_ALLOWMODEX`. Fail → "Fehler beim Setzen der Kooperationslevel".
5. `QueryInterface(IID_IDirect3D3 @0x45da88)`.
6. Enumerate device caps (D3DDEVICEDESC, 0x20c bytes) → copies desc to this.
7. Log "START ENUMDISPLAYMODES"; `IDirectDraw4::EnumDisplayModes(cb=0x413000)` builds mode list at **this+0x44**; "END ENUMDISPLAYMODES".
8. Pick a mode → **this+0x50** = {width, height, bpp}.
9. `IDirectDraw4::SetDisplayMode(w,h,bpp)` — logs "trying to set mode %dx%dx%d";
   on fail logs "...failed (%x)" then falls back to the first enumerated mode.
   Total fail → "Fehler beim Setzen des Videomodus".
10. Create **primary surface** (flip chain): `DDSURFACEDESC2{dwSize=0x7c, flags=0x21, caps=0x2218, backbuffercount}` → `IDirectDraw4::CreateSurface` → **this+0x38**.
11. `GetAttachedSurface(DDSCAPS_BACKBUFFER=0x4)` → **this+0x3c** (back buffer). Fail → "Fehler beim Abfragen des BackBuffers".
12. `QueryInterface(IID_IDirect3D3)` again → **this+0x04**. Fail → "Direct3D3-Schnittstelle nicht gefunden".
13. `EnumZBufferFormats(deviceGUID)`; require **this+0x14 (z-buffer bit depth) == 0x20** else abort.
14. Create **Z-buffer** surface `{dwSize=0x7c, flags=0x1007, caps=0x20800 (+0x3800 if HW)}`; logs "Z Buffer Bit Depth: %d" / "Stencil Buffer Bit Depth: %d" → **this+0x34**. Fail → "Fehler beim Anlegen des Z Puffers".
15. `AddAttachedSurface(zbuffer)` onto the back buffer.
16. `IDirect3D3::CreateDevice(guid, backbuffer)` → **this+0x0c** (IDirect3DDevice3). Fail → "Fehler beim Anlegen des D3DDevice".
17. `IDirect3D3::CreateViewport` → **this+0x08**; set viewport rect + minZ=-1.0 maxZ=2.0; `AddViewport`, `SetViewport2`, `SetCurrentViewport`. Fail → "Fehler beim Anlegen des D3DViewport".
18. return 1 (success).

## Device-type GUIDs tried (in .rdata)
`0x45dac8` = HAL, `0x45daa8` = RGB (software), `0x45dab8` = ramp/ref — selected by
a hardware flag (`cStack_88`/`cStack_a0`).

## `direct3d_interface_obj` field map (param `this`)
| off | field |
|-----|-------|
| +0x04 | IDirect3D3* |
| +0x08 | IDirect3DViewport3* |
| +0x0c | IDirect3DDevice3*  (`lpDirect3dDevice3`) |
| +0x10 | render bit depth |
| +0x14 | z-buffer bit depth (**must be 32**) |
| +0x24 | stencil bit depth |
| +0x34 | Z-buffer surface |
| +0x38 | primary surface |
| +0x3c | back-buffer surface (`lpDirectDrawSurface4`) |
| +0x44 | display-mode list head |
| +0x50 | chosen mode {w,h,bpp} |
| +0xb8 | IDirectDraw4* |
| +0xbc | hWnd |

## Proxy implications
- The proxy must return success for: DirectDrawCreate, QI(IDirectDraw4),
  SetCooperativeLevel(0x811), QI(IDirect3D3), EnumDisplayModes, SetDisplayMode,
  CreateSurface (primary+flip and z-buffer), GetAttachedSurface, EnumZBufferFormats
  (advertising a 32-bit z format), CreateDevice, CreateViewport.
- The hard 32-bit z-buffer requirement (step 13) is a likely failure point if the
  proxy/host advertises a different depth — worth special-casing.
- Callbacks to implement/observe: `0x413000` (display-mode enum), `0x413100` (z-buffer format enum).

Related names set in Ghidra: `directdraw_d3d_init` (0x412740), `report_init_error`
(0x412f80), `fwrite` (0x4513c7). Decompiler comment with this map is on 0x412740.
