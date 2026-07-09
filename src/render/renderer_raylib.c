/* raylib backend for renderer.h. The ONLY file that includes raylib.h.
 * Meshes/textures live in small fixed pools indexed by handle. */
#include "render/renderer.h"

#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <stdlib.h>
#include <string.h>

#define MAX_MESHES  256
#define MAX_TEX     256

/* We keep meshes as CPU vertex arrays and draw them via rlgl immediate mode
 * (the same path the textured tiles use). This sidesteps DrawMesh/material
 * shader quirks and is the natural fit for vertex-morph animation, where the
 * vertex data changes every frame anyway. */
typedef struct { mdl_vertex *v; int nverts; int nframes; } cpu_mesh;

static cpu_mesh  g_meshes[MAX_MESHES];
static int       g_mesh_count = 0;
static Texture2D g_tex[MAX_TEX];
static int       g_tex_count = 0;

static void tex_quad(unsigned tid, Color col, float umin, float vmin, float umax, float vmax,
                     float ax, float ay, float az, float bx, float by, float bz,
                     float cx, float cy, float cz, float dx, float dy, float dz);

/* alpha-test (cutout) shader: discards transparent fragments in the OPAQUE world pass, so
 * a texture the .thm marks "Alpha" (elevator, side vines, ...) cuts out instead of showing
 * its transparent parts as black — without needing a sorted transparent pass. */
static Shader g_alpha_shader;
static bool   g_alpha_ok;

bool r_init(int width, int height, const char *title) {
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(width, height, title);
    if (!IsWindowReady()) return false;
    SetExitKey(0);        /* ESC belongs to the menus; quit via the menu / window X */
    SetTargetFPS(60);
    rlDisableBackfaceCulling();   /* M1 blockout: draw both sides; revisit at M5 */
    const char *fs =
        "#version 330\n"
        "in vec2 fragTexCoord; in vec4 fragColor;\n"
        "uniform sampler2D texture0; uniform vec4 colDiffuse;\n"
        "out vec4 finalColor;\n"
        "void main(){ vec4 c = texture(texture0, fragTexCoord)*colDiffuse*fragColor;\n"
        "  if (c.a < 0.5) discard; finalColor = c; }\n";
    g_alpha_shader = LoadShaderFromMemory(0, fs);
    g_alpha_ok = (g_alpha_shader.id != 0);
    return true;
}

/* enable/disable the cutout shader around alpha-marked draws in the opaque pass. */
void r_alpha_test(int on) {
    if (!g_alpha_ok) return;
    if (on) BeginShaderMode(g_alpha_shader);
    else    EndShaderMode();
}

void r_reset(void) {
    for (int i = 0; i < g_mesh_count; i++) MemFree(g_meshes[i].v);
    for (int i = 0; i < g_tex_count; i++) UnloadTexture(g_tex[i]);
    g_mesh_count = g_tex_count = 0;
}

/* --- audio --- */
/* Each sound gets a small pool of aliases (share the sample data) so rapid
 * re-triggers play on a free voice and are allowed to FINISH instead of
 * cutting the previous instance off. When all voices are busy we skip. */
#define MAX_SND    64
#define SND_VOICES 6
static Sound g_snd[MAX_SND];                 /* owns the sample data */
static Sound g_voice[MAX_SND][SND_VOICES];   /* aliases used for playback */
static int   g_snd_count = 0;
static bool  g_audio_on = false;

void r_audio_init(void) {
    InitAudioDevice();
    g_audio_on = IsAudioDeviceReady();
}

void r_unload_sounds(void) {
    if (g_audio_on)
        for (int i = 0; i < g_snd_count; i++) {
            for (int v = 0; v < SND_VOICES; v++) UnloadSoundAlias(g_voice[i][v]);
            UnloadSound(g_snd[i]);
        }
    g_snd_count = 0;
}

void r_audio_shutdown(void) {
    r_unload_sounds();
    r_unload_music();
    r_track_stop();
    if (g_audio_on) { CloseAudioDevice(); g_audio_on = false; }
}

r_sound r_load_sound(const char *path) {
    if (!g_audio_on || g_snd_count >= MAX_SND) return -1;
    Sound s = LoadSound(path);
    if (s.frameCount == 0) return -1;
    g_snd[g_snd_count] = s;
    for (int v = 0; v < SND_VOICES; v++) g_voice[g_snd_count][v] = LoadSoundAlias(s);
    return g_snd_count++;
}

/* master/music/sfx gains, applied at the playback chokepoints below. */
static float g_vol[3] = { 1.0f, 1.0f, 1.0f };

void r_set_volume(int kind, float vol) {
    if (kind < 0 || kind > 2) return;
    g_vol[kind] = vol < 0.0f ? 0.0f : vol > 1.0f ? 1.0f : vol;
}

/* volume 0..1, pan 0..1 (raylib: 0.5 = centre). Plays the first idle voice. */
void r_play_sound_ex(r_sound s, float vol, float pan) {
    if (!g_audio_on || s < 0 || s >= g_snd_count) return;
    for (int v = 0; v < SND_VOICES; v++) {
        if (!IsSoundPlaying(g_voice[s][v])) {
            SetSoundVolume(g_voice[s][v], vol * g_vol[R_VOL_MASTER] * g_vol[R_VOL_SFX]);
            SetSoundPan(g_voice[s][v], pan);
            PlaySound(g_voice[s][v]);
            return;
        }
    }
    /* all voices busy: let them finish, drop this trigger */
}

void r_play_sound(r_sound s) { r_play_sound_ex(s, 1.0f, 0.5f); }

void r_stop_sound(r_sound s) {
    if (!g_audio_on || s < 0 || s >= g_snd_count) return;
    for (int v = 0; v < SND_VOICES; v++)
        if (IsSoundPlaying(g_voice[s][v]))
            StopSound(g_voice[s][v]);
}

bool r_sound_playing(r_sound s) {
    if (!g_audio_on || s < 0 || s >= g_snd_count) return false;
    for (int v = 0; v < SND_VOICES; v++)
        if (IsSoundPlaying(g_voice[s][v]))
            return true;
    return false;
}

/* --- gapless looping ambience via a raw AudioStream fed from a ring buffer ---
 * raylib's Music loops by SEEKING to 0 on end, which inserts a small gap — very
 * audible on a short clip (the ~1.5s fountain "cuts off + restarts"). Instead we hold
 * the whole clip as PCM and refill the stream by wrapping the read cursor modulo the
 * clip length: samples flow contiguously across the loop point, so it's truly gapless. */
#define MAX_MUSIC   24   /* .leo ambience + the six state-loop streams */
#define AMB_CHUNK   1024            /* frames fed per sub-buffer (matches stream buffer) */
typedef struct {
    AudioStream stream;
    short      *pcm;                /* interleaved 16-bit samples (frames * channels) */
    int         frames, channels, cursor;
    bool        active;
} amb_stream;
static amb_stream g_music[MAX_MUSIC];
static int        g_music_count = 0;

r_music r_load_music(const char *path) {
    if (!g_audio_on || g_music_count >= MAX_MUSIC) return -1;
    Wave w = LoadWave(path);
    if (w.frameCount == 0) return -1;
    if (w.sampleSize != 16 || w.channels > 2)
        WaveFormat(&w, w.sampleRate, 16, w.channels > 2 ? 2 : w.channels);
    amb_stream *a = &g_music[g_music_count];
    a->frames = (int)w.frameCount;
    a->channels = (int)w.channels;
    a->cursor = 0;
    a->pcm = malloc((size_t)a->frames * a->channels * sizeof(short));
    if (!a->pcm) { UnloadWave(w); return -1; }
    memcpy(a->pcm, w.data, (size_t)a->frames * a->channels * sizeof(short));
    UnloadWave(w);
    SetAudioStreamBufferSizeDefault(AMB_CHUNK);
    a->stream = LoadAudioStream((unsigned)w.sampleRate, 16, (unsigned)a->channels);
    PlayAudioStream(a->stream);
    a->active = true;
    return g_music_count++;
}

/* call every frame: set the mix, then refill any free sub-buffers, wrapping the read
 * cursor around the clip so the loop is seamless. vol 0..1, pan -1..1 (0 = centre). */
void r_music_update(r_music h, float vol, float pan) {
    if (!g_audio_on || h < 0 || h >= g_music_count || !g_music[h].active) return;
    amb_stream *a = &g_music[h];
    /* ambience/state loops are world SFX (the options "3D sound" bucket), not
     * music — the CD track (r_track_*) is the only R_VOL_MUSIC consumer. */
    SetAudioStreamVolume(a->stream, vol * g_vol[R_VOL_MASTER] * g_vol[R_VOL_SFX]);
    SetAudioStreamPan(a->stream, pan);
    while (IsAudioStreamProcessed(a->stream)) {
        short tmp[AMB_CHUNK * 2];              /* up to stereo */
        int ch = a->channels;
        for (int f = 0; f < AMB_CHUNK; f++) {
            for (int c = 0; c < ch; c++)
                tmp[f * ch + c] = a->pcm[a->cursor * ch + c];
            if (++a->cursor >= a->frames) a->cursor = 0;   /* wrap = seamless loop point */
        }
        UpdateAudioStream(a->stream, tmp, AMB_CHUNK);
    }
}

void r_unload_music(void) {
    if (g_audio_on)
        for (int i = 0; i < g_music_count; i++) {
            StopAudioStream(g_music[i].stream);
            UnloadAudioStream(g_music[i].stream);
            free(g_music[i].pcm);
        }
    g_music_count = 0;
}

/* --- background-music track: raylib Music = true disk streaming (the CD rips are
 * 25-50MB WAVs; the amb_stream path above would hold all of that as PCM). One at a
 * time, like CD audio. Survives level reloads (not touched by r_unload_music). */
static Music g_track;
static bool  g_track_on = false;

bool r_track_play(const char *path, bool loop) {
    r_track_stop();
    if (!g_audio_on) return false;
    g_track = LoadMusicStream(path);
    if (g_track.frameCount == 0) return false;
    g_track.looping = loop;
    PlayMusicStream(g_track);
    g_track_on = true;
    return true;
}

void r_track_stop(void) {
    if (!g_track_on) return;
    StopMusicStream(g_track);
    UnloadMusicStream(g_track);
    g_track_on = false;
}

void r_track_set_paused(bool paused) {
    if (!g_track_on) return;
    if (paused && IsMusicStreamPlaying(g_track))
        PauseMusicStream(g_track);
    else if (!paused && !IsMusicStreamPlaying(g_track))
        ResumeMusicStream(g_track);
}

void r_track_update(void) {
    if (!g_track_on) return;
    SetMusicVolume(g_track, g_vol[R_VOL_MASTER] * g_vol[R_VOL_MUSIC]);
    UpdateMusicStream(g_track);
}

void r_shutdown(void) {
    r_audio_shutdown();
    r_reset();
    if (IsWindowReady()) CloseWindow();
}

bool r_should_close(void) { return WindowShouldClose(); }

r_tex r_load_texture_rgba(const uint8_t *rgba, int w, int h) {
    if (g_tex_count >= MAX_TEX) return -1;
    Image img = {
        .data = (void *)rgba,               /* LoadTextureFromImage copies to GPU */
        .width = w, .height = h, .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
    };
    Texture2D t = LoadTextureFromImage(img);
    SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    g_tex[g_tex_count] = t;
    return g_tex_count++;
}

void r_texture_repeat(r_tex tex) {
    if (tex < 0 || tex >= g_tex_count) return;
    SetTextureWrap(g_tex[tex], TEXTURE_WRAP_REPEAT);   /* so scrolling UVs tile (glue flow) */
}

/* load a colour BMP and take its alpha from a companion mask BMP (mask R channel).
 * For the menu panel (MENU.bmp + menu_mask.bmp). Falls back to opaque if the mask
 * can't be loaded. -1 on failure of the colour image. */
r_tex r_load_masked_bmp(const char *color_path, const char *mask_path) {
    if (g_tex_count >= MAX_TEX) return -1;
    Image ci = LoadImage(color_path);
    if (ci.data == NULL) return -1;
    ImageFormat(&ci, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    Image mi = LoadImage(mask_path);
    if (mi.data) {
        ImageFormat(&mi, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        unsigned char *c = ci.data, *m = mi.data;
        int n = ci.width * ci.height, mn = mi.width * mi.height;
        for (int i = 0; i < n && i < mn; i++)
            c[i * 4 + 3] = m[i * 4];        /* mask R -> alpha */
        UnloadImage(mi);
    }
    Texture2D t = LoadTextureFromImage(ci);
    SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    UnloadImage(ci);
    g_tex[g_tex_count] = t;
    return g_tex_count++;
}

r_tex r_load_texture_file(const char *path) {
    if (g_tex_count >= MAX_TEX) return -1;
    Texture2D t = LoadTexture(path);           /* handles bmp/png/... */
    if (t.id == 0) return -1;
    SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    g_tex[g_tex_count] = t;
    return g_tex_count++;
}

void r_draw_fullscreen(r_tex tex, r_color tint) {
    if (tex < 0 || tex >= g_tex_count) return;
    Texture2D t = g_tex[tex];
    Rectangle src = { 0, 0, (float)t.width, (float)t.height };
    Rectangle dst = { 0, 0, (float)GetScreenWidth(), (float)GetScreenHeight() };
    DrawTexturePro(t, src, dst, (Vector2){ 0, 0 }, 0.0f,
                   (Color){ tint.r, tint.g, tint.b, tint.a });
}

r_mesh r_upload_anim(const mdl_vertex *frames, int num_verts, int num_frames) {
    if (g_mesh_count >= MAX_MESHES || num_verts <= 0 || num_frames <= 0) return -1;
    int total = num_verts * num_frames;
    mdl_vertex *copy = (mdl_vertex *)MemAlloc(total * (int)sizeof(mdl_vertex));
    for (int i = 0; i < total; i++) copy[i] = frames[i];
    g_meshes[g_mesh_count] = (cpu_mesh){ copy, num_verts, num_frames };
    return g_mesh_count++;
}

r_mesh r_upload_mesh(const mdl_vertex *verts, int num_verts) {
    return r_upload_anim(verts, num_verts, 1);
}

void r_begin_frame(r_color clear) {
    BeginDrawing();
    ClearBackground((Color){clear.r, clear.g, clear.b, clear.a});
}

static Camera3D g_cam;

void r_set_camera(r_camera cam) {
    g_cam.position   = (Vector3){cam.pos.x, cam.pos.y, cam.pos.z};
    g_cam.target     = (Vector3){cam.target.x, cam.target.y, cam.target.z};
    g_cam.up         = (Vector3){cam.up.x, cam.up.y, cam.up.z};
    g_cam.fovy       = cam.fovy;
    g_cam.projection = CAMERA_PERSPECTIVE;
    BeginMode3D(g_cam);
    /* Opaque world pass: ignore texture alpha (many skins, e.g. k_normal256,
     * ship a zeroed alpha channel — the engine only blends textures the .thm
     * marks Alpha/SrcBlend). EndMode3D flushes this batch with blend still off. */
    rlDisableColorBlend();
}

void r_draw_skybox(const r_tex faces[6]) {
	const float H = 60.0f;             /* half-size; centered on camera = infinite */
	float cx = g_cam.position.x, cy = g_cam.position.y, cz = g_cam.position.z;
	float x0 = cx - H, x1 = cx + H;
	float y0 = cy - H, y1 = cy + H;
	float z0 = cz - H, z1 = cz + H;
	Color w = WHITE;
	unsigned id[6];
	for (int i = 0; i < 6; i++)
		id[i] = (faces[i] >= 0 && faces[i] < g_tex_count) ? g_tex[faces[i]].id : 0;

	rlDisableDepthMask();              /* sky never occludes world */
	/* Corner order a,b,c,d = image TL,TR,BR,BL, as seen from INSIDE the box.
	 * The arrangement is SOLVED FROM THE DATA, not assumed: scoring edge-pixel
	 * continuity across every ring order/mirroring of the Forest + Space skies
	 * gives one clear winner — the panorama runs FR->LF->BK->RT with NO mirroring
	 * (the old FR->RT->BK->LF ring was the D3D left-handed order = mirrored sky,
	 * wrong faces left/right of each other). FR anchors at -Y per the same
	 * d3d->world basis the models/.par use (d3d +Z fwd -> world -Y). UP is the
	 * image as-is on +Z; DN solved to rot90+mirror (see scratch sky_updn). */
	tex_quad(id[R_SKY_FR], w, 0,0,1,1, x1,y0,z1, x0,y0,z1, x0,y0,z0, x1,y0,z0); /* -Y, right=-X */
	tex_quad(id[R_SKY_LF], w, 0,0,1,1, x0,y0,z1, x0,y1,z1, x0,y1,z0, x0,y0,z0); /* -X, right=+Y */
	tex_quad(id[R_SKY_BK], w, 0,0,1,1, x0,y1,z1, x1,y1,z1, x1,y1,z0, x0,y1,z0); /* +Y, right=+X */
	tex_quad(id[R_SKY_RT], w, 0,0,1,1, x1,y1,z1, x1,y0,z1, x1,y0,z0, x1,y1,z0); /* +X, right=-Y */
	tex_quad(id[R_SKY_UP], w, 0,0,1,1, x0,y0,z1, x1,y0,z1, x1,y1,z1, x0,y1,z1); /* +Z */
	tex_quad(id[R_SKY_DN], w, 0,0,1,1, x1,y1,z0, x1,y0,z0, x0,y0,z0, x0,y1,z0); /* -Z */
	rlEnableDepthMask();
}

static void draw_frame(r_mesh m, r_tex tex, r_vec3 pos, r_vec3 scale, float yaw_deg, int frame) {
    if (m < 0 || m >= g_mesh_count) return;
    const cpu_mesh *cm = &g_meshes[m];
    if (frame < 0) frame = 0;
    if (frame >= cm->nframes) frame = cm->nframes - 1;
    const mdl_vertex *fv = cm->v + (size_t)frame * cm->nverts;
    unsigned tid = (tex >= 0 && tex < g_tex_count) ? g_tex[tex].id : 0;

    rlPushMatrix();
    rlTranslatef(pos.x, pos.y, pos.z);
    rlRotatef(yaw_deg, 0, 0, 1);              /* yaw about world up (Z) */
    rlScalef(scale.x, scale.y, scale.z);
    rlSetTexture(tid);
    rlBegin(RL_TRIANGLES);                    /* TRIANGLELIST: every 3 verts = 1 tri */
    rlColor4ub(255, 255, 255, 255);
    for (int i = 0; i < cm->nverts; i++) {
        const mdl_vertex *v = &fv[i];
        rlNormal3f(v->nx, v->ny, v->nz);
        rlTexCoord2f(v->u0, v->v0);           /* UV set 0 */
        rlVertex3f(v->x, v->y, v->z);
    }
    rlEnd();
    rlSetTexture(0);
    rlPopMatrix();
}

void r_draw_mesh(r_mesh m, r_tex tex, r_vec3 pos, r_vec3 scale, float yaw_deg) {
    draw_frame(m, tex, pos, scale, yaw_deg, 0);
}

/* Sphere-mapped reflection pass (theme Environment): re-draw the mesh with UVs derived
 * from each vertex's VIEW-space normal, so the reflection tex sweeps as the camera orbits
 * or the model spins. Additive blend + depth state are the caller's. */
void r_draw_mesh_env(r_mesh m, r_tex envtex, r_vec3 pos, r_vec3 scale, float yaw_deg,
                     int fa, int fb, float t) {
    if (m < 0 || m >= g_mesh_count) return;
    const cpu_mesh *cm = &g_meshes[m];
    if (fa < 0) fa = 0;
    if (fa >= cm->nframes) fa = cm->nframes - 1;
    if (fb < 0) fb = 0;
    if (fb >= cm->nframes) fb = cm->nframes - 1;
    const mdl_vertex *A = cm->v + (size_t)fa * cm->nverts;
    const mdl_vertex *B = cm->v + (size_t)fb * cm->nverts;
    float s = 1.0f - t;
    unsigned tid = (envtex >= 0 && envtex < g_tex_count) ? g_tex[envtex].id : 0;

    /* camera basis (right, up) for projecting normals into view space */
    float fx = g_cam.target.x - g_cam.position.x, fy = g_cam.target.y - g_cam.position.y,
          fz = g_cam.target.z - g_cam.position.z;
    float fl = sqrtf(fx*fx + fy*fy + fz*fz); if (fl > 1e-6f) { fx/=fl; fy/=fl; fz/=fl; }
    float rx = fy*g_cam.up.z - fz*g_cam.up.y, ry = fz*g_cam.up.x - fx*g_cam.up.z,
          rz = fx*g_cam.up.y - fy*g_cam.up.x;
    float rl = sqrtf(rx*rx + ry*ry + rz*rz); if (rl > 1e-6f) { rx/=rl; ry/=rl; rz/=rl; }
    float ux = ry*fz - rz*fy, uy = rz*fx - rx*fz, uz = rx*fy - ry*fx;   /* up = right x fwd */

    float rad = yaw_deg * (PI / 180.0f), cyw = cosf(rad), syw = sinf(rad);
    rlPushMatrix();
    rlTranslatef(pos.x, pos.y, pos.z);
    rlRotatef(yaw_deg, 0, 0, 1);
    rlScalef(scale.x, scale.y, scale.z);
    rlSetTexture(tid);
    rlBegin(RL_TRIANGLES);
    rlColor4ub(255, 255, 255, 255);
    for (int i = 0; i < cm->nverts; i++) {
        const mdl_vertex *a = &A[i], *b = &B[i];
        /* interpolated normal, then spun by yaw about Z (uniform scale keeps direction) */
        float nx = a->nx*s + b->nx*t, ny = a->ny*s + b->ny*t, nz = a->nz*s + b->nz*t;
        float wnx = nx*cyw - ny*syw, wny = nx*syw + ny*cyw, wnz = nz;
        float nvx = wnx*rx + wny*ry + wnz*rz;      /* view right component */
        float nvy = wnx*ux + wny*uy + wnz*uz;      /* view up component    */
        rlTexCoord2f(0.5f + 0.5f*nvx, 0.5f - 0.5f*nvy);
        rlVertex3f(a->x*s + b->x*t, a->y*s + b->y*t, a->z*s + b->z*t);
    }
    rlEnd();
    rlSetTexture(0);
    rlPopMatrix();
}

/* Explode a mesh into its OWN triangles (theme "Explode" FX: catcher death, bomb
 * blast). Each tri flies outward from the model centre with an upward kick + fall
 * + spin, and shrinks toward its centroid as t: 0->1, so it vanishes cleanly with
 * no alpha (drawn in the opaque pass). */
void r_draw_mesh_shatter(r_mesh m, r_tex tex, r_vec3 pos, r_vec3 scale, float yaw_deg,
                         int frame, float t) {
    if (m < 0 || m >= g_mesh_count) return;
    const cpu_mesh *cm = &g_meshes[m];
    if (frame < 0) frame = 0;
    if (frame >= cm->nframes) frame = cm->nframes - 1;
    const mdl_vertex *fv = cm->v + (size_t)frame * cm->nverts;
    unsigned tid = (tex >= 0 && tex < g_tex_count) ? g_tex[tex].id : 0;

    Vector3 mn = { 1e9f, 1e9f, 1e9f }, mx = { -1e9f, -1e9f, -1e9f };
    for (int i = 0; i < cm->nverts; i++) {
        const mdl_vertex *v = &fv[i];
        mn.x = fminf(mn.x, v->x); mn.y = fminf(mn.y, v->y); mn.z = fminf(mn.z, v->z);
        mx.x = fmaxf(mx.x, v->x); mx.y = fmaxf(mx.y, v->y); mx.z = fmaxf(mx.z, v->z);
    }
    Vector3 mc = { (mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f };

    float spread = 1.5f, shrink = 1.0f - t;
    rlPushMatrix();
    rlTranslatef(pos.x, pos.y, pos.z);
    rlRotatef(yaw_deg, 0, 0, 1);
    rlScalef(scale.x, scale.y, scale.z);
    rlSetTexture(tid);
    rlBegin(RL_TRIANGLES);
    rlColor4ub(255, 255, 255, 255);
    int ntri = cm->nverts / 3;
    for (int tri = 0; tri < ntri; tri++) {
        const mdl_vertex *vs[3] = { &fv[tri * 3], &fv[tri * 3 + 1], &fv[tri * 3 + 2] };
        Vector3 cen = { (vs[0]->x + vs[1]->x + vs[2]->x) / 3.0f,
                        (vs[0]->y + vs[1]->y + vs[2]->y) / 3.0f,
                        (vs[0]->z + vs[1]->z + vs[2]->z) / 3.0f };
        Vector3 dir = { cen.x - mc.x, cen.y - mc.y, cen.z - mc.z };
        float dl = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        if (dl > 1e-4f) { dir.x /= dl; dir.y /= dl; dir.z /= dl; } else { dir = (Vector3){ 0, 0, 1 }; }
        float h = sinf(tri * 12.9898f) * 43758.5453f; h -= (int)h; if (h < 0) h += 1.0f;  /* 0..1 */
        float dispx = dir.x * spread * t;
        float dispy = dir.y * spread * t;
        float dispz = dir.z * spread * t + (1.2f + h * 1.4f) * t - 4.0f * t * t;   /* up then fall (Z up) */
        float ang = (h - 0.5f) * 12.0f * t, ca = cosf(ang), sa = sinf(ang);
        for (int j = 0; j < 3; j++) {
            const mdl_vertex *v = vs[j];
            float lx = (v->x - cen.x) * shrink, ly = (v->y - cen.y) * shrink, lz = (v->z - cen.z) * shrink;
            float rx = lx * ca - ly * sa, ry = lx * sa + ly * ca;    /* spin in local XY */
            rlNormal3f(v->nx, v->ny, v->nz);
            rlTexCoord2f(v->u0, v->v0);
            rlVertex3f(cen.x + rx + dispx, cen.y + ry + dispy, cen.z + lz + dispz);
        }
    }
    rlEnd();
    rlSetTexture(0);
    rlPopMatrix();
}

void r_draw_mesh_frame(r_mesh m, r_tex tex, r_vec3 pos, r_vec3 scale, float yaw_deg, int frame) {
    draw_frame(m, tex, pos, scale, yaw_deg, frame);
}

void r_draw_mesh_lerp(r_mesh m, r_tex tex, r_vec3 pos, r_vec3 scale, float yaw_deg,
                      int fa, int fb, float t) {
    if (m < 0 || m >= g_mesh_count) return;
    const cpu_mesh *cm = &g_meshes[m];
    if (fa < 0) fa = 0;
    if (fa >= cm->nframes) fa = cm->nframes - 1;
    if (fb < 0) fb = 0;
    if (fb >= cm->nframes) fb = cm->nframes - 1;
    const mdl_vertex *A = cm->v + (size_t)fa * cm->nverts;
    const mdl_vertex *B = cm->v + (size_t)fb * cm->nverts;
    float s = 1.0f - t;
    unsigned tid = (tex >= 0 && tex < g_tex_count) ? g_tex[tex].id : 0;

    rlPushMatrix();
    rlTranslatef(pos.x, pos.y, pos.z);
    rlRotatef(yaw_deg, 0, 0, 1);
    rlScalef(scale.x, scale.y, scale.z);
    rlSetTexture(tid);
    rlBegin(RL_TRIANGLES);
    rlColor4ub(255, 255, 255, 255);
    for (int i = 0; i < cm->nverts; i++) {
        const mdl_vertex *a = &A[i], *b = &B[i];
        rlNormal3f(a->nx * s + b->nx * t, a->ny * s + b->ny * t, a->nz * s + b->nz * t);
        rlTexCoord2f(a->u0, a->v0);          /* UVs constant across frames */
        rlVertex3f(a->x * s + b->x * t, a->y * s + b->y * t, a->z * s + b->z * t);
    }
    rlEnd();
    rlSetTexture(0);
    rlPopMatrix();
}

void r_draw_billboard_ex(r_tex tex, r_vec3 pos, float size, r_color tint, int diamond) {
    if (tex < 0 || tex >= g_tex_count) return;
    /* Camera-facing quad drawn via immediate mode (like r_draw_quad_flat) so it
     * HONORS the current blend mode — raylib's DrawBillboardPro doesn't respect
     * rlSetBlendMode(ADDITIVE), which made additive sparkles render as dark boxes.
     * Screen-aligned: right/up perpendicular to the view direction (no roll).
     * `diamond` rotates the quad 45° (verts at edge midpoints) — small dark specks
     * read as insects, not axis-aligned squares. */
    Vector3 fwd   = Vector3Normalize(Vector3Subtract(g_cam.target, g_cam.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(fwd, (Vector3){ 0, 0, 1 }));
    Vector3 up    = Vector3CrossProduct(right, fwd);
    float h = size * 0.5f;
    Vector3 r = { right.x * h, right.y * h, right.z * h };
    Vector3 u = { up.x * h, up.y * h, up.z * h };
    rlSetTexture(g_tex[tex].id);
    rlBegin(RL_QUADS);
    rlColor4ub(tint.r, tint.g, tint.b, tint.a);
    if (diamond) {
        rlTexCoord2f(0, 0); rlVertex3f(pos.x - r.x, pos.y - r.y, pos.z - r.z);          /* left  */
        rlTexCoord2f(0, 1); rlVertex3f(pos.x - u.x, pos.y - u.y, pos.z - u.z);          /* down  */
        rlTexCoord2f(1, 1); rlVertex3f(pos.x + r.x, pos.y + r.y, pos.z + r.z);          /* right */
        rlTexCoord2f(1, 0); rlVertex3f(pos.x + u.x, pos.y + u.y, pos.z + u.z);          /* up    */
    } else {
        rlTexCoord2f(0, 0); rlVertex3f(pos.x - r.x + u.x, pos.y - r.y + u.y, pos.z - r.z + u.z);
        rlTexCoord2f(0, 1); rlVertex3f(pos.x - r.x - u.x, pos.y - r.y - u.y, pos.z - r.z - u.z);
        rlTexCoord2f(1, 1); rlVertex3f(pos.x + r.x - u.x, pos.y + r.y - u.y, pos.z + r.z - u.z);
        rlTexCoord2f(1, 0); rlVertex3f(pos.x + r.x + u.x, pos.y + r.y + u.y, pos.z + r.z + u.z);
    }
    rlEnd();
    rlSetTexture(0);
}

void r_draw_billboard(r_tex tex, r_vec3 pos, float size, r_color tint) {
    r_draw_billboard_ex(tex, pos, size, tint, 0);
}

/* a horizontal (ground-plane, +Z up) textured quad centred at (cx,cy,z), full
 * texture 0..1, half-extent `half`. For flat decals like the exit glow ring. */
void r_draw_quad_flat(r_tex tex, float cx, float cy, float z, float half, r_color c, int mirror) {
    if (tex < 0 || tex >= g_tex_count) return;
    float u0 = mirror ? 1.0f : 0.0f, u1 = mirror ? 0.0f : 1.0f;   /* flip U -> other diagonal */
    rlSetTexture(g_tex[tex].id);
    rlBegin(RL_QUADS);
    rlColor4ub(c.r, c.g, c.b, c.a);
    rlNormal3f(0, 0, 1);
    rlTexCoord2f(u0, 0); rlVertex3f(cx - half, cy - half, z);
    rlTexCoord2f(u1, 0); rlVertex3f(cx + half, cy - half, z);
    rlTexCoord2f(u1, 1); rlVertex3f(cx + half, cy + half, z);
    rlTexCoord2f(u0, 1); rlVertex3f(cx - half, cy + half, z);
    rlEnd();
    rlSetTexture(0);
}

/* horizontal quad at tile top with SCROLLING/TILING UVs — the texture is sampled
 * from (uoff,voff) spanning `tiles` repeats, so animating the offsets makes the
 * pattern "flow" (glue). Relies on the default REPEAT wrap. Honors current blend. */
void r_draw_quad_turn(r_tex tex, float cx, float cy, float z, float half, float angle, r_color c) {
    if (tex < 0 || tex >= g_tex_count) return;
    float ca = cosf(angle), sa = sinf(angle);
    /* UV corners rotated about (0.5,0.5). Inscribe by 1/sqrt(2) so the rotated square
     * samples the texture's central DISC (radius 0.5), never its out-of-[0,1] corners —
     * a clean spinning portal instead of the square corners wrapping through the tile. */
    const float S = 0.70710678f;
    const float bu[4] = {0,1,1,0}, bv[4] = {0,0,1,1};
    float u[4], v[4];
    for (int i = 0; i < 4; i++) {
        float du = (bu[i] - 0.5f) * S, dv = (bv[i] - 0.5f) * S;
        u[i] = 0.5f + du*ca - dv*sa;
        v[i] = 0.5f + du*sa + dv*ca;
    }
    rlSetTexture(g_tex[tex].id);
    rlBegin(RL_QUADS);
    rlColor4ub(c.r, c.g, c.b, c.a);
    rlNormal3f(0, 0, 1);
    rlTexCoord2f(u[0], v[0]); rlVertex3f(cx - half, cy - half, z);
    rlTexCoord2f(u[1], v[1]); rlVertex3f(cx + half, cy - half, z);
    rlTexCoord2f(u[2], v[2]); rlVertex3f(cx + half, cy + half, z);
    rlTexCoord2f(u[3], v[3]); rlVertex3f(cx - half, cy + half, z);
    rlEnd();
    rlSetTexture(0);
}

void r_draw_tile_scroll(r_tex tex, float cx, float cy, float z, float half,
                        float uoff, float voff, float tiles, r_color c, int swap) {
    if (tex < 0 || tex >= g_tex_count) return;
    float u0 = uoff, u1 = uoff + tiles, v0 = voff, v1 = voff + tiles;
    rlSetTexture(g_tex[tex].id);
    rlBegin(RL_QUADS);
    rlColor4ub(c.r, c.g, c.b, c.a);
    rlNormal3f(0, 0, 1);
    if (swap) {   /* transpose UVs: texture rotated 90 deg (U runs along worldY, V along worldX) */
        rlTexCoord2f(u0, v0); rlVertex3f(cx - half, cy - half, z);
        rlTexCoord2f(u0, v1); rlVertex3f(cx + half, cy - half, z);
        rlTexCoord2f(u1, v1); rlVertex3f(cx + half, cy + half, z);
        rlTexCoord2f(u1, v0); rlVertex3f(cx - half, cy + half, z);
    } else {
        rlTexCoord2f(u0, v0); rlVertex3f(cx - half, cy - half, z);
        rlTexCoord2f(u1, v0); rlVertex3f(cx + half, cy - half, z);
        rlTexCoord2f(u1, v1); rlVertex3f(cx + half, cy + half, z);
        rlTexCoord2f(u0, v1); rlVertex3f(cx - half, cy + half, z);
    }
    rlEnd();
    rlSetTexture(0);
}

/* horizontal tile-top quad whose TEXTURE is sine-warped (not merely offset): a
 * subdivided grid where each vertex's UV is perturbed by sin() of its position +
 * time, so the image ripples/flows like fluid (the theme "Wobble" FX for glue). */
void r_draw_tile_wobble(r_tex tex, float cx, float cy, float z, float half,
                        float t, float ampu, float ampv, r_color c) {
    if (tex < 0 || tex >= g_tex_count) return;
    enum { N = 8 };                       /* grid subdivisions per side */
    const float K = 6.2831853f * 1.5f;    /* ~1.5 wavelengths across the tile */
    rlSetTexture(g_tex[tex].id);
    rlBegin(RL_QUADS);
    rlColor4ub(c.r, c.g, c.b, c.a);
    rlNormal3f(0, 0, 1);
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++) {
            /* emit the 4 grid corners CCW; flat geometry, warped UVs */
            float gu[4] = { (float)i / N, (float)(i + 1) / N, (float)(i + 1) / N, (float)i / N };
            float gv[4] = { (float)j / N, (float)j / N, (float)(j + 1) / N, (float)(j + 1) / N };
            for (int k = 0; k < 4; k++) {
                float u = gu[k], v = gv[k];
                float wu = u + ampu * sinf(v * K + t);
                float wv = v + ampv * sinf(u * K + t * 1.1f);
                rlTexCoord2f(wu, wv);
                rlVertex3f(cx + (u * 2.0f - 1.0f) * half, cy + (v * 2.0f - 1.0f) * half, z);
            }
        }
    rlEnd();
    rlSetTexture(0);
}

/* ONE composable tile-top layer — the generic .thm Field renderer's primitive.
 * UV pipeline: rotate by `turn` about the centre (disc-inscribed, like the
 * teleporter swirl, so corners never wrap) -> transpose if `swap` (X-axis bridge
 * flow) -> scroll offsets -> sine wobble warp when amps are set (subdivided grid,
 * as r_draw_tile_wobble). half=0.5 covers the tile. Honors the current blend;
 * the tint carries Pulse/Flash brightness. */
void r_draw_layer(r_tex tex, float cx, float cy, float z, float half,
                  float turn, float uoff, float voff,
                  float wob_t, float wob_au, float wob_av, r_color c, int swap)
{
    if (tex < 0 || tex >= g_tex_count) return;
    float ca = 1.0f, sa = 0.0f, S = 1.0f;
    if (turn != 0.0f) { ca = cosf(turn); sa = sinf(turn); S = 0.70710678f; }
    int wob = (wob_au != 0.0f || wob_av != 0.0f);
    const float K = 6.2831853f * 1.5f;    /* ~1.5 wobble wavelengths across the tile */
    int N = wob ? 8 : 1;
    rlSetTexture(g_tex[tex].id);
    rlBegin(RL_QUADS);
    rlColor4ub(c.r, c.g, c.b, c.a);
    rlNormal3f(0, 0, 1);
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++) {
            float gu[4] = { (float)i/N, (float)(i+1)/N, (float)(i+1)/N, (float)i/N };
            float gv[4] = { (float)j/N, (float)j/N, (float)(j+1)/N, (float)(j+1)/N };
            for (int k = 0; k < 4; k++) {
                float du = (gu[k] - 0.5f) * S, dv = (gv[k] - 0.5f) * S;
                float ru = 0.5f + du * ca - dv * sa;
                float rv = 0.5f + du * sa + dv * ca;
                if (swap) { float tmp = ru; ru = rv; rv = tmp; }
                ru += uoff;
                rv += voff;
                if (wob) {
                    ru += wob_au * sinf(gv[k] * K + wob_t);
                    rv += wob_av * sinf(gu[k] * K + wob_t * 1.1f);
                }
                rlTexCoord2f(ru, rv);
                rlVertex3f(cx + (gu[k] * 2.0f - 1.0f) * half,
                           cy + (gv[k] * 2.0f - 1.0f) * half, z);
            }
        }
    rlEnd();
    rlSetTexture(0);
}

void r_draw_box(r_vec3 c, r_vec3 s, r_color f) {
    DrawCube((Vector3){c.x, c.y, c.z}, s.x, s.y, s.z, (Color){f.r, f.g, f.b, f.a});
}
void r_draw_box_wires(r_vec3 c, r_vec3 s, r_color f) {
    DrawCubeWires((Vector3){c.x, c.y, c.z}, s.x, s.y, s.z, (Color){f.r, f.g, f.b, f.a});
}

/* one textured quad (corners a,b,c,d), UV rect (umin,vmin)..(umax,vmax):
 * a->(umin,vmin) b->(umax,vmin) c->(umax,vmax) d->(umin,vmax) */
static void tex_quad(unsigned tid, Color col, float umin, float vmin, float umax, float vmax,
                     float ax,float ay,float az, float bx,float by,float bz,
                     float cx,float cy,float cz, float dx,float dy,float dz) {
    rlSetTexture(tid);
    rlBegin(RL_QUADS);
    rlColor4ub(col.r, col.g, col.b, col.a);
    rlTexCoord2f(umin, vmin); rlVertex3f(ax, ay, az);
    rlTexCoord2f(umax, vmin); rlVertex3f(bx, by, bz);
    rlTexCoord2f(umax, vmax); rlVertex3f(cx, cy, cz);
    rlTexCoord2f(umin, vmax); rlVertex3f(dx, dy, dz);
    rlEnd();
    rlSetTexture(0);
}

/* side64-style edge textures put the wood plank-edge strip in the bottom of the
 * image (measured V band [0.825,1.0]); the rest is black. The engine maps only
 * that strip onto the thin edge (TextureAdress Wrap), so we do the same instead
 * of stretching the whole texture (which showed the black top). */
#define SIDE_V_TOP 0.825f
#define SIDE_V_BOT 1.0f

void r_draw_tile(r_vec3 c, r_vec3 s, r_tex top, r_tex side, r_color tint, int side_mask,
                 float top_turn, int draw_bottom) {
    float hx = s.x * 0.5f, hy = s.y * 0.5f, hz = s.z * 0.5f;
    float x0 = c.x - hx, x1 = c.x + hx;
    float y0 = c.y - hy, y1 = c.y + hy;
    float z0 = c.z - hz, z1 = c.z + hz;   /* z1 = top surface */
    unsigned tt = (top  >= 0 && top  < g_tex_count) ? g_tex[top].id  : 0;
    unsigned ts = (side >= 0 && side < g_tex_count) ? g_tex[side].id : 0;
    Color col = (Color){tint.r, tint.g, tint.b, tint.a};
    const float T = SIDE_V_TOP, B = SIDE_V_BOT;   /* side V band (top->bottom) */

    /* top face: the theme "Turn" swirl rotates its UVs in place (inscribed 1/sqrt(2) so it
     * samples the texture's central disc, not the out-of-[0,1] corners). Drawn as the tile's
     * OWN top face so it stays flush with the grid — no separate lifted quad. */
    if (top_turn != 0.0f) {
        float ca = cosf(top_turn), sa = sinf(top_turn);
        const float S = 0.70710678f, bu[4] = {0,1,1,0}, bv[4] = {0,0,1,1};
        float u[4], v[4];
        for (int i = 0; i < 4; i++) {
            float du = (bu[i]-0.5f)*S, dv = (bv[i]-0.5f)*S;
            u[i] = 0.5f + du*ca - dv*sa; v[i] = 0.5f + du*sa + dv*ca;
        }
        rlSetTexture(tt);
        rlBegin(RL_QUADS);
        rlColor4ub(col.r, col.g, col.b, col.a);
        rlTexCoord2f(u[0],v[0]); rlVertex3f(x0,y0,z1);
        rlTexCoord2f(u[1],v[1]); rlVertex3f(x1,y0,z1);
        rlTexCoord2f(u[2],v[2]); rlVertex3f(x1,y1,z1);
        rlTexCoord2f(u[3],v[3]); rlVertex3f(x0,y1,z1);
        rlEnd();
        rlSetTexture(0);
    } else {
        tex_quad(tt, col, 0,0,1,1, x0,y0,z1,  x1,y0,z1,  x1,y1,z1,  x0,y1,z1);   /* top */
    }
    if (draw_bottom)   /* skipped for vine-curtain platforms (no solid dirt underside) */
        tex_quad(tt, col, 0,0,1,1, x0,y0,z0,  x0,y1,z0,  x1,y1,z0,  x1,y0,z0);   /* bottom */
    /* exposed sides only — map to the wood strip (V band), not the full texture */
    if (side_mask & R_SIDE_NY) tex_quad(ts, col, 0,T,1,B, x0,y0,z1,  x1,y0,z1,  x1,y0,z0,  x0,y0,z0);
    if (side_mask & R_SIDE_PY) tex_quad(ts, col, 0,T,1,B, x1,y1,z1,  x0,y1,z1,  x0,y1,z0,  x1,y1,z0);
    if (side_mask & R_SIDE_NX) tex_quad(ts, col, 0,T,1,B, x0,y1,z1,  x0,y0,z1,  x0,y0,z0,  x0,y1,z0);
    if (side_mask & R_SIDE_PX) tex_quad(ts, col, 0,T,1,B, x1,y0,z1,  x1,y1,z1,  x1,y1,z0,  x1,y0,z0);
}

/* just the exposed side faces, mapping the FULL texture (V 0..1) — for a full-image side
 * (e.g. Water's vine curtain), drawn under r_alpha_test after the opaque tops. */
void r_draw_tile_sides(r_vec3 c, r_vec3 s, r_tex side, r_color tint, int side_mask) {
    float hx = s.x * 0.5f, hy = s.y * 0.5f, hz = s.z * 0.5f;
    float x0 = c.x - hx, x1 = c.x + hx, y0 = c.y - hy, y1 = c.y + hy, z0 = c.z - hz, z1 = c.z + hz;
    unsigned ts = (side >= 0 && side < g_tex_count) ? g_tex[side].id : 0;
    Color col = (Color){tint.r, tint.g, tint.b, tint.a};
    if (side_mask & R_SIDE_NY) tex_quad(ts, col, 0,0,1,1, x0,y0,z1,  x1,y0,z1,  x1,y0,z0,  x0,y0,z0);
    if (side_mask & R_SIDE_PY) tex_quad(ts, col, 0,0,1,1, x1,y1,z1,  x0,y1,z1,  x0,y1,z0,  x1,y1,z0);
    if (side_mask & R_SIDE_NX) tex_quad(ts, col, 0,0,1,1, x0,y1,z1,  x0,y0,z1,  x0,y0,z0,  x0,y1,z0);
    if (side_mask & R_SIDE_PX) tex_quad(ts, col, 0,0,1,1, x1,y0,z1,  x1,y1,z1,  x1,y1,z0,  x1,y0,z0);
}

void r_begin_transparent(void) {
    rlDrawRenderBatchActive();          /* flush the opaque batch (blend still off) */
    rlEnableColorBlend();               /* default RL_BLEND_ALPHA = SrcAlpha/InvSrcAlpha */
}

void r_set_blend(int mode) {
    rlSetBlendMode(mode == R_BLEND_ADD ? RL_BLEND_ADDITIVE : RL_BLEND_ALPHA);
}

/* flush queued geometry NOW, under the current blend + depth-write state. Used to
 * commit a transparent batch before toggling depth-write back on (a state change
 * alone doesn't flush the rlgl batch — see the exit-fountain box fix). */
void r_flush(void) {
    rlDrawRenderBatchActive();
}

void r_depth_write(int on) {
    if (on) rlEnableDepthMask();
    else    rlDisableDepthMask();
}

void r_end_3d(void) {
    EndMode3D();           /* flushes the 3D batch */
    rlEnableColorBlend();  /* restore for 2D HUD text alpha */
}

void r_draw_text(const char *s, int x, int y, int px, r_color c) {
    /* dark shadow/outline so HUD text stays legible over the busy 3D scene */
    Color sh = (Color){ 0, 0, 0, 200 };
    DrawText(s, x - 1, y,     px, sh);
    DrawText(s, x + 1, y,     px, sh);
    DrawText(s, x,     y - 1, px, sh);
    DrawText(s, x + 2, y + 2, px, sh);
    DrawText(s, x, y, px, (Color){c.r, c.g, c.b, c.a});
}

void r_draw_sprite(r_tex tex, float dx, float dy, float dw, float dh,
                   float sx, float sy, float sw, float sh, r_color tint)
{
    if (tex < 0 || tex >= g_tex_count)
        return;
    Rectangle src = { sx, sy, sw, sh };
    Rectangle dst = { dx, dy, dw, dh };
    DrawTexturePro(g_tex[tex], src, dst, (Vector2){ 0, 0 }, 0.0f,
                   (Color){ tint.r, tint.g, tint.b, tint.a });
}

void r_draw_rect(float x, float y, float w, float h, r_color c)
{
    DrawRectangleRec((Rectangle){ x, y, w, h }, (Color){ c.r, c.g, c.b, c.a });
}

void r_draw_circle(float cx, float cy, float rad, r_color c)
{
    DrawCircleV((Vector2){ cx, cy }, rad, (Color){ c.r, c.g, c.b, c.a });
}

void r_draw_line(float x1, float y1, float x2, float y2, float w, r_color c)
{
    DrawLineEx((Vector2){ x1, y1 }, (Vector2){ x2, y2 }, w, (Color){ c.r, c.g, c.b, c.a });
}

void r_screen_size(int *w, int *h)
{
    if (w) *w = GetScreenWidth();
    if (h) *h = GetScreenHeight();
}

void r_end_frame(void) { EndDrawing(); }

float r_frame_time(void) { return GetFrameTime(); }

void r_screenshot(const char *path) { TakeScreenshot(path); }

/* map engine-agnostic r_key -> raylib KEY_* */
static int rl_key(int key)
{
	if (key >= R_KEY_F1 && key <= R_KEY_F12)
		return KEY_F1 + (key - R_KEY_F1);
	switch (key) {
	case R_KEY_SPACE: return KEY_SPACE;
	case R_KEY_ENTER: return KEY_ENTER;
	case R_KEY_SHIFT: return KEY_LEFT_SHIFT;
	case R_KEY_CTRL:  return KEY_LEFT_CONTROL;
	case R_KEY_UP:    return KEY_UP;
	case R_KEY_DOWN:  return KEY_DOWN;
	case R_KEY_LEFT:  return KEY_LEFT;
	case R_KEY_RIGHT: return KEY_RIGHT;
	case R_KEY_ESC:   return KEY_ESCAPE;
	case R_KEY_BACKSPACE: return KEY_BACKSPACE;
	default:          return key;   /* 'A'..'Z' match raylib letter codes */
	}
}

bool r_any_key_pressed(void) { return GetKeyPressed() != 0 || IsMouseButtonPressed(MOUSE_BUTTON_LEFT); }
bool r_key_pressed(int key) { return IsKeyPressed(rl_key(key)); }
bool r_key_down(int key)    { return IsKeyDown(rl_key(key)); }
int  r_char_pressed(void)   { return GetCharPressed(); }
void r_poll_input(void)     { PollInputEvents(); }
float r_mouse_wheel(void)   { return GetMouseWheelMove(); }

bool r_mouse_down(int button)
{
	int b = (button == 1) ? MOUSE_BUTTON_RIGHT
	      : (button == 2) ? MOUSE_BUTTON_MIDDLE
	                      : MOUSE_BUTTON_LEFT;
	return IsMouseButtonDown(b);
}

bool r_mouse_pressed(int button)
{
	int b = (button == 1) ? MOUSE_BUTTON_RIGHT
	      : (button == 2) ? MOUSE_BUTTON_MIDDLE
	                      : MOUSE_BUTTON_LEFT;
	return IsMouseButtonPressed(b);
}

void r_mouse_delta(float *dx, float *dy)
{
	Vector2 d = GetMouseDelta();
	if (dx) *dx = d.x;
	if (dy) *dy = d.y;
}

void r_mouse_ray(r_vec3 *origin, r_vec3 *dir)
{
	Ray r = GetScreenToWorldRay(GetMousePosition(), g_cam);
	if (origin) *origin = (r_vec3){ r.position.x, r.position.y, r.position.z };
	if (dir)    *dir    = (r_vec3){ r.direction.x, r.direction.y, r.direction.z };
}
