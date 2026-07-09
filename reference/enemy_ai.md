# Enemy AI (follow-player) — A* pathfinding

Enemies chase the player using grid A*. Full chain, all named in Ghidra:

## Call chain
```
enemy_ai_move_toward_target(this=enemy catcher, player_x, player_y)   0x412240
  -> ai_pathfind_step_toward(this, player_x, player_y, opts)          0x43a9d0
       -> ai_compute_path(enemy_x, enemy_y, player_x, player_y)       0x401c20  (guard/wrapper)
            -> ai_pathfind_astar(from, to)                            0x401db0  (the A* search)
       -> read first waypoint -> cardinal buffered_move_command
  -> skippy_tick(this)                                                0x438770  (executes the step)
```

## ai_compute_path (0x401c20) — guard wrapper
Validates both endpoints (`is_cell_walkable` 0x401cd0), skips if enemy & player are
in the same `grid_cell_id` (0x401cb0), resets search state (FUN_00401d60), runs A*.

## ai_pathfind_astar (0x401db0) — the algorithm
Textbook A* over the tile grid:
- **Node** = 0x44 (68) bytes: `+0 f`, `+4 cost`, `+8 g`, `+0x10 x`, `+0x14 y`,
  `+0x18 cell_id`, `+0x40 list-link`. Allocated via `calloc` (0x4507ff).
- **Heuristic** h = dx*dx + dy*dy (squared Euclidean).
- **open list** @ pathobj+6, **closed list** @ pathobj+10.
- Loop (capped at `pathobj[0x2f]` iterations):
  `cur = astar_pop_best_node()` (0x401ec0, lowest f); if null -> no path;
  if `cur.cell_id == goal` -> done; else `astar_expand_neighbors(cur, from)` (0x401ef0).
- **Searches BACKWARD**: start node = target(player), goal = source(enemy). So the
  result node chain (`pathobj+0xe`) starts adjacent to the enemy, and
  ai_pathfind_step_toward just reads the first node -> next cardinal step.

## ai_pathfind_step_toward (0x43a9d0) — waypoint -> move command
Reads next waypoint (`pathobj+0xe -> +0x10/+0x14`), converts delta to cardinal
`buffered_move_command` (dy=-1->1 up, dx=1->2 right, dy=1->3 down, dx=-1->4 left),
remaps to turn-commands if on a rotation tile (`pad8[3]==1`), then validates the
target cell (cancels if empty / occupied / hole 0x0e / leaf 0x09 wrong-height /
mistimed platform 0x0c). Enemy only steps along its facing axis (turns otherwise).

## Cross-confirms catcher fields
Enemy path writes `buffered_move_command@0x145` + `last_move_cmd@0x125` on the enemy
catcher — same fields/semantics as the player movers. pathfinder-state ptr @ catcher
+0x13b (its own struct: target @+0x31/32, result node @+0xe, open/closed @+6/+10,
maxiters @+0x2f — define during a future dig if needed).

## Pathfind helpers
grid_cell_id (0x401cb0), is_cell_walkable (0x401cd0), astar_pop_best_node (0x401ec0),
astar_expand_neighbors (0x401ef0), calloc (0x4507ff), pathfind_reset (0x401d60).
