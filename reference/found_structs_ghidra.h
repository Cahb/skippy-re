typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;



// Ensure types are defined only if they haven't been already.
// Windows headers often define these, so be careful.
typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;

// If _WIN32 is defined, we'll assume standard Windows headers provide these.


// Forward declarations for DirectX types (placeholder)
struct IDirectDrawSurface4;
struct IDirect3D3;
struct IDirect3DViewport3;
struct IDirect3DDevice3;
struct IDirectDraw4;
struct IDirectInputA;
typedef void* LPDIRECTDRAWSURFACE4;
typedef void* LPDIRECT3D3;
typedef void* LPDIRECT3DVIEWPORT3;
typedef void* LPDIRECT3DDEVICE3;
typedef void* LPDIRECTDRAW4;
typedef void* LPDIRECTINPUTA;

// --- Enums ---

typedef enum {
    SKIPPY_TILE_WOODEN = 1,
    SKIPPY_TILE_HONEYCOMB_GLUE = 2,
    SKIPPY_TILE_START_SPAWN = 3,
    SKIPPY_TILE_END_TELEPORT = 4,
    SKIPPY_TILE_LADDER_Y_NEG = 5,
    SKIPPY_TILE_LADDER_X_POS = 6,
    SKIPPY_TILE_LADDER_Y_POS = 7,
    SKIPPY_TILE_LADDER_X_NEG = 8,
    SKIPPY_TILE_LEAF_FALLS = 9,
    SKIPPY_TILE_NON_TEXTURED_CLIPPED = 10,
    SKIPPY_TILE_NON_TEXTURED_CLIPPED_11 = 11,
    SKIPPY_TILE_LEAF_STAYS_FALLS = 13,
    SKIPPY_TILE_HOLE = 14,
    SKIPPY_TILE_GAME_END_TELEPORT = 15,
    SKIPPY_TILE_BUSH_HONEYCOMB = 16,
    SKIPPY_TILE_NON_TEXTURED_CLIPPED_18 = 18,
    SKIPPY_TILE_NON_TEXTURED_CLIPPED_19 = 19
} SkippyTileType;

typedef enum {	// engine order from game_load_animation_file_gamefiles (0x401070) strcmp->slot map
	SKIPPY_CATCHER_ANIM_WALK_FORWARD = 0,
	SKIPPY_CATCHER_ANIM_WALK_BACKWARD = 1,
	SKIPPY_CATCHER_ANIM_SPEED_FORWARD = 2,
	SKIPPY_CATCHER_ANIM_SPEED_BACKWARD = 3,
	SKIPPY_CATCHER_ANIM_SLOW_FORWARD = 4,
	SKIPPY_CATCHER_ANIM_SLOW_BACKWARD = 5,
	SKIPPY_CATCHER_ANIM_CELEBRATION = 6,
	SKIPPY_CATCHER_ANIM_JUMP = 7,
	SKIPPY_CATCHER_ANIM_GLUE = 8,
	SKIPPY_CATCHER_ANIM_GHOST = 9,
	SKIPPY_CATCHER_ANIM_ICE = 10,
	SKIPPY_CATCHER_ANIM_FALL = 11,
	SKIPPY_CATCHER_ANIM_PARAGLIDE = 12,
	SKIPPY_CATCHER_ANIM_SLIDE = 13,
	SKIPPY_CATCHER_ANIM_IDLE1 = 14,
	SKIPPY_CATCHER_ANIM_IDLE2 = 15,
	SKIPPY_CATCHER_ANIM_FIELD_STAIR_UP = 16,
	SKIPPY_CATCHER_ANIM_FIELD_STAIR_DOWN = 17,
	SKIPPY_CATCHER_ANIM_STAIR_STAIR_UP = 18,
	SKIPPY_CATCHER_ANIM_STAIR_STAIR_DOWN = 19,
	SKIPPY_CATCHER_ANIM_STAIR_FIELD_UP = 20,
	SKIPPY_CATCHER_ANIM_STAIR_FIELD_DOWN = 21,
	SKIPPY_CATCHER_ANIM_TURN_LEFT = 22,
	SKIPPY_CATCHER_ANIM_TURN_RIGHT = 23,
} SkippyCatcherAnim;

typedef enum {	// theme_manager object-array slot; block = 0x2ef0 bytes, base @ theme_mgr+0x104 (i.e. offset = 0x104 + slot*0x2ef0). From game_load_theme_gamefiles(0x40c110) strcmp dispatch. See claude/theme_object_slots.md.
	THEME_OBJ_JOHN = 0,		// player
	THEME_OBJ_CATCHER = 1,		// enemy (chases) ; 1..4 = enemies
	THEME_OBJ_CATCHERFX = 2,
	THEME_OBJ_THROWER = 3,		// enemy (throws)
	THEME_OBJ_THROWERFX = 4,
	THEME_OBJ_PLATE = 5,
	THEME_OBJ_SIDE = 6,
	THEME_OBJ_PLATFORM = 7,
	THEME_OBJ_PARAGLIDE = 8,
	THEME_OBJ_PARAGLIDEFX = 9,
	THEME_OBJ_ELEVATOR = 10,
	THEME_OBJ_EXIT = 11,
	THEME_OBJ_GLUE = 12,
	THEME_OBJ_DESTRUCTFIELD = 13,
	THEME_OBJ_DESTRUCTFIELDFX = 14,
	THEME_OBJ_JUMPPAD = 15,
	THEME_OBJ_SLIDE = 16,
	THEME_OBJ_STAIR = 17,
	THEME_OBJ_TELEPORTER = 18,
	THEME_OBJ_CRYSTAL = 19,
	THEME_OBJ_CRYSTALFX = 20,
	THEME_OBJ_AMMUNITION = 21,
	THEME_OBJ_BOMB = 22,
	THEME_OBJ_EXPLOSION = 23,
	THEME_OBJ_SURPRISE = 24,
	THEME_OBJ_FREEZE = 25,
	THEME_OBJ_SPEED = 26,
	THEME_OBJ_SPEEDFX = 27,
	THEME_OBJ_COLLFX = 28,
	THEME_OBJ_LIFE = 29,
	THEME_OBJ_SWITCH = 30,
	THEME_OBJ_TIME = 31,
	THEME_OBJ_ICE = 32,
	THEME_OBJ_OBSTACLE = 33,
	THEME_OBJ_OBSTACLEFX = 34,
	THEME_OBJ_PROTECTION = 35,
	THEME_OBJ_PROTECTIONFX = 36,
	THEME_OBJ_BRIDGE = 37,
	THEME_OBJ_COUNT = 38
} SkippyThemeObject;

typedef enum {	// Sound event ID; theme "Sound <event> <wav>" dispatched by theme_parse_sound_line(0x4113e0). Categorized ID space (gaps reserved per category). See claude/sound_events.md.
	SND_MOVE_CATCHER = 0,
	SND_MOVE_JJ = 1,
	SND_MOVE_THROWER = 2,
	SND_MOVE_ICE_SLIDING = 3,
	SND_MOVE_SLIDING = 4,
	SND_MOVE_PARAGLIDING = 5,
	SND_MOVE_JUMPPAD = 7,		// note: 6 is reserved/unused
	SND_TELEPORTER = 8,
	SND_ELEVATOR = 9,
	SND_PLATFORM = 10,
	SND_SWITCH = 11,
	SND_GLUE = 12,
	SND_BRIDGE = 13,
	SND_DESTRUCT_START = 14,
	SND_DESTRUCT_REGEN = 15,
	SND_OBSTACLE = 16,
	SND_CRYSTAL = 30,
	SND_SPLAT_JJ = 40,		// squished death
	SND_SPLAT_CATCHER = 41,
	SND_SPLAT_THROWER = 42,
	SND_FALL_JJ = 50,		// fell-into-hole death
	SND_FALL_CATCHER = 51,
	SND_FALL_THROWER = 52,
	SND_BOMB_TICK = 60,
	SND_EXPLOSION_BOMB = 70,
	SND_EXPLOSION_CATCHER = 71
} SkippySoundEvent;

// ============================================================================
//  ROOT GLOBALS — top-level structs applied to fixed global ADDRESSES in Ghidra.
//  These show as "orphan" in a struct-member audit ONLY because they are the
//  ROOTS of the type tree (nothing contains them). They are NOT dead types.
//  To make each live in the decompiler: Ghidra GUI -> G <addr> -> T <struct>.
//    game_main_struct_instance @0x46c498 -> skippy_main_game_struct *  (POINTER to malloc'd ~5MB god object)
//    skippy_theme_manager_obj  @0x46c890 -> skippy_theme_manager_struct (INLINE static, 0x6fd90; in the
//                                           uninit image gap 0x46d000..0x4e2000 between .data and .rsrc)
//    world_tile_geometry       @0x4e0070 -> skippy_level_data_struct    (INLINE static; batched tile render geometry)
//  (Entities: the god object embeds skippy_obj (player) inline + catchers[]/throwers[] as pointers.)
// ============================================================================

// ===== Theme sub-mesh sub-types (from game_load_theme_gamefiles 0x40c110; see claude/theme_object_slots.md) =====
// These are the ELEMENT types for the EXISTING skippy_theme_manager_theme_cfg_struct (= a sub-mesh)
// and skippy_theme_manager_theme_struct (= an object block, cfg[8]) defined further below. Indexed by SkippyThemeObject.
#pragma pack(push, 1)
struct theme_ani_clip {			// one .ani clip slot; 24 per sub-mesh @ cfg+0xc9
	DWORD first_frame;
	DWORD num_frames;
	DWORD fps;
	DWORD flag;			// one-shot marker (optional 5th .ani token)
};						// 0x10

struct theme_texture {			// up to 8 per sub-mesh @ cfg+0x3cd (Texture{} entries)
	DWORD condition;		// @0x00 1=active 2=inactive 3=dead 4=alive 5=paraglide 6=? (Condition)
	DWORD tga_handle;		// @0x04 Texture <name> [Alpha] -> loaded tga handle
	DWORD unk_08;			// @0x08
	DWORD src_blend;		// @0x0c SrcBlend  (D3DBLEND: 1=zero 2=one 3=srccolor ...)
	DWORD dest_blend;		// @0x10 DestBlend (D3DBLEND)
	DWORD texaddr_mode;		// @0x14 TextureAdress: 1=wrap 2=mirror 3=clamp 4=border
	DWORD fx_anim_type;		// @0x18 1=flash 2=pulse 3=turn 4=wobble 5=environment 6=scroll
	float fx_param[3];		// @0x1c FX floats (used count depends on fx_anim_type)
	char pad_28[0x14];		// -> 0x3C
};						// 0x3C
#pragma pack(pop)

// --- Structures ---

#pragma pack(push, 1)
struct skippy_highscore_manager_record_struct
{
	BYTE data[55];
};
#pragma pack(pop)

#pragma pack(push, 1)
struct skippy_highscore_manager_struct
{
	char pad1 [5];
	struct skippy_highscore_manager_record_struct records[256];
	BYTE num_of_highscore_records;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct skippy_level_data_struct
{
	// world tile-render geometry, batched by tile type. HOME: global world_tile_geometry @0x4e0070.
	// Built by build_world_tile_geometry (0x404dd0) — counts tiles by type, allocates vertex buffers,
	// emits a quad per tile; consumed by main_render_func. Field names resolved from the builder's type->index map.
	float quad_template[32];		// @0x00 unit-quad corner templates (4 x 8 floats) + scratch
	DWORD num_of_floor_tiles;		// @0x80 tile type 1
	DWORD verticies_buf_ptr_floor_tiles;	// @0x84
	char exit_transform[24];		// @0x88 single Exit tile world pos+rot (builder fill case 4)
	DWORD num_of_leaf_tiles;		// @0xa0 type 9
	DWORD verticies_buf_ptr_leaf_tiles;
	DWORD verticies_buf_ptr_2_leaf_tiles;
	DWORD num_of_platform_tiles;		// @0xac count = num_of_platforms_on_lvl (moving-platform geometry)
	DWORD verticies_buf_ptr_platform_tiles;
	DWORD verticies_buf_ptr_2_platform_tiles;
	DWORD num_of_destruct_tiles;		// @0xb8 type 0x0d (destructible)
	DWORD verticies_buf_ptr_destruct_tiles;
	DWORD verticies_buf_ptr_2_destruct_tiles;
	DWORD num_of_jumppad_tiles;		// @0xc4 type 0x0e (jump pad / elevator-hole)
	DWORD verticies_buf_ptr_jumppad_tiles;
	DWORD verticies_buf_ptr_2_jumppad_tiles;
	DWORD num_of_teleporter_tiles;		// @0xd0 type 0x0f (teleporter / lose)
	DWORD verticies_buf_ptr_teleporter_tiles;
	DWORD verticies_buf_ptr_2_teleporter_tiles;
	DWORD num_of_glue_tiles;		// @0xdc type 2
	DWORD verticies_buf_ptr_glue_tiles;
	DWORD verticies_buf_ptr_2_glue_tiles;
	DWORD num_of_switch_tiles;		// @0xe8 type 0x11
	DWORD verticies_buf_ptr_switch_tiles;
	DWORD verticies_buf_ptr_2_switch_tiles;
	DWORD num_of_stairs_tiles;		// @0xf4 types 5-8 (ladder/stairs, per-dir angle)
	DWORD verticies_buf_ptr_stairs_tiles;
	DWORD verticies_buf_ptr_2_stairs_tiles;
	DWORD num_of_forceddir_tiles;		// @0x100 type 0x10 (arrow/redirect)
	DWORD verticies_buf_ptr_forceddir_tiles;
	DWORD verticies_buf_ptr_2_forceddir_tiles;
	DWORD num_of_ice_tiles;			// @0x10c type 0x15 (ice/slippery)
	DWORD verticies_buf_ptr_ice_tiles;
	DWORD verticies_buf_ptr_2_ice_tiles;
	DWORD num_of_obstacle_tiles;		// @0x118 type 0x17 (obstacle)
	DWORD verticies_buf_ptr_obstacle_tiles;
	DWORD verticies_buf_ptr_2_obstacle_tiles;
	DWORD num_of_side_verts;		// @0x124 side-wall (tile skirt) vertex count; built by build_world_side_walls (0x406530)
	DWORD verticies_buf_ptr_side_walls;	// @0x128 side-wall vertex buffer (vertical faces between tiles of differing height; theme "Side"/SideHeight)
	BYTE offs_012c_end;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct skippy_level_manager_tile_struct
{
	BYTE z_pos;
	BYTE type;
	BYTE clip_rule;			// cell walkability/collision: sticky / fall-through / clipped-invisible / clipped-visible (blocks entry; 3D object mesh is theme-only)
	BYTE pickup_type;
	// --- pad1[123] runtime state (cell offset = 4 + pad1 index). Split from gameplay_mechanics.md §7. ---
	char pad1a [5];				// pad1[0..4]: [0]=overhead/bridge surface z, [1..4]=bridge scratch (writers external, UNKNOWN)
	BYTE entity_reservation;		// pad1[5]  cell entered/reserved by an entity (blocks pathing)
	float floor_render_z;			// pad1[6]  cell floor world-Z; fall settles here
	BYTE platform_id;			// pad1[0x0a] moving-platform(type 0x0c) id -> rider offs_011E
	char pad1b [17];			// pad1[0x0b..0x1b]
	DWORD walkable_override;		// pad1[0x1c] nonzero => bridge makes an empty cell walkable
	BYTE platform_master_x;			// pad1[0x20] back-ref to platform anchor cell X
	BYTE platform_master_y;			// pad1[0x21] back-ref to platform anchor cell Y
	char pad1c [1];				// pad1[0x22]
	BYTE platform_cur_x;			// pad1[0x23] platform current grid X (on anchor cell)
	BYTE platform_cur_y;			// pad1[0x24] platform current grid Y
	char pad1d [1];				// pad1[0x25]
	float platform_world_y;			// pad1[0x26] platform live world Y (rider snap test)
	float platform_world_z;			// pad1[0x2a] platform live world Z
	float platform_world_x;			// pad1[0x2e] platform live world X
	char pad1e [27];			// pad1[0x32..0x4c]
	BYTE telep_pair_id;			// pad1[0x4d] teleporter(type 0x0f) pairing key
	BYTE telep_dest_x;			// pad1[0x4e] teleport destination grid X
	BYTE telep_dest_y;			// pad1[0x4f] teleport destination grid Y
	char pad1f [1];				// pad1[0x50]
	BYTE elevator_target_z;			// pad1[0x51] type 0x0e ballistic-rise target z
	BYTE slide_forced_dir;			// pad1[0x52] forced dir on slide/redirect tile (type 0x10)
	BYTE switch_group_id;			// pad1[0x53] switch(type 0x11) group id (INFERRED)
	BYTE bridge_obj_index;			// pad1[0x54] bridge object handle (INFERRED; build_bridge_object 0x419ed0)
	BYTE bridge_orientation;		// pad1[0x55] 1=X-bridge(0x12) 2=Y-bridge(0x13) (INFERRED)
	DWORD bridge_state;			// pad1[0x56] bridge runtime activation/extension state (INFERRED)
	char pad1g [8];				// pad1[0x5a..0x61]
	BYTE obstacle_hidden_pickup;		// pad1[0x62] pickup hidden under destructible(0x17); restored on death-retry
	DWORD pickup_collected_flag;		// pad1[0x63] render skip-draw when set
	char pad1h [8];				// pad1[0x67..0x6e]
	DWORD render_instance_handle;		// pad1[0x6f] cached render object handle for the cell
	float pickup_spin_phase;		// pad1[0x73] per-pickup random bob/spin phase
	DWORD occupied_by_entity;		// pad1[0x77] occupancy; walkability + glue/obstacle-blocking gate
};	// one grid CELL = 0x7f (127) bytes. grid is tiles[X][Y]: X-stride 0x319c(=100 cells), Y-stride 0x7f. Only first 4 bytes from .jjm.
#pragma pack(pop)

#pragma pack(push, 1)
struct skippy_level_manager_struct
{
	char pad1 [12];
	BYTE is_bonus_lvl;
	char world_name[1]; // Removed const, was causing issues
	char pad2 [2];
	BYTE unused_meta_name[128];	// @0x10 last string fread from .jjm; write-only (zero readers in .text) — internal/author comment
	BYTE level_display_name[128];	// @0x90 human-readable level title; write_level_report prints "Levelname: %s"
	BYTE world[128];		// @0x110 theme/world name -> themes\<world>.thm + CD track
	char pad3 [2];
	DWORD time_limit;		// @0x192 level par/time limit; skippy_game_start_level copies to timer base, game_tick times out -> death; write_level_report "Time"
	DWORD crystals_needed;
	BYTE level_dim_x;
	BYTE level_dim_Y;
	struct skippy_level_manager_tile_struct tiles[100][100];	// [X][Y] LIVE mutable grid (crystals removed, destructibles gone, etc.)
	struct skippy_level_manager_tile_struct tiles1[100][100];	// [X][Y] PRISTINE reset copy; restore_level_from_pristine(0x4184a0) copies this->tiles on death-retry
	char pad4 [2];	// was 4: trimmed 2 bytes so the god-object total = 0x51790d (engine new() size)
	BYTE offs_26c380_end;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct skippy_mdl_verticy_struct
{
	float x_pos;
	float y_pos;
	float z_pos;
	float vn_x_value;
	float vn_y_value;
	float vn_z_value;
	float u_value;			// tex coord set 0 (u)
	float v_value;			// tex coord set 0 (v)
	float u2_value;			// tex coord set 1 (u) -- FVF 0x212 = XYZ|NORMAL|TEX2 (two UV sets); was unk1
	float v2_value;			// tex coord set 1 (v) -- second texture layer; was unk2
};
#pragma pack(pop)

#pragma pack(push, 1)
struct skippy_mdl_animation_hdr_struct
{
	DWORD data[6];
};
#pragma pack(pop)

#pragma pack(push, 1)
struct skippy_mdl_struct
{
	DWORD mdl_struct_vtable;
	struct skippy_mdl_verticy_struct *anim_buf;
	DWORD num_of_verticies;
	struct skippy_mdl_animation_hdr_struct *anim_hdrs;
	WORD num_of_anims;
	char *mdl_name;
	char pad1 [96];
	struct skippy_mdl_verticy_struct *main_mdl_buf;
	BYTE offs_0079_end;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct skippy_catcher_struct
{
	DWORD *destructor_ptr;
	double sim_time_now;		// @0x04 current sim time ("now"), refreshed each tick from *sim_clock_ptr
	DWORD *sim_clock_ptr;		// @0x0c -> global sim-clock double
	DWORD *frame_dt_ptr;		// @0x10 -> global frame-delta double
	BYTE facing_direction;		// @0x14 1=TOP 2=RIGHT 3=DOWN 4=LEFT
	double frame_dt;		// @0x15 frame delta-time, refreshed from *frame_dt_ptr; fall/descent multiplier
	char pad2b [8];			// @0x1d
	float pos_y;			// @0x25 render X
	float pos_z;			// @0x29 render Z (height)
	float pos_x;			// @0x2d render Y
	BYTE pos_x_1;			// @0x31 grid X
	BYTE pos_y_1;			// @0x32 grid Y
	BYTE pos_z_1;			// @0x33 grid Z
	struct skippy_level_manager_struct *level_manager_obj;	// @0x34
	double idle_anim_interval;	// @0x38 idle-anim cycle interval (level/anim setup; copied to move_interval when idle)
	DWORD anim_phase_flag;		// @0x40 transient anim-phase flag (stair anims; recomputed each tick)
	DWORD moving_back_flag;		// @0x44 1 iff last_move_cmd==3 (moving backward)
	double settle_threshold;	// @0x48 settle/overshoot timeout (ctor default 20.0)
	double move_completion_time;	// @0x50 = last_move_time + move_interval (scheduled arrival)
	DWORD ice_slide_latch;		// @0x58 ice(0x15) auto-slide: holds move cmd to re-issue; 0 = not sliding
	float fall_velocity;		// @0x5c fall/jump parabola v0 (pure fall seeds -3.0)
	BYTE queued_turn_cmd;		// @0x60 staged turn cmd (rotated dir + 0x0a) when turning mid-move
	BYTE queued_turn_id;		// @0x61 queued turn id (2=right, 4=left, 0=none)
	BYTE ai_behavior_category;	// @0x62 enemy behavior: 3=crystal-pickup, 2=switch-tile, 4=re-pathfind
	BYTE last_pickup_type;		// @0x63
	WORD ai_pathfind_heading;	// @0x64 heading param passed to ai_pathfind_step_toward
	double default_move_interval;	// @0x66 default cell-cross time (ctor 200.0); copied to move_interval when not idle/sliding
	DWORD timing_dirty_latch;	// @0x6e nonzero suppresses reset of move_interval to default
	double anim_start_time;		// @0x72 snapshot of now when an animation begins
	DWORD pause_sub_flag;		// @0x7a pause/settle sub-flag
	DWORD pause_complete_flag;	// @0x7e set when settle elapsed >= threshold
	DWORD pause_started_flag;	// @0x82 0->start pause & save time; 1->timing
	DWORD pause_active_flag;	// @0x86 settle/pause active (another entity occupies this tile - collision wait)
	double pause_start_time;	// @0x8a copy of now when pause began
	char pad4b_spare [8];		// @0x92 (no observed access)
	BYTE current_anim_pos;		// @0x9a RENDER-ID (NOT SkippyCatcherAnim slot): walk_fwd=0x14,walk_back=0x15,ice=3,slide=4,paraglide=5,fall=8,glue=9,ghost=0xa,jump=0xb,stairs=0x16-0x1b,turnR=0x1e,turnL=0x1f,idle=0xfa/0xfb. anim_renderid_to_clip(0x401970) maps it to a clip slot.
	DWORD pad5 [15];		// @0x9b per-effect SOUND-ENABLE flags/handles: pad5[N]!=0 gates+is the Sound obj for effect N ([3,5,6,7,8,9,12]->sound_play, [4,10,11]->sound_stop, [13]->0x442d90/df0). pad5[0]=init flag, pad5[14]=pending auto-turn flag (not sound).
	char pad5x [1];			// @0xd7
	WORD num_of_hops_from_death;	// @0xd8
	char pad6a_head [2];		// @0xda
	DWORD attack_target_lo;		// @0xdc enemy attack-lunge target (saved player pos lo)
	DWORD attack_target_hi;		// @0xe0 attack-lunge target hi
	DWORD attack_active_flag;	// @0xe4 set when enemy adjacent to player (catch active)
	char pad6a_tail [1];		// @0xe8
	BYTE bounce_count;		// @0xe9 bounces remaining on a hard landing
	DWORD airborne_flag;		// @0xea (read as int) 0=free parabola, !=0=settling to tile top
	BYTE warp_entry_dir;		// @0xee = move_turn_state when stepping onto a warp tile (type 0xf)
	DWORD interruptible_lock;	// @0xef nonzero cancels the buffered move (non-interruptible state)
	char pad6b_mid [8];		// @0xf3
	DWORD forced_walk_flag;		// @0xfb nonzero forces walk anim (0x14) + render-z refresh (post-slide)
	BYTE warp_phase;		// @0xff warp/teleport phase 0 idle -> 1 charging -> 2 emerging (type 0xf)
	double warp_phase_start_time;	// @0x100 warp-phase start time (paired w/ warp_phase)
	BYTE stored_move_dir;		// @0x108 last/stored move dir; re-issued on ice-slide + jump-pad exit
	char pad6c [8];			// @0x109
	BYTE arc_start_z;		// @0x111 launch/fall arc start height (grid z)
	double ride_start_time;		// @0x112 sim-time the jump-pad/elevator ride began
	DWORD ride_active_flag;		// @0x11a (read as int) elevator/jump-pad ride in progress
	BYTE offs_011E;			// @0x11e riding-platform id (0xFF = none)
	BYTE anim_lock_phase;		// @0x11f anim-lock/move-phase (0 or 4): while nonzero forces buffered cmd=0, blocks anim reset
	DWORD is_falling;		// @0x120 4-byte flag (single dword MOV)
	BYTE facing_deriv_mode;		// @0x124 (ctor=1) 0->facing=move_turn_state; else for states 11-19 facing=state-10
	BYTE last_move_cmd;		// @0x125 move VARIANT: 1=fwd 2=turnR 3=back 4=turnL
	double glue_stuck_start_time;	// @0x126 glue(type 9) stuck start time (0=not stuck)
	DWORD stair_transition_code;	// @0x12e stair step code (1=up-step, 2=flat/down); selects stair anim; correlates move_dz
	double move_interval;		// @0x132 time to cross one cell
	char pad9b1_head [1];		// @0x13a
	DWORD sub_object_ptr;		// @0x13b -> allocated per-entity sub-object (0=none; alloc 0x450e9d + init 0x401bb0)
	char move_dx;			// @0x13f grid X step this move (-1/0/+1)
	char move_dy;			// @0x140 grid Y step
	char move_dz;			// @0x141 grid Z step (stairs/ladders; signed)
	char pad9b2 [3];		// @0x142
	BYTE buffered_move_command;	// @0x145 queued move DIRECTION (1=TOP 2=RIGHT 3=DOWN 4=LEFT)
	double last_move_time;		// @0x146 sim-time current move started
	int move_turn_state;		// @0x14e active move dir in progress (0=idle)
	BYTE entity_mode;		// @0x152 9=ghost/dead
	char pad10c [3];		// @0x153
	DWORD control_reversal_flag;	// @0x156 (ctor=1) gates whether backward(3)/turn(4) cmds apply vs abort move_turn_state
	char pad10d [1];		// @0x15a
	BYTE ai_target_x;		// @0x15b stored AI target grid X
	BYTE ai_target_y;		// @0x15c stored AI target grid Y
	char pad_tail [3];		// @0x15d
	BYTE end_of_pad;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct skippy_thrower_struct
{
	struct skippy_catcher_struct catcher;
	char pad1 [19];
	BYTE end_of_pad;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct skippy_player_struct		// embedded catcher base (0x161) + player-only fields; @god+0x1751c9; size 0x264
{
	struct skippy_catcher_struct catcher;	// @0x00 (0x161)
	char pad_161 [0x69];			// @0x161
	char powerup_effects [0x48];		// @0x1ca timed power-up state (speed/freeze/protect + speed variants): flag+start-time dword pairs; includes a saved vec3 @0x202/0x206/0x20a
	float per_level_default;		// @0x212 per-level default float (NOT a crystal count; old num_of_crystals_actual was mislabeled — it's a float, fstp @0x416672)
	char pad_216 [4];			// @0x216
	WORD total_pickups;			// @0x21a incremented on EVERY pickup of any type (pickup handler 0x41fcb0)
	char pad_21c [0x15];			// @0x21c
	double level_start_time;		// @0x231 sim-clock snapshot at play start (game_tick copies god+0x170a54 -> god+0x1753fa)
	DWORD lives;				// @0x239 lives (god+0x175402); heart pickup(type 7)++, game_tick decrements on death, restore_from_pristine
	DWORD crystals_collected;		// @0x23d crystals this level (god+0x175406); game_tick compares <= level_manager.crystals_needed for completion; reset at level-complete
	char pad_tail [0x23];			// @0x241
};
#pragma pack(pop)

#pragma pack(push, 1)
struct skippy_sound_manager_struct	// DirectSound SFX manager (CD audio is the separate cd_manager); ctor sound_manager_constructor 0x4430e0
{
	void *vtable;			// @0x00
	DWORD directsound_device;	// @0x04 IDirectSound*
	DWORD owns_directsound;		// @0x08 bool: created the device (Release gate)
	DWORD sound3d_enable;		// @0x0c bool: 3D-sound enable (tentative)
	DWORD unk_10;			// @0x10 (zeroed)
	char dup_pool [0x78];		// @0x14 voice/buffer duplication-pool sub-object (dup ctx @+0x20)
	DWORD sound_enabled;		// @0x8c bool: sound system initialized (create methods early-out if 0)
	DWORD buffer_caps_flags;	// @0x90 buffer caps/creation flags (default 2)
	char static_sound_registry [4];	// @0x94 StaticSoundbuffer registry obj (vtable)
	DWORD static_sound_list_head;	// @0x98 -> linked list of Sound objs (node: sound@+0x100, next@+0x104)
	char pad_9c [8];		// @0x9c
	char multi_sound_registry [4];	// @0xa4 MultiStaticSoundbuffer registry obj
	DWORD multi_sound_list_head;	// @0xa8 -> linked list head
	char pad_ac [14];		// @0xac
	char lock [24];			// @0xba CRITICAL_SECTION guarding sound ops
	char pad_tail [306];		// @0xd2 (unmapped)
};
#pragma pack(pop)

#pragma pack(push, 1)
struct skippy_savegame_manager_save_struct
{
	BYTE savename[10];
	char pad1 [4];
	BYTE current_lvl;
	BYTE hearts_left;
	WORD total_score;
	char pad2 [23];
	BYTE offs_0029_end;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct skippy_savegame_manager_struct
{
	char pad1 [48];
	BYTE num_of_savegames;
	struct skippy_savegame_manager_save_struct savegames[6];
	char pad2 [722];
	BYTE offs_03ff_end;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct skippy_main_game_struct
{
	char pad1 [4];
	BYTE offs_0004;
	char pad2 [3];
	BYTE offs_0008;
	char pad3 [3];
	BYTE offs_000c;
	char pad4 [3];
	BYTE offs_0010;
	char pad5 [7];
	BYTE offs_0018;
	char pad6 [3];
	BYTE offs_001c;
	char pad7 [131612];
	BYTE offs_20239;
	char pad8 [8197];
	BYTE cd_manager_obj;
	char pad9 [65310];
	char level_descriptions[256][256];	// @0x3215e: .gam level table, 256 bytes/level, deobfuscated (byte-5). Loaded by game_load_jj_gamefile.
	BYTE num_of_levels;	// @0x4215e: from .gam 4th byte (after 06 06 06 magic)
	char gamefile_name[128];
	WORD num_of_teleps_on_lvl;
	WORD num_of_obstacles_on_lvl;
	WORD num_of_fields_on_lvl;
	char pad10 [4];
	WORD num_of_bridges_on_lvl;
	char pad11 [2];
	WORD num_of_glue_on_lvl;
	WORD num_of_slides_on_lvl;
	WORD num_of_ices_on_lvl;
	WORD num_of_freezes_on_lvl;
	WORD num_of_times_on_lvl;
	WORD num_of_lives_on_lvl;
	WORD num_of_protects_on_lvl;
	WORD num_of_crystals_collected;
	WORD num_of_hearts_collected;
	WORD num_of_paraglides_on_lvl;
	WORD num_of_speeds_on_lvl;
	WORD num_of_bombs_on_lvl;
	char pad12 [4];
	WORD num_of_jumps_on_lvl;
	char pad13 [7];
	DWORD score_number;
	char pad14 [47];
	WORD num_of_bonus_levels;
	WORD num_of_total_level_extra_objects;
	char pad15 [9];
	BYTE num_of_crystals_on_lvl;
	char pad16 [1];
	BYTE offs_42254;
	char pad17 [3];
	BYTE offs_42258;
	char pad18 [26811];
	DWORD current_lvl_idx;
	BYTE current_lvl_name[128];
	BYTE offs_48b98;
	char pad19 [999439];
	struct skippy_sound_manager_struct sound_manager_obj;
	BYTE offs_13cdac;
	char pad36 [14];
	struct skippy_highscore_manager_struct highscore_manager_obj;
	char pad37 [196738];
	BYTE offs_170543;
	char pad20 [1279];
	BYTE offs_170a43;
	char pad21 [41];
	BYTE offs_170a6d;
	char pad22 [14];
	struct skippy_savegame_manager_struct savegame_manager_obj;	// @0x170a7c (0x400; savegame_manager_constructor 0x43b390; set-defaults 0x43b560)
	char pad23 [10396];		// was offs_170a7c(1)+11419; savegame_manager_obj is 0x400 -> pad = 11420-0x400
	WORD num_of_platforms_on_lvl;
	char pad24 [1023];
	WORD num_of_elevators_on_lvl;
	char pad25 [803];
	WORD num_of_destr_on_lvl;
	struct skippy_thrower_struct *throwers[256];
	char pad26 [975];
	BYTE offs_17460f;
	char pad27 [500];
	struct skippy_catcher_struct *catchers[256];
	char pad28 [976];
	BYTE num_of_enemies_on_lvl;
	char pad29 [500];
	struct skippy_player_struct skippy_obj;
	BYTE offs_17531d;
	char pad30 [233];	// was 236: trimmed 3 bytes so menu/JJS/config/level land at their constructor offsets (ground truth: ctor @0x4145c0)
	BYTE offs_175517;
	BYTE menu_manager_obj;		// @0x175518 menu manager sub-object (vtable @0x45d41c). All fields below are inside it.
	char pad31a [7];			// +0x00: (+3 int) = input-enabled gate checked before ESC/ENTER in menu_update
	WORD menu_confirmed_id;		// @0x175520 (pad31+0x07) committed/displayed menu id; ENTER only fires when == menu_screen_id
	char pad31b [19];			// +0x09
	BYTE menu_selected_item;	// @0x175535 (pad31+0x1c) currently highlighted item index (UP/DOWN move it)
	char pad31c [255];			// +0x1d
	BYTE menu_item_count_by_id [0xff];	// @0x175635 (pad31+0x11c) per-menu-id item count; up/down disabled when <=1
	BYTE menu_action_table [0x20000];	// @0x175734 (pad31+0x21b) action code = table[menu_id*0xff + selected_item]; stride 0xff. big switch in menu_update dispatches on it (see claude/menu_manager.md)
	BYTE menu_screen_id;		// @0x195734 (pad31+0x2021b) current menu screen id (0=main,2,4,5,0xa..); switched on by menu_update(0x418d20) + draw_menu_screen(0x42e000)
	BYTE offs_195735;
	char pad32 [1004536];
	BYTE offs_28ab2e;
	char pad33 [133725];
	BYTE game_state;
	struct skippy_level_manager_struct level_manager_obj;
	BYTE offs_51790d_end;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct skippy_theme_manager_unk_struct
{
	char pad1 [27];
	BYTE offs_001b_end;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct skippy_theme_manager_theme_cfg_struct		// a THEME SUB-MESH (0x5DD=1501). An object block = cfg[8]. (was 0x5DC; +1 stride fix proven from parser ASM: submesh_base = obj+idx*0x5DD)
{
	char pad_00 [8];		// @0x00 (in cfg[0], bytes +4 alias the object's sub-mesh count)
	DWORD type;			// @0x08 1=Model 2=Field 3=Billboard 4=ParticleSystem 0=load-fail
	struct skippy_mdl_struct *model_handle;	// @0x0c loaded mesh (load_model result); wires submesh -> skippy_mdl_struct
	char pad_10 [0xd];		// @0x10
	char explode_name [0x9c];	// @0x1d Explode target name
	DWORD explode_enabled;		// @0xb9 Explode flag
	char pad_bd [0xc];		// @0xbd
	struct theme_ani_clip anim_clips [24];	// @0xc9 .ani clip table (== anim system's "cfg+0xc9")
	float billboard_size;		// @0x249 Billboard size (Model-type: geometry ptr set at load)
	DWORD ps_handle0;		// @0x24d ParticleSystem handle[0]
	char pad_251 [0x14c];		// @0x251 (extra particle handles etc.)
	DWORD ps_extra_count;		// @0x39d
	DWORD ps_subflag;		// @0x3a1
	float position [3];		// @0x3a5 Position xyz
	float scale [3];		// @0x3b1 Scale xyz
	float rotate [3];		// @0x3bd Rotate xyz
	DWORD texture_count;		// @0x3c9
	struct theme_texture textures [8];	// @0x3cd
	DWORD lit;			// @0x5ad Lit
	DWORD nomovestates;		// @0x5b1 NoMoveStates (== anim system's "cfg+0x5b1")
	DWORD nozwrite;			// @0x5b5 NoZWrite
	DWORD noshadow;			// @0x5b9 NoShadow
	DWORD specular;			// @0x5bd Specular
	DWORD randomyangle;		// @0x5c1 RandomYAngle
	DWORD oscillate_random;		// @0x5c5 Oscillate 'random' sub-flag
	float oscillate [3];		// @0x5c9 Oscillate params
	float pump [2];			// @0x5d5 Pump[0..1]; [2..3] overflow into next cfg's head (benign)
};						// 0x5DD
#pragma pack(pop)

#pragma pack(push, 1)
struct skippy_theme_manager_theme_struct		// one THEME OBJECT block (0x2ef0); slot index = SkippyThemeObject
{
	struct skippy_theme_manager_theme_cfg_struct cfg[8];	// @0x00  (cfg[0] head +4 = sub-mesh count, written at object close)
	char pad1 [7];			// @0x2ee8  (cfg[8]=0x2ee8; was 15 when cfg was 0x5DC)
	BYTE offs_2eef_end;		// @0x2eef
};
#pragma pack(pop)

#pragma pack(push, 1)
struct skippy_theme_manager_enviroment_struct	// theme "Environment {}" block @ theme_mgr+0x6f8a4; each directive stores at base+4*idx
{
	DWORD hud_tex;			// cfg[0]  HUD base texture
	DWORD menu_tex;			// cfg[1]  Menu texture
	DWORD edge_tex;			// cfg[2]  Edge texture
	DWORD radar_tex;		// cfg[3]  Radar texture
	DWORD pointer_tex;		// cfg[4]  Pointer texture
	DWORD freeze_icon;		// cfg[5]  Freeze powerup HUD icon
	DWORD inversecontrol_icon;	// cfg[6]  InverseControl HUD icon (a texture, NOT a bool)
	DWORD protection_icon;		// cfg[7]  Protection HUD icon
	DWORD slowdown_icon;		// cfg[8]  Slowdown HUD icon (a texture, NOT a bool)
	DWORD speed_icon;		// cfg[9]  Speed powerup HUD icon
	DWORD hud_text_color[2];	// cfg[10/11] HUDTextColors (normal, highlight) RGB
	DWORD menu_text_colors[50];	// cfg[12..61] 25 Menu*TextColors screens x (normal,highlight); order in claude/theme_format.md
	BYTE fog_enabled;		// @0xf8 Fog directive present (mode/color/planes go to render obj via vtable, not stored here)
	DWORD skybox_cfg;		// @0xf9 start of skybox loader object (sky <base> -> 6 cube faces %s_RT/LF/BK/FR/DN/UP.tga)
};
#pragma pack(pop)

#pragma pack(push, 1)
struct skippy_theme_manager_struct
{
	char pad1 [260];
	struct skippy_theme_manager_theme_struct themes[38];
	struct skippy_theme_manager_enviroment_struct env;
	char pad2 [4];
	struct skippy_theme_manager_unk_struct unkn1[6];
	char pad3 [834];
	BYTE offs_6fd8f_end;
};
#pragma pack(pop)
