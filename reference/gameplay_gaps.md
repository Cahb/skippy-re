# Gameplay-fidelity gaps (movement / AI) — RE-first backlog

Behaviours that feel "off" vs the original and need the binary's exact rules, not guesses.
Ghidra MCP is wired (see [ghidra_mcp_setup.md]). Existing RE to build on noted per item.

| # | Gap | RE status | Lead / where |
|---|-----|-----------|--------------|
| ~~G1~~ | **Platform fall-off** — FIXED. Root cause: the idle "ground vanished → fall" check fired against `dynamic_floor`, which only reports floor when the platform is within 0.3 of a grid centre (a *boarding* gate) — so a rider mid-slide read as over-void and dropped every step. Fix: a rider (`ride_kind != 0`) is exempt from the idle void-fall; it's carried by `ride_carry` until it hops off. | ✅ done (`sim.c` tick_entity) |
| ~~G2~~ | **Bombs ignore jump-pads** — FIXED. Bombs now launch off pads (`bomb_launch_jumppad`, rise+forward hop along throw facing, chains). Part of the jump-pad launch rework: it's now **two-phase** (rise-in-place → forward hop-GLIDE, no apex teleport — RE §"Jump-pad launch"), launch dir = **momentum** (fixes backward-bounce), and **enemies launch too** (fall-landing trigger, before ai_step moves them). Glide ≈ `move_dur×0.1` (user-calibrated snappy). | ✅ done (`sim.c`) |
| G3 | **Paraglide is wrong** — we trigger it by fall-depth + spend a charge. Original: one deploy gives **mid-air directional control until you touch ground**. | partial (`gameplay_mechanics.md`): paraglide = a ride state, anim 12, buffered exit dir `pad6[0x2e]`, ride flag `pad6[0x40]`, turn L/R keys `0x1e/0x1f` | RE the paraglide state machine fully; reimplement `sim.c` glide |
| G4 | **Movement feel stiff** — tank movement is off; hop timing/curves don't match. | hop/turn timing + parabola curves in `skippy_tick_dissection.md`; verify the exact durations/constants | RE-verify move/turn tick constants vs `sim.c` |
| ~~G5~~ | **Jump-pad skip** — FIXED. Cause was an input race, not the trigger condition: `tank_input` calls `sim_forward` on *held* W every frame (before `sim_tick`), so on arriving at a pad a buffered hop started before `sim_tick`'s idle jump-pad check → you hopped off without launching. Fix: launch on **arrival** (`arrive_entity` → `try_launch_jumppad`), so `launching` blocks the buffered step. Idle check kept as fallback (fall-onto-pad, spawn-on-pad). | ✅ done (`sim.c`) |
| G6 | **Enemy navigation scheme** — need the exact follow rules: aggro **RANGE** at which they start chasing; whether they can use elevators/platforms/portals; and **aware vs just-use** (suspected: they just traverse whatever's under them, no special awareness). | `enemy_ai.md` has follow-A*; `skippy_tick_dissection.md` marks enemy move-cmd generation **OPEN** | finish enemy-AI RE (range gate, mover/portal traversal); implement |

## Jump-pad launch — RE'd (skippy_tick @0x438770, type 0x0e block)
The pad is a **ballistic rise-in-place then a normal forward hop**, NOT a teleport:
1. On the 0x0e cell, once aligned: `ride_active_flag=1`, then rise IN PLACE via a `SQRT`
   ease-out curve to `elevator_target_z` (the height; `clip_rule`), `current_anim_pos=0x0b`.
2. On reaching the target: `ride_active_flag=0`, `pos_z_1=target`, and
   `buffered_move_command = stored_move_dir` → a **standard forward hop** one cell in the
   LAST movement direction (momentum — confirms the momentum choice). Then normal
   step-down/fall applies from the raised cell.
Our bug: we teleport cx/cy at the apex (instant), which kills the "sway/glide to the
destined tile" feel. Fix = two-phase: rise-in-place, then glide horizontally to the
forward cell (descending to its floor). (The odd `(11,11)` landing is the pick display's
X/Y swap + a follow-on hop; the RE hop is one cell.)

## Done this session (landing/mover cluster)
- **Enemies + player teleport on arrival** (per-entity `tele_lock`) — enemies lured onto a teleporter warp too (trap levels). Was gated `is_player` + raced by `ai_step`; now fires in `land_effects` on arrival.
- **Ice slide**: player AND enemies slide, along ENTRY MOMENTUM (backward hop → slide backward), whole-run direction locked; a slide into a jump-pad launches, into a switch toggles.
- **`land_effects` refactor**: the on-rest logic (stomp / mover attach / pickup / teleporter / jump-pad / switch / glue / ice) is shared by walked arrivals AND jump-pad glide landings — so landing on a switch/teleporter/pad/ice via a pad behaves identically (fixes the pad→switch not toggling).
- **Switches**: toggle for player AND enemies (never was `is_player`-gated; now also on pad landings).
- **Elevator Field pane** (G10): the alpha grid top surface (Space `elevator.tga`) is drawn as an alpha-cutout quad on the elevator; the Model is only the side frame (was hollow).

## More gaps (captured this session, not yet built)
| ~~G12~~ | **Bridge = theme force-field** — DONE. Space bridge is an additive (One/One), scrolling (`Scroll`, now parsed), wrap-tiled green force-field pane (not a solid plank). The ANCHOR tile (0x12/0x13) is part of the bridge: invisible + a gap until activated (its `plank_z` is set by `bridges_tick` while extended → walkable + rendered). Flow runs along the span (X-bridge transposes the UV so lines follow it), outward. Also fixed: bridge axis X/Y-swap (0x13=+X), and edge-culling of tiles adjacent to an anchor. | ✅ |
| # | Gap | Notes |
|---|-----|-------|
| G7 | **SLIDE tiles** — like ice but *downward-only*, stacked back-to-back so you slide down a run. Distinct from ice (0x15) and slide/redirect (0x10). | needs the tile type + RE of the slide-down rule |
| ~~G8~~ | **Flying/airborne animation** — DONE. `launching` → `ANI_JUMP`, `falling` → `ANI_FALL`, for player (main.c) + catcher/thrower (render_scene). No longer idle mid-air. | ✅ |
| ~~G9~~ | **Move/stomp SFX timing** — DONE. The hop/stomp (MoveJJ) fired at push-off; moved to LANDING — `arrive_entity` for hops (non-slide) and turn-completion for hop-turns. Ice-slide slip stays at its start. | ✅ |

RE-first, one system at a time: decompile the relevant tick fn, write the exact rule into the
doc above, THEN implement + verify in `sim.c`. Movers (G1) are the most RE-complete → lowest risk.
Paraglide (G3) and enemy nav (G6) are the biggest behavioural wins but need the most RE.
