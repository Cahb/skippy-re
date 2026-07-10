# Enemy AI — aggro / activation, pathfinding, traversal (RE, implementation-ready)

Reverse-engineered from RUS_Karoo.exe via Ghidra MCP (read-only). This resolves the
HIGHEST-PRIORITY gap: why the rewrite has *all* enemies aggro instantly (e.g. Space
"Bonus"), and what the original actually does.

## TL;DR — the aggro rule (the critical part)

**There is NO per-enemy "awake/activated" latch.** Every catcher runs its chase step
*every frame* (`game_tick` → `enemy_ai_move_toward_target` for each enemy, unconditionally).

The de-facto aggro **range** is a **bounded A\* iteration cap**. The pathfinder
(`ai_pathfind_astar`) is a best-first search rooted **at the player**, expanding toward the
enemy, and it is **hard-capped at N node-expansions**, where N = the `opts` argument =
`pathobj[0x2f]` (a `uint16`). For a plain player-chasing catcher **N = 50** (`0x32`). If the
search does not reach the enemy's cell within 50 expansions, `ai_pathfind_astar` returns 0 →
`buffered_move_command` stays 0 → **the enemy does not step this frame.**

Because the search is *directed* (best-first, heuristic `h = dx²+dy²`) and *capped*, an enemy
that is far from the player along the walkable graph simply never receives a move command.
As the player walks closer, enemies enter the 50-expansion bubble one region at a time — this
is what produces the "wake one-by-one" behaviour. It is **recomputed every frame with no
memory**, so an enemy the player walks away from goes dormant again (it is a live range gate,
not a one-shot trigger).

**The rewrite bug:** `ai_next_dir` in `sim.c` is an **unbounded BFS** from enemy → player.
Any enemy with *any* walkable path chases immediately. Fix = cap the search (see §7).

## 1. Call chain & addresses

```
game_tick (enemy loop @ ~0x4158xx..0x415b60; call site 0x415aa8)
  per enemy, EVERY frame:
    pick target (tx,ty) + search-cap by behaviour type pad3b[2] (=catcher+0x62)   (see §3)
    enemy_ai_move_toward_target(enemy, tx, ty)                       0x412240
      -> ai_pathfind_step_toward(enemy, tx, ty, opts=pad4a word)     0x43a9d0
           writes pathobj[0x2f]=opts (ITERATION CAP), target @+0x31/+0x32
           -> ai_compute_path(from_x,from_y, to_x,to_y) -> bool       0x401c20
                is_cell_walkable(to) && is_cell_walkable(from)
                && grid_cell_id(from)!=grid_cell_id(to)               (skip if same cell)
                -> ai_pathfind_astar(from,to)                         0x401db0
                     capped at pathobj[0x2f] pops; else return 0
           -> read first result node -> cardinal buffered_move_command
      -> if buffered_move_command != 0 -> skippy_tick(enemy)          0x438770  (executes step)
```
Helpers: `astar_pop_best_node` 0x401ec0, `astar_expand_neighbors` 0x401ef0,
`is_cell_walkable` 0x401cd0, `grid_cell_id` 0x401cb0, `tile_passable` (~0x41f5xx, shared with
player movement), `pathfind_reset` 0x401d60, `rotate_direction` 0x43ad40.
Spawn: `spawn_enemy` (called from `skippy_game_start_level` 0x416420 @0x416e8b/0x416ebc, and
from the factory path in `game_tick` @0x41529a).

## 2. ai_pathfind_astar (0x401db0) — the cap is the range gate

```c
iVar1 = grid_cell_id(from_x, from_y);          // GOAL = the enemy cell
root = calloc(0x44);                            // START node = the PLAYER cell (to_x,to_y)
root.f = root.cost = (to_x-from_x)^2 + (to_y-from_y)^2;   // squared-Euclidean seed
root.cell_id = grid_cell_id(to_x, to_y);
open = { root };
iters = 0;
if (pathobj[0x2f] != 0) do {
    cur = astar_pop_best_node();               // lowest f
    if (cur == 0) return 0;                     // open list empty -> unreachable
    if (cur.cell_id == iVar1) break;            // reached the enemy -> path found
    astar_expand_neighbors(cur, from_x, from_y);
    iters++;
} while (iters < pathobj[0x2f]);                // <-- THE CAP (=50 for a normal catcher)
if (pathobj[0x2f] <= iters) return 0;           // exhausted the cap -> NO path this frame
pathobj[0xe] = cur;                             // result chain, first node is adjacent to enemy
return 1;
```
- Node = 0x44 bytes: `+0 f`, `+4 cost`, `+8 g`, `+0x10 x`, `+0x14 y`, `+0x18 cell_id`,
  `+0x40 open-list link`. Heuristic `h = dx²+dy²` (squared Euclidean, admissible-ish, drives
  a strong beeline toward the goal).
- **Backward search** (start=player, goal=enemy) so the returned node adjacent to the enemy
  gives the enemy's next hop directly.
- `ai_pathfind_step_toward` reads `pathobj[0xe] -> +0x10/+0x14`, converts the delta to a
  cardinal command: `dy=-1→1(up)`, `dx=+1→2(right)`, `dy=+1→3(down)`, `dx=-1→4(left)`.

## 2b. A* internals — complete spec (dig 2026-07-10, closes the §2 gaps)

Decompiled `astar_expand_neighbors` 0x401ef0, `astar_pop_best_node` 0x401ec0, and the
node-insert/relax chain `FUN_00402000` / `FUN_00402170` (sorted insert) /
`FUN_00402130`+`FUN_00402150` (open/closed lookup by cell_id) / `FUN_004021b0` (relax
propagation). This makes the search byte-explainable:

- **f = g + h**, where **g = hop count** (`parent.g + 1`, uniform cost 1/step) and
  **h = (dx² + dy²) to the goal (enemy)**. Node ints: `[0]=f [1]=h [2]=g [4]=x [5]=y
  [6]=cell_id [7]=parent +0x20..+0x3c children[8] +0x40 link`.
- **Open list is a singly-linked list kept sorted ascending by f at INSERT time**
  (`FUN_00402170`); `astar_pop_best_node` does NOT scan — it just unlinks the head and
  pushes it onto the closed list (+6 = open sentinel, +10 = closed sentinel). §2's
  "lowest f" is realized by the sorted insert, not the pop.
- **Tie-break: a new node is inserted BEFORE existing equal-f nodes** (insert walks
  while `list.f < new.f`), i.e. LIFO on f-plateaus → depth-first-ish sweep. Observable
  in path shapes; must be replicated for exact wake boundaries.
- **Expansion order is fixed**: (x, y-1), (x+1, y), (x, y+1), (x-1, y) — up, right,
  down, left.
- **Neighbor filter = `is_cell_walkable(nb)` && `tile_passable(to=cur, from=nb)`** —
  note the argument order: expand validates the move **nb → cur**, i.e. every edge is
  checked in **the enemy's own travel direction** (the tree path goal→root is exactly
  the enemy's walk toward the target). Drops go downhill toward the player; there is
  no orientation reversal. (Corrects the earlier draft of this section.)
- **Duplicates/relax**: neighbor already in OPEN with better g → fields updated in
  place (parent/g/f), **no re-sort** (stale position tolerated). Already in CLOSED
  with better g → update + `FUN_004021b0`: propagate the improved g through the
  recorded `children[8]` arrays with a work-queue (classic Nilsson A* child
  propagation, no reopening).
- Every expanded node records the neighbor in its `children[8]` array regardless of
  which case hit (used only by the propagation above).
- The **cap counts pops** (loop `iters < pathobj[0x2f]`), default 50 (§3 overrides).

Rewrite deltas this exposes (beyond §7's cap fix, which is already in `sim.c`):
`ai_next_dir` is enemy-rooted (orig: player-rooted, reversed edge orientation),
pure-greedy on h (orig: f=g+h, ties LIFO, fixed NESW order), and uses the simplified
`ai_passable` instead of `is_cell_walkable`+`tile_passable` — plus the §4 step gates
are not applied per hop. On sparse maps (Space) these change which enemies a 50-pop
budget reaches, i.e. the observed aggro mismatches.

## 2c. tile_passable (0x41f500) — full clause transcription (dig 2026-07-10, part 2)

Decompiled in full; it is a **write-then-override chain** (later clauses overwrite the
verdict), with `from` = the cell the mover leaves, `to` = the cell being entered:

1. from is a stair + move along its axis → pass.
2. `to.z == from.z` and to is not a rutsche(0x10) → pass. Heights differ (or to is a
   rutsche) → pass only via **elevator endpoints**: from-or-to of type 9 whose
   `z0/z1 (+0x1d3/+0x1d4)` equals the other cell's z (plannable while the car is away;
   boarding TIMING is step-validation's job, §4).
3. from not a stair: `0 < from.z - to.z < 3` onto a non-rutsche → verdict =
   `to.walkable_override == 0` (a 1–2 **hop-down**, not onto a bridge plank; this
   OVERWRITES an earlier pass). from is a stair: descend rule (to.z == from.z − 1
   along the axis).
4. to is a rutsche at equal z → verdict = move dir equals its forced dir (+0x1f2);
   from is a rutsche → pass (always exitable).
5. to is a jump-pad(0x0e) → verdict = the cell one-further along the move dir sits at
   the pad's launch height (+0x1f1); from is a jump-pad → pass (portal exit, height
   ignored).
6. **Glue gate (the tail RETURN)**: to is glue(0x02) with `entity_reservation == 0` →
   RETURN `(cell two steps from `from` along the move dir).entity_reservation == 4`.

`+0x1a5` is **entity_reservation** (found_structs tile `pad1[5]`): stamped on cells an
entity occupies/is entering, **4 = the player**. So clause 6 means: *a fresh glue cell
can only be ENTERED while the player is standing right beyond it*. Glue pens stay
sealed until the player crosses the escape lane — verified vs Space\Bonus footage:
all four robots dormant with the player at spawn (7,9), the (5,8) robot wakes exactly
when the player is on the (7,8) ice, the (10,5) robot at (10,7). `is_cell_walkable`'s
mode-2 switch clause uses the same field (switch passable for catchers only when
occupied or reservation == 4). **Ice (0x15) has NO special clause** — plain floor in
the graph; ice behaviour is execution physics, not planning.

**Axis note:** the original's (x,y) are TRANSPOSED vs the rewrite's `tiles[x][y]`
(its dir codes map 1 → the rewrite's −X, cf. SPAWN_FACE). The expansion order in
rewrite coords is therefore `(x-1,y) (x,y+1) (x+1,y) (x,y-1)` — verified by the
Forest\EnemyStart frog opening −Y along column 13 (LIFO ties pop +X before +Y).

**Mover tracks are plannable floor:** `register_platform_cell` (0x417e20) walks the
platform's WHOLE track run through the empty cells and stamps a per-cell flag (=1),
the platform id, and the track z into a parallel per-cell grid (manager+0x2ab729
area). So the void cells a platform crosses are graph NODES at the track height;
only the step EXECUTION is gated on the car actually being there (§4). That's how an
enemy paths up to a mover gap, waits at the lip, boards, and rides across
(verified: Candy\Candy01 (1,8) type-0x0a mover).

**Rewrite port:** `src/sim/sim.c` `mover_track_z/ai_z/ai_walkable/ai_edge/
ai_next_dir/ai_target/ai_step` implement all of §2b/§2c plus the §3 target/cap table
(types 5/7 approximated) and §4 step gates; regression-locked against the footage
facts in `tests/test_ai.c` (`make test_ai`).

## 3. Behaviour types (pad3b[2] = catcher+0x62) & their target + cap  [data-driven]

`spawn_enemy(x,y,z, entity_mode, param6=tile.clip_rule)`:
- **pickup byte 2 → entity_mode=2 (CATCHER, kills on contact)**; `spawn_enemy(...,2,clip_rule)`.
- **pickup byte 3 → entity_mode=3 (THROWER, lobs bombs)**; `spawn_enemy(...,3,clip_rule)`.
- `param6` (the tile's **clip_rule**) becomes the AI behaviour type at catcher+0x62 = `pad3b[2]`.
  If `clip_rule >= 0x65` it is instead a model/skin variant and the AI type is reset to 0.
  So **the behaviour/aggro-range is authored per enemy in the map's clip_rule byte** — do NOT
  hardcode per-theme.

Per-frame in `game_tick`, target `(tx,ty)` and cap `opts` (written to `pad4a`, a little-endian
word) are chosen by `pad3b[2]`:

| pad3b[2] | target (tx,ty) | cap (opts) | notes |
|----------|----------------|-----------|-------|
| 0 (default) | **player grid pos** | **50** (`0x0032`) | the normal chaser |
| 1 | the level **exit/goal cell** (`pad9b2`, from spawn-scan type-4 cell) | 400 (`0x0190`) | walks to the exit |
| 2 | nearest **crystal** (type-7 cell via `FUN_0041b680`), else player | 100 (`0x0064`) | crystal-seeker; falls back to player when none |
| 3 | nearest **type-5 cell** (`find_cell_of_type`), else player | 150 (`0x0096`) | objective-seeker |
| 5 | another **catcher with entity_mode==2** (leader-follow), else idle | (per-target) | herding/escort |
| 7 | `FUN_00412530` target-finder, else idle | 150 | special |

`pad4a[0]='2'(0x32), pad4a[1]='\0'` is written **unconditionally at the top of every enemy's
iteration**, i.e. the default cap is 50; the table above overrides it for the non-zero types.
So the cap is re-derived every frame from the (static) behaviour type.

## 4. Move-selection algorithm (once a path exists)

`ai_pathfind_step_toward` (0x43a9d0):
1. If already mid-move (`move_turn_state != 0`) → do nothing (finish the current hop).
2. Compute path; take the first waypoint → cardinal command (deltas above).
3. **Facing gate (same as the player):** the enemy will only *commit a step* along its
   facing axis (`cmd == facing || cmd == opposite(facing)`). Otherwise it emits a **turn**
   command this frame and steps next frame. (Turn variants use `rotate_direction`, adding
   `+0x0a` to the base dir for turn-in-place, `last_move_cmd` 1=fwd/2=turnR/3=bwd/4=turnL.)
4. **Target-cell validation** (cancels the move, `buffered_move_command=0`, if):
   - target tile `type == 0` (empty/void) — never walks off into the void;
   - riding (`offs_011E != 0xff`) onto a `walkable_override` (bridge) cell;
   - standing on a `0x0e` jump-pad (must ride it, can't side-step off);
   - target is `0x09` (leaf/elevator) at the wrong z, or beyond a distance/height check;
   - target is `0x0c` (active moving-platform cell) whose z isn't currently matched.

So: **greedy consumption of a best-first A\* path**, one cardinal hop per move, subject to the
same facing/turn rule and tile gates the player obeys. It is NOT a per-frame greedy step and
NOT uniform-cost BFS.

## 5. Traversal of movers / portals / bridges  (question 3)

Walkability of a cell for the path graph — `is_cell_walkable(x,y)`:
- Walkable iff `type != 0` **or** `walkable_override(+0x1bc) != 0` (a deployed bridge plank makes
  an empty cell walkable). `type 0x16` (a stair variant) is excluded; `type 0x17` walkable only
  if occupied flag `+0x217` set. Mode-specific extras: mode 7 avoids forced-dir tiles whose
  dir is 3/4; mode 2 treats `0x11` (switch) as blocked unless occupied or forced-dir==4.

Edge/step legality — `tile_passable(from,to)` (shared with the player):
- **Jump-pad `0x0e` = a directional portal in the enemy graph.** Entry onto a `0x0e` is
  allowed only if the cell one-further in the entry direction sits at the pad's target height
  (`+0x1f1`); source `0x0e` is always exitable. So **enemies path *through* jump-pads** and get
  launched exactly like the player.
- **Elevator `0x09`**: passable only when the elevator's endpoints (`+0x1d3/+0x1d4`) match the
  neighbour's height — i.e. an enemy boards only when the car is aligned to the adjacent floor.
- **Moving platform (`0x0a/0x0b`, stamped `0x0c` at runtime)**: `ai_pathfind_step_toward` cancels
  the step onto a `0x0c` cell unless the platform's live z matches — boards only when aligned.
- **Redirect/forced-dir `0x10`**: always exitable; entry only from the direction consistent
  with its `slide_forced_dir(+0x1f2)`.
- **Teleporter `0x0f`**: NOT special-cased in the path graph — it is a normal walkable floor
  step; the actual warp happens in `skippy_tick` when the enemy *stands on it* (same passive
  mechanism as the player). Enemies therefore get teleported if lured onto one, but do not
  "plan" teleporter routes.
- **Stairs**: via `tile_type_is_stair`, height change of exactly +1 along the stair axis.
- Non-stair height change: only small climbs (`0 < to.z-from.z < 3`) and gated by
  `walkable_override==0`; glue `0x02` has a 2-cell look-ahead pass-through rule.

Net: enemies ARE "aware" of jump-pads (portal edges), elevators & platforms (conditional edges
when aligned) and bridges (edges when deployed). They are NOT aware of teleporters as routes.

## 6. Catch / attack + per-enemy timing  (question 4)

- **Catcher (entity_mode 2) kill:** in `game_tick`, 3D render-space distance
  `sqrt(dx²+dy²+dz²) < 0.5` (`_DAT_0045d318`) between enemy and player → sets player caught
  flag (`skippy.catcher.pad7[0]=1`).
- **Thrower (entity_mode 3) bomb:** in `enemy_ai_move_toward_target`, if `entity_mode==3` and
  `|tx-pos_x| < 2 && |ty-pos_y| < 2` (Chebyshev ≤ 1, incl. diagonal) and the throw timer has
  elapsed (`1999 < now_ms - last_ms`, i.e. **2000 ms cooldown**), it arms `pad6a[10]`, which
  `game_tick` turns into `bomb_spawn(...)`. Throwers keep chasing while throwing.
- Global freeze: `pad6b[1]` is set to 1 (halt) when `game_state != playing`, on level-complete
  (`state 3`), or while the player is dying (`skippy pad7[0]!=0`); 0 otherwise. This is a global
  pause, **not** a per-enemy aggro flag.
- Hop cadence uses `move_interval` (+0x132, double) / `last_move_time` (+0x146). Exact
  catcher-vs-thrower interval constants were not isolated here; `spawn_enemy` writes
  per-mode floats at catcher `+0x66/+0x6a` (mode2: 0/`3.9375`, mode3: 0/`4.1836`) — role
  unconfirmed (candidate: catch radius or hop timing). Rewrite's ENEMY_SLOW=2.5 and
  thrower×1.4 are feel-tuned, not yet matched to a verified constant.

## 7. Mapping to the current rewrite (`rewrite/src/sim/sim.c`)

| aspect | original | rewrite (`ai_step`/`ai_next_dir`) | verdict |
|--------|----------|-----------------------------------|---------|
| when chase runs | every frame, every enemy, no latch | every frame, every enemy | ✅ matches — no awake flag needed |
| aggro range | **bounded A\*, cap 50 expansions**, rooted at player | **unbounded BFS** to player | ❌ **THE BUG** — add the cap |
| search kind | best-first A\*, h=dx²+dy² (beelines) | uniform-cost BFS (shortest path) | ⚠ path shape differs; cap matters most |
| target | behaviour-typed (player / exit / crystal / …) from `clip_rule` | always the player | ❌ missing behaviour types (crystal/exit seekers) |
| facing/turn-then-step | yes (turn one frame, step next) | yes (`ai_step` turns then steps) | ✅ matches |
| passable rule | `tile_passable`: climbs +1/+2, jump-pad portal, elevator/platform when aligned, bridge when deployed | `ai_passable`: step or drop ≤2 | ⚠ close but not identical (see §5) |
| catch | 3D dist < 0.5 (mode-2 only) | dx²+dy²<0.36 & |dz|<1 | ⚠ radius slightly generous |
| thrower | Chebyshev ≤1, 2000 ms cd | Chebyshev ≤1, BOMB_CD 2.0 | ✅ matches |

**Minimum fix for the Space-Bonus bug:** bound the enemy search. Replace the unbounded BFS in
`ai_next_dir` with a capped best-first search (root at the player, expand toward the enemy,
heuristic dx²+dy², stop after `cap` pops → return "no move"), with `cap = 50` for a normal
catcher (and 100/150/400 for the clip_rule-driven behaviour types once those are implemented).
An equivalent quick approximation that also fixes the immediate bug: keep BFS but abort when
the popped-node count exceeds 50. This makes distant enemies dormant until the player closes
in, reproducing the one-by-one activation without any per-enemy state.

Second-priority: read the enemy's spawn-tile `clip_rule` into a behaviour-type field and add
the exit/crystal/objective seek targets (§3) instead of always targeting the player.

## Confidence
- Aggro = bounded-A\* cap (50), recomputed per frame, no latch: **HIGH** (decompiled directly
  from `ai_pathfind_astar` + the `pad4a`/`opts` flow in `game_tick`/`ai_pathfind_step_toward`).
- Behaviour-type table + clip_rule sourcing: **HIGH** for types 0/1/2/3 (seen in `game_tick`
  and `spawn_enemy`); MEDIUM for 5/7 (target-finder internals not fully traced).
- Jump-pad-as-portal, elevator/platform alignment, teleporter-passive: **HIGH/MEDIUM**
  (from `tile_passable` + `is_cell_walkable` + the step-validation block).
- Per-mode float `+0x66/+0x6a` meaning: **LOW** (unresolved).
