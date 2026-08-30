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
/* The wave takes nearly five seconds to travel, so tracing it over a sixth of
 * a second is invisible - and 400 rays a frame was costing 56 ms when the
 * frame budget is 16.7. */
#define PING_CHUNK       100   /* rays traced per frame, so nothing stalls */
#define RAY_STEPS         56
#define WAVE_SPEED     11.0f   /* metres per second the wavefront travels */
/* Sound moves four times faster in water than in air - 1480 against 343 - and
 * putting that in the constant is the cheapest way to make a ping feel like it
 * is travelling through something else. */
#define WAVE_SPEED_WET 26.0f
#define SWIM_DRAG      0.945f  /* what a stroke leaves behind */
#define SWIM_LIFT      0.42f   /* and how it drifts up if you do nothing */
#define MOTES          900     /* things suspended in it, lit only in passing */
#define MOVE_SPEED      4.3f
#define MOUSE_SENS      0.0022f
#define PING_COOLDOWN   0.45f

#ifndef START_LIVES
#define START_LIVES        3
#endif
/* Four thresholds and an end. Difficulty is a curve across the whole descent
 * rather than one that flattens two thirds of the way down, and each gate is
 * a chamber wide enough that the ping goes out and does not come back - which
 * is the only announcement the game makes. */
#define DEPTH_FULL     480.0f  /* metres at which the cave is at its worst */
#define GATE_1         120.0f
#define GATE_2         240.0f
#define GATE_3         360.0f
#define GATE_END       480.0f
/* Past the last gate the corridor runs on to a door. Opening it is the
 * ending; the eyelid sequence starts from the handle, not from a depth. */
#define WAKE_Z        (GATE_END + 17.0f)
#define GATE_R           9.5f  /* how wide a threshold chamber opens */
#define GATE_W           5.0f  /* and how long it runs
 */

#define MON_POINTS      2800   /* limbs need volume, not a dotted line */
#define LEG_TUBE           6   /* points around the curve at each sample */
#define MAX_MON            4
#define MON_KILL_DIST   1.05f
#define WALL_HUG       0.34f   /* how far off the rock it rides */
#define WEAVE_RATE      1.8f   /* how fast it swings side to side */
#define WEAVE_AMT      0.52f   /* and how far - too much and it circles instead of closing */
#define STRIDE         0.44f   /* how far a foot drifts before it is picked up */
#define SWING_TIME     0.11f   /* and how long the step through the air takes */
#define DASH_ON        0.22f   /* it runs in bursts, not at a constant speed */
#define DASH_OFF       0.15f
#define MON_RANGE      24.0f   /* how far it will chase before it comes apart */
#define BURST_TIME     0.55f   /* and how long it takes to scatter */

/* --- point cloud -------------------------------------------------------- */

typedef struct { float x, y, z, reveal, gain; } Point;   /* 20 bytes */

static Point *g_pts;
static int    g_count;
static Point *g_wpts;          /* the wave in flight, a ring that nobody reads back */
static int    g_wcount;
static GLuint g_vao, g_vbo, g_wvao, g_wvbo, g_mvao, g_mvbo, g_hvao, g_hvbo,
              g_rvao, g_rvbo, g_prog, g_wake_prog, g_wvao_full;
static GLint  u_vp, u_cam, u_time, u_monster, u_persist, u_flat, u_base, u_ink;
static GLint  w_res, w_time, w_open, w_bright, w_sharp;
static GLuint g_cave_prog;
static GLint  c_res, c_cam, c_fwd, c_right, c_up, c_seed, c_wander,
              c_rough, c_time, c_light, c_wet, c_room, c_bra, c_brb;
static GLint  u_fade, u_grey;
static Point *g_mpts;          /* heap: 48 KB of it has no business in the exe */

/* --- state -------------------------------------------------------------- */

enum { ST_TITLE, ST_PLAY, ST_SURFACE, ST_WAKE, ST_FLATLINE };
static int g_state;

extern int plat_text_points(const char *str, int px, float *out_xy, int max);

/* --- player ------------------------------------------------------------- */

static float g_px, g_py, g_pz;
static float g_yaw, g_pitch;
static float g_ping_ready;
static float g_flash;
static int   g_lives;
static float g_best_depth;
static float g_start_depth;
static int   g_stage;
static float g_stage_flash;
static float g_wake;
static float g_surf;              /* 0..1 through a glimpse of the room */
static float g_flat;              /* 0..1 through the flatline */
static float g_stagef;            /* g_stage, arriving over ~2 s */
static float g_dive;              /* the plunge at gate one */
static int   g_pings;
static float g_clarity;           /* how much of it resolves this time */
static int   g_has_moved;
static float g_travelled;
static int   g_wet;              /* stage two: the passage is flooded */
static float g_vx, g_vy, g_vz;   /* swimming carries momentum */
static Point *g_motes;
static GLuint g_movao, g_movbo;
#ifdef DEMO_ENDING
static float g_demo;      /* seconds of cave before the ending takes over */
#endif

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

static float smoothstep01(float a, float b, float x)
{
    float t = (x - a) / (b - a);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

/* Points give way to lit rock across stages three and four. Nothing is
 * switched: at 58 m it is all points, by 105 m it is nearly all surface, and
 * in between both are on screen at once - which is the whole idea. The light
 * ahead comes up on the same curve, so the world does not just resolve, it
 * gets lit. */
static float surface_mix(float z)
{
    float d = -z;
    float t = (d - GATE_2) / (GATE_END - 15.0f - GATE_2);
    float ts;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    t = t * t * (3.0f - 2.0f * t);
    /* a crossed gate brings some of it immediately */
    ts = smoothstep01(1.8f, 3.3f, g_stagef) * 0.9f;
    return t > ts ? t : ts;
}

static float depth_k(float z)
{
    float d = -z / DEPTH_FULL;
    if (d < 0.0f) return 0.0f;
    if (d > 1.0f) return 1.0f;
    return d;
}

/* The axis wanders on two scales at once: a long swing that decides roughly
 * where the passage is heading, and a short one that keeps it from ever being
 * straight for more than a dozen metres.
 *
 * The short term is what was missing. Wavelengths used to be forty-eight and
 * a hundred and thirty metres, so across the twenty-six a ping reaches the
 * tunnel was effectively a line. These bend every sixteen.
 *
 * Amplitudes are held down to keep the slope under about one: any steeper and
 * the cross-section, which is measured across z rather than across the axis,
 * pinches shut and the passage stops being passable. */
static void tunnel_centre(float z, float *cx, float *cy)
{
    float w = g_wander;
    *cx = (float)sin(z * 0.100 + g_seed)        * 3.8f * w      /* 63 m swing */
        + (float)sin(z * 0.400 + g_seed * 1.7)  * 1.4f;         /* 16 m kinks */
    *cy = (float)cos(z * 0.070 + g_seed * 2.3)  * 2.4f * w      /* 90 m swing */
        + (float)sin(z * 0.310 + g_seed * 3.1)  * 1.1f;         /* 20 m kinks */
}

/* --- side passages -------------------------------------------------------
 * A branch is a capsule leaving the main axis at an angle. The cave is the
 * union of the two, and because air is positive that union is a max. */

#define BRANCH_SPACING 34.0f
#define BRANCH_LEN     17.0f
#define BRANCH_RAD      1.75f

static float seg_dist(float px, float py, float pz,
                      float ax, float ay, float az,
                      float bx, float by, float bz)
{
    float dx = bx - ax, dy = by - ay, dz = bz - az;
    float wx = px - ax, wy = py - ay, wz = pz - az;
    float dd = dx * dx + dy * dy + dz * dz;
    float t  = dd > 1e-6f ? (wx * dx + wy * dy + wz * dz) / dd : 0.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    wx -= dx * t; wy -= dy * t; wz -= dz * t;
    return (float)sqrt(wx * wx + wy * wy + wz * wz);
}

#define BRANCHES 16

static float g_br[BRANCHES][6];   /* ax ay az bx by bz, worked out once */

/* branch_air used to call tunnel_centre, which is three sines and three
 * cosines - and the field is evaluated over a million times per ping. The
 * segments are fixed for a given cave, so they are built once instead. */
static void build_branches(void)
{
    int i;
    for (i = 0; i < BRANCHES; i++) {
        float idx = (float)i;
        float z0  = idx * -BRANCH_SPACING;
        float a   = hash1(idx * 7.31f + g_seed) * 6.2831853f;
        float ln  = 0.35f + hash1(idx * 3.17f + g_seed) * 0.55f;
        float cx, cy;
        tunnel_centre(z0, &cx, &cy);
        g_br[i][0] = cx;
        g_br[i][1] = cy;
        g_br[i][2] = z0;
        g_br[i][3] = cx + (float)cos(a) * BRANCH_LEN;
        g_br[i][4] = cy + (float)sin(a) * BRANCH_LEN * 0.45f;
        g_br[i][5] = z0 - BRANCH_LEN * ln;
    }
}

static float branch_air(float x, float y, float z, int i)
{
    if (i < 0 || i >= BRANCHES) return -1000.0f;
    return BRANCH_RAD - seg_dist(x, y, z,
                                 g_br[i][0], g_br[i][1], g_br[i][2],
                                 g_br[i][3], g_br[i][4], g_br[i][5]);
}

/* How much the passage opens out at this depth. Zero everywhere except at the
 * thresholds, where it swells into a room. */
static float gate_bulge(float z)
{
    static const float GATES[4] = { GATE_1, GATE_2, GATE_3, GATE_END };
    float d = -z, best = 1e9f, dz;
    int i, k = 0;
    /* four exps per field lookup was three too many: only one gate can be
     * within reach of any given depth */
    for (i = 0; i < 4; i++) {
        float t = d - GATES[i]; if (t < 0.0f) t = -t;
        if (t < best) { best = t; k = i; }
    }
    if (best > GATE_W * 4.0f) return 0.0f;
    dz = d - GATES[k];
    return GATE_R * (float)exp(-(dz * dz) / (2.0f * GATE_W * GATE_W));
}

static float cave_sdf(float x, float y, float z)
{
    float cx, cy, dx, dy, r, rad;
    float k = depth_k(z);
    tunnel_centre(z, &cx, &cy);
    dx = x - cx;
    dy = (y - cy) * 1.25f;                       /* flatter than it is wide */
    r  = (float)sqrt(dx * dx + dy * dy);
    rad = (2.35f - 0.95f * k)
        + 1.15f * g_rough * fbm2((float)atan2(dy, dx) * 1.6f, z * 0.42f + g_seed)
        + gate_bulge(z);
    {   /* whichever is more open here, the main passage or a branch */
        float main_air = rad - r;
        {   /* the corridor ends at the door */
            float wall = z + WAKE_Z;
            if (wall < main_air) main_air = wall;
        }
        int   i0 = (int)(-z / BRANCH_SPACING);
        float b0 = branch_air(x, y, z, i0);
        float b1 = branch_air(x, y, z, i0 + 1);
        if (b0 > main_air) main_air = b0;
        if (b1 > main_air) main_air = b1;
        return main_air;
    }
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
    float timer, seed, travel;
    float foot[8][3], fa[8][3], fb[8][3], ft[8];  /* planted feet, and steps */
    float dash;  int moving, group;
    float speed, warn, hear;   /* the archetype, as three numbers */
    float scale, legspan;      /* and how big a spider it is */
} Monster;

static Monster g_mon[MAX_MON];

static void build_room(void);
static void upload_room(float clarity);

static void mon_feet(Monster *m, float dt, const float *f,
                     const float *sd, const float *n);
static int     g_mon_count;

static void mon_make(Monster *m, int type, float k)
{
    m->type = type;
    switch (type) {
    case T_RUSHER:      /* the big one: little warning, fast, poor hearing */
        m->speed   = 5.4f + 1.6f * k;
        m->warn    = 0.42f - 0.12f * k;
        m->hear    = 19.0f;
        m->scale   = 1.15f;
        m->legspan = 1.00f;
        break;
    case T_LISTENER:    /* slow and generous, but hears you from far away */
        m->speed   = 3.5f + 0.8f * k;
        m->warn    = 0.72f;
        m->hear    = 32.0f + 8.0f * k;
        m->scale   = 0.80f;
        m->legspan = 2.10f;      /* a harvestman: mostly leg */
        break;
    default:            /* T_STALKER - the small one you learn the game on */
        m->speed   = 4.4f + 1.5f * k;
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
    m->dash   = 0.0f;
    m->moving = 1;
    m->group  = 0;
    {   /* start with every foot already on the rock under it */
        int q;
        for (q = 0; q < 8; q++) {
            m->foot[q][0] = m->x - n[0] * WALL_HUG;
            m->foot[q][1] = m->y - n[1] * WALL_HUG;
            m->foot[q][2] = m->z - n[2] * WALL_HUG;
            m->ft[q] = 1.0f;
        }
    }

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
            m->timer  = 3.2f;
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

        /* Nothing with eight legs moves at a constant speed. It goes in
         * bursts with a beat of stillness between them, which is most of
         * what makes an insect look like an insect. */
        m->dash -= dt;
        if (m->dash <= 0.0f) {
            m->moving = !m->moving;
            m->dash = m->moving
                    ? DASH_ON  + hash1(now * 13.0f + m->seed) * 0.20f
                    : DASH_OFF + hash1(now * 17.0f + m->seed) * 0.16f;
        }

        step = m->moving ? m->speed * 1.75f * dt : 0.0f;
        m->x += m->dx * step;
        m->y += m->dy * step;
        m->z += m->dz * step;
        m->travel += step;

        /* hold on: keep a fixed distance off the rock as the wall curves */
        cave_normal(m->x, m->y, m->z, n);
        sd = cave_sdf(m->x, m->y, m->z);
        m->x += n[0] * (WALL_HUG - sd);
        m->y += n[1] * (WALL_HUG - sd);
        m->z += n[2] * (WALL_HUG - sd);
        m->nx = n[0]; m->ny = n[1]; m->nz = n[2];

        {   /* let the legs catch up to wherever the body ended up */
            float bf[3], bs[3], dot2, l2;
            bf[0] = m->dx; bf[1] = m->dy; bf[2] = m->dz;
            dot2 = bf[0]*n[0] + bf[1]*n[1] + bf[2]*n[2];
            bf[0] -= dot2*n[0]; bf[1] -= dot2*n[1]; bf[2] -= dot2*n[2];
            l2 = (float)sqrt(bf[0]*bf[0] + bf[1]*bf[1] + bf[2]*bf[2]);
            if (l2 > 1e-4f) { bf[0]/=l2; bf[1]/=l2; bf[2]/=l2; }
            bs[0] = n[1]*bf[2] - n[2]*bf[1];
            bs[1] = n[2]*bf[0] - n[0]*bf[2];
            bs[2] = n[0]*bf[1] - n[1]*bf[0];
            mon_feet(m, dt, bf, bs, n);
        }

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

/* Where a foot wants to be: out to the side of the body and down against the
 * rock. The body already rides WALL_HUG off the surface, so dropping that far
 * along the inward normal lands on it without another field lookup. */
static void foot_target(const Monster *m, int i,
                        const float *f, const float *sd, const float *n, float *out)
{
    float side  = (i & 1) ? 1.0f : -1.0f;
    int   pair  = i >> 1;
    float along = (-0.34f + 0.24f * (float)pair) * m->scale;
    float reach = (0.60f + 0.16f * (float)pair) * m->scale * m->legspan;
    out[0] = m->x + f[0]*along + sd[0]*side*reach - n[0]*WALL_HUG;
    out[1] = m->y + f[1]*along + sd[1]*side*reach - n[1]*WALL_HUG;
    out[2] = m->z + f[2]*along + sd[2]*side*reach - n[2]*WALL_HUG;
}

/* The gait. Feet are held in world space and simply stay where they were put;
 * a leg is only lifted once the body has dragged it further than a stride
 * from where it should be, and then only if its half of the animal is the
 * half currently allowed to move. Four down, four stepping, alternating -
 * which is what an eight-legged thing actually does, and the reason it reads
 * as walking rather than sliding. */
static void mon_feet(Monster *m, float dt, const float *f, const float *sd, const float *n)
{
    int i, swinging = 0;
    float want[3];

    for (i = 0; i < 8; i++) if (m->ft[i] < 1.0f) swinging++;
    if (!swinging) m->group ^= 1;          /* the other four may go now */

    for (i = 0; i < 8; i++) {
        int mine = ((i == 0 || i == 3 || i == 4 || i == 7) ? 0 : 1) == m->group;
        foot_target(m, i, f, sd, n, want);

        if (m->ft[i] < 1.0f) {             /* mid-step */
            float u, e, lift;
            m->ft[i] += dt / SWING_TIME;
            if (m->ft[i] > 1.0f) m->ft[i] = 1.0f;
            u = m->ft[i];
            e = u * u * (3.0f - 2.0f * u);
            lift = (float)sin(3.1415927f * u) * 0.30f * m->scale;
            m->foot[i][0] = m->fa[i][0] + (m->fb[i][0] - m->fa[i][0]) * e + n[0] * lift;
            m->foot[i][1] = m->fa[i][1] + (m->fb[i][1] - m->fa[i][1]) * e + n[1] * lift;
            m->foot[i][2] = m->fa[i][2] + (m->fb[i][2] - m->fa[i][2]) * e + n[2] * lift;
        } else {                           /* planted: pick it up if it lags */
            float dx = want[0] - m->foot[i][0];
            float dy = want[1] - m->foot[i][1];
            float dz = want[2] - m->foot[i][2];
            float st = STRIDE * m->scale;
            float lag = dx*dx + dy*dy + dz*dz;
            /* Waiting for your half of the gait is how walking works - but a
             * foot left two strides behind is how legs ended up pinned to the
             * rock, stretched flat behind a body that had moved on. Past that
             * point it steps regardless. */
            if (lag > st * st * (mine ? 1.0f : 3.2f)) {
                int k;
                for (k = 0; k < 3; k++) m->fa[i][k] = m->foot[i][k];
                /* overshoot slightly, the way a stepping leg reaches ahead */
                m->fb[i][0] = want[0] + dx * 0.45f;
                m->fb[i][1] = want[1] + dy * 0.45f;
                m->fb[i][2] = want[2] + dz * 0.45f;
                m->ft[i] = 0.0f;
            }
        }
    }
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
    float sc = m->scale;
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

    /* Eight legs drawn to wherever their feet are actually standing. The
     * knee rides high above the midpoint, so a leg whose foot has been left
     * behind visibly stretches and then snaps forward when it steps. */
    for (i = 0; i < 8; i++) {
        float wx = m->foot[i][0] - m->x;
        float wy = m->foot[i][1] - m->y;
        float wz = m->foot[i][2] - m->z;
        /* into the body frame */
        float ex = wx*f[0] + wy*f[1] + wz*f[2];
        float ey = wx*sd[0] + wy*sd[1] + wz*sd[2];
        float ez = wx*n[0] + wy*n[1] + wz*n[2];
        float kx = ex * 0.42f, ky = ey * 0.46f, kz = 0.88f * sc;

        for (k = 0; k < 46; k++) {
            float u   = (float)k / 45.0f;
            float iu  = 1.0f - u;
            float a   = 2.0f * iu * u * kx + u * u * ex;
            float b2  = 2.0f * iu * u * ky + u * u * ey;
            float c2  = 2.0f * iu * u * kz + u * u * ez;
            float rad = (0.175f * iu * iu + 0.028f) * sc;
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

/* Stage two is under water: the second gate is where it drains again. */
static int depth_is_wet(float z)
{
    float d = -z;
    return (d >= GATE_1 - 1.0f && d < GATE_2 - 1.0f);
}

static float wave_speed(void)
{
    return g_wet ? WAVE_SPEED_WET : WAVE_SPEED;
}

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

        m->wake = now + d / wave_speed();
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

    g_pings++;
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
                    g_wpts[g_wcount].reveal = g_ping_t0 + (travelled + march) / wave_speed();
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
                    g_pts[g_count].reveal = g_ping_t0 + travelled / wave_speed();
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
    tunnel_centre(-g_start_depth, &cx, &cy);
    g_px = cx; g_py = cy; g_pz = -g_start_depth;
    {   /* The tunnel bends, so a fixed heading points into the wall as often
         * as not - and in the dark that is indistinguishable from the game
         * being broken. Face the way the passage actually runs. */
        float ax, ay, fx, fz;
        tunnel_centre(-g_start_depth - 3.0f, &ax, &ay);
        fx = ax - cx;
        fz = -3.0f;
        g_yaw = (float)atan2(fx, -fz);
    }
    g_pitch = 0.0f;
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
    g_wander = 0.80f + rndf() * 0.45f;      /* 0.80 .. 1.25 */
    g_rough  = 0.70f + rndf() * 0.70f;      /* 0.70 .. 1.40 */
    build_branches();
    build_room();
    g_count  = 0;
    g_pings  = 0;
    g_lives  = START_LIVES;
    g_best_depth = 0.0f;
    g_has_moved = 0;
    /* Starting deep means the thresholds above you are already behind you.
     * This lived in game_init, but the click that starts a run calls
     * new_attempt again and reset it - so a stage build opened by walking
     * straight into an interlude it had not earned. */
    g_stage = 0;
    while (g_stage < 4 && g_start_depth >= (g_stage == 0 ? GATE_1 :
                          g_stage == 1 ? GATE_2 : g_stage == 2 ? GATE_3 : GATE_END))
        g_stage++;
    g_stage_flash = 0.0f;
    g_stagef = (float)g_stage;
    g_dive = 0.0f;
    g_wake = 0.0f;
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

/* Two lines centred inside the eyelid, for the one screen where the corners
 * of the display are behind a closed eye. */
static void hud_build_wake(const char *a, const char *b)
{
    char key[96];
    sprintf(key, "W|%.28s|%.28s", a, b);
    if (strcmp(key, g_hud_cache) == 0) return;
    strncpy(g_hud_cache, key, sizeof g_hud_cache - 1);
    g_hud_cache[sizeof g_hud_cache - 1] = 0;

    g_hud_n = 0;
    if (a[0]) hud_line(a, 0.150f, 0.0f,  0.10f, 0.85f);
    if (b[0]) hud_line(b, 0.105f, 0.0f, -0.10f, 0.70f);
    if (g_hud_n > 0) {
        glBindBuffer(GL_ARRAY_BUFFER, g_hvbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        (GLsizeiptr)(g_hud_n * (int)sizeof(Point)), g_hud);
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

/* Water is not empty, and that is most of what separates it from air. A few
 * hundred specks drift in it and light up only as the front goes past, so the
 * space between the walls stops being nothing. */
static void motes_refresh(float now)
{
    int i;
    float sp = wave_speed();
    for (i = 0; i < MOTES; i++) {
        float a = hash1((float)i * 1.7f) * 6.2831853f;
        float r = 0.4f + hash1((float)i * 3.1f) * 2.0f;
        float zz = g_pz + 1.5f - hash1((float)i * 5.3f) * 24.0f;
        float cx, cy, d;
        tunnel_centre(zz, &cx, &cy);
        g_motes[i].x = cx + (float)cos(a + now * 0.11f) * r;
        g_motes[i].y = cy + (float)sin(a * 1.7f) * r * 0.8f
                     + (float)sin(now * 0.23f + (float)i) * 0.25f;
        g_motes[i].z = zz;
        d = (float)sqrt((g_motes[i].x - g_px) * (g_motes[i].x - g_px)
                      + (g_motes[i].y - g_py) * (g_motes[i].y - g_py)
                      + (g_motes[i].z - g_pz) * (g_motes[i].z - g_pz));
        g_motes[i].reveal = g_ping_t0 + d / sp;
        g_motes[i].gain = 0.55f;
    }
    glBindBuffer(GL_ARRAY_BUFFER, g_movbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)(MOTES * (int)sizeof(Point)), g_motes);
}

/* --- the room -----------------------------------------------------------
 * What surfaces between stages. Not a place you stand in - a flat smear of
 * what eyes half-open would take in: strip lights overhead, a window, a rail,
 * someone leaning over. Clarity decides how much of it holds together, and it
 * rises at every threshold, so the reality you keep sinking out of gets
 * harder to mistake for anything else each time.
 *
 * Sketched from primitives, so the whole room is a few hundred bytes. */

#define ROOM_MAX 3000

static Point *g_room;
static int    g_room_n;

static void room_put(float x, float y, float g)
{
    if (g_room_n >= ROOM_MAX) return;
    g_room[g_room_n].x = x;
    g_room[g_room_n].y = y;
    g_room[g_room_n].z = 0.0f;
    g_room[g_room_n].reveal = 0.0f;
    g_room[g_room_n].gain = g;
    g_room_n++;
}

static void room_line(float x0, float y0, float x1, float y1, int n, float g)
{
    int i;
    for (i = 0; i < n; i++) {
        float u = (float)i / (float)(n - 1);
        room_put(x0 + (x1 - x0) * u, y0 + (y1 - y0) * u, g);
    }
}

static void upload_room(float clarity)
{
    int i;
    float blur = (1.0f - clarity) * 0.16f;
    for (i = 0; i < g_room_n; i++) {
        float h = (float)i * 1.37f + clarity * 91.0f;
        g_room[i].reveal = (hash1(h) - 0.5f) * 2.0f * blur;        /* dx, reused */
        g_room[i].z      = (hash1(h + 5.1f) - 0.5f) * 2.0f * blur; /* dy, reused */
    }
    for (i = 0; i < g_room_n; i++) {
        g_room[i].x += g_room[i].reveal;
        g_room[i].y += g_room[i].z;
        g_room[i].z = 0.0f;
        g_room[i].reveal = 0.0f;
    }
    glBindBuffer(GL_ARRAY_BUFFER, g_rvbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)(g_room_n * (int)sizeof(Point)), g_room);
}

static void build_room(void)
{
    int i;
    g_room_n = 0;

    /* two strip lights on the ceiling, the brightest thing in the room */
    for (i = 0; i < 2; i++) {
        float y = 0.74f - (float)i * 0.16f;
        float w = 0.52f - (float)i * 0.13f;
        room_line(-w, y, w, y, 150, 1.00f);
        room_line(-w, y - 0.035f, w, y - 0.035f, 120, 0.72f);
    }

    /* a window off to one side: the only daylight */
    room_line(-0.94f,  0.42f, -0.52f,  0.42f, 60, 0.85f);
    room_line(-0.94f, -0.20f, -0.52f, -0.20f, 60, 0.85f);
    room_line(-0.94f,  0.42f, -0.94f, -0.20f, 70, 0.85f);
    room_line(-0.52f,  0.42f, -0.52f, -0.20f, 70, 0.85f);
    for (i = 0; i < 260; i++) {                /* the light coming through it */
        float u = hash1((float)i * 1.7f), v = hash1((float)i * 3.1f + 5.0f);
        room_put(-0.94f + u * 0.42f, -0.20f + v * 0.62f, 0.34f);
    }

    /* a bed rail across the bottom of the view */
    room_line(-0.80f, -0.62f, 0.80f, -0.62f, 180, 0.66f);
    for (i = 0; i < 9; i++) {
        float x = -0.72f + (float)i * 0.18f;
        room_line(x, -0.62f, x, -0.78f, 22, 0.52f);
    }

    /* a drip stand */
    room_line(0.78f, 0.62f, 0.78f, -0.55f, 90, 0.58f);
    room_line(0.66f, 0.62f, 0.90f, 0.62f, 26, 0.58f);

    /* and someone leaning in over the right of the bed */
    for (i = 0; i < 200; i++) {                /* head */
        float a = hash1((float)i * 2.3f) * 6.2831853f;
        float r = 0.115f * (float)sqrt(hash1((float)i * 4.7f + 2.0f));
        room_put(0.34f + (float)cos(a) * r, 0.20f + (float)sin(a) * r * 1.15f, 0.95f);
    }
    for (i = 0; i < 420; i++) {                /* shoulders and chest */
        float u = hash1((float)i * 1.9f + 7.0f), v = hash1((float)i * 5.3f + 1.0f);
        float w = 0.30f - 0.10f * v;
        room_put(0.34f + (u - 0.5f) * 2.0f * w, 0.03f - v * 0.46f, 0.88f);
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

void game_init(unsigned seed, float start_depth)
{
    g_pts = (Point *)malloc((size_t)MAX_POINTS * sizeof(Point));
    g_text_xy = (float *)malloc((size_t)TEXT_MAX_PTS * 2 * sizeof(float));
    g_wpts    = (Point *)malloc((size_t)WAVE_POINTS * sizeof(Point));
    g_hud     = (Point *)malloc((size_t)HUD_MAX * sizeof(Point));
    g_mpts    = (Point *)malloc((size_t)MON_POINTS * sizeof(Point));
    g_room    = (Point *)malloc((size_t)ROOM_MAX * sizeof(Point));
    g_motes   = (Point *)malloc((size_t)MOTES * sizeof(Point));
    g_flash = 0.0f;

    g_prog    = gfx_build_program(POINT_VS, POINT_FS);
    u_vp      = glGetUniformLocation(g_prog, "uVP");
    u_cam     = glGetUniformLocation(g_prog, "uCam");
    u_time    = glGetUniformLocation(g_prog, "uTime");
    u_monster = glGetUniformLocation(g_prog, "uMonster");
    u_persist = glGetUniformLocation(g_prog, "uPersist");
    u_flat    = glGetUniformLocation(g_prog, "uFlat");
    u_base    = glGetUniformLocation(g_prog, "uBase");
    u_ink     = glGetUniformLocation(g_prog, "uInk");
    u_fade    = glGetUniformLocation(g_prog, "uFade");
    u_grey    = glGetUniformLocation(g_prog, "uGrey");

    g_wake_prog = gfx_build_program(WAKE_VS, WAKE_FS);
    w_res    = glGetUniformLocation(g_wake_prog, "uRes");
    w_time   = glGetUniformLocation(g_wake_prog, "uTime");
    w_open   = glGetUniformLocation(g_wake_prog, "uOpen");
    w_bright = glGetUniformLocation(g_wake_prog, "uBright");
    w_sharp  = glGetUniformLocation(g_wake_prog, "uSharp");
    glGenVertexArrays(1, &g_wvao_full);

    g_cave_prog = gfx_build_program(WAKE_VS, CAVE_FS);   /* same fullscreen tri */
    c_res    = glGetUniformLocation(g_cave_prog, "uRes");
    c_cam    = glGetUniformLocation(g_cave_prog, "uCam");
    c_fwd    = glGetUniformLocation(g_cave_prog, "uFwd");
    c_right  = glGetUniformLocation(g_cave_prog, "uRight");
    c_up     = glGetUniformLocation(g_cave_prog, "uUp");
    c_seed   = glGetUniformLocation(g_cave_prog, "uSeed");
    c_wander = glGetUniformLocation(g_cave_prog, "uWander");
    c_rough  = glGetUniformLocation(g_cave_prog, "uRough");
    c_time   = glGetUniformLocation(g_cave_prog, "uTime");
    c_light  = glGetUniformLocation(g_cave_prog, "uLight");
    c_wet    = glGetUniformLocation(g_cave_prog, "uWet");
    c_room   = glGetUniformLocation(g_cave_prog, "uRoom");
    c_bra    = glGetUniformLocation(g_cave_prog, "uBrA");
    c_brb    = glGetUniformLocation(g_cave_prog, "uBrB");

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

    glGenVertexArrays(1, &g_movao);
    glGenBuffers(1, &g_movbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_movbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)MOTES * (GLsizeiptr)sizeof(Point),
                 0, GL_DYNAMIC_DRAW);
    setup_attribs(g_movao, g_movbo);

    glGenVertexArrays(1, &g_rvao);
    glGenBuffers(1, &g_rvbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_rvbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)ROOM_MAX * (GLsizeiptr)sizeof(Point),
                 0, GL_DYNAMIC_DRAW);
    setup_attribs(g_rvao, g_rvbo);

    glGenVertexArrays(1, &g_mvao);
    glGenBuffers(1, &g_mvbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_mvbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)MON_POINTS * (GLsizeiptr)sizeof(Point),
                 0, GL_DYNAMIC_DRAW);
    setup_attribs(g_mvao, g_mvbo);

    glUseProgram(g_prog);
    glUniform1f(u_fade, 1.0f);

    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);          /* light adds up, like real returns */
    glDisable(GL_DEPTH_TEST);             /* additive, so order does not matter */

    g_start_depth = start_depth;
    new_attempt(seed, 0.0f);
    enter_title(0.0f);
}

/* --- movement ------------------------------------------------------------
 * Axis-separated so walking into a wall slides along it instead of stopping
 * dead. There is no gravity yet: this build swims. */

#define PLAYER_R 0.62f

/* Pushing out of the rock kept you out of it but did nothing to help you get
 * anywhere: hold a direction into a wall and you simply stopped. Take the
 * wall-parallel part of the step instead and you slide along it, which is
 * what walking down a bending tunnel in the dark actually requires. */
static void try_move(float dx, float dy, float dz)
{
    float n[3], d, dot;
    int i;

    d = cave_sdf(g_px + dx, g_py + dy, g_pz + dz);
    if (d < PLAYER_R) {
        cave_normal(g_px, g_py, g_pz, n);
        dot = dx * n[0] + dy * n[1] + dz * n[2];
        if (dot < 0.0f) {                  /* heading into the rock */
            dx -= dot * n[0];
            dy -= dot * n[1];
            dz -= dot * n[2];
        }
    }

    g_px += dx; g_py += dy; g_pz += dz;

    for (i = 0; i < 2; i++) {
        d = cave_sdf(g_px, g_py, g_pz);
        if (d >= PLAYER_R) break;
        cave_normal(g_px, g_py, g_pz, n);
        g_px += n[0] * (PLAYER_R - d);
        g_py += n[1] * (PLAYER_R - d);
        g_pz += n[2] * (PLAYER_R - d);
    }
}

void game_frame(const GameInput *in, float dt, float now, int width, int height)
{
    float f[3], r[3], u[3];
    float mx = 0.0f, my = 0.0f, mz = 0.0f, len;
    float proj[16], view[16], vp[16];
    float limit = 1.5533f;                       /* just under 89 degrees */
    int i, want;

    if (g_state == ST_FLATLINE) {
        /* A monitor losing its patient. The map you made stays on screen and
         * dims - it outlives you - while the trace goes to a line. */
        float e = g_flat;
        char buf[48];
        g_flat += dt / 6.5f;

        glClearColor(0.015f + e * 0.010f, 0.008f, 0.010f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        if (g_count > 0) {
            mat4_persp(proj, 1.30f,
                       (float)width / (height > 0 ? (float)height : 1.0f), 0.05f, 60.0f);
            mat4_view(view, g_px, g_py, g_pz, g_yaw, g_pitch);
            mat4_mul(vp, proj, view);
            glUseProgram(g_prog);
            glUniformMatrix4fv(u_vp, 1, GL_FALSE, vp);
            glUniform3f(u_cam, g_px, g_py, g_pz);
            glUniform1f(u_time, now);
            glUniform1f(u_flat, 0.0f);
            glUniform1f(u_ink, 0.0f);
            glUniform1f(u_monster, 0.0f);
            glUniform1f(u_persist, 1.0f);
            glUniform1f(u_base, 0.0f);
            glUniform1f(u_grey, 1.0f);
            glUniform1f(u_fade, 1.0f - smoothstep01(0.15f, 0.85f, e) * 0.88f);
            glBindVertexArray(g_vao);
            glDrawArrays(GL_POINTS, 0, g_count);
            glUniform1f(u_fade, 1.0f);
        }

        {   /* the trace: a beat or two more, then flat */
            int i, nseg = 220;
            float beat = e < 0.32f ? (float)sin(now * 9.0f) : 0.0f;
            g_hud_n = 0;
            for (i = 0; i < nseg && g_hud_n < HUD_MAX; i++) {
                float x  = -0.92f + 1.84f * (float)i / (float)(nseg - 1);
                float ph = x * 14.0f + now * 2.2f;
                float y  = -0.55f;
                if (e < 0.32f) {
                    float k = (float)fmod(ph, 6.2831853);
                    if (k > 5.1f && k < 5.6f) y += (k - 5.1f) * 1.4f * beat;
                }
                g_hud[g_hud_n].x = x;
                g_hud[g_hud_n].y = y;
                g_hud[g_hud_n].z = 0.0f;
                g_hud[g_hud_n].reveal = 0.0f;
                g_hud[g_hud_n].gain = 0.9f;
                g_hud_n++;
            }
            g_hud_cache[0] = 0;              /* the trace never caches */
            glBindBuffer(GL_ARRAY_BUFFER, g_hvbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0,
                            (GLsizeiptr)(g_hud_n * (int)sizeof(Point)), g_hud);
            glUseProgram(g_prog);
            glUniform1f(u_time, now);
            glUniform1f(u_monster, 1.0f);    /* the line is red */
            glUniform1f(u_persist, 1.0f);
            glUniform1f(u_flat, 1.0f);
            glUniform1f(u_base, 0.9f);
            glUniform1f(u_grey, 0.0f);
            glBindVertexArray(g_hvao);
            glDrawArrays(GL_POINTS, 0, g_hud_n);
        }

        if (e > 0.55f) {
            sprintf(buf, "DEPTH %5.1f M   %d SOUNDINGS", g_best_depth, g_pings);
            hud_build_wake(e >= 1.0f ? "CLICK" : "SIGNAL LOST", buf);
            if (g_hud_n > 0) {
                glUniform1f(u_monster, 0.0f);
                glUniform1f(u_flat, 1.0f);
                glUniform1f(u_base, 0.55f);
                glBindVertexArray(g_hvao);
                glDrawArrays(GL_POINTS, 0, g_hud_n);
            }
        }
        glUniform1f(u_flat, 0.0f);
        glUniform1f(u_base, 0.0f);
        glUniform1f(u_monster, 0.0f);

        if (g_flat >= 1.0f && in->ping) {
            new_attempt((unsigned)(now * 1000.0f) ^ rnd(), now);
            enter_title(now);
        }
        return;
    }

    if (g_state == ST_SURFACE) {
        /* Two and a half seconds of somewhere else. It comes up, holds, and
         * lets go again - and each time a little more of it stays put. */
        static const char *HEARD[3] = {
            "CAN YOU HEAR ME",
            "NO RESPONSE TO PAIN",
            "HE MOVED - DID YOU SEE THAT"
        };
        float e;
        g_surf += dt / 2.6f;
        if (g_surf >= 1.0f) { g_state = ST_PLAY; g_ping_ready = now + 0.3f; return; }

        e = (float)sin(3.1415927f * g_surf);          /* up, hold, back down */
        e = e * e;

        /* Behind closed eyes the room is not a lit scene - it is light
         * getting in. So it is drawn the same way the cave is, as points that
         * add, against a ground that stays nearly black. Ink on pale was the
         * wrong way round and came out invisible. */
        glClearColor(0.02f + e * g_clarity * 0.09f,
                     0.03f + e * g_clarity * 0.09f,
                     0.05f + e * g_clarity * 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(g_prog);
        glUniform1f(u_time, now);
        glUniform1f(u_monster, 0.0f);
        glUniform1f(u_persist, 1.0f);
        glUniform1f(u_flat, 1.0f);
        glUniform1f(u_ink, 0.0f);
        glUniform1f(u_base, e * (0.30f + 0.70f * g_clarity));

        if (g_room_n > 0) {
            glBindVertexArray(g_rvao);
            glDrawArrays(GL_POINTS, 0, g_room_n);
        }
        if (e > 0.5f && g_stage >= 1 && g_stage <= 3) {
            hud_build("", "", HEARD[g_stage - 1]);
            if (g_hud_n > 0) {
                glUniform1f(u_base, e * 0.85f);
                glBindVertexArray(g_hvao);
                glDrawArrays(GL_POINTS, 0, g_hud_n);
            }
        }

        glUniform1f(u_base, 0.0f);
        glUniform1f(u_flat, 0.0f);
        return;
    }

    if (g_state == ST_WAKE) {
        /* Nine seconds, and the game runs backwards through everything it has
         * been. The cave dims out, a room marches in behind it, an eyelid
         * opens on the room, and the light that arrives is not one you made.
         *
         * Each beat overlaps the next, so nothing cuts. */
        float w      = g_wake;
        float sink   = 1.0f - smoothstep01(0.02f, 0.30f, w);   /* the cave leaving */
        float room   = smoothstep01(0.12f, 0.50f, w);          /* the room arriving */
        float open   = smoothstep01(0.34f, 0.78f, w);          /* the eye */
        float bright = smoothstep01(0.58f, 1.00f, w);          /* the light */
        float sharp  = smoothstep01(0.45f, 0.94f, w);
        char line[48];

        g_wake += dt * 0.112f;                                 /* about nine seconds */
        if (g_wake > 1.0f) g_wake = 1.0f;

        glClearColor(0.01f, 0.012f, 0.018f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        if (room > 0.004f) {
            glUseProgram(g_wake_prog);
            glUniform2f(w_res, (float)width, (float)height);
            glUniform1f(w_time, now);
            glUniform1f(w_open, open);
            glUniform1f(w_bright, bright);
            glUniform1f(w_sharp, sharp);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glBindVertexArray(g_wvao_full);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBlendFunc(GL_ONE, GL_ONE);
        }

        if (sink > 0.01f && g_count > 0) {
            mat4_persp(proj, 1.30f,
                       (float)width / (height > 0 ? (float)height : 1.0f), 0.05f, 60.0f);
            mat4_view(view, g_px, g_py, g_pz, g_yaw, g_pitch);
            mat4_mul(vp, proj, view);
            glUseProgram(g_prog);
            glUniformMatrix4fv(u_vp, 1, GL_FALSE, vp);
            glUniform3f(u_cam, g_px, g_py, g_pz);
            glUniform1f(u_time, now);
            glUniform1f(u_flat, 0.0f);
            glUniform1f(u_ink, 0.0f);
            glUniform1f(u_monster, 0.0f);
            glUniform1f(u_persist, 1.0f);
            glUniform1f(u_base, 0.0f);
            glUniform1f(u_fade, sink);
            glBindVertexArray(g_vao);
            glDrawArrays(GL_POINTS, 0, g_count);
            glUniform1f(u_fade, 1.0f);
        }

        if (open > 0.82f) {
            sprintf(line, w >= 1.0f ? "CLICK TO BEGIN AGAIN" : "BIS %d",
                    40 + (int)(bright * 60.0f));
            hud_build_wake("AWAKE", line);
            if (g_hud_n > 0) {
                glUseProgram(g_prog);
                glUniform1f(u_time, now);
                glUniform1f(u_monster, 0.0f);
                glUniform1f(u_persist, 1.0f);
                glUniform1f(u_flat, 1.0f);
                glUniform1f(u_base, 0.0f);
                glUniform1f(u_ink, 1.0f);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glBindVertexArray(g_hvao);
                glDrawArrays(GL_POINTS, 0, g_hud_n);
                glBlendFunc(GL_ONE, GL_ONE);
                glUniform1f(u_ink, 0.0f);
                glUniform1f(u_flat, 0.0f);
            }
        }

        if (w >= 1.0f && in->ping) enter_title(now);
        return;
    }

    if (g_state == ST_TITLE) {
        if (in->ping) {
            /* the click that starts the game is also the first sounding, so
             * the cave answers immediately instead of staying black */
            g_state = ST_PLAY;
            new_attempt((unsigned)(now * 100000.0f) ^ rnd(), now);
            basis(g_yaw, g_pitch, f, r, u);
            ping_begin(now, f, r, u);
            g_ping_ready = now + PING_COOLDOWN;
#ifdef DEMO_ENDING
            g_demo = 1.9f;     /* long enough to watch one ping land */
#endif
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
                glUniform1f(u_grey, 0.0f);
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

    if (g_wet) {
        /* Under water nothing starts or stops when you tell it to. A stroke
         * adds to what you already had, drag takes most of it back, and left
         * alone you drift upward - which is also the only reason you ever know
         * which way is up down here. */
        float ox = g_px, oy = g_py, oz = g_pz;
        if (len > 0.0001f) {
            float a = MOVE_SPEED * 3.4f * dt / len;
            g_vx += mx * a; g_vy += my * a; g_vz += mz * a;
            g_has_moved = 1;
        }
        g_vy += SWIM_LIFT * dt;
        g_vx *= SWIM_DRAG; g_vy *= SWIM_DRAG; g_vz *= SWIM_DRAG;
        try_move(g_vx * dt, g_vy * dt, g_vz * dt);
        g_travelled += (float)sqrt((g_px-ox)*(g_px-ox) + (g_py-oy)*(g_py-oy)
                                 + (g_pz-oz)*(g_pz-oz));
    } else if (len > 0.0001f) {
        float s = MOVE_SPEED * dt / len;
        {   float ox = g_px, oy = g_py, oz = g_pz;
            try_move(mx * s, my * s, mz * s);
            g_travelled += (float)sqrt((g_px-ox)*(g_px-ox) + (g_py-oy)*(g_py-oy)
                                     + (g_pz-oz)*(g_pz-oz));
        }
        g_has_moved = 1;
    }

#ifdef DEMO_ENDING
    /* the ending demo: one sounding, then it takes you */
    g_demo -= dt;
    if (g_demo <= 0.0f) { g_state = ST_WAKE; g_wake = 0.0f; return; }
#endif

#ifdef DEMO_FLAT
    if (now > 1.6f) {   /* test rig: straight to the monitor losing you */
        g_state = ST_FLATLINE; g_flat = 0.0f; audio_flatline(); return;
    }
#endif

    {   /* the flooded stretch, and the moment of crossing into it */
        int wet = depth_is_wet(g_pz);
        if (wet != g_wet) {
            g_wet = wet;
            audio_submerged(wet);
            audio_splash();
            g_vx = g_vy = g_vz = 0.0f;
        }
    }
    if (g_wet) motes_refresh(now);

    if (-g_pz > g_best_depth) g_best_depth = -g_pz;

    {   /* thresholds passed, and the one that ends it */
        static const float GATES[4] = { GATE_1, GATE_2, GATE_3, GATE_END };
        while (g_stage < 4 && -g_pz >= GATES[g_stage]) {
            g_stage++;
            g_stage_flash = 1.0f;
            if (g_stage >= 4) {
                audio_beep();                  /* the corridor is ahead */
            } else if (g_stage == 1) {
                /* Gate one is the waterline. No interlude - you go in.
                   The plunge is a shove downward, a flash of foam, and the
                   ears closing over; depth_is_wet picks it up from there. */
                g_dive = 1.0f;
                g_vx = 0.0f; g_vy = -6.5f; g_vz = -2.0f;
                g_flash = 0.35f;
            } else {
                /* gates two and three land as a shift in the world itself -
                   the cutaway to the room read as a non sequitur and is gone */
                audio_beep();
            }
        }
    }
    if (g_stage_flash > 0.0f) g_stage_flash -= dt * 0.55f;
    g_stagef += ((float)g_stage - g_stagef) * (1.0f - (float)exp(-dt * 1.6f));
    if (g_dive > 0.0f) g_dive -= dt * 0.8f;

    /* the door at the end of the corridor */
    if (-g_pz >= WAKE_Z - 1.6f && in->ping) {
        g_state = ST_WAKE;
        g_wake  = 0.0f;
        audio_beep();
        return;
    }

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
                g_state = ST_FLATLINE;
                g_flat  = 0.0f;
                audio_flatline();
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
    glClearColor(0.008f + g_flash * 0.30f + g_stage_flash * 0.05f
                        + (g_dive > 0.0f ? g_dive * 0.10f : 0.0f),
                 0.012f + g_stage_flash * 0.07f,
                 0.020f + g_stage_flash * 0.09f, 1.0f);
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

    {   /* the rock itself, once there is enough of the world to light */
        float sm = surface_mix(g_pz);
        if (sm > 0.004f) {
            float bra[48], brb[48];
            int b;
            for (b = 0; b < 16; b++) {
                bra[b*3+0] = g_br[b][0]; bra[b*3+1] = g_br[b][1]; bra[b*3+2] = g_br[b][2];
                brb[b*3+0] = g_br[b][3]; brb[b*3+1] = g_br[b][4]; brb[b*3+2] = g_br[b][5];
            }
            glUseProgram(g_cave_prog);
            glUniform2f(c_res, (float)width, (float)height);
            glUniform3f(c_cam, g_px, g_py, g_pz);
            glUniform3f(c_fwd, f[0], f[1], f[2]);
            glUniform3f(c_right, r[0], r[1], r[2]);
            glUniform3f(c_up, u[0], u[1], u[2]);
            glUniform1f(c_seed, g_seed);
            glUniform1f(c_wander, g_wander);
            glUniform1f(c_rough, g_rough);
            glUniform1f(c_time, now);
            {   /* Surfaces before the corridor are a sheen in the dark,
                 * or sounding would stop meaning anything: the full light
                 * belongs to stage four alone. */
                float ls = smoothstep01(3.02f, 3.85f, g_stagef);
                float dim = sm * 0.05f;   /* a sheen, not a reveal */
                glUniform1f(c_light, dim > ls ? dim : ls);
            }
            glUniform1f(c_wet, g_wet ? 1.0f : 0.0f);
            glUniform1f(c_room, smoothstep01(GATE_3 + 8.0f, GATE_END - 4.0f, -g_pz));
            glUniform3fv(c_bra, 16, bra);
            glUniform3fv(c_brb, 16, brb);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glBindVertexArray(g_wvao_full);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBlendFunc(GL_ONE, GL_ONE);
        }
        glUseProgram(g_prog);
        glUniformMatrix4fv(u_vp, 1, GL_FALSE, vp);
        glUniform3f(u_cam, g_px, g_py, g_pz);
        glUniform1f(u_time, now);
        glUniform1f(u_flat, 0.0f);
        glUniform1f(u_base, 0.0f);
        glUniform1f(u_ink, 0.0f);
        /* and the points step back as it arrives */
        glUniform1f(u_fade, 1.0f - sm * 0.82f);
        {   /* grey rises with depth, but a crossed gate pulls it ahead, so
             * the change is felt at the boundary and not only along the way */
            float gd = smoothstep01(GATE_2, GATE_3 + 16.0f, -g_pz);
            float gs = smoothstep01(1.7f, 3.2f, g_stagef);
            glUniform1f(u_grey, gd > gs ? gd : gs);
        }
    }

    /* the map: surfaces, and they stay */
    if (g_count > 0) {
        glUniform1f(u_monster, 0.0f);
        glUniform1f(u_persist, 1.0f);
        glBindVertexArray(g_vao);
        glDrawArrays(GL_POINTS, 0, g_count);
    }

    if (g_wet) {   /* what the front catches on its way through the water */
        glUniform1f(u_persist, 0.0f);
        glBindVertexArray(g_movao);
        glDrawArrays(GL_POINTS, 0, MOTES);
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
        const char *hint = (-g_pz >= WAKE_Z - 7.0f) ? "CLICK TO OPEN"
                         : (g_has_moved ? "" : "WASD TO MOVE");
        sprintf(left, g_wet ? "STAGE %d   %5.1f M  SUBMERGED"
                            : "STAGE %d   %5.1f M", g_stage + 1,
                -g_pz < 0.0f ? 0.0f : -g_pz);
        sprintf(right, "LIFE %s", g_lives >= 3 ? "* * *"
                                : (g_lives == 2 ? "* *" : (g_lives == 1 ? "*" : "-")));
        hud_build(left, right, hint);

        if (g_hud_n > 0) {
            glUniform1f(u_monster, 0.0f);
            glUniform1f(u_persist, 1.0f);
            glUniform1f(u_fade, 1.0f);
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
    m->travel = 0.0f; m->dash = 0.0f; m->moving = 1;
    mon_make(m, type, 0.3f);
    m->state = MON_CHARGING;
    m->timer = 30.0f;
    (void)now;
}

int   game_point_count(void) { return g_count; }
float game_depth(void)       { return -g_pz; }
int   game_lives(void)       { return g_lives; }
int   game_monsters(void)    { return g_mon_count; }
int   game_stage(void)       { return g_stage; }
int   game_state(void)       { return g_state; }
float game_px(void)          { return g_px; }
float game_py(void)          { return g_py; }
float game_pz(void)          { return g_pz; }
float game_travelled(void)   { return g_travelled; }
