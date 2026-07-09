# .par — ParticleSystem format (reverse-engineered)

`Models/**/*.par` are **serialized C++ object graphs** describing a particle system
(emitter + forces). Referenced from themes as `ParticleSystem models\...\X.par` (e.g. Forest
`ausgang2.par` = the open-exit fountain, `fackel2.par` = torch, Castle `kerze_*.par` = candles,
`crytalFX.par`/`CollFX.par` = crystal pickup). The `.thm` supplies each system's **Texture +
blend** (usually `flare01_*` / `star3_32`, additive `One/One`); the `.par` supplies the
**emitter geometry + timing + forces**. Dev source paths in the binary: `E:\WORK\VC\…JumpinJohn…
Ra…Particlesystem` (working title "JumpinJohn").

## Load path (functions renamed in Ghidra)
| addr | name | role |
|------|------|------|
| 0x448ce0 | `par_load_file` | fopen → `par_read_from_file` → fclose (wrapper) |
| 0x448d80 | `par_read_from_file` | read `u32 len` + `len` bytes = class descriptor → factory → vtable **+0x38** reads the rest of the file |
| 0x448ab0 | `par_factory_system` | classname → construct: `ParticleSystem`(new 0x28) / `PointParticleSystem`(0x30) / `FaceParticleSystem`(0x7a) / `XFaceParticleSystem`(0x96) |
| 0x448ca0 | `par_read_subobject` | generic nested read: `par_factory_system` + vtable **+0x08** |
| 0x448000 | `particlesystem_base_read` | base ParticleSystem deserializer (reads the Generator + Environment sub-objects) |
| 0x4485d0 | `par_factory_generator` | classname → Generator object (e.g. `StdGenerator`) |
| 0x4488f0 | `par_factory_environment` | classname → Environment object (e.g. `GravityEnvironment`) |
| 0x44d2c0 | `faceparticlesystem_ctor` | FaceParticleSystem ctor; vtable = `faceparticlesystem_vtable` |
| 0x44dc40 | `faceparticlesystem_read` | vtable **+0x38** (from file): base read + `f32 @obj+0x76` (face size) + `FUN_0044d460` extras |
| 0x44d420 | `faceparticlesystem_read_stream` | vtable **+0x08** (nested/stream read) |
| 0x45f17c | `faceparticlesystem_vtable` | [+0x00]=dtor [+0x08]=read_stream [+0x14]=? [+0x38]=read(file) |

## File structure (recursive)
```
.par:
  u32  len                         # descriptor length
  char classdesc[len]              # class name (+ ctor args?) -> par_factory_system
  <vtable+0x38 read (FaceParticleSystem = faceparticlesystem_read)>:
    particlesystem_base_read:
      u32  ?field                  # base header word
      <particlesystem_base_init FUN_00447900>
      u32  glen ; char gen[glen]   # -> par_factory_generator  -> generator.vtable+0x14 reads params
      u32  elen ; char env[elen]   # -> par_factory_environment -> environment.vtable+0x14 reads params
    f32  face_size  @ obj+0x76     # (ctor default 1.0)
    <faceparticlesystem_read_extras FUN_0044d460>
```
Three parallel factories (system / generator / environment), each dispatching on a leading
class-name string. Read entry points by vtable slot: **+0x38** = top-level from file, **+0x08** =
nested from stream, **+0x14** = sub-object param read (used by the base for gen/env).

## Generators (par_factory_generator @0x4485d0)
Classname → class: `Generator`(0x10) / `PointGenerator`(0x1184) / `BoxGenerator`(0x244c) /
**`StdGenerator`(0x3420, ctor `stdgenerator_ctor` @0x49670, vtable `stdgenerator_vtable` @0x45f094)** /
`XStdGenerator`(0x3438) / `CylinderGenerator`(0x3444). The huge sizes are baked over-life lookup
tables (StdGenerator: two 1500-dword ramp tables @obj+0x78/+0x1968, a 200-dword RGBA table
init 0xffffffff @obj+0x3418-ish).

### StdGenerator read — `stdgenerator_read` @0x44a2e0 (vtable +0x14)
Reads this struct straight from the file (offsets are into the generator object):
```
u32   mode        @0x14   # emission mode; 0 -> uses range (0x18,0x24), 1 -> uses (0x30,0x3c)
f32[3] vecA_hi     @0x3c
f32[3] vecA_lo     @0x30   # pair A (mode 1): min/max
f32[3] vecB_lo     @0x18
f32[3] vecB_hi     @0x24   # pair B (mode 0): min/max
f32[3] vecC_lo     @0x48
f32[3] vecC_hi     @0x54   # pair C (always): min/max  -> FUN_00449c50(lo,hi,u32@0x60,u32@0x64)
u32    n0          @0x60
u32    n1          @0x64
f32/u32            @0x68
f32/u32            @0x6c   # -> FUN_00449fa0(@0x68,@0x6c)
u32                @0x10
then: stdgenerator_read_ramp @0x44a530  (over-life ramp — see below)
```
**Confirmed semantics** (from the precompute helpers — each builds a randomized sample table via
`gen_rand_range_table(dst, N, lo, hi, hi*variance)` @0x448fb0):
- **`mode` @0x14** — position-emit shape: `0` → `stdgenerator_build_pos_mode0(vecB 0x18, 0x24)`,
  `1` → `stdgenerator_build_pos_mode1(vecA 0x30, 0x3c)`. So **vecA/vecB = spawn POSITION min/max**
  (which pair depends on mode).
- **vecC (0x48 lo / 0x54 hi) + (u32 0x60 / f32 0x64) = initial VELOCITY** — `stdgenerator_build_velocity`
  makes a **500-entry velocity table** @gen+0x17f0: normalize(random dir in vecC range) × (random
  speed in [0x60,0x64]).
- **(0x68 / 0x6c) = a scalar range → LIFETIME (likely)** — `stdgenerator_set_lifetime_range` builds a
  **100-entry** table @gen+0x2f58.
- **ramp** (`stdgenerator_read_ramp`) → over-life curve sampled to a 1500-entry table (size/alpha/color).
- `@0x10` — trailing u32 (count/flag), meaning TBD.

### Ramp — `stdgenerator_read_ramp` @0x44a530
```
u32   count
{ f32 time, f32 value } keyframes[count]     # 8 bytes each
```
`FUN_00449ea0(keys,count)` samples the keyframes into a 1500-entry lookup table (over-life curve;
one call per ramp — size / alpha / color). This is the "look" (fade + grow/shrink over lifetime).

## Environments (par_factory_environment @0x4488f0)
Classname → class: `Environment`(0xc, empty) / **`GravityEnvironment`(0x6c, ctor
`gravityenvironment_ctor` @0x44c130, vtable `gravityenvironment_vtable` @0x45f110)** /
`MagnetEnvironment`(0x50). GravityEnvironment ctor default field `[0x10]=10`.
### GravityEnvironment read — `gravityenvironment_read` @0x44c7f0 (vtable +0x14)
Unambiguous 64-byte `fread` sequence (file order):
```
f32[3] A          # -> setup FUN_0044c410(this, A, D)     (gravity dir + magnitude?)
f32    B
f32    C
f32    D          # -> setup FUN_0044c320(this, D, B)
f32    @this+0x40 ; f32 @0x44 ; f32 @0x48 ; f32 @0x4c     # 4 scalars stored directly
f32[3] @this+0x50 # vec3  (range min?)
f32[3] @this+0x5c # vec3  (range max?)
```
Total 64 bytes: `vec3, f32,f32,f32, f32,f32,f32,f32, vec3, vec3`. The two stored vec3s @0x50/0x5c
look like a min/max range (generator pattern); A + scalars feed the setup helpers (gravity
direction/strength). Exact field naming (which is the gravity vector) via FUN_0044c320/c410, but the
byte layout is fully parseable now.

## Loader status (src/formats/par.c)
Parses by scanning for the generator/environment class strings (fields sit right after `name\0`).
Validated against real files:
- **CylinderGenerator (exits) — WORKS.** `Forest/Ausgang2.par` → center (0,0,0), axis (0,1,0),
  radius 0.35, dir (0,1,0), speed 1.5, life 2.2, gravity (0,-1,0). (Ranges encode `[base, extra]`
  with extra usually 0 = fixed; loader clamps hi>=lo.) NOTE the `.par` is **Y-up** — map Y→Z for
  our Z-up world when emitting.
- **StdGenerator (crystal/candle/torch/fountain) — FULLY DECODED.** The "packed tail" was a red
  herring: the `.par` files were serialized in Win32 **text mode**, and the shipping loader reads
  them with `fopen(...,"r")` (CRLF→LF). We opened `"rb"`, so every float whose low byte is `0x0a`
  had a stray `0x0d` (the `0d 0a d7 23 3c` = `0.01f` corruption). **Fix: undo `\r\n`→`\n` on load**
  (done in `slurp`); then the whole block is plain LE floats. Layout (after CRLF-undo, from
  `stdgenerator_read` @0x44a2e0): fixed **96 bytes** = `u32 mode` + 6 vec3 (aHi,aLo,bLo,bHi,cLo,**cHi**)
  + `f32 speed, speed_var, life, life_var, emit_rate`, then a **colour ramp** `u32 count;
  {u8 rgb[3]; u8 pad; u32 weight}*count` (weighted-random palette, NOT a size/alpha curve).
  Position range = mode 0 ? vecB : vecA; velocity = normalize(random dir in [cLo,cHi]) × speed;
  gravity from the env (down=sparks, UP=flames/bubbles). Validated 47/47 files via `test_par`
  (crytalFX 5.0/0.8 cyan+green; Kerze 0.2/0.8 buoyant; Fackel torch).
- **XStdGenerator (Fackel torch / triebwerk thruster / helmblubber bubbles) — DECODED.** =
  StdGenerator + **two trailing vec3** read after the ramp (`xstdgenerator_read` @0x44aa60):
  `pos_off` @0x3420 (constant spawn-position offset — fold into pos_lo/hi) and `vel_off` @0x342c
  (constant velocity bias — new `par_system.vel_off`). Emitter: spawn = pos-range + pos_off;
  velocity = dir×speed + vel_off. Validated: torch (-0.43,1.73,0.45)/0; thruster 0/(0,-1.6,0) jet;
  bubbles (0.1,0.75,0)/(0.42,-0.42,0) drift.

`par.c` now parses Std/XStd/Cylinder + gravity + colours + emit_rate + vel_off. `.par` is Y-up:
map (x,y,z)→(x,z,y) when emitting.

## TODO — remaining (minor)
- `emit_rate` (StdGen @0x10) units not traced into the emit loop (MEDIUM confidence; scales with
  visual density: bubbles 10 … explosion 1M). GravityEnvironment exact field naming via
  FUN_0044c320/c410. `faceparticlesystem_read_extras` (0x44d460) — minor.
The full **byte layout** of the container + StdGenerator + GravityEnvironment is decoded — enough to
write the loader and feed our existing generic emitter (mode/position/velocity/lifetime/ramp/gravity);
the RE supplies the real numbers instead of our hand-tuned fountain guess.
