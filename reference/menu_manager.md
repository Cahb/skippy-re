# Menu manager + `menu_update` dissection

Sub-object at god+`0x175518` (`menu_manager_obj`, vtable @`0x45d41c`). Drives every
non-gameplay screen. Input is polled via **`GetAsyncKeyState`** here (NOT the DirectInput
action system used for gameplay): UP `0x26` / DOWN `0x28` navigate, LEFT `0x25` / RIGHT `0x27`
adjust option values, ENTER `0xd` select, ESC `0x1b` back. Debounce/last-key @`0x175517`
(`offs_175517`, held so a press fires once).

Driver: `menu_update` (0x418d20), called from `game_tick` in menu states. Render:
`draw_menu_screen` (0x42e000), dispatch by `menu_screen_id`.

## Layout (all inside pad31, base 0x175519 — now modeled in found_structs.h)
| abs | field | meaning |
|-----|-------|---------|
| 0x175518 | `menu_manager_obj` | sub-object base |
| 0x17551c | (pad31+3, int) | input-enabled gate — ESC/ENTER ignored while 0 |
| 0x175520 | `menu_confirmed_id` (WORD) | committed/shown menu id; ENTER fires only when `== menu_screen_id` (guards against acting on a mid-transition screen) |
| 0x175534 | (pad31+0x1b, BYTE) | last-bound key display code (set during rebind: `&`/`(`/`'`/`%`…) |
| 0x175535 | `menu_selected_item` (BYTE) | current highlighted item; UP/DOWN move it; index into action table |
| 0x175635 | `menu_item_count_by_id[0xff]` | per-menu-id item count; UP/DOWN disabled when `<=1` |
| 0x175734 | `menu_action_table[0x20000]` | **action code = table[menu_id\*0xff + selected_item]**, stride 0xff |
| 0x195734 | `menu_screen_id` (BYTE) | current screen (0=main, 2/4/5/0xa…); see draw dispatch |

## Action-code switch (the big `switch` in menu_update, keyed on the table byte)
- **0x22**: item just calls `menu_go_back` (plain nav/label rows).
- **0x29**: "next level / continue" — bumps level (`pad23[0x2b06]`+1), `skippy_game_start_level`,
  `game_state=4` (intro), copies checkpoint state from `pad21` into menu scratch, logs
  "level completed - continue".
- **0x3c**: toggle (reflection?) `pad33[0x20a35]` → `FUN_004439d0`.
- **0x3d**: CD-audio toggle — start/stop via `cdm_cdaudio_stop_audio`/`FUN_004033e0`, flag `pad33[0x1f627]`.
- **0x47**: toggle `pad33[0x1f608]`.
- **0x14–0x20**: **KEY REBIND**. Each copies an action-name string into the rebind buffer
  (`skippy_obj.pad4+8`), sets rebind-pending `pad30[0xe5]=1` and the action id `pad4[7]`:
  | code | action string |
  |------|---------------|
  | 0x14 | "John Move Forward" |
  | 0x15 | "John Move Back" |
  | 0x16 | "John Turn Right" |
  | 0x17 | "John Turn Left" |
  | 0x18 | "John Zoom In" |
  | 0x19 | "John Zoom Out" |
  | 0x1a | "John Release Bomb" |
  | 0x1b | "John Harakiri" |
  | 0x1c | "John OverView" |
  | 0x1d–0x20 | "CamMode Left/Right/Up/Down" |
  After copy: `pad31[0x1b]` = the current binding's display key. At end of frame, if rebind
  pending and ENTER released, `FUN_00446f80(1, buf, 100, 10, 0)` binds the next pressed key,
  then clears the pending flag.

## Per-`menu_screen_id` post-switch handlers
- **id 1**: start game → `skippy_game_start_level`, `game_state=4`, stop CD audio, seed frame state.
- **id 5**: pause bookkeeping (sound-pause latch).
- **id 6**: quit path → `game_state=7`; `PostQuitMessage(1)` when a flag is 0.
- **savegame slots**: `menu_screen_id` in `[200, 200+num_slots)` → load that slot & start;
  in `[200+num_slots, 200+2*num_slots)` → highscore/name-entry path for that slot.
  (`num_slots` = `pad23[0x2f]`.) Slot index = `id + 0x38` into the savegame table at `pad23`.

## Option adjusters (LEFT/RIGHT while an option row is selected — cases '"','>','?','H','I','J')
Sound master vol `pad33+0x20a4f` (step 10, cap 0x5a) → `FUN_00446020(0/4,...)`; music/effects
vol `pad33+0x1f62b` / `pad33+0x20a39` (→ `waveOutSetVolume`); toggles `pad33+0x1f607/09/0a`.

## Named / renamed this round
found_structs.h: `menu_confirmed_id`, `menu_selected_item`, `menu_item_count_by_id[0xff]`,
`menu_action_table[0x20000]` (were inside pad31). Verified: struct size 0x51790d preserved,
`game_state`@0x2ab58c unshifted (gcc -m32 offsetof asserts).
