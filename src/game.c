/* game.c - the cave, the ping, the point cloud, and the thing that hears it.
 *
 * There is no mesh anywhere in this file. The cave is a signed distance
 * function; a ping fires a few thousand rays into it and keeps whatever they
 * hit. Those hits are the only geometry the game ever draws, which is why the
 * whole thing costs almost nothing on disk. */

#include "gl33.h"
#include "game.h"
#include "audio.h"
#include "shaders.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* --- tuning ------------------------------------------------------------- */

#define MAX_POINTS   1200000   /* 19 MB of RAM, 0 bytes on disk */
#define PING_RAYS      14000
#define RAY_STEPS         96
#define RAY_MAX_DIST   26.0f
#define WAVE_SPEED     11.0f   /* metres per second the wavefront travels */
#define MOVE_SPEED      2.7f
#define MOUSE_SENS      0.0022f
#define PING_COOLDOWN   0.45f

#define MON_POINTS       800
#define MON_WAKE_TIME   0.55f  /* warning it gives before it commits */
#define MON_SPEED       7.4f
#define MON_CHARGE_TIME 3.20f
#define MON_KILL_DIST   0.85f
#define MON_HEAR_DIST  24.0f

/* --- point cloud -------------------------------------------------------- */

typedef struct { float x, y, z, reveal; } Point;   /* 16 bytes */

static Point *g_pts;
static int    g_count;
static GLuint g_vao, g_vbo, g_mvao, g_mvbo, g_prog;
static GLint  u_vp, u_cam, u_time, u_monster;
static Point  g_mpts[MON_POINTS];

/* --- player ------------------------------------------------------------- */

static float g_px, g_py, g_pz;
static float g_yaw, g_pitch;
static float g_ping_ready;
static float g_flash;              /* red wash after a hit */
static int   g_hits;

/* --- the thing ---------------------------------------------------------- */

enum { MON_DORMANT, MON_WAKING, MON_CHARGING, MON_SPENT };

static int   g_mstate;
static float g_mx, g_my, g_mz;             /* where it is */
static float g_mtx, g_mty, g_mtz;          /* where the ping came from */
static float g_mdx, g_mdy, g_mdz;          /* charge direction, normalised */
static float g_mwake;                      /* when the wavefront reaches it */
static float g_mtimer;
static float g_mseed;

/* --- noise -------------------------------------------------------------- */

static float hash1(float n)
{
    float s = (float)sin(n) * 43758.5453f;
    return s - (float)floor(s);
}

static float noise1(float x)
{
    float i = (float)floor(x), f = x - i;
    float u = f * f * (3.0f - 2.0f * f);
    return hash1(i) * (1.0f - u) + hash1(i + 1.0f) * u;
}

static float fbm2(float a, float b)
{
    return 0.55f * noise1(a * 1.7f + b * 3.1f)
         + 0.30f * noise1(a * 3.9f - b * 2.3f + 17.0f)
         + 0.15f * noise1(a * 8.1f + b * 6.7f + 43.0f);
}

/* --- the cave ------------------------------------------------------------
 * A tunnel whose centre wanders as it descends, with an eroded radius.
 * Positive in air, negative in rock, zero on the wall. */

static void tunnel_centre(float z, float *cx, float *cy)
{
    *cx = (float)sin(z * 0.13) * 3.2f + (float)sin(z * 0.047) * 2.1f;
    *cy = (float)cos(z * 0.097) * 1.6f;
}

static float cave_sdf(float x, float y, float z)
{
    float cx, cy, dx, dy, r, rad;
    tunnel_centre(z, &cx, &cy);
    dx = x - cx;
    dy = (y - cy) * 1.25f;                       /* flatter than it is wide */
    r  = (float)sqrt(dx * dx + dy * dy);
    rad = 2.35f + 1.15f * fbm2((float)atan2(dy, dx) * 1.6f, z * 0.42f);
    return rad - r;
}

/* March until we leave the air. The field is not a true distance function
 * (the fbm lies about how far the wall is), so steps stay conservative. */
static int cave_ray(float ox, float oy, float oz,
                    float dx, float dy, float dz, float *hit)
{
    float t = 0.06f;
    int i;
    for (i = 0; i < RAY_STEPS && t < RAY_MAX_DIST; i++) {
        float d = cave_sdf(ox + dx * t, oy + dy * t, oz + dz * t);
        if (d < 0.02f) { *hit = t; return 1; }
        t += (d * 0.55f > 0.035f) ? d * 0.55f : 0.035f;
    }
    return 0;
}

/* --- the thing, continued ------------------------------------------------ */

static void mon_reposition(float now)
{
    float cx, cy, z;
    g_mseed += 1.618f;
    /* it waits further down the tunnel, off to one side of the axis */
    z = g_pz - 13.0f - hash1(now * 3.7f + g_mseed) * 11.0f;
    tunnel_centre(z, &cx, &cy);
    g_mx = cx + (hash1(g_mseed * 5.1f) - 0.5f) * 2.2f;
    g_my = cy + (hash1(g_mseed * 9.3f) - 0.5f) * 1.4f;
    g_mz = z;
    g_mstate = MON_DORMANT;
    g_mwake  = -1.0f;
    g_mtimer = 0.0f;
}

/* A ping does not travel instantly, so the moment it reaches the thing is
 * scheduled rather than immediate. That delay is what gives the player the
 * eerie beat between firing and hearing the answer. */
static void mon_hear_ping(float ox, float oy, float oz, float now)
{
    float dx = g_mx - ox, dy = g_my - oy, dz = g_mz - oz;
    float d  = (float)sqrt(dx * dx + dy * dy + dz * dz);

    if (g_mstate != MON_DORMANT) return;
    if (d > MON_HEAR_DIST) return;

    g_mwake = now + d / WAVE_SPEED;
    g_mtx = ox; g_mty = oy; g_mtz = oz;     /* it remembers where, not who */
}

/* Its own body, scattered as returns. Regenerated every frame it is audible,
 * so these points never join the permanent map - the map is walls only. */
static void mon_emit_points(float now)
{
    int i;
    for (i = 0; i < MON_POINTS; i++) {
        float a = hash1((float)i * 1.7f + g_mseed) * 6.2831853f;
        float b = hash1((float)i * 3.1f + g_mseed + 4.0f) * 3.1415927f;
        float r = 0.75f * (float)pow(hash1((float)i * 5.3f + now * 0.7f), 0.35);
        g_mpts[i].x = g_mx + r * (float)sin(b) * (float)cos(a) * 0.9f;
        g_mpts[i].y = g_my + r * (float)cos(b) * 1.3f;
        g_mpts[i].z = g_mz + r * (float)sin(b) * (float)sin(a) * 0.9f;
        g_mpts[i].reveal = now;              /* always at the wavefront */
    }
    glBindBuffer(GL_ARRAY_BUFFER, g_mvbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)sizeof g_mpts, g_mpts);
}

static void mon_update(float dt, float now)
{
    float dx, dy, dz, d;

    switch (g_mstate) {
    case MON_DORMANT:
        if (g_mwake > 0.0f && now >= g_mwake) {
            g_mstate = MON_WAKING;
            g_mtimer = MON_WAKE_TIME;
            audio_roar();
        }
        break;

    case MON_WAKING:
        /* it is lit and loud but not yet moving - this is the dodge window */
        g_mtimer -= dt;
        if (g_mtimer <= 0.0f) {
            dx = g_mtx - g_mx; dy = g_mty - g_my; dz = g_mtz - g_mz;
            d  = (float)sqrt(dx * dx + dy * dy + dz * dz);
            if (d < 0.001f) d = 1.0f;
            g_mdx = dx / d; g_mdy = dy / d; g_mdz = dz / d;
            g_mstate = MON_CHARGING;
            g_mtimer = MON_CHARGE_TIME;
        }
        break;

    case MON_CHARGING:
        /* dead straight, through rock if rock is in the way */
        g_mx += g_mdx * MON_SPEED * dt;
        g_my += g_mdy * MON_SPEED * dt;
        g_mz += g_mdz * MON_SPEED * dt;
        g_mtimer -= dt;

        dx = g_px - g_mx; dy = g_py - g_my; dz = g_pz - g_mz;
        if (dx * dx + dy * dy + dz * dz < MON_KILL_DIST * MON_KILL_DIST) {
            g_hits++;
            g_flash = 1.0f;
            audio_hit();
            g_mstate = MON_SPENT;
            g_mtimer = 2.2f;
        } else if (g_mtimer <= 0.0f) {
            g_mstate = MON_SPENT;
            g_mtimer = 1.6f;
        }
        break;

    case MON_SPENT:
        g_mtimer -= dt;
        if (g_mtimer <= 0.0f) mon_reposition(now);
        break;
    }
}

static int mon_visible(void)
{
    return g_mstate == MON_WAKING || g_mstate == MON_CHARGING;
}

/* --- ping ----------------------------------------------------------------
 * Rays are spread over the sphere with the golden angle, which covers evenly
 * instead of clumping at the poles the way naive lat/long sampling does. */

static void emit_ping(float now)
{
    int i;
    int start = g_count;
    int added = 0;

    for (i = 0; i < PING_RAYS; i++) {
        float k     = ((float)i + 0.5f) / (float)PING_RAYS;
        float phi   = (float)acos(1.0 - 2.0 * k);
        float theta = 2.39996323f * (float)i;          /* golden angle */
        float sp    = (float)sin(phi);
        float dx    = sp * (float)cos(theta);
        float dy    = (float)cos(phi);
        float dz    = sp * (float)sin(theta);
        float t;

        if (g_count >= MAX_POINTS) break;
        if (!cave_ray(g_px, g_py, g_pz, dx, dy, dz, &t)) continue;

        g_pts[g_count].x = g_px + dx * t;
        g_pts[g_count].y = g_py + dy * t;
        g_pts[g_count].z = g_pz + dz * t;
        g_pts[g_count].reveal = now + t / WAVE_SPEED;
        g_count++;
        added++;
    }

    if (added > 0) {
        glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
        glBufferSubData(GL_ARRAY_BUFFER,
                        (GLintptr)(start * (int)sizeof(Point)),
                        (GLsizeiptr)(added * (int)sizeof(Point)),
                        g_pts + start);
    }

    audio_ping();
    mon_hear_ping(g_px, g_py, g_pz, now);
}

/* --- matrices ----------------------------------------------------------- */

static void mat4_persp(float *m, float fovy, float aspect, float zn, float zf)
{
    float f = 1.0f / (float)tan(fovy * 0.5f);
    memset(m, 0, 16 * sizeof(float));
    m[0]  = f / aspect;
    m[5]  = f;
    m[10] = (zf + zn) / (zn - zf);
    m[11] = -1.0f;
    m[14] = (2.0f * zf * zn) / (zn - zf);
}

static void mat4_mul(float *out, const float *a, const float *b)
{
    int c, r, k;
    float t[16];
    for (c = 0; c < 4; c++)
        for (r = 0; r < 4; r++) {
            float s = 0.0f;
            for (k = 0; k < 4; k++) s += a[k * 4 + r] * b[c * 4 + k];
            t[c * 4 + r] = s;
        }
    memcpy(out, t, sizeof t);
}

static void basis(float yaw, float pitch, float *f, float *r, float *u)
{
    float cy = (float)cos(yaw),   sy = (float)sin(yaw);
    float cp = (float)cos(pitch), sp = (float)sin(pitch);
    f[0] =  sy * cp; f[1] =  sp;  f[2] = -cy * cp;
    r[0] =  cy;      r[1] =  0.0f; r[2] =  sy;
    u[0] = -sy * sp; u[1] =  cp;  u[2] =  cy * sp;
}

static void mat4_view(float *m, float px, float py, float pz,
                      float yaw, float pitch)
{
    float f[3], r[3], u[3];
    basis(yaw, pitch, f, r, u);
    m[0]  =  r[0]; m[4] =  r[1]; m[8]  =  r[2]; m[12] = -(r[0]*px + r[1]*py + r[2]*pz);
    m[1]  =  u[0]; m[5] =  u[1]; m[9]  =  u[2]; m[13] = -(u[0]*px + u[1]*py + u[2]*pz);
    m[2]  = -f[0]; m[6] = -f[1]; m[10] = -f[2]; m[14] =  (f[0]*px + f[1]*py + f[2]*pz);
    m[3]  =  0.0f; m[7] =  0.0f; m[11] =  0.0f; m[15] =  1.0f;
}

/* --- setup -------------------------------------------------------------- */

extern GLuint gfx_build_program(const char *vs, const char *fs);  /* main.c */

static void setup_attribs(GLuint vao, GLuint vbo)
{
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Point), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(Point),
                          (void *)(3 * sizeof(float)));
}

void game_init(void)
{
    g_pts = (Point *)malloc((size_t)MAX_POINTS * sizeof(Point));
    g_count = 0;

    g_px = 0.0f; g_py = 0.0f; g_pz = 0.0f;
    g_yaw = 0.0f; g_pitch = 0.0f;
    g_ping_ready = 0.0f;
    g_flash = 0.0f;
    g_hits = 0;
    g_mseed = 7.0f;

    g_prog    = gfx_build_program(POINT_VS, POINT_FS);
    u_vp      = glGetUniformLocation(g_prog, "uVP");
    u_cam     = glGetUniformLocation(g_prog, "uCam");
    u_time    = glGetUniformLocation(g_prog, "uTime");
    u_monster = glGetUniformLocation(g_prog, "uMonster");

    glGenVertexArrays(1, &g_vao);
    glGenBuffers(1, &g_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)MAX_POINTS * (GLsizeiptr)sizeof(Point),
                 0, GL_DYNAMIC_DRAW);
    setup_attribs(g_vao, g_vbo);

    glGenVertexArrays(1, &g_mvao);
    glGenBuffers(1, &g_mvbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_mvbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof g_mpts, 0, GL_DYNAMIC_DRAW);
    setup_attribs(g_mvao, g_mvbo);

    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);          /* light adds up, like real returns */
    glDisable(GL_DEPTH_TEST);             /* additive, so order does not matter */

    mon_reposition(0.0f);
}

/* --- movement ------------------------------------------------------------
 * Axis-separated so walking into a wall slides along it instead of stopping
 * dead. There is no gravity yet: this build swims. */

static void try_move(float dx, float dy, float dz)
{
    if (cave_sdf(g_px + dx, g_py, g_pz) > 0.42f) g_px += dx;
    if (cave_sdf(g_px, g_py + dy, g_pz) > 0.42f) g_py += dy;
    if (cave_sdf(g_px, g_py, g_pz + dz) > 0.42f) g_pz += dz;
}

void game_frame(const GameInput *in, float dt, float now, int width, int height)
{
    float f[3], r[3], u[3];
    float mx = 0.0f, my = 0.0f, mz = 0.0f, len;
    float proj[16], view[16], vp[16];
    float limit = 1.5533f;                       /* just under 89 degrees */

    /* look */
    g_yaw   += in->mdx * MOUSE_SENS;
    g_pitch -= in->mdy * MOUSE_SENS;
    if (g_pitch >  limit) g_pitch =  limit;
    if (g_pitch < -limit) g_pitch = -limit;

    basis(g_yaw, g_pitch, f, r, u);

    /* move */
    if (in->fwd)   { mx += f[0]; my += f[1]; mz += f[2]; }
    if (in->back)  { mx -= f[0]; my -= f[1]; mz -= f[2]; }
    if (in->right) { mx += r[0]; my += r[1]; mz += r[2]; }
    if (in->left)  { mx -= r[0]; my -= r[1]; mz -= r[2]; }

    len = (float)sqrt(mx * mx + my * my + mz * mz);
    if (len > 0.0001f) {
        float s = MOVE_SPEED * dt / len;
        try_move(mx * s, my * s, mz * s);
    }

    /* ping */
    if (in->ping && now >= g_ping_ready) {
        emit_ping(now);
        g_ping_ready = now + PING_COOLDOWN;
    }

    mon_update(dt, now);

    g_flash -= dt * 1.6f;
    if (g_flash < 0.0f) g_flash = 0.0f;

    /* draw */
    glClearColor(0.008f + g_flash * 0.30f, 0.012f, 0.020f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    mat4_persp(proj, 1.30f,
               (float)width / (height > 0 ? (float)height : 1.0f),
               0.05f, 60.0f);
    mat4_view(view, g_px, g_py, g_pz, g_yaw, g_pitch);
    mat4_mul(vp, proj, view);

    glUseProgram(g_prog);
    glUniformMatrix4fv(u_vp, 1, GL_FALSE, vp);
    glUniform3f(u_cam, g_px, g_py, g_pz);
    glUniform1f(u_time, now);

    if (g_count > 0) {
        glUniform1f(u_monster, 0.0f);
        glBindVertexArray(g_vao);
        glDrawArrays(GL_POINTS, 0, g_count);
    }

    if (mon_visible()) {
        mon_emit_points(now);
        glUniform1f(u_monster, 1.0f);
        glBindVertexArray(g_mvao);
        glDrawArrays(GL_POINTS, 0, MON_POINTS);
    }
}

int   game_point_count(void) { return g_count; }
float game_depth(void)       { return g_pz; }
int   game_hits(void)        { return g_hits; }
