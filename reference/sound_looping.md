# DirectSound looping / seamless SFX (RE findings)

RE'd from RUS_Karoo.exe via the Ghidra project. Scope: how the original keeps
sustained sounds (fuse, slides, fountains, movers) seamless, and what the
rewrite needs to mirror it.

## Play path

There is **no separate looping wrapper**. `sound_play` (0x442900) passes its
argument straight to `IDirectSoundBuffer::Play(0, 0, flags)` (vtable+0x30 on
Sound+0x10); each call site pushes `0` (one-shot) or `1` (DSBPLAY_LOOPING).
`sound_stop` (0x4429a0) = `IDirectSoundBuffer::Stop()` (vtable+0x48).

Two distinct seamlessness idioms:

1. **DSBPLAY_LOOPING static buffers** for sustained states (slides, movers,
   ambient props).
2. **Retrigger-every-tick with flags=0** — Play() is a no-op while the buffer
   is still playing and restarts it the instant it ends. Used for the bomb
   fuse, the bonus-screen Count.wav, and LastSeconds.wav. The fuse is NOT a
   DirectSound loop.

## Loop / one-shot table

| Sound event (owner field) | Mode | Starts | Stops |
|---|---|---|---|
| MoveSliding "rutsche" (player+0xaf) | **LOOP** | skippy_tick 0x438deb, slide state entered (+0xfb=1) | 0x438e37 / 0x439b3f slide ends; player death game_tick 0x41534b |
| MoveIceSliding (player+0xab) | **LOOP** | skippy_tick 0x439257, stepping onto ice tile 0x15 (anim 3) | 0x4392b7 next tile != ice; death 0x41535a |
| MoveParagliding (player+0xc7) | **LOOP** | skippy_tick 0x4399ec, fall >=3z with charge (+0xea=1) | 0x43975a on landing (after footstep multi); death 0x415369 |
| Elevator (elevator+0x3a) | **LOOP** | elevator_tick 0x411f32 when it starts moving | 0x411d66/0x411e2e on arrival |
| Platform (platform+0x39) | **LOOP** | platform_tick 0x43b266 leg start | 0x43afb8/0x43b16e leg end |
| Bridge (bridge+0x47) | **LOOP** | game_tick 0x415646 switch toggles group (edi=1 @0x415553) | bridge_tick 0x43ecb1/0x43eeec travel complete |
| .leo Sound props / fountains (prop+0xf4a) | **LOOP** | level_sounds_init 0x41cbbc at level load — plays forever | prop teardown FUN_00425210 (0x425226) at level unload |
| BombTick fuse (bomb+0x15a, theme slot 60) | retrigger/tick | bomb_tick 0x4028f0 while fuse burns | sound_stop 0x4028fd at detonation |
| Count.wav (god+0x13cc68) | retrigger/frame | bonus count-up FUN_0041a970 | counting phase done |
| LastSeconds.wav (god+0x13cc6c) | 1/second edge | game_tick 0x415415: next-tick mark (skippy+0x65 double) armed to 10.0 while remaining > threshold; when remaining crosses below it -> play once, re-arm to floor(remaining). NOT a per-tick retrigger (the wav is a 69 ms blip) | self-stops past 0 / reset by the >threshold branch |
| Glue (player+0xbf), Switch (+0xcb), Teleporter (+0xb7/+0xbb, two instances), JumpPad (+0xb3), Splat (+0xa7), Fall (+0xc3), Move footsteps (+0xcf multi x10 player / x5 enemy), Crystal (+0x9f multi x3), ExplosionBomb (bomb+0x15e), Destruct Start/Regen, TimeOut, LevelCompleted, MenuUpDown multi, addNN announcer | one-shot (0) | event sites in skippy_tick / destructfield_tick / game_tick | n/a (Fall sound explicitly stopped at splat 0x4396e4 and enemy despawn 0x4175f0) |

## Instance model

- **No `DuplicateSoundBuffer`.** The sound manager keeps a by-filename
  registry; every entity/mover/prop gets its **own CStaticSoundbuffer**
  (whole wav in a DSBCAPS_STATIC|CTRL3D|GETCURRENTPOSITION2 buffer). If the
  cached buffer is in use, a new full buffer is re-created from the wav,
  forced LOCSOFTWARE.
- Rapid-fire sounds use **MultiStaticSoundbuffer**: N pre-created buffers
  played round-robin (0x442d90 = stop-next-instance, advance index, play).
- **3D**: 0x4429c0 is mislabeled `set_entity_render_pos_from_grid` in the
  Ghidra project — it is actually `IDirectSound3DBuffer::SetPosition`
  (vtable+0x4c on Sound+0x14); called with the sound's world pos before every
  play and per-tick for movers. UI sounds (menu, Count, LastSeconds, ...) are
  created 2D.
- **Streaming**: CStreamSoundbuffer (IDirectSoundNotify-based) exists but is
  used ONLY by the intro-script `playwave` command (0x41e5e8/0x41e6fa) —
  gameplay SFX are plain static-buffer loops. No notification tricks.
- Theme sound table = god+0x42258, stride 0x10c/slot (name +0xa, exists-flag
  +0x10a); slot IDs match the SkippySoundEvent enum (e.g. bomb+0x15a <- slot
  60 BombTick, +0x15e <- slot 70 ExplosionBomb in bomb_spawn). Entity type
  byte +0x152==2 = catcher (MoveCatcher/SplatCatcher/FallCatcher).
  Player+0xa3 = reserved slot 6 — no theme keyword maps to it, always NULL
  (vestigial one-shot at paraglide start 0x4399bf).

## Rewrite implications

Loop-handle API (`r_sound_loop_start(key, sfx, pos)` /
`r_sound_loop_stop(key)`) with loops **owned by sim state**:

- player slide / ice-slide / paraglide flags (all three stopped on death);
- per-elevator / platform / bridge motion state (start on leg start, stop on
  arrival);
- per-prop ambient loops started at level load, stopped at level unload.

Bomb fuse / Count / LastSeconds: emulate the retrigger idiom as
"if not playing -> play from start" each tick (or a true loop with an
explicit stop — audibly identical for seamless wavs).
