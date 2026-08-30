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
#include <stdio.h>

/* --- tuning ------------------------------------------------------------- */

#define MAX_POINTS   1200000   /* 19 MB of RAM, 0 bytes on disk */
#define PING_RAYS       1100   /* every one of them is a visible bullet */
#define PING_BOUNCES      10   /* it dies of exhaustion, not of a counter */
#define MAX_TRAVEL     52.0f   /* hard stop, but energy gets there first */
#define GAIN_BOUNCE    0.74f   /* what a wall costs it */
#define MARK_SPLASH        5   /* marks left per impact - density makes surfaces */
#define SPLASH_R       0.19f   /* how far they scatter across the rock */
#define VERT_SPREAD    0.34f   /* the spray is much wider than it is tall */
#define VERT_DAMP      0.50f   /* and a ricochet is pulled hard back toward level */
#define GAIN_PER_METRE 0.028f  /* what the air costs it */
#define GAIN_FLOOR     0.045f  /* below this it has faded out */
#define GRAZE_MIN      0.15f   /* below this the hit is a graze, not a bounce */
#define MIN_SEGMENT    0.40f   /* a bounce that goes nowhere is a graze too */
#define WAVE_POINTS   230000   /* the bullets in flight: shown, never kept */
#define MUZZLE_DROP    0.12f   /* barely below the eye - see MUZZLE_FWD */
#define MUZZLE_FWD     0.35f   /* and a step ahead, so they fly away from you */
#define WAVE_STEP      0.32f   /* dense enough that a bullet reads as a dash */
#define PING_CHUNK       400   /* rays traced per frame, so nothing stalls */
#define RAY_STEPS         72
#define WAVE_SPEED     11.0f   /* metres per second the wavefront travels */
#define MOVE_SPEED      2.7f
#define MOUSE_SENS      0.0022f
#define PING_COOLDOWN   0.45f

#define START_LIVES        3
#define DEPTH_FULL     140.0f  /* metres at which the cave is at its worst */

#define MON_POINTS      2400   /* limbs need volume, not a dotted line */
#define LEG_TUBE           5   /* points around the curve at each sample */
#define MAX_MON            4
#define MON_KILL_DIST   1.05f
#define WALL_HUG       0.34f   /* how far off the rock it rides */
#define WEAVE_RATE      2.9f   /* how fast it swings side to side */
#define WEAVE_AMT      0.52f   /* and how far - too much and it circles instead of closing */
#define GAIT_RATE       4.4f   /* leg cycles per metre travelled */
#define MON_RANGE      30.0f   /* how far it will chase before it comes apart */
#define BURST_TIME     0.75f   /* and how long it takes to scatter */

/* --- point cloud -------------------------------------------------------- */

typedef struct { float x, y, z, reveal, gain; } Point;   /* 20 bytes */

static Point *g_pts;
static int    g_count;
static Point *g_wpts;          /* the wave in flight, a ring that nobody reads back */
static int    g_wcount;
static GLuint g_vao, g_vbo, g_wvao, g_wvbo, g_mvao, g_mvbo, g_hvao, g_hvbo, g_prog;
static GLint  u_vp, u_cam, u_time, u_monster, u_persist, u_flat, u_base;
static Point *g_mpts;          /* heap: 48 KB of it has no business in the exe */

/* --- state -------------------------------------------------------------- */

enum { ST_TITLE, ST_PLAY };
static int g_state;

extern int plat_text_points(const char *str, int px, float *out_xy, int max);

/* --- player ------------------------------------------------------------- */

static float g_px, g_py, g_pz;
static float g_yaw, g_pitch;
static float g_ping_ready;
static float g_flash;
static int   g_lives;
static float g_best_depth;
static int   g_has_moved;

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

/* This used to be sin(n)*43758.5453 fract, the shader-toy idiom. Every call
 * to the distance field runs it six times, so a single ping was paying for
 * roughly ten million sines. Hashing the float's bits instead took one ping
 * from 51 ms to 21. */
static float hash1(float n)
{
    unsigned h;
    memcpy(&h, &n, sizeof h);
    h ^= h >> 15; h *= 0x2c1b3c6du;
    h ^= h >> 12; h *= 0x297a2d39u;
    h ^= h >> 15;
    return (float)(h & 0xFFFFFFu) / 16777216.0f;
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
        t += (d * 0.70f > 0.035f) ? d * 0.70f : 0.035f;
    }
    return 0;
}

/* The field is positive inside the air, so its gradient points back into the
 * cave - which is exactly the normal a wave needs to bounce off. */
static void cave_normal(float x, float y, float z, float *n)
{
    const float e = 0.035f;
    float nx = cave_sdf(x + e, y, z) - cave_sdf(x - e, y, z);
    float ny = cave_sdf(x, y + e, z) - cave_sdf(x, y - e, z);
    float nz = cave_sdf(x, y, z + e) - cave_sdf(x, y, z - e);
    float l  = (float)sqrt(nx * nx + ny * ny + nz * nz);
    if (l < 1e-6f) { n[0] = 0.0f; n[1] = 1.0f; n[2] = 0.0f; return; }
    n[0] = nx / l; n[1] = ny / l; n[2] = nz / l;
}

/* --- the things ----------------------------------------------------------
 * Three archetypes, and the difference between them is three numbers. Deeper
 * water brings more of them, and brings the nastier kinds. */

enum { MON_DORMANT, MON_WAKING, MON_CHARGING, MON_BURST, MON_SPENT };
enum { T_STALKER, T_RUSHER, T_LISTENER };

typedef struct {
    int   state, type;
    float x, y, z;
    float tx, ty, tz;          /* where the ping came from */
    float dx, dy, dz;          /* the way it is facing along the wall */
    float nx, ny, nz;          /* the rock it is clinging to */
    float wake;                /* absolute time the wavefront arrives */
    float timer, seed, travel, gait;
    float speed, warn, hear;   /* the archetype, as three numbers */
    float scale, legspan;      /* and how big a spider it is */
} Monster;

static Monster g_mon[MAX_MON];
static int     g_mon_count;

static void mon_make(Monster *m, int type, float k)
{
    m->type = type;
    switch (type) {
    case T_RUSHER:      /* the big one: little warning, fast, poor hearing */
        m->speed   = 8.0f + 2.4f * k;
        m->warn    = 0.42f - 0.12f * k;
        m->hear    = 19.0f;
        m->scale   = 1.15f;
        m->legspan = 1.00f;
        break;
    case T_LISTENER:    /* slow and generous, but hears you from far away */
        m->speed   = 5.0f + 1.2f * k;
        m->warn    = 0.72f;
        m->hear    = 32.0f + 8.0f * k;
        m->scale   = 0.80f;
        m->legspan = 2.10f;      /* a harvestman: mostly leg */
        break;
    default:            /* T_STALKER - the small one you learn the game on */
        m->speed   = 6.6f + 2.2f * k;
        m->warn    = 0.58f - 0.16f * k;
        m->hear    = 24.0f + 8.0f * k;
        m->scale   = 0.55f;
        m->legspan = 1.00f;
        break;
    }
}

/* March out from the tunnel axis until the rock stops us, then sit a little
 * way off it. Everything these things do happens on a surface. */
static void wall_spot(float z, float ang, float *out)
{
    float cx, cy, r;
    tunnel_centre(z, &cx, &cy);
    for (r = 0.2f; r < 6.0f; r += 0.06f) {
        float x = cx + (float)cos(ang) * r;
        float y = cy + (float)sin(ang) * r;
        if (cave_sdf(x, y, z) < WALL_HUG) {
            out[0] = x; out[1] = y; out[2] = z;
            return;
        }
    }
    out[0] = cx; out[1] = cy; out[2] = z;
}

static void mon_place(Monster *m, float now)
{
    float z, k, ang, pos[3], n[3];
    m->seed += 1.618f;
    z   = g_pz - 13.0f - hash1(now * 3.7f + m->seed) * 13.0f;
    ang = hash1(m->seed * 5.1f) * 6.2831853f;
    k   = depth_k(z);

    wall_spot(z, ang, pos);
    m->x = pos[0]; m->y = pos[1]; m->z = pos[2];

    cave_normal(m->x, m->y, m->z, n);
    m->nx = n[0]; m->ny = n[1]; m->nz = n[2];
    m->dx = 0.0f; m->dy = 0.0f; m->dz = -1.0f;

    m->state  = MON_DORMANT;
    m->wake   = -1.0f;
    m->timer  = 0.0f;
    m->travel = 0.0f;
    m->gait   = hash1(m->seed * 3.3f) * 6.2831853f;

    /* deeper water is where the other kinds live */
    if (k > 0.62f)      mon_make(m, (rnd() & 1) ? T_RUSHER : T_LISTENER, k);
    else if (k > 0.28f) mon_make(m, (rnd() & 3) ? T_STALKER : T_RUSHER, k);
    else                mon_make(m, T_STALKER, k);
}

/* how many of them are awake in the cave at this depth */
#define SAFE_DEPTH 14.0f       /* nothing hunts you while you learn to listen */

static int mon_target_count(void)
{
    float k = depth_k(g_pz);
    if (-g_pz < SAFE_DEPTH) return 0;
    if (k > 0.72f) return 4;
    if (k > 0.46f) return 3;
    if (k > 0.20f) return 2;
    return 1;
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
            m->state  = MON_CHARGING;
            m->timer  = 6.0f;
            m->travel = 0.0f;
        }
        break;

    case MON_CHARGING: {
        /* It never crosses open air. The direction it wants is toward you,
         * flattened onto the rock it is holding, with a sideways swing laid
         * over the top - so it arrives along whichever wall it started on,
         * weaving, rather than on a straight line through the middle. */
        float n[3], tang[3], bino[3], len, w, sd, step;

        cave_normal(m->x, m->y, m->z, n);
        dx = g_px - m->x; dy = g_py - m->y; dz = g_pz - m->z;
        d  = dx * n[0] + dy * n[1] + dz * n[2];
        tang[0] = dx - d * n[0];
        tang[1] = dy - d * n[1];
        tang[2] = dz - d * n[2];
        len = (float)sqrt(tang[0]*tang[0] + tang[1]*tang[1] + tang[2]*tang[2]);
        if (len < 1e-4f) { tang[0] = m->dx; tang[1] = m->dy; tang[2] = m->dz; len = 1.0f; }
        tang[0] /= len; tang[1] /= len; tang[2] /= len;

        bino[0] = n[1]*tang[2] - n[2]*tang[1];
        bino[1] = n[2]*tang[0] - n[0]*tang[2];
        bino[2] = n[0]*tang[1] - n[1]*tang[0];

        w = (float)sin(now * WEAVE_RATE + m->seed) * WEAVE_AMT;
        m->dx = tang[0] + bino[0] * w;
        m->dy = tang[1] + bino[1] * w;
        m->dz = tang[2] + bino[2] * w;
        len = (float)sqrt(m->dx*m->dx + m->dy*m->dy + m->dz*m->dz);
        if (len > 1e-6f) { m->dx /= len; m->dy /= len; m->dz /= len; }

        step = m->speed * dt;
        m->x += m->dx * step;
        m->y += m->dy * step;
        m->z += m->dz * step;
        m->travel += step;
        m->gait   += step * GAIT_RATE;

        /* hold on: keep a fixed distance off the rock as the wall curves */
        cave_normal(m->x, m->y, m->z, n);
        sd = cave_sdf(m->x, m->y, m->z);
        m->x += n[0] * (WALL_HUG - sd);
        m->y += n[1] * (WALL_HUG - sd);
        m->z += n[2] * (WALL_HUG - sd);
        m->nx = n[0]; m->ny = n[1]; m->nz = n[2];

        m->timer -= dt;
        dx = g_px - m->x; dy = g_py - m->y; dz = g_pz - m->z;
        if (dx*dx + dy*dy + dz*dz < MON_KILL_DIST * MON_KILL_DIST) {
            killed = 1;
            m->state = MON_BURST;
            m->timer = BURST_TIME;
        } else if (m->travel > MON_RANGE || m->timer <= 0.0f) {
            /* it has run itself out and simply comes apart */
            m->state = MON_BURST;
            m->timer = BURST_TIME;
        }
        break;
    }

    case MON_BURST:
        m->timer -= dt;
        if (m->timer <= 0.0f) { m->state = MON_SPENT; m->timer = 1.4f; }
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
    return m->state == MON_WAKING || m->state == MON_CHARGING
        || m->state == MON_BURST;
}

/* A spider, built out of returns.
 *
 * Eight legs on a body that rides the rock: the normal is up, the way it is
 * travelling is forward. Knees stand above the body and the feet reach down
 * past it, so the silhouette is a low sprawl rather than a blob. The gait
 * advances with distance covered rather than with time, which is why it reads
 * as scuttling instead of sliding.
 *
 * Regenerated every frame it is visible, so it never joins the permanent map. */
static void mon_emit_points(const Monster *m, float now)
{
    float n[3], f[3], sd[3], len;
    float sc = m->scale, sp = m->legspan;
    int i, k, w = 0;

    n[0] = m->nx; n[1] = m->ny; n[2] = m->nz;
    f[0] = m->dx; f[1] = m->dy; f[2] = m->dz;
    /* forward, flattened onto the rock so the body lies along the surface */
    len = f[0]*n[0] + f[1]*n[1] + f[2]*n[2];
    f[0] -= len*n[0]; f[1] -= len*n[1]; f[2] -= len*n[2];
    len = (float)sqrt(f[0]*f[0] + f[1]*f[1] + f[2]*f[2]);
    if (len < 1e-4f) { f[0] = 1.0f; f[1] = 0.0f; f[2] = 0.0f; len = 1.0f; }
    f[0] /= len; f[1] /= len; f[2] /= len;
    sd[0] = n[1]*f[2] - n[2]*f[1];
    sd[1] = n[2]*f[0] - n[0]*f[2];
    sd[2] = n[0]*f[1] - n[1]*f[0];

#define PUT(A, B, C, G)     do { if (w < MON_POINTS) {         g_mpts[w].x = m->x + f[0]*(A) + sd[0]*(B) + n[0]*(C);         g_mpts[w].y = m->y + f[1]*(A) + sd[1]*(B) + n[1]*(C);         g_mpts[w].z = m->z + f[2]*(A) + sd[2]*(B) + n[2]*(C);         g_mpts[w].reveal = now; g_mpts[w].gain = (G); w++; } } while (0)

    /* body: a squat abdomen and a smaller head slung forward */
    for (i = 0; i < 260; i++) {
        float a = hash1((float)i * 1.7f + m->seed) * 6.2831853f;
        float b = hash1((float)i * 3.1f + m->seed + 4.0f) * 3.1415927f;
        float r = 0.26f * sc * (float)pow(hash1((float)i * 5.3f + m->seed), 0.34);
        PUT(-0.10f * sc + r*(float)sin(b)*(float)cos(a) * 1.25f,
             r*(float)sin(b)*(float)sin(a),
             r*(float)cos(b) * 0.75f, 1.0f);
    }
    for (i = 0; i < 95; i++) {
        float a = hash1((float)i * 2.3f + m->seed + 9.0f) * 6.2831853f;
        float b = hash1((float)i * 4.7f + m->seed + 2.0f) * 3.1415927f;
        float r = 0.13f * sc;
        PUT(0.26f * sc + r*(float)sin(b)*(float)cos(a),
            r*(float)sin(b)*(float)sin(a),
            r*(float)cos(b) * 0.8f, 1.0f);
    }

    /* eight legs, alternating in two sets the way a real one walks */
    for (i = 0; i < 8; i++) {
        float side  = (i & 1) ? 1.0f : -1.0f;
        int   pair  = i >> 1;
        float along = (-0.30f + 0.22f * (float)pair) * sc;
        float reach = (0.54f + 0.14f * (float)pair) * sc * sp;
        float ph    = m->gait + ((i & 1) ? 3.1415927f : 0.0f) + (float)pair * 0.55f;
        float lift  = (float)sin(ph); lift = lift > 0.0f ? lift * 0.30f * sc : 0.0f;
        float swing = (float)cos(ph) * 0.26f * sc;

        /* Two straight segments read as wire spokes. One quadratic with the
         * knee as its control point gives the high arched limb that says
         * spider before anything else does - the body slung low under a tent
         * of legs. */
        float kx = along + swing * 0.40f, ky = side * reach * 0.46f, kz = 0.86f * sc;
        float ex = along + swing,         ey = side * reach,          ez = -0.34f * sc + lift;

        /* A single line of points along the curve reads as wire. Scattering a
         * few around it at every sample gives the limb a thickness that
         * tapers from a heavy haunch down to a fine tip - which is what makes
         * it look like something that could carry the body. */
        for (k = 0; k < 48; k++) {
            float u   = (float)k / 47.0f;
            float iu  = 1.0f - u;
            float a   = 2.0f * iu * u * kx + u * u * ex;
            float b2  = 2.0f * iu * u * ky + u * u * ey;
            float c2  = 2.0f * iu * u * kz + u * u * ez;
            float rad = (0.105f * iu * iu + 0.016f) * sc;
            int   q;
            for (q = 0; q < LEG_TUBE; q++) {
                float h  = (float)(i * 97 + k * 13 + q) * 1.31f + m->seed;
                float aa = hash1(h) * 6.2831853f;
                float bb = hash1(h + 3.7f) * 3.1415927f;
                float rr = rad * (float)pow(hash1(h + 8.1f), 0.34);
                PUT(a  + rr * (float)sin(bb) * (float)cos(aa),
                    b2 + rr * (float)sin(bb) * (float)sin(aa),
                    c2 + rr * (float)cos(bb),
                    0.55f + 0.45f * iu);
            }
        }
    }
#undef PUT

    /* When it is spent the whole thing lets go at once and drifts apart. */
    if (m->state == MON_BURST) {
        float prog = 1.0f - m->timer / BURST_TIME;
        float fade = m->timer / BURST_TIME;
        for (i = 0; i < w; i++) {
            float ox = g_mpts[i].x - m->x, oy = g_mpts[i].y - m->y, oz = g_mpts[i].z - m->z;
            float ol = (float)sqrt(ox*ox + oy*oy + oz*oz);
            float push = (0.5f + hash1((float)i * 7.1f + m->seed) * 1.9f) * prog;
            if (ol > 1e-4f) {
                g_mpts[i].x += ox / ol * push;
                g_mpts[i].y += oy / ol * push;
                g_mpts[i].z += oz / ol * push;
            }
            g_mpts[i].gain *= fade * fade;
        }
    }

    for (i = w; i < MON_POINTS; i++) g_mpts[i].gain = 0.0f;

    glBindBuffer(GL_ARRAY_BUFFER, g_mvbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)MON_POINTS * (GLsizeiptr)sizeof(Point), g_mpts);
}

/* --- ping ----------------------------------------------------------------
 * On foot the ping is a beam, not a sphere: it lights what you are facing and
 * nothing else. That makes looking a deliberate act with a direction, and it
 * means the dark behind you stays dark. (The full spherical wave comes back
 * when the cave floods and you are swimming.)
 *
 * Directions are laid out over the spherical cap with the golden angle, which
 * covers evenly instead of clumping in the middle. */

/* Nearly a full forward hemisphere. A narrow cone only ever lit the middle of
 * the screen, which reads as a torch; spraying everything in front of you is
 * what makes the shape of the tunnel arrive all at once. Behind you stays
 * dark, and that is the only limit. */
#define CONE_HALF_ANGLE 1.50f      /* radians, about 86 degrees */

static float ping_reach(void)
{
    return 26.0f - 11.0f * depth_k(g_pz);   /* rock swallows more, deeper */
}

/* Did the beam actually land on it? Being behind you, or around a corner,
 * means it never knows you were there. */
static void mon_lit_by(const float *f, float reach, float now)
{
    int i;
    for (i = 0; i < g_mon_count; i++) {
        Monster *m = &g_mon[i];
        float dx = m->x - g_px, dy = m->y - g_py, dz = m->z - g_pz;
        float d  = (float)sqrt(dx * dx + dy * dy + dz * dz);
        float cosang, wall;

        if (m->state != MON_DORMANT) continue;
        if (d > reach || d > m->hear || d < 0.001f) continue;

        cosang = (dx * f[0] + dy * f[1] + dz * f[2]) / d;
        if (cosang < (float)cos(CONE_HALF_ANGLE)) continue;   /* outside the beam */

        /* rock in between swallows it */
        if (cave_ray(g_px, g_py, g_pz, dx / d, dy / d, dz / d, d, &wall)) continue;

        m->wake = now + d / WAVE_SPEED;
        m->tx = g_px; m->ty = g_py; m->tz = g_pz;  /* it remembers where, not who */
    }
}

/* A ping is 1,600 rays bouncing up to ten times, which is far too much work
 * to do between two frames. The front only travels eleven metres a second, so
 * it does not reach the first wall for a fifth of a second - about thirteen
 * frames. Tracing a slice per frame is therefore invisible, and the reveal
 * times all come from when the ping was fired rather than when a given ray
 * happened to be computed. */

static int   g_ping_busy, g_ping_i;
static float g_ping_ox, g_ping_oy, g_ping_oz, g_ping_t0;
static float g_pf[3], g_pr[3], g_pu[3];

static void ping_begin(float now, const float *f, const float *r, const float *u)
{
    int j;
    for (j = 0; j < 3; j++) { g_pf[j] = f[j]; g_pr[j] = r[j]; g_pu[j] = u[j]; }
    /* Firing from foot height made the volley appear to climb up out of the
     * floor. It leaves from just in front of the chest instead, and a little
     * ahead, so what you see is bullets going away rather than rising past
     * you. */
    g_ping_ox = g_px + f[0] * MUZZLE_FWD;
    g_ping_oy = g_py + f[1] * MUZZLE_FWD - MUZZLE_DROP;
    g_ping_oz = g_pz + f[2] * MUZZLE_FWD;
    g_ping_t0 = now;
    g_ping_i  = 0;
    g_ping_busy = 1;
    g_wcount = 0;                       /* the previous wave has passed */

    audio_ping();
    mon_lit_by(f, ping_reach(), now);
}

static void ping_work(void)
{
    int b;
    int budget = PING_CHUNK;
    int wall_start = g_count, air_start = g_wcount;
    int added, air_added;
    float cosmax = (float)cos(CONE_HALF_ANGLE);

    if (!g_ping_busy) return;

    while (g_ping_i < PING_RAYS && budget-- > 0) {
        int i = g_ping_i++;
        /* The golden angle covers a cap evenly, but it covers it as a lattice,
         * and a lattice printed onto a wall shows up as concentric rings - a
         * machine's signature, not a cave's. Jittering each ray by about half
         * the spacing keeps the coverage and destroys the pattern. */
        float j1 = hash1((float)i * 1.37f + g_seed) - 0.5f;
        float j2 = hash1((float)i * 2.91f + g_seed * 3.0f) - 0.5f;
        float k  = ((float)i + 0.5f + j1) / (float)PING_RAYS;
        float ct = 1.0f - k * (1.0f - cosmax);            /* uniform on the cap */
        float st = (float)sqrt(1.0f - ct * ct);
        float ph = 2.39996323f * (float)i + j2 * 1.7f;
        float lx = st * (float)cos(ph);
        float ly = st * (float)sin(ph);

        /* it leaves from the ground you just stepped on */
        float ox = g_ping_ox, oy = g_ping_oy, oz = g_ping_oz;
        /* A bullet that goes straight up tells you about the ceiling, which is
         * not what you are trying to find out. Squashing the vertical spread
         * keeps most of them in the plane you actually walk through. */
        float vy = ly * VERT_SPREAD;
        float dx = g_pr[0] * lx + g_pu[0] * vy + g_pf[0] * ct;
        float dy = g_pr[1] * lx + g_pu[1] * vy + g_pf[1] * ct;
        float dz = g_pr[2] * lx + g_pu[2] * vy + g_pf[2] * ct;
        float dl0 = (float)sqrt(dx * dx + dy * dy + dz * dz);
        float travelled = 0.0f;
        float gain = 1.0f;

        if (dl0 > 1e-6f) { dx /= dl0; dy /= dl0; dz /= dl0; }

        /* Each ray keeps going after it lands. The distance it has covered so
         * far decides when the front gets there, so a wave visibly rounds a
         * corner instead of stopping dead at the first wall. */
        for (b = 0; b < PING_BOUNCES; b++) {
            float t, n[3], dot;

            if (g_count >= MAX_POINTS) break;
            if (!cave_ray(ox, oy, oz, dx, dy, dz, MAX_TRAVEL - travelled, &t)) break;

            /* Trace the segment it just crossed.
             *
             * Only a hundred-odd rays get drawn in flight, and that sparseness
             * is the point. Six thousand at once average into an expanding
             * wash where no single ricochet is visible; a hundred read as
             * separate tracers you can watch travel, strike and kick off.
             * The dense part of the ping is the map it leaves on the walls,
             * not the part you watch move. */
            {
                float march;
                for (march = WAVE_STEP; march < t; march += WAVE_STEP) {
                    if (g_wcount >= WAVE_POINTS) break;
                    g_wpts[g_wcount].x = ox + dx * march;
                    g_wpts[g_wcount].y = oy + dy * march;
                    g_wpts[g_wcount].z = oz + dz * march;
                    g_wpts[g_wcount].reveal = g_ping_t0 + (travelled + march) / WAVE_SPEED;
                    g_wpts[g_wcount].gain = gain * 1.00f;
                    g_wcount++;
                }
            }

            ox += dx * t; oy += dy * t; oz += dz * t;
            travelled += t;

            /* One point per impact left the walls looking like floating dust:
             * isolated specks never resolve into a surface no matter how many
             * you accumulate. A small splash across the rock at each hit costs
             * no extra ray marching and is what turns marks into geometry. */
            cave_normal(ox, oy, oz, n);
            {
                float t1[3], t2[3], inv;
                int m;
                /* any two directions across the face */
                if (n[1] * n[1] < 0.9f) { t1[0] = -n[2]; t1[1] = 0.0f; t1[2] = n[0]; }
                else                    { t1[0] = 1.0f;  t1[1] = 0.0f; t1[2] = 0.0f; }
                inv = 1.0f / (float)sqrt(t1[0]*t1[0] + t1[1]*t1[1] + t1[2]*t1[2]);
                t1[0] *= inv; t1[1] *= inv; t1[2] *= inv;
                t2[0] = n[1]*t1[2] - n[2]*t1[1];
                t2[1] = n[2]*t1[0] - n[0]*t1[2];
                t2[2] = n[0]*t1[1] - n[1]*t1[0];

                for (m = 0; m < MARK_SPLASH && g_count < MAX_POINTS; m++) {
                    float a = (hash1((float)i * 3.7f + (float)b * 11.0f + (float)m * 2.3f) - 0.5f) * 2.0f * SPLASH_R;
                    float c = (hash1((float)i * 5.1f + (float)b * 7.0f  + (float)m * 3.9f) - 0.5f) * 2.0f * SPLASH_R;
                    if (m == 0) { a = 0.0f; c = 0.0f; }      /* one dead on the hit */
                    g_pts[g_count].x = ox + t1[0]*a + t2[0]*c;
                    g_pts[g_count].y = oy + t1[1]*a + t2[1]*c;
                    g_pts[g_count].z = oz + t1[2]*a + t2[2]*c;
                    g_pts[g_count].reveal = g_ping_t0 + travelled / WAVE_SPEED;
                    /* The mark keeps more of itself than the bullet does: a
                     * tired ricochet still proves a wall is there, and the
                     * cave only takes shape if late hits stay readable. */
                    g_pts[g_count].gain = 0.42f + 0.58f * gain;
                    g_count++;
                    added++;
                }
            }

            /* the air wears it down as it goes */
            gain *= (float)exp(-GAIN_PER_METRE * t);
            if (gain < GAIN_FLOOR || travelled >= MAX_TRAVEL * 0.98f) break;

            dot = dx * n[0] + dy * n[1] + dz * n[2];   /* negative going in */

            /* A tunnel is long and the beam points down it, so most rays meet
             * the wall at eighty-odd degrees. Reflecting those makes the wave
             * skim the rock, hit again a hand's width later, and crawl along
             * the surface - which is not what a ricochet looks like.
             *
             * So a graze is absorbed and only a squarer hit comes back off,
             * the way a bullet does: down a corridor it just travels, into a
             * wall it kicks. */
            if (-dot < GRAZE_MIN) break;
            if (b > 0 && t < MIN_SEGMENT) break;

            dx -= 2.0f * dot * n[0];
            dy -= 2.0f * dot * n[1];
            dz -= 2.0f * dot * n[2];
            {   /* bleed off the vertical so ricochets keep running the tunnel */
                float dl;
                dy *= VERT_DAMP;
                dl = (float)sqrt(dx * dx + dy * dy + dz * dz);
                if (dl > 1e-6f) { dx /= dl; dy /= dl; dz /= dl; }
            }
            /* step well clear, or the eroded wall catches it again at once */
            ox += n[0] * 0.14f; oy += n[1] * 0.14f; oz += n[2] * 0.14f;
            gain *= GAIN_BOUNCE;              /* each kick costs it energy */
        }
    }


    added     = g_count  - wall_start;
    air_added = g_wcount - air_start;

    if (added > 0) {
        glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
        glBufferSubData(GL_ARRAY_BUFFER,
                        (GLintptr)(wall_start * (int)sizeof(Point)),
                        (GLsizeiptr)(added * (int)sizeof(Point)),
                        g_pts + wall_start);
    }
    if (air_added > 0) {
        glBindBuffer(GL_ARRAY_BUFFER, g_wvbo);
        glBufferSubData(GL_ARRAY_BUFFER,
                        (GLintptr)(air_start * (int)sizeof(Point)),
                        (GLsizeiptr)(air_added * (int)sizeof(Point)),
                        g_wpts + air_start);
    }

    if (g_ping_i >= PING_RAYS) g_ping_busy = 0;
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
    float cx, cy;
    /* The tunnel does not run through the origin - rerolling the seed moves
     * it. Spawning at 0,0,0 therefore buried the player inside rock, where
     * every ping ray hit a wall 6 cm away and the screen filled with a ball
     * of yellow returns. Start on the axis instead. */
    tunnel_centre(0.0f, &cx, &cy);
    g_px = cx; g_py = cy; g_pz = 0.0f;
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
    g_has_moved = 0;
    respawn(now);
}

/* --- title ---------------------------------------------------------------
 * Not an overlay: the words are scattered into the cave as points and lit by
 * the same wavefront as the walls. Two seconds of it teaches the whole game -
 * a click throws light, light reveals, revealed things linger. */

static float  g_title_pulse;
static float *g_text_xy;              /* heap, so it costs no file bytes */

#define TEXT_MAX_PTS 30000

static void title_line(const char *str, int px, float scale, float yoff,
                       float delay, float now)
{
    int n, i;

    if (!g_text_xy) return;
    /* A 150 px word is well over four thousand lit pixels. Capping the scan
     * low truncated it mid-glyph, and because the cut landed differently each
     * time the title was rebuilt it read as the game restarting. */
    n = plat_text_points(str, px, g_text_xy, TEXT_MAX_PTS);
    for (i = 0; i < n && g_count < MAX_POINTS; i++) {
        float x = g_px + g_text_xy[i * 2 + 0] * scale;
        float y = g_py - g_text_xy[i * 2 + 1] * scale + yoff;   /* bitmaps run down */
        float z = g_pz - 5.0f;
        /* No wavefront on the title. It is simply lit, and the whole word
         * swells and settles together - a monitor breathing rather than one
         * being switched on over and over. Brightness comes from uBase. */
        (void)delay;
        g_pts[g_count].x = x;
        g_pts[g_count].y = y;
        g_pts[g_count].z = z;
        g_pts[g_count].reveal = now - 1000.0f;
        g_pts[g_count].gain   = 1.0f;
        g_count++;
    }
}

static void enter_title(float now)
{
    g_state = ST_TITLE;
    g_count = 0;
    title_line("SOUNDING",       150, 2.05f, 0.55f, 0.30f, now);
    title_line("CLICK TO START",  40, 2.05f, -1.15f, 1.35f, now);

    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)(g_count * (int)sizeof(Point)), g_pts);
    g_title_pulse = now + 2.9f;      /* let the wave wash over it again */
}

/* --- the readout --------------------------------------------------------
 * A patient monitor, not a game HUD: it is the same point cloud as the cave,
 * pinned to the screen. Rebuilt only when the text it shows actually changes,
 * so GDI is touched a few times a second at worst. */

#define HUD_MAX 9000

static Point *g_hud;
static int    g_hud_n;
static char   g_hud_cache[96];

/* x,y in normalised device coordinates; scale is height in NDC */
static void hud_line(const char *str, float scale, float cx, float cy, float bright)
{
    int n, i;
    if (!g_text_xy) return;
    n = plat_text_points(str, 96, g_text_xy, TEXT_MAX_PTS);
    for (i = 0; i < n && g_hud_n < HUD_MAX; i++) {
        g_hud[g_hud_n].x = cx + g_text_xy[i * 2 + 0] * scale;
        g_hud[g_hud_n].y = cy - g_text_xy[i * 2 + 1] * scale * 1.78f;
        g_hud[g_hud_n].z = 0.0f;
        g_hud[g_hud_n].reveal = 0.0f;
        g_hud[g_hud_n].gain = bright;
        g_hud_n++;
    }
}

static void hud_build(const char *left, const char *right, const char *hint)
{
    char key[96];
    sprintf(key, "%.28s|%.28s|%.30s", left, right, hint);
    if (strcmp(key, g_hud_cache) == 0) return;      /* nothing moved */
    strncpy(g_hud_cache, key, sizeof g_hud_cache - 1);
    g_hud_cache[sizeof g_hud_cache - 1] = 0;

    g_hud_n = 0;
    if (left[0])  hud_line(left,  0.135f, -0.68f,  0.86f, 0.78f);
    if (right[0]) hud_line(right, 0.135f,  0.70f,  0.86f, 0.78f);
    if (hint[0])  hud_line(hint,  0.130f,  0.00f, -0.74f, 0.66f);

    if (g_hud_n > 0) {
        glBindBuffer(GL_ARRAY_BUFFER, g_hvbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        (GLsizeiptr)(g_hud_n * (int)sizeof(Point)), g_hud);
    }
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
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Point),
                          (void *)(4 * sizeof(float)));
}

void game_init(unsigned seed)
{
    g_pts = (Point *)malloc((size_t)MAX_POINTS * sizeof(Point));
    g_text_xy = (float *)malloc((size_t)TEXT_MAX_PTS * 2 * sizeof(float));
    g_wpts    = (Point *)malloc((size_t)WAVE_POINTS * sizeof(Point));
    g_hud     = (Point *)malloc((size_t)HUD_MAX * sizeof(Point));
    g_mpts    = (Point *)malloc((size_t)MON_POINTS * sizeof(Point));
    g_flash = 0.0f;

    g_prog    = gfx_build_program(POINT_VS, POINT_FS);
    u_vp      = glGetUniformLocation(g_prog, "uVP");
    u_cam     = glGetUniformLocation(g_prog, "uCam");
    u_time    = glGetUniformLocation(g_prog, "uTime");
    u_monster = glGetUniformLocation(g_prog, "uMonster");
    u_persist = glGetUniformLocation(g_prog, "uPersist");
    u_flat    = glGetUniformLocation(g_prog, "uFlat");
    u_base    = glGetUniformLocation(g_prog, "uBase");

    glGenVertexArrays(1, &g_vao);
    glGenBuffers(1, &g_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)MAX_POINTS * (GLsizeiptr)sizeof(Point),
                 0, GL_DYNAMIC_DRAW);
    setup_attribs(g_vao, g_vbo);

    glGenVertexArrays(1, &g_wvao);
    glGenBuffers(1, &g_wvbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_wvbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)WAVE_POINTS * (GLsizeiptr)sizeof(Point),
                 0, GL_DYNAMIC_DRAW);
    setup_attribs(g_wvao, g_wvbo);

    glGenVertexArrays(1, &g_hvao);
    glGenBuffers(1, &g_hvbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_hvbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)HUD_MAX * (GLsizeiptr)sizeof(Point),
                 0, GL_DYNAMIC_DRAW);
    setup_attribs(g_hvao, g_hvbo);

    glGenVertexArrays(1, &g_mvao);
    glGenBuffers(1, &g_mvbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_mvbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)MON_POINTS * (GLsizeiptr)sizeof(Point),
                 0, GL_DYNAMIC_DRAW);
    setup_attribs(g_mvao, g_mvbo);

    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);          /* light adds up, like real returns */
    glDisable(GL_DEPTH_TEST);             /* additive, so order does not matter */

    new_attempt(seed, 0.0f);
    enter_title(0.0f);
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

    if (g_state == ST_TITLE) {
        if (in->ping) {
            /* the click that starts the game is also the first sounding, so
             * the cave answers immediately instead of staying black */
            g_state = ST_PLAY;
            new_attempt((unsigned)(now * 100000.0f) ^ rnd(), now);
            basis(g_yaw, g_pitch, f, r, u);
            ping_begin(now, f, r, u);
            g_ping_ready = now + PING_COOLDOWN;
            return;
        }
        glClearColor(0.008f, 0.012f, 0.020f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        if (g_count > 0) {
            mat4_persp(proj, 1.30f,
                       (float)width / (height > 0 ? (float)height : 1.0f),
                       0.05f, 60.0f);
            mat4_view(view, g_px, g_py, g_pz, g_yaw, g_pitch);
            mat4_mul(vp, proj, view);
            glUseProgram(g_prog);
            glUniformMatrix4fv(u_vp, 1, GL_FALSE, vp);
            glUniform3f(u_cam, g_px, g_py, g_pz);
            glUniform1f(u_time, now);
            glUniform1f(u_flat, 0.0f);
            {   /* a slow swell: lit throughout, brightest once every three
                 * seconds, then back to where it was */
                float ph  = (float)fmod(now * 0.345, 1.0);
                float sw  = (float)exp(-pow((ph - 0.5) / 0.20, 2.0));
                glUniform1f(u_base, 0.30f + 0.62f * sw);
            }
            glUniform1f(u_monster, 0.0f);
            glUniform1f(u_persist, 1.0f);
            glBindVertexArray(g_vao);
            glDrawArrays(GL_POINTS, 0, g_count);
        }
        return;
    }

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
        g_has_moved = 1;
    }

    if (-g_pz > g_best_depth) g_best_depth = -g_pz;

    /* ping */
    if (in->ping && now >= g_ping_ready) {
        ping_begin(now, f, r, u);
        g_ping_ready = now + PING_COOLDOWN;
    }

    /* the cave gets busier as you descend */
    ping_work();   /* trace this frame's slice of the wave */

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
                /* the last life: throw the cave away and offer a new one */
                new_attempt((unsigned)(now * 1000.0f) ^ rnd(), now);
                enter_title(now);
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
    glUniform1f(u_flat, 0.0f);
    glUniform1f(u_base, 0.0f);

    /* the map: surfaces, and they stay */
    if (g_count > 0) {
        glUniform1f(u_monster, 0.0f);
        glUniform1f(u_persist, 1.0f);
        glBindVertexArray(g_vao);
        glDrawArrays(GL_POINTS, 0, g_count);
    }

    /* the wave itself, crossing open air. Visible only while the front is on
     * it, so it sweeps through and leaves the map exactly as it found it. */
    if (g_wcount > 0) {
        glUniform1f(u_persist, 0.0f);
        glBindVertexArray(g_wvao);
        glDrawArrays(GL_POINTS, 0, g_wcount);
    }

    glUniform1f(u_monster, 1.0f);
    glUniform1f(u_persist, 0.0f);
    glBindVertexArray(g_mvao);
    for (i = 0; i < g_mon_count; i++) {
        if (!mon_visible(&g_mon[i])) continue;
        mon_emit_points(&g_mon[i], now);
        glDrawArrays(GL_POINTS, 0, MON_POINTS);
    }

    {   /* the readout */
        char left[40], right[40];
        const char *hint = g_has_moved ? "" : "WASD TO MOVE";
        sprintf(left, "DEPTH %5.1f M", -g_pz < 0.0f ? 0.0f : -g_pz);
        sprintf(right, "LIFE %s", g_lives >= 3 ? "* * *"
                                : (g_lives == 2 ? "* *" : (g_lives == 1 ? "*" : "-")));
        hud_build(left, right, hint);

        if (g_hud_n > 0) {
            glUniform1f(u_monster, 0.0f);
            glUniform1f(u_persist, 1.0f);
            glUniform1f(u_flat, 1.0f);
            glBindVertexArray(g_hvao);
            glDrawArrays(GL_POINTS, 0, g_hud_n);
            glUniform1f(u_flat, 0.0f);
        }
    }
}

/* -shot only: drop a spider right in front and wake it, so the thing can be
 * looked at without waiting for one to come along. */
void game_debug_spider(float now, int type)
{
    Monster *m = &g_mon[0];
    float pos[3], n[3];
    g_mon_count = 1;
    m->seed = 4.0f + (float)type;
    wall_spot(g_pz - 6.0f, 0.10f, pos);
    m->x = pos[0]; m->y = pos[1]; m->z = pos[2];
    cave_normal(m->x, m->y, m->z, n);
    m->nx = n[0]; m->ny = n[1]; m->nz = n[2];
    m->dx = 0.0f; m->dy = 0.0f; m->dz = 1.0f;
    m->travel = 0.0f; m->gait = 1.1f;
    mon_make(m, type, 0.3f);
    m->state = MON_CHARGING;
    m->timer = 30.0f;
    (void)now;
}

int   game_point_count(void) { return g_count; }
float game_depth(void)       { return -g_pz; }
int   game_lives(void)       { return g_lives; }
int   game_monsters(void)    { return g_mon_count; }
