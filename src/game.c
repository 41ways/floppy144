/* game.c - the cave, the ping, the point cloud, and the things that hear it.
 *
 * There is no mesh anywhere in this file. The cave is a signed distance
 * function; a ping fires a few thousand rays into it and keeps whatever they
 * hit. Those hits are the only geometry the game ever draws, which is why the
 * whole thing costs almost nothing on disk.
 *
 * Structure: three lives make one attempt. Dying keeps the map you lit and
 * puts you back at the entrance, so the second and third descents are a silent
 * sprint through your own memory. Losing the last life throws the cave away
 * and generates a new one from a fresh seed. */

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
#define WAVE_SPEED     11.0f   /* metres per second the wavefront travels */
#define MOVE_SPEED      2.7f
#define MOUSE_SENS      0.0022f
#define PING_COOLDOWN   0.45f

#define START_LIVES        3
#define DEPTH_FULL     140.0f  /* metres at which the cave is at its worst */

#define MON_POINTS       800
#define MAX_MON            4
#define MON_KILL_DIST   0.85f

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
static float g_flash;
static int   g_lives;
static float g_best_depth;

/* --- cave shape, rerolled every attempt --------------------------------- */

static float    g_seed;      /* phase offset fed into every noise lookup */
static float    g_wander;    /* how much the tunnel snakes */
static float    g_rough;     /* how eroded the walls are */
static unsigned g_rng;

static unsigned rnd(void)
{
    g_rng = g_rng * 1664525u + 1013904223u;
    return g_rng;
}

static float rndf(void)
{
    return (float)(rnd() >> 8) / 16777216.0f;   /* 0..1 */
}

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
 * How far down a point is, from 0 at the entrance to 1 at the worst depth.
 * Every difficulty dial in the game is a function of this one number. */

static float depth_k(float z)
{
    float d = -z / DEPTH_FULL;
    if (d < 0.0f) return 0.0f;
    if (d > 1.0f) return 1.0f;
    return d;
}

static void tunnel_centre(float z, float *cx, float *cy)
{
    float w = g_wander;
    *cx = (float)sin(z * 0.13 + g_seed)        * 3.2f * w
        + (float)sin(z * 0.047 + g_seed * 1.7) * 2.1f * w;
    *cy = (float)cos(z * 0.097 + g_seed * 2.3) * 1.6f * w;
}

/* Positive in air, negative in rock, zero on the wall.
 * The radius shrinks with depth, which is the whole difficulty curve. */
static float cave_sdf(float x, float y, float z)
{
    float cx, cy, dx, dy, r, rad;
    float k = depth_k(z);
    tunnel_centre(z, &cx, &cy);
    dx = x - cx;
    dy = (y - cy) * 1.25f;                       /* flatter than it is wide */
    r  = (float)sqrt(dx * dx + dy * dy);
    rad = (2.35f - 0.95f * k)
        + 1.15f * g_rough * fbm2((float)atan2(dy, dx) * 1.6f, z * 0.42f + g_seed);
    return rad - r;
}

/* March until we leave the air. The field is not a true distance function
 * (the fbm lies about how far the wall is), so steps stay conservative. */
static int cave_ray(float ox, float oy, float oz,
                    float dx, float dy, float dz, float maxd, float *hit)
{
    float t = 0.06f;
    int i;
    for (i = 0; i < RAY_STEPS && t < maxd; i++) {
        float d = cave_sdf(ox + dx * t, oy + dy * t, oz + dz * t);
        if (d < 0.02f) { *hit = t; return 1; }
        t += (d * 0.55f > 0.035f) ? d * 0.55f : 0.035f;
    }
    return 0;
}

/* --- the things ----------------------------------------------------------
 * Three archetypes, and the difference between them is three numbers. Deeper
 * water brings more of them, and brings the nastier kinds. */

enum { MON_DORMANT, MON_WAKING, MON_CHARGING, MON_SPENT };
enum { T_STALKER, T_RUSHER, T_LISTENER };

typedef struct {
    int   state, type;
    float x, y, z;
    float tx, ty, tz;          /* where the ping came from */
    float dx, dy, dz;          /* charge direction */
    float wake;                /* absolute time the wavefront arrives */
    float timer;
    float seed;
    float speed, warn, hear;   /* the archetype, as three numbers */
} Monster;

static Monster g_mon[MAX_MON];
static int     g_mon_count;

static void mon_make(Monster *m, int type, float k)
{
    m->type = type;
    switch (type) {
    case T_RUSHER:      /* little warning, very fast, poor hearing */
        m->speed = 9.0f + 2.6f * k;
        m->warn  = 0.42f - 0.12f * k;
        m->hear  = 19.0f;
        break;
    case T_LISTENER:    /* slow and generous, but hears you from far away */
        m->speed = 5.6f + 1.2f * k;
        m->warn  = 0.72f;
        m->hear  = 32.0f + 8.0f * k;
        break;
    default:            /* T_STALKER - the one you learn the game on */
        m->speed = 7.4f + 2.4f * k;
        m->warn  = 0.58f - 0.16f * k;
        m->hear  = 24.0f + 8.0f * k;
        break;
    }
}

static void mon_place(Monster *m, float now)
{
    float cx, cy, z, k;
    m->seed += 1.618f;
    /* it waits further down the tunnel, off to one side of the axis */
    z = g_pz - 13.0f - hash1(now * 3.7f + m->seed) * 13.0f;
    k = depth_k(z);
    tunnel_centre(z, &cx, &cy);
    m->x = cx + (hash1(m->seed * 5.1f) - 0.5f) * 2.0f;
    m->y = cy + (hash1(m->seed * 9.3f) - 0.5f) * 1.3f;
    m->z = z;
    m->state = MON_DORMANT;
    m->wake  = -1.0f;
    m->timer = 0.0f;

    /* deeper water is where the other kinds live */
    if (k > 0.62f)      mon_make(m, (rnd() & 1) ? T_RUSHER : T_LISTENER, k);
    else if (k > 0.28f) mon_make(m, (rnd() & 3) ? T_STALKER : T_RUSHER, k);
    else                mon_make(m, T_STALKER, k);
}

/* how many of them are awake in the cave at this depth */
static int mon_target_count(void)
{
    float k = depth_k(g_pz);
    if (k > 0.72f) return 4;
    if (k > 0.46f) return 3;
    if (k > 0.20f) return 2;
    return 1;
}

/* A ping does not travel instantly, so the moment it reaches them is
 * scheduled rather than immediate. That delay is the eerie beat between
 * firing and hearing the answer. */
static void mon_hear_ping(float ox, float oy, float oz, float now)
{
    int i;
    for (i = 0; i < g_mon_count; i++) {
        Monster *m = &g_mon[i];
        float dx = m->x - ox, dy = m->y - oy, dz = m->z - oz;
        float d  = (float)sqrt(dx * dx + dy * dy + dz * dz);
        if (m->state != MON_DORMANT) continue;
        if (d > m->hear) continue;
        m->wake = now + d / WAVE_SPEED;
        m->tx = ox; m->ty = oy; m->tz = oz;   /* it remembers where, not who */
    }
}

static int mon_step(Monster *m, float dt, float now)
{
    float dx, dy, dz, d;
    int killed = 0;

    switch (m->state) {
    case MON_DORMANT:
        if (m->wake > 0.0f && now >= m->wake) {
            m->state = MON_WAKING;
            m->timer = m->warn;
            audio_roar();
        }
        break;

    case MON_WAKING:
        /* lit and loud but not yet moving - this is the dodge window */
        m->timer -= dt;
        if (m->timer <= 0.0f) {
            dx = m->tx - m->x; dy = m->ty - m->y; dz = m->tz - m->z;
            d  = (float)sqrt(dx * dx + dy * dy + dz * dz);
            if (d < 0.001f) d = 1.0f;
            m->dx = dx / d; m->dy = dy / d; m->dz = dz / d;
            m->state = MON_CHARGING;
            m->timer = 3.2f;
        }
        break;

    case MON_CHARGING:
        /* dead straight, through rock if rock is in the way */
        m->x += m->dx * m->speed * dt;
        m->y += m->dy * m->speed * dt;
        m->z += m->dz * m->speed * dt;
        m->timer -= dt;

        dx = g_px - m->x; dy = g_py - m->y; dz = g_pz - m->z;
        if (dx * dx + dy * dy + dz * dz < MON_KILL_DIST * MON_KILL_DIST) {
            killed = 1;
            m->state = MON_SPENT;
            m->timer = 2.2f;
        } else if (m->timer <= 0.0f) {
            m->state = MON_SPENT;
            m->timer = 1.6f;
        }
        break;

    case MON_SPENT:
        m->timer -= dt;
        if (m->timer <= 0.0f) mon_place(m, now);
        break;
    }
    return killed;
}

static int mon_visible(const Monster *m)
{
    return m->state == MON_WAKING || m->state == MON_CHARGING;
}

/* Its body, scattered as returns. Regenerated every frame it is audible, so
 * these never join the permanent map - the map is walls only. */
static void mon_emit_points(const Monster *m, float now)
{
    int i;
    for (i = 0; i < MON_POINTS; i++) {
        float a = hash1((float)i * 1.7f + m->seed) * 6.2831853f;
        float b = hash1((float)i * 3.1f + m->seed + 4.0f) * 3.1415927f;
        float r = 0.75f * (float)pow(hash1((float)i * 5.3f + now * 0.7f), 0.35);
        g_mpts[i].x = m->x + r * (float)sin(b) * (float)cos(a) * 0.9f;
        g_mpts[i].y = m->y + r * (float)cos(b) * 1.3f;
        g_mpts[i].z = m->z + r * (float)sin(b) * (float)sin(a) * 0.9f;
        g_mpts[i].reveal = now;              /* always at the wavefront */
    }
    glBindBuffer(GL_ARRAY_BUFFER, g_mvbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)sizeof g_mpts, g_mpts);
}

/* --- ping ----------------------------------------------------------------
 * Rays are spread over the sphere with the golden angle, which covers evenly
 * instead of clumping at the poles the way naive lat/long sampling does. */

static void emit_ping(float now)
{
    int i;
    int start = g_count;
    int added = 0;
    float reach = 26.0f - 11.0f * depth_k(g_pz);   /* rock swallows more, deeper */

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
        if (!cave_ray(g_px, g_py, g_pz, dx, dy, dz, reach, &t)) continue;

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
    f[0] =  sy * cp; f[1] =  sp;   f[2] = -cy * cp;
    r[0] =  cy;      r[1] =  0.0f; r[2] =  sy;
    u[0] = -sy * sp; u[1] =  cp;   u[2] =  cy * sp;
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

/* --- attempts and lives -------------------------------------------------- */

static void respawn(float now)
{
    int i;
    g_px = 0.0f; g_py = 0.0f; g_pz = 0.0f;
    g_yaw = 0.0f; g_pitch = 0.0f;
    g_ping_ready = now + 0.6f;
    g_mon_count = mon_target_count();
    for (i = 0; i < MAX_MON; i++) mon_place(&g_mon[i], now + (float)i);
}

/* A fresh cave: new phase offsets, new snake, new roughness. The map you
 * built is thrown away with it, because it described a place that no longer
 * exists. */
static void new_attempt(unsigned seed, float now)
{
    g_rng    = seed ? seed : 1u;
    g_seed   = rndf() * 1000.0f;
    g_wander = 0.75f + rndf() * 0.85f;      /* 0.75 .. 1.60 */
    g_rough  = 0.70f + rndf() * 0.70f;      /* 0.70 .. 1.40 */
    g_count  = 0;
    g_lives  = START_LIVES;
    g_best_depth = 0.0f;
    respawn(now);
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

void game_init(unsigned seed)
{
    g_pts = (Point *)malloc((size_t)MAX_POINTS * sizeof(Point));
    g_flash = 0.0f;

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

    new_attempt(seed, 0.0f);
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
    int i, want;

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

    if (-g_pz > g_best_depth) g_best_depth = -g_pz;

    /* ping */
    if (in->ping && now >= g_ping_ready) {
        emit_ping(now);
        g_ping_ready = now + PING_COOLDOWN;
    }

    /* the cave gets busier as you descend */
    want = mon_target_count();
    if (want > g_mon_count) {
        for (i = g_mon_count; i < want; i++) mon_place(&g_mon[i], now + (float)i);
        g_mon_count = want;
    }

    for (i = 0; i < g_mon_count; i++) {
        if (mon_step(&g_mon[i], dt, now)) {
            g_lives--;
            g_flash = 1.0f;
            audio_hit();
            if (g_lives <= 0) {
                /* the last life: throw the cave away and roll a new one */
                new_attempt((unsigned)(now * 1000.0f) ^ rnd(), now);
            } else {
                /* you keep the map - walking back down is the reward for it */
                respawn(now);
            }
            break;
        }
    }

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

    glUniform1f(u_monster, 1.0f);
    glBindVertexArray(g_mvao);
    for (i = 0; i < g_mon_count; i++) {
        if (!mon_visible(&g_mon[i])) continue;
        mon_emit_points(&g_mon[i], now);
        glDrawArrays(GL_POINTS, 0, MON_POINTS);
    }
}

int   game_point_count(void) { return g_count; }
float game_depth(void)       { return -g_pz; }
int   game_lives(void)       { return g_lives; }
int   game_monsters(void)    { return g_mon_count; }
