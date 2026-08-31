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
#define MOTES            900
#define WATER_PTS        700   /* the surface itself, glinting before you jump */
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
#define WAKE_Z        (GATE_END + 46.0f)
#define GATE_R           9.5f  /* how wide a threshold chamber opens */
#define GATE_W           5.0f  /* and how long it runs
 */

#define MON_POINTS      2800   /* limbs need volume, not a dotted line */
#define LEG_TUBE           6   /* points around the curve at each sample */
#define MAX_MON           12
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
              g_prog, g_wake_prog, g_wvao_full;
static GLint  u_vp, u_cam, u_time, u_monster, u_persist, u_flat, u_base, u_ink;
static GLint  w_res, w_time, w_open, w_bright, w_sharp, w_lamp, w_lampb;
static GLuint g_cave_prog;
static GLint  c_res, c_cam, c_fwd, c_right, c_up, c_seed, c_wander,
              c_rough, c_time, c_light, c_wet, c_room, c_road, c_white,
              c_pulse, c_bra, c_brb;
static GLint  u_fade, u_grey;
static Point *g_mpts;          /* heap: 48 KB of it has no business in the exe */

/* --- state -------------------------------------------------------------- */

enum { ST_TITLE, ST_PLAY, ST_WAKE, ST_FLATLINE, ST_ROAD, ST_MENU_ };
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
static float g_flat;              /* 0..1 through the flatline */
static float g_stagef;            /* g_stage, arriving over ~2 s */
static int   g_shock;             /* defibrillator hits taken so far */
static float g_shockf;            /* their effect, arriving over seconds */
static int   g_ev;                /* 0 calm, 1 horde closing, 2 aftermath */
static float g_ev_t;
static float g_guide_next;        /* the heartbeat that knows the way */
static float g_road;
static int   g_quit;
static int   g_menu_sel, g_menu_mode;
static int   g_menu_prev;         /* the state the menu will hand back */
static const char *g_note;        /* one line, from the other side */
static float g_note_t;
static float g_check;             /* the depth a death puts you back at */
static float g_gate_t;            /* the stage card, counting down */
static int   g_gate_n;            /* and which stage it is announcing */
static float g_back;              /* the trip back to the checkpoint */
static float g_step_acc;          /* metres since the last footfall */
static float g_wetfeet;           /* 1 straight out of the water, drying */
static int   g_maul_i = -1;       /* the one that has you, while it has you */
static float g_maul_x, g_maul_y, g_maul_z, g_maul_sc;
static int   g_steps;             /* footfalls so far - read by -shot only */
static float g_pulse;             /* the beat you feel as the door nears */
static float g_dive;              /* the plunge at gate one */
static int   g_pings;
static int   g_has_moved;
static float g_travelled;
static int   g_wet;              /* stage two: the passage is flooded */
static float g_vx, g_vy, g_vz;   /* swimming carries momentum */
static Point *g_motes;
static GLuint g_movao, g_movbo;
static Point *g_water;
static GLuint g_wtvao, g_wtvbo;
static float g_stroke_next;
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
    ts = smoothstep01(0.4f, 1.8f, g_shockf) * 0.9f;
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
    {
        /* the first chamber is a hall with a pool in it, so it opens wider
         * and longer than the others */
        static const float R[4] = {  4.0f, GATE_R, GATE_R, 11.0f };   /* GATE_1 is a room now */
        static const float W[4] = {  8.0f, GATE_W, GATE_W, GATE_W };
        if (best > W[k] * 4.0f) return 0.0f;
        dz = d - GATES[k];
        return R[k] * (float)exp(-(dz * dz) / (2.0f * W[k] * W[k]));
    }
}

/* where the doorway sits in row ri of the hall maze */
static float tunnel_gapx(float ri)
{
    float cx, cy;
    tunnel_centre(ri * -7.0f + 3.5f, &cx, &cy);
    return cx + (hash1(ri * 7.77f + g_seed * 2.0f) - 0.5f) * 16.0f;
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
        {   /* The first threshold is not a wider bit of cave. It is a room:
             * a flat floor, flat walls, a flat ceiling, and nothing in it
             * except a rectangular pool cut into the middle of the floor.
             * Walking forward used to put you in water without your ever
             * having been anywhere -- you have to arrive somewhere first,
             * and see what you are about to go into. */
            float dep = -z;
            float rd  = (float)fabs(dep - (GATE_1 + 5.0f));
            if (rd < 21.0f) {
                float fl   = cy - 1.05f;             /* the water is flush with it */
                float wall = 12.0f - (float)fabs(dx);
                float ceil = (cy + 3.2f) - y;
                float ends = 17.0f - rd;
                float flor = y - fl;
                /* the pool, exactly where the surface is drawn */
                float p1 = 8.5f - (float)fabs(dx);
                float p2 = 8.0f - (float)fabs(dep - 125.0f);
                float p3 = y - (fl - 7.0f);
                float pool = p1 < p2 ? p1 : p2;
                if (p3 < pool) pool = p3;
                if (pool > flor) flor = pool;        /* the hole in the floor */
                {   float room = wall;
                    if (ceil < room) room = ceil;
                    if (ends < room) room = ends;
                    if (flor < room) room = flor;
                    /* soft at the doorways so the corridor runs into it */
                    {   float kk = 1.0f - smoothstep01(13.0f, 20.0f, rd);
                        main_air = main_air * (1.0f - kk) + room * kk;
                    }
                }
            }
        }
        if (-z > GATE_2 + 4.0f) {
            /* The hall would not be the hall if you could walk it straight.
             * Every seven metres a wall crosses it with one doorway in it,
             * and stubs jut from the pillars, so the place is full of rooms
             * that stop - but every wall has its gap, so one way always
             * leads on. The doorways come from the row hash: the maze is the
             * seed, like everything else. */
            /* The hall. A ceiling you could touch, pillars on a seven-metre
             * grid, no walls to speak of - the same room over and over in
             * every direction, which is the whole architecture of the place
             * you are almost out of. */
            float slab = 1.45f - (float)fabs(y - cy + 0.3f);
            float mx = (float)fmod(fmod(x, 7.0f) + 10.5f, 7.0f) - 3.5f;
            float mz = (float)fmod(fmod(z, 7.0f) + 10.5f, 7.0f) - 3.5f;
            float ax = (float)fabs(mx), az = (float)fabs(mz);
            float pil = (ax > az ? ax : az) - 0.42f;
            float hall = slab < pil ? slab : pil;
            {   /* row walls, one gap apiece */
                float rz = (float)fmod(fmod(z, 7.0f) + 10.5f, 7.0f) - 3.5f;
                float ri = (float)floor((-z + 3.5f) / 7.0f);
                float gx = tunnel_gapx(ri);
                float wd = (float)fabs(rz) - 0.20f;
                if ((float)fabs(x - gx) > 1.60f && wd < hall) hall = wd;
            }
            {   /* stubs off the pillars, one direction per cell */
                float ci = (float)floor((x + 3.5f) / 7.0f) * 57.0f
                         + (float)floor((-z + 3.5f) / 7.0f);
                float hsel = hash1(ci * 3.17f + g_seed);
                if (hsel < 0.36f) {
                    /* a sealed lane: the wall runs pillar to pillar, across
                     * one axis or the other depending on the cell */
                    float w2 = (hsel < 0.18f)
                        ? ((mz > 0.40f && mz < 3.12f) ? (float)fabs(mx) - 0.17f : 1000.0f)
                        : ((mx > 0.40f && mx < 3.12f) ? (float)fabs(mz) - 0.17f : 1000.0f);
                    if (w2 < hall) hall = w2;
                }
            }
            {   /* The doorway wins. A gap that happened to land on a pillar
                 * or a stub sealed its whole row - and one sealed row seals
                 * the rest of the game. Carve it through everything. */
                float rz = (float)fmod(fmod(z, 7.0f) + 10.5f, 7.0f) - 3.5f;
                float ri = (float)floor((-z + 3.5f) / 7.0f);
                float gx = tunnel_gapx(ri);
                float ox2 = 1.60f - (float)fabs(x - gx);
                float oz2 = 1.60f - (float)fabs(rz);
                float open2 = ox2 < oz2 ? ox2 : oz2;
                if (open2 > slab) open2 = slab;
                if (open2 > hall) hall = open2;
            }
            float k2 = ((-z) - (GATE_2 + 4.0f)) / 6.0f;
            if (k2 > 1.0f) k2 = 1.0f;
            main_air = main_air * (1.0f - k2) + hall * k2;
        }
        {   /* and it ends at the door */
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
        m->scale   = 1.55f;      /* unmistakably the large one */
        m->legspan = 0.80f;      /* stocky, low, dense */
        break;
    case T_LISTENER:    /* slow and generous, but hears you from far away */
        m->speed   = 3.5f + 0.8f * k;
        m->warn    = 0.72f;
        m->hear    = 32.0f + 8.0f * k;
        m->scale   = 0.70f;
        m->legspan = 3.10f;      /* stilts radiating from almost no body */
        break;
    default:            /* T_STALKER - the small one you learn the game on */
        m->speed   = 4.4f + 1.5f * k;
        m->warn    = 0.58f - 0.16f * k;
        m->hear    = 24.0f + 8.0f * k;
        m->scale   = 0.42f;      /* the small quick one */
        m->legspan = 1.30f;
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

static int g_calm;             /* -calm: nothing hunts, so a capture survives */

static int mon_target_count(void)
{
    float k = depth_k(g_pz);
    if (g_calm) return 0;
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
            audio_roar(m->type);
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
        if (m->type == T_LISTENER) { m->moving = 1; }
        else if (m->dash <= 0.0f) {
            m->moving = !m->moving;
            m->dash = m->moving
                    ? DASH_ON  + hash1(now * 13.0f + m->seed) * 0.20f
                    : DASH_OFF + hash1(now * 17.0f + m->seed) * 0.16f;
        }

        step = m->moving ? m->speed * (m->type == T_LISTENER ? 1.0f : 1.75f) * dt : 0.0f;
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
            /* Every leg took exactly as long as every other one, so the
             * two halves of the gait came down like a metronome. Give each
             * its own swing and the tetrapod rhythm stays but stops being
             * countable. */
            m->ft[i] += dt / (SWING_TIME
                              * (0.76f + hash1((float)i * 2.71f + m->seed) * 0.54f));
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
    float bob, br;
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

    /* The body rides on the legs, so it rises and falls with them. Held at
     * a fixed height off the rock it walked like something on rails; half a
     * centimetre of bob is the difference between a machine and an animal. */
    {   float ph = 0.0f;
        for (i = 0; i < 8; i++)
            ph += (float)sin(3.1415927f * (m->ft[i] < 1.0f ? m->ft[i] : 1.0f));
        bob = (ph * 0.125f - 0.22f) * 0.13f * sc;
    }
    /* and it breathes, slowly, whether or not it is doing anything else */
    br = 1.0f + 0.055f * (float)sin(now * 2.1f + m->seed * 3.0f);

    /* body: a swollen abdomen, a thin neck, and a head hung off the end of
     * it. The head used to sit against the body like a second ball; slung
     * forward on a neck it reads as something carried out in front, which is
     * worse to have coming at you. */
    for (i = 0; i < (m->type == T_LISTENER ? 70 : 260); i++) {
        float a = hash1((float)i * 1.7f + m->seed) * 6.2831853f;
        float b = hash1((float)i * 3.1f + m->seed + 4.0f) * 3.1415927f;
        float r = 0.26f * sc * br * (float)pow(hash1((float)i * 5.3f + m->seed), 0.34);
        PUT(-0.13f * sc + r*(float)sin(b)*(float)cos(a) * 1.25f,
             r*(float)sin(b)*(float)sin(a),
             bob + r*(float)cos(b) * 0.75f, 1.0f);
    }
    for (i = 0; i < 34; i++) {          /* the neck */
        float u = (float)i / 33.0f;
        float a = hash1((float)i * 6.1f + m->seed + 7.0f) * 6.2831853f;
        float r = 0.055f * sc;
        PUT(0.10f * sc + u * 0.22f * sc + r*(float)cos(a),
            r*(float)sin(a) * 0.7f,
            bob - u * 0.10f * sc + r*(float)sin(a) * 0.7f, 0.85f);
    }
    for (i = 0; i < 95; i++) {          /* the head, hung low and forward */
        float a = hash1((float)i * 2.3f + m->seed + 9.0f) * 6.2831853f;
        float b = hash1((float)i * 4.7f + m->seed + 2.0f) * 3.1415927f;
        float r = 0.115f * sc;
        PUT(0.36f * sc + r*(float)sin(b)*(float)cos(a),
            r*(float)sin(b)*(float)sin(a),
            bob - 0.10f * sc + r*(float)cos(b) * 0.8f, 1.0f);
    }
    for (i = 0; i < 26; i++) {          /* a pair of them, in front of that */
        float u    = (float)(i % 13) / 12.0f;
        float side = (i < 13) ? 1.0f : -1.0f;
        PUT(0.44f * sc + u * 0.14f * sc,
            side * (0.048f - u * 0.032f) * sc,
            bob - 0.10f * sc - u * 0.050f * sc, 1.0f);
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
        /* No two of them are built the same. Eight identical legs is a
         * machine; eight that disagree about how long they are is a thing
         * that grew. */
        float lv = 0.68f + hash1((float)i * 3.37f + m->seed * 2.0f) * 0.74f;
        float tk = 0.72f + hash1((float)i * 5.11f + m->seed * 4.0f) * 0.66f;
        float k1x, k1y, k1z, k2x, k2y, k2z;
        /* Two joints, not one. A knee that rises above the body and a shin
         * that comes back down to the foot is what a spider's leg does, and
         * a single arc never looked like anything but a hoop. */
        if (m->type == T_LISTENER) {
            k1x = ex * 0.32f; k1y = ey * 0.34f; k1z = 0.44f * sc * lv;
            k2x = ex * 0.80f; k2y = ey * 0.82f; k2z = 0.10f * sc;
        } else {
            k1x = ex * 0.26f; k1y = ey * 0.30f; k1z = 1.22f * sc * lv;
            k2x = ex * 0.76f; k2y = ey * 0.79f; k2z = 0.30f * sc;
        }

        for (k = 0; k < 46; k++) {
            float u   = (float)k / 45.0f;
            float iu  = 1.0f - u;
            float w3  = 3.0f * iu * iu * u, w2 = 3.0f * iu * u * u, w1 = u * u * u;
            float a   = w3 * k1x + w2 * k2x + w1 * ex;
            float b2  = w3 * k1y + w2 * k2y + w1 * ey;
            float c2  = w3 * k1z + w2 * k2z + w1 * ez + bob * iu * iu;
            float rad = (0.175f * iu * iu + 0.028f) * sc * tk
                      * (m->type == T_LISTENER ? 0.40f : 1.0f);
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
    return (d >= GATE_1 - 3.0f && d < GATE_2 - 1.0f);   /* the pool's near rim */
}

static float wave_speed(void)
{
    return g_wet ? WAVE_SPEED_WET : WAVE_SPEED;
}

/* How much of the sounding's boost applies here.
 *
 * This started out as the room's own shape and that was wrong: the room is
 * symmetric about its middle, so its far edge sits six metres past the far
 * rim of the pool, and measured, the brightest ping in the whole game came
 * back at 140 m -- out in open water, swimming, which is the one place it
 * was never meant to be. It runs in over the dry floor and stops at the
 * rim: full from 108 to 132, gone by 139. */
static float room1_ping_k(float z)
{
    float d = -z;
    return smoothstep01(102.0f, 108.0f, d) * (1.0f - smoothstep01(132.0f, 139.0f, d));
}

static float ping_reach(void)
{
    /* Rock swallows it, and more of it the deeper you are. The room at the
     * first threshold is not rock: it is tiled and flat and full of water,
     * and one sounding in it comes back with the whole place. That is the
     * only room in the game you get to see all of at once, which is why it
     * is where the game shows you what a sounding is actually for. */
    return (26.0f - 11.0f * depth_k(g_pz)) * (1.0f + 0.62f * room1_ping_k(g_pz));
}

/* Did the beam actually land on it? Being behind you, or around a corner,
 * means it never knows you were there. */
/* --- the voices ----------------------------------------------------------
 *
 * Sixteen side passages come off the main tunnel and until now they led
 * nowhere. There is a voice at the end of each one, and a ping that reaches
 * it and has line of sight to it brings back a line from outside.
 *
 * They are in order of depth, and they are the resuscitation: the room you
 * are actually lying in, told by the people standing over you. The game
 * never says what the depth counter counts. It does not have to -- at 141
 * metres somebody says "2분 경과", at 306 "5분 넘었습니다", at 374 "6분입니다",
 * and the number is on screen the whole time. The arithmetic is the player's.
 *
 * The clinical ones land where their event does: 238 m is two metres before
 * the first defibrillator shock, so the warning arrives just ahead of it. */

static const char *VOICE[BRANCHES] = {
    "환자분, 제 목소리 들리세요?",
    "맥박 없습니다. 압박 시작합니다",
    "기도 확보됐습니다",
    "에피네프린 1밀리그램 들어갑니다",
    "2분 경과, 리듬 확인하겠습니다",
    "보호자분은 잠깐 나가 계시겠어요",
    "여보, 나 여기 있어. 여기 있어",
    "제세동 200줄, 다들 물러나세요",
    "리듬 잡혔다가 다시 빠집니다",
    "5분 넘었습니다",
    "한 번 더. 200줄, 물러나세요",
    "6분 넘었습니다",
    "아빠, 일어나 봐. 아빠",
    "보호자분들 들어오시라고 할까요",
    "잠깐만요. 잠깐만",
    "맥박 있습니다. 맥박 있어요"
};

static unsigned g_heard;      /* which ones this attempt has brought back */
static int      g_heard_n;

/* Same test the monsters get -- in the beam, in range, and nothing in the
 * way. You have to walk into the passage: the far end sits about fourteen
 * metres in and a ping only carries fifteen to twenty-six. */
static void voice_lit_by(const float *f, float reach)
{
    int i;
    for (i = 0; i < BRANCHES; i++) {
        float vx, vy, vz, dx, dy, dz, d, wall;
        if (g_heard & (1u << i)) continue;
        vx = g_br[i][0] + (g_br[i][3] - g_br[i][0]) * 0.72f;
        vy = g_br[i][1] + (g_br[i][4] - g_br[i][1]) * 0.72f;
        vz = g_br[i][2] + (g_br[i][5] - g_br[i][2]) * 0.72f;
        dx = vx - g_px; dy = vy - g_py; dz = vz - g_pz;
        d  = (float)sqrt(dx * dx + dy * dy + dz * dz);
        if (d > reach || d < 0.001f) continue;
        if ((dx * f[0] + dy * f[1] + dz * f[2]) / d < (float)cos(CONE_HALF_ANGLE))
            continue;
        if (cave_ray(g_px, g_py, g_pz, dx / d, dy / d, dz / d, d, &wall)) continue;

        g_heard |= 1u << i;
        g_heard_n++;
        g_note   = VOICE[i];
        g_note_t = 4.2f;      /* longer than the guide's: these are sentences */
        return;               /* one at a time, or two passages at once talk over each other */
    }
}

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
static float g_ping_room;      /* how much of the first room this ping is in */
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
    g_ping_room = room1_ping_k(g_pz);
    g_ping_i  = 0;
    g_ping_busy = 1;
    g_wcount = 0;                       /* the previous wave has passed */

    g_pings++;
    audio_ping();
    mon_lit_by(f, ping_reach(), now);
    voice_lit_by(f, ping_reach());
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

                /* A wide room takes fewer marks than a tunnel does, not
                 * more: the rays fan out and every impact is metres from the
                 * last one, so five specks apiece leaves it looking like fog.
                 * The one room in the game worth seeing all of gets a bigger,
                 * denser splash, and a sounding in it comes back with walls
                 * instead of a scatter. */
                int   nmark = MARK_SPLASH + (int)(g_ping_room * 11.0f);
                float srad  = SPLASH_R * (1.0f + g_ping_room * 1.7f);
                for (m = 0; m < nmark && g_count < MAX_POINTS; m++) {
                    float a = (hash1((float)i * 3.7f + (float)b * 11.0f + (float)m * 2.3f) - 0.5f) * 2.0f * srad;
                    float c = (hash1((float)i * 5.1f + (float)b * 7.0f  + (float)m * 3.9f) - 0.5f) * 2.0f * srad;
                    if (m == 0) { a = 0.0f; c = 0.0f; }      /* one dead on the hit */
                    g_pts[g_count].x = ox + t1[0]*a + t2[0]*c;
                    g_pts[g_count].y = oy + t1[1]*a + t2[1]*c;
                    g_pts[g_count].z = oz + t1[2]*a + t2[2]*c;
                    g_pts[g_count].reveal = g_ping_t0 + travelled / wave_speed();
                    /* The mark keeps more of itself than the bullet does: a
                     * tired ricochet still proves a wall is there, and the
                     * cave only takes shape if late hits stay readable. */
                    g_pts[g_count].gain = (0.42f + 0.58f * gain)
                                        * (1.0f + 0.35f * g_ping_room);
                    g_count++;
                    added++;
                }
            }

            /* the air wears it down as it goes */
            /* and the air in it costs the sound almost nothing */
            gain *= (float)exp(-GAIN_PER_METRE * t * (1.0f - 0.62f * g_ping_room));
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

/* The wave that is not yours. Full sphere, three times the reach, marks so
 * bright they hurt - and they close over in about two seconds, because this
 * light was lent, not earned. Everything hunting you comes apart in it. */
static void emit_defib(float now)
{
    int i, b;
    int start = g_count;
    int added = 0;

    for (i = 0; i < 1400; i++) {
        float k  = ((float)i + 0.5f) / 1400.0f;
        float ct = 1.0f - 2.0f * k;
        float st = (float)sqrt(1.0f - ct * ct);
        float ph = 2.39996323f * (float)i;
        float dx = st * (float)cos(ph), dy = ct, dz = st * (float)sin(ph);
        float ox = g_px, oy = g_py, oz = g_pz;
        float travelled = 0.0f, gain = 2.4f;

        for (b = 0; b < 3; b++) {
            float t, n2[3], dot2;
            if (g_count >= MAX_POINTS) break;
            if (!cave_ray(ox, oy, oz, dx, dy, dz, 46.0f - travelled, &t)) break;
            ox += dx * t; oy += dy * t; oz += dz * t; travelled += t;
            cave_normal(ox, oy, oz, n2);
            {   int m2;
                for (m2 = 0; m2 < 3 && g_count < MAX_POINTS; m2++) {
                    float h = (float)(i * 31 + b * 7 + m2) * 1.7f;
                    g_pts[g_count].x = ox + (hash1(h) - 0.5f) * 0.3f;
                    g_pts[g_count].y = oy + (hash1(h + 2.0f) - 0.5f) * 0.3f;
                    g_pts[g_count].z = oz + (hash1(h + 5.0f) - 0.5f) * 0.3f;
                    g_pts[g_count].reveal = now + travelled / 34.0f;
                    g_pts[g_count].gain = gain;
                    g_count++; added++;
                }
            }
            if (travelled > 44.0f) break;
            dot2 = dx * n2[0] + dy * n2[1] + dz * n2[2];
            dx -= 2.0f * dot2 * n2[0];
            dy -= 2.0f * dot2 * n2[1];
            dz -= 2.0f * dot2 * n2[2];
            ox += n2[0] * 0.14f; oy += n2[1] * 0.14f; oz += n2[2] * 0.14f;
            gain *= 0.85f;
        }
    }
    if (added > 0) {
        glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
        glBufferSubData(GL_ARRAY_BUFFER,
                        (GLintptr)(start * (int)sizeof(Point)),
                        (GLsizeiptr)(added * (int)sizeof(Point)),
                        g_pts + start);
    }
}

/* A quieter cousin: every few seconds after the first shock, a cone of the
 * same borrowed light leaves your chest toward the exit. The heart knows the
 * way out; the marks it leaves fade like the shock's do. The pulses come
 * quicker as the door gets closer, which is itself the hint. */
static void emit_guide(float now)
{
    int i;
    int start = g_count, added = 0;
    float dxx, dyy, dzz, dl;
    float doorx, doory;
    tunnel_centre(-WAKE_Z + 4.0f, &doorx, &doory);
    dxx = doorx - g_px; dyy = doory - g_py; dzz = (-WAKE_Z + 2.0f) - g_pz;
    dl = (float)sqrt(dxx * dxx + dyy * dyy + dzz * dzz);
    if (dl < 1.0f) return;
    dxx /= dl; dyy /= dl; dzz /= dl;

    for (i = 0; i < 240 && g_count < MAX_POINTS; i++) {
        float j1 = (hash1((float)i * 1.91f + now) - 0.5f) * 0.55f;
        float j2 = (hash1((float)i * 3.37f + now * 2.0f) - 0.5f) * 0.55f;
        float rx = dxx + j1 * (dzz + 0.3f), ry = dyy + j2, rz = dzz - j1 * (dxx + 0.3f);
        float rl = (float)sqrt(rx * rx + ry * ry + rz * rz), t;
        rx /= rl; ry /= rl; rz /= rl;
        if (!cave_ray(g_px, g_py, g_pz, rx, ry, rz, 26.0f, &t)) continue;
        g_pts[g_count].x = g_px + rx * t;
        g_pts[g_count].y = g_py + ry * t;
        g_pts[g_count].z = g_pz + rz * t;
        g_pts[g_count].reveal = now + t / 30.0f;
        g_pts[g_count].gain = 2.0f;
        g_count++; added++;
    }
    if (added > 0) {
        glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
        glBufferSubData(GL_ARRAY_BUFFER,
                        (GLintptr)(start * (int)sizeof(Point)),
                        (GLsizeiptr)(added * (int)sizeof(Point)),
                        g_pts + start);
    }
    audio_beep();
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
    /* Not the entrance. Walking the first two hundred metres again for the
     * third time is not a second chance, it is a punishment for having had
     * one -- so a death puts you back at the last threshold you crossed. */
    tunnel_centre(-g_check, &cx, &cy);
    g_px = cx; g_py = cy; g_pz = -g_check;
    {   /* The tunnel bends, so a fixed heading points into the wall as often
         * as not - and in the dark that is indistinguishable from the game
         * being broken. Face the way the passage actually runs. */
        float ax, ay, fx, fz;
        tunnel_centre(-g_check - 3.0f, &ax, &ay);
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
    g_count  = 0;
    g_pings  = 0;
    g_heard  = 0;
    g_heard_n = 0;
    g_check  = g_start_depth;
    g_gate_t = 0.0f; g_back = 0.0f;
    g_wetfeet = 0.0f; g_step_acc = 0.0f;
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
    /* Spawning deep has to bring what you would have been carrying. The two
     * ambushes are at gates two and three, and everything about how the
     * world looks past them hangs off having been shocked -- uLight does not
     * come on at all until the second one, and uRoom follows it. Starting at
     * 340 with a clean sheet gave the right geometry with the lights off,
     * which is exactly why every deep demo came out looking like more cave. */
    g_shock  = (g_start_depth >= GATE_3) ? 2 : (g_start_depth >= GATE_2 ? 1 : 0);
    g_shockf = (float)g_shock;
    g_ev = 0; g_ev_t = 0.0f;
    g_guide_next = 0.0f; g_road = 0.0f;
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
        /* The word never goes dark and comes back on -- that read as the game
         * restarting. It sits at uBase and a wavefront crosses it, so the
         * title is the mechanic: a sounding sweeps left to right and the
         * letters brighten as it reaches them. delay staggers the lines so
         * the sweep arrives at each in turn. */
        g_pts[g_count].x = x;
        g_pts[g_count].y = y;
        g_pts[g_count].z = z;
        g_pts[g_count].reveal = now + delay;
        g_pts[g_count].gain   = 1.0f;
        g_count++;
    }
}

/* Re-time the sweep so it crosses the word again. Cheaper than rebuilding the
 * title -- the letters do not move, only the moment each is reached. */
static void title_sweep(float now)
{
    int i;
    for (i = 0; i < g_count; i++)
        g_pts[i].reveal = now + (g_pts[i].x - g_px + 3.5f) * 0.125f;
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)(g_count * (int)sizeof(Point)), g_pts);
    g_title_pulse = now + 4.4f;
}

static void enter_title(float now)
{
    /* The spawn heading follows the tunnel, but the title is laid out in
     * world axes - so with a rotated camera the word came up sheared.
     * The title looks straight down -z, always. */
    g_yaw = 0.0f; g_pitch = 0.0f;
    g_state = ST_TITLE;
    g_count = 0;
    title_line("SOUNDING",             150, 2.05f,  0.55f, 0.30f, now);
    title_line("EVERY CLICK IS A SOUNDING", 30, 2.05f, -0.64f, 0.95f, now);
    title_line("CLICK TO START",        40, 2.05f, -1.35f, 1.35f, now);

    title_sweep(now);
}

/* --- the readout --------------------------------------------------------
 * A patient monitor, not a game HUD: it is the same point cloud as the cave,
 * pinned to the screen. Rebuilt only when the text it shows actually changes,
 * so GDI is touched a few times a second at worst. */

/* Three lines of 96 px lettering, scattered. Measured on a Mac build: the
 * plainest screen needs 9,600 points and "STAGE 2  130.0 M  SUBMERGED" needs
 * 13,200 -- so the old 9,000 truncated the last line appended, the hint, in
 * every single state. That is why "WASD TO MOVE" was never on screen and
 * "CLICK TO OPEN" came up sliced in half. Korean glyphs are denser than the
 * fallback the measurement was taken through, hence the room left over. It
 * costs 480 KB of RAM and not one byte on disk. */
#define HUD_MAX 24000

static Point *g_hud;
static int    g_hud_n;
static char   g_hud_cache[96];

/* x,y in normalised device coordinates; scale is height in NDC.
 *
 * align pins which part of the line lands on cx: -1 its left edge, +1 its
 * right edge, 0 its middle. Centring everything meant the readout's width
 * decided where it started, so the one line long enough to matter -- the
 * submerged one -- walked off the edge of the screen. A corner readout should
 * be measured from its corner. */
static void hud_line(const char *str, float scale, float cx, float cy,
                     float bright, int align)
{
    int   n, i;
    float lo = 1e9f, hi = -1e9f, off = 0.0f;
    if (!g_text_xy) return;
    n = plat_text_points(str, 96, g_text_xy, TEXT_MAX_PTS);
    if (align) {
        for (i = 0; i < n; i++) {
            float t = g_text_xy[i * 2 + 0];
            if (t < lo) lo = t;
            if (t > hi) hi = t;
        }
        if (n > 0) off = (align < 0 ? -lo : -hi) * scale;
    }
    for (i = 0; i < n && g_hud_n < HUD_MAX; i++) {
        g_hud[g_hud_n].x = cx + off + g_text_xy[i * 2 + 0] * scale;
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
    if (a[0]) hud_line(a, 0.150f, 0.0f,  0.10f, 0.85f, 0);
    if (b[0]) hud_line(b, 0.105f, 0.0f, -0.10f, 0.70f, 0);
    if (g_hud_n > 0) {
        glBindBuffer(GL_ARRAY_BUFFER, g_hvbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        (GLsizeiptr)(g_hud_n * (int)sizeof(Point)), g_hud);
    }
}

/* Stage by stage, the body you left behind answers.
 *
 * The first voice in the first side passage asks you to move your hand.
 * This is the hand. At the first threshold the little finger goes and that
 * is the whole of it; the thumb does not join until the last one, and by
 * then it is coming up off the sheet. Nobody says anything about it -- it
 * is at the bottom of the screen for three seconds, where your own hand
 * would be, and either you notice or you do not.
 *
 * wake is 0..1 across the four thresholds; t is seconds since the card. */
static void hud_hand(float wake, float t)
{
    /* thumb, then the four, so index 4 is the little finger */
    static const float FX[5] = { -0.250f, -0.092f,  0.051f,  0.186f,  0.300f };
    static const float FL[5] = {  0.169f,  0.270f,  0.305f,  0.265f,  0.186f };
    const float cx = 0.40f, cy = -0.74f;
    int   fi, k;

    /* The back of a hand, lying over. Everything else on this screen is
     * made of thousands of points; two hundred read as dust rather than as
     * a hand, so this is drawn at the density of the lettering beside it. */
    for (k = 0; k < 900 && g_hud_n < HUD_MAX; k++) {
        float a = hash1((float)k * 1.71f + 3.0f) * 6.2831853f;
        float r = (float)sqrt(hash1((float)k * 2.93f + 1.0f)) * 0.173f;
        g_hud[g_hud_n].x = cx + (float)cos(a) * r * 1.25f;
        g_hud[g_hud_n].y = cy + (float)sin(a) * r * 0.78f;
        g_hud[g_hud_n].z = 0.0f;
        g_hud[g_hud_n].reveal = 0.0f;
        g_hud[g_hud_n].gain = 0.80f;
        g_hud_n++;
    }
    for (fi = 0; fi < 5; fi++) {
        /* the little one first; the thumb is last to have anything in it */
        float own = wake * 5.0f - (float)(4 - fi);
        float ph, tw, amp;
        if (own < 0.0f) own = 0.0f;
        if (own > 1.0f) own = 1.0f;
        /* involuntary: fast, small, and over before you are sure of it */
        ph  = t * 8.5f - (float)(4 - fi) * 0.55f;   /* it runs down the hand */
        tw  = (ph > 0.0f) ? (float)sin(ph) * (float)exp(-ph * 0.50f) : 0.0f;
        amp = tw * own * 0.135f;
        for (k = 0; k <= 70 && g_hud_n < HUD_MAX; k++) {
            float u    = (float)k / 70.0f;
            float bend = amp * u * u;
            float bx   = cx + FX[fi] * (1.0f - 0.22f * u) + bend * 0.40f;
            float by   = cy + 0.07f + u * FL[fi] + bend;
            int   q;
            for (q = 0; q < 5 && g_hud_n < HUD_MAX; q++) {
                float h  = (float)(fi * 173 + k * 7 + q) * 1.37f;
                float aa = hash1(h) * 6.2831853f;
                float rr = (float)sqrt(hash1(h + 2.1f)) * 0.030f * (1.0f - 0.35f * u);
                g_hud[g_hud_n].x = bx + (float)cos(aa) * rr;
                g_hud[g_hud_n].y = by + (float)sin(aa) * rr;
                g_hud[g_hud_n].z = 0.0f;
                g_hud[g_hud_n].reveal = 0.0f;
                g_hud[g_hud_n].gain = 0.72f + 0.55f * own;
                g_hud_n++;
            }
        }
    }
    glBindBuffer(GL_ARRAY_BUFFER, g_hvbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)(g_hud_n * (int)sizeof(Point)), g_hud);
}

static void hud_build(const char *left, const char *right, const char *hint)
{
    char key[96];
    sprintf(key, "%.28s|%.28s|%.30s", left, right, hint);
    if (strcmp(key, g_hud_cache) == 0) return;      /* nothing moved */
    strncpy(g_hud_cache, key, sizeof g_hud_cache - 1);
    g_hud_cache[sizeof g_hud_cache - 1] = 0;

    g_hud_n = 0;
    if (left[0])  hud_line(left,  0.135f, -0.88f,  0.86f, 0.78f, -1);
    if (right[0]) hud_line(right, 0.135f,  0.88f,  0.86f, 0.78f,  1);
    if (hint[0])  hud_line(hint,  0.130f,  0.00f, -0.74f, 0.66f,  0);

    if (g_hud_n > 0) {
        glBindBuffer(GL_ARRAY_BUFFER, g_hvbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        (GLsizeiptr)(g_hud_n * (int)sizeof(Point)), g_hud);
    }
}

/* Water is not empty, and that is most of what separates it from air. A few
 * hundred specks drift in it and light up only as the front goes past, so the
 * space between the walls stops being nothing. */
/* The pool fills the first threshold chamber. You see it before you are in
 * it: a sheet of glints rocking on the surface, drawn with a base glow so it
 * needs no sounding - the first water in the game announces itself. */
static void water_refresh(float now)
{
    int i;
    float cx, cy;
    tunnel_centre(-GATE_1, &cx, &cy);
    for (i = 0; i < WATER_PTS; i++) {
        float u = hash1((float)i * 1.31f) - 0.5f;
        float v = hash1((float)i * 2.77f + 4.0f);
        float x = cx + u * 17.0f;
        float z = -GATE_1 + 3.0f - v * 16.0f;
        float rip = (float)sin(x * 1.7f + now * 1.3f)
                  + (float)sin(z * 2.3f - now * 0.9f);
        g_water[i].x = x;
        g_water[i].y = cy - 1.05f + rip * 0.055f;
        g_water[i].z = z;
        g_water[i].reveal = now - 1000.0f;      /* always lit, via uBase */
        g_water[i].gain   = 0.35f + 0.30f * hash1((float)i * 5.3f + (float)((int)(now * 2.0f)));
    }
    glBindBuffer(GL_ARRAY_BUFFER, g_wtvbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)(WATER_PTS * (int)sizeof(Point)), g_water);
}
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
    g_motes   = (Point *)malloc((size_t)MOTES * sizeof(Point));
    g_water   = (Point *)malloc((size_t)WATER_PTS * sizeof(Point));
    g_water   = (Point *)malloc((size_t)WATER_PTS * sizeof(Point));
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
    w_lamp   = glGetUniformLocation(g_wake_prog, "uLamp");
    w_lampb  = glGetUniformLocation(g_wake_prog, "uLampB");
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
    c_road   = glGetUniformLocation(g_cave_prog, "uRoad");
    c_white  = glGetUniformLocation(g_cave_prog, "uWhite");
    c_pulse  = glGetUniformLocation(g_cave_prog, "uPulse");
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

    glGenVertexArrays(1, &g_wtvao);
    glGenBuffers(1, &g_wtvbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_wtvbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)WATER_PTS * (GLsizeiptr)sizeof(Point),
                 0, GL_DYNAMIC_DRAW);
    setup_attribs(g_wtvao, g_wtvbo);

    glGenVertexArrays(1, &g_wtvao);
    glGenBuffers(1, &g_wtvbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_wtvbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)WATER_PTS * (GLsizeiptr)sizeof(Point),
                 0, GL_DYNAMIC_DRAW);
    setup_attribs(g_wtvao, g_wtvbo);

    glGenVertexArrays(1, &g_movao);
    glGenBuffers(1, &g_movbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_movbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)MOTES * (GLsizeiptr)sizeof(Point),
                 0, GL_DYNAMIC_DRAW);
    setup_attribs(g_movao, g_movbo);

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

    if (in->menu) {
        if (g_state == ST_PLAY) {
            g_menu_prev = g_state; g_state = ST_MENU_;
            g_menu_sel = 0; g_menu_mode = 0;
        } else if (g_state == ST_MENU_) {
            if (g_menu_mode == 1) g_menu_mode = 0;
            else g_state = g_menu_prev;
        }
    }

    if (g_state == ST_MENU_) {
        /* The pause screen is the same instrument as everything else: dark,
         * and a few lines of the machine's own lettering. */
        static int hold;
        char v[40];
        int i2;
        /* Not while the slider has the keys. The list is not on screen then,
         * so a W or an S moved the highlight where nobody could see it -- and
         * backing out of the volume left you standing on a different item than
         * the one you went in on, with EXIT one Enter away. */
        if (g_menu_mode == 1) hold = 0;
        else if (in->fwd  && !hold) { g_menu_sel = (g_menu_sel + 2) % 3; hold = 1; }
        else if (in->back && !hold) { g_menu_sel = (g_menu_sel + 1) % 3; hold = 1; }
        else if (!in->fwd && !in->back) hold = 0;

        if (g_menu_mode == 1) {
            if (in->left)  audio_set_volume(audio_get_volume() - dt * 0.6f);
            if (in->right) audio_set_volume(audio_get_volume() + dt * 0.6f);
            if (in->enter || in->ping) g_menu_mode = 0;
        } else if (in->enter || in->ping) {
            if (g_menu_sel == 0) g_state = g_menu_prev;
            else if (g_menu_sel == 1) g_menu_mode = 1;
            else g_quit = 1;
        }

        glClearColor(0.008f, 0.010f, 0.014f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        g_hud_n = 0;
        g_hud_cache[0] = 0;
        {   /* The words hold still and only the marker moves. Folding the
             * "> " into the label centred the arrow along with it, so every
             * item shuffled sideways the moment it was picked. Three lines
             * evenly spaced -- except with the slider open, where the two
             * extra lines need the room. */
            float mid  = (g_menu_mode == 1) ? 0.02f : 0.02f;
            float bot  = (g_menu_mode == 1) ? -0.44f : -0.26f;
            float ex   = -0.20f;             /* left edge of the labels */
            float mark = -0.27f;             /* and where the arrow sits */
            hud_line("CONTINUE", 0.13f, ex, 0.30f,
                     g_menu_sel == 0 ? 0.95f : 0.45f, -1);
            if (g_menu_mode == 1) {
                int bars = (int)(audio_get_volume() * 10.0f + 0.5f);
                v[0] = 0;
                for (i2 = 0; i2 < 10; i2++) v[i2] = (i2 < bars) ? '#' : '-';
                v[10] = 0;
                {   char lbl[64];
                    sprintf(lbl, "VOLUME  %s", v);
                    hud_line(lbl, 0.11f, ex, mid, 0.95f, -1);
                }
                hud_line("A / D TO ADJUST", 0.07f, ex, -0.20f, 0.5f, -1);
            } else {
                hud_line("SETTING", 0.13f, ex, mid,
                         g_menu_sel == 1 ? 0.95f : 0.45f, -1);
            }
            hud_line("EXIT", 0.13f, ex, bot,
                     g_menu_sel == 2 ? 0.95f : 0.45f, -1);
            if (g_menu_mode != 1)
                hud_line(">", 0.13f, mark,
                         g_menu_sel == 0 ? 0.30f : g_menu_sel == 1 ? mid : bot,
                         0.95f, -1);
        }
        if (g_hud_n > 0) {
            glBindBuffer(GL_ARRAY_BUFFER, g_hvbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0,
                            (GLsizeiptr)(g_hud_n * (int)sizeof(Point)), g_hud);
            glUseProgram(g_prog);
            glUniform1f(u_time, now);
            glUniform1f(u_monster, 0.0f);
            glUniform1f(u_persist, 1.0f);
            glUniform1f(u_fade, 1.0f);
            glUniform1f(u_grey, 0.0f);
            glUniform1f(u_flat, 1.0f);
            glUniform1f(u_ink, 0.0f);
            glUniform1f(u_base, 0.9f);
            glBindVertexArray(g_hvao);
            glDrawArrays(GL_POINTS, 0, g_hud_n);
            glUniform1f(u_base, 0.0f);
            glUniform1f(u_flat, 0.0f);
        }
        return;
    }

    if (g_state == ST_ROAD) {
        /* Through the door there is no room - there is distance. You are
         * carried down it, the hall repeating past you, and the world takes
         * about four seconds to go entirely white. */
        float wt;
        g_road += dt;
        g_pz   -= 6.8f * dt;
        g_yaw  *= (float)exp(-dt * 3.0f);
        g_pitch *= (float)exp(-dt * 3.0f);
        wt = smoothstep01(1.1f, 3.6f, g_road);

        glClearColor(wt, wt, wt, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        {
            float bra[48], brb[48];
            int b2;
            basis(g_yaw, g_pitch, f, r, u);
            for (b2 = 0; b2 < 16; b2++) {
                bra[b2*3+0] = g_br[b2][0]; bra[b2*3+1] = g_br[b2][1]; bra[b2*3+2] = g_br[b2][2];
                brb[b2*3+0] = g_br[b2][3]; brb[b2*3+1] = g_br[b2][4]; brb[b2*3+2] = g_br[b2][5];
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
            glUniform1f(c_light, 1.0f);
            glUniform1f(c_wet, 0.0f);
            glUniform1f(c_room, 1.0f);
            glUniform1f(c_road, 1.0f);
            glUniform1f(c_white, wt);
            glUniform3fv(c_bra, 16, bra);
            glUniform3fv(c_brb, 16, brb);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glBindVertexArray(g_wvao_full);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBlendFunc(GL_ONE, GL_ONE);
            glUniform1f(c_road, 0.0f);
            glUniform1f(c_white, 0.0f);
        }
        if (g_road > 4.1f) { g_state = ST_WAKE; g_wake = 0.0f; }
        return;
    }

    if (g_state == ST_WAKE) {
        /* Nine seconds, and the game runs backwards through everything it has
         * been. The cave dims out, a room marches in behind it, an eyelid
         * opens on the room, and the light that arrives is not one you made.
         *
         * Each beat overlaps the next, so nothing cuts. */
        float w      = g_wake;
        float sink   = 0.0f;                                   /* the road took the cave */
        float lamp   = 1.0f - smoothstep01(0.235f, 0.245f, w); /* the last room but one */
        float lampb  = 1.0f - smoothstep01(0.150f, 0.205f, w); /* its bulb dying */
        float room   = smoothstep01(0.30f, 0.52f, w);
        float open   = smoothstep01(0.36f, 0.72f, w);          /* the eye */
        float bright = smoothstep01(0.60f, 1.00f, w);          /* the light */
        float sharp  = smoothstep01(0.46f, 0.94f, w);
        char line[48];

        /* It keeps counting past one. Every smoothstep above already clamps,
         * so nothing moves any more -- but the overflow is a clock the last
         * screen can be paced by, and the last screen is where the game
         * finally says what it has been counting. */
        g_wake += dt * 0.082f;

        glClearColor(0.01f, 0.012f, 0.018f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        if (room > 0.004f || lamp > 0.004f) {
            glUseProgram(g_wake_prog);
            glUniform1f(w_lamp, lamp);
            glUniform1f(w_lampb, lampb);
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
            /* The one thing the game has been careful not to say. You have
             * watched that number climb for the whole run and it was never
             * metres -- it is how long you were gone, and every voice down
             * there was a clock you could have checked against it. Say it
             * once, plainly, and then say how much of it you actually heard. */
            {   int secs = (int)(g_best_depth < 0.0f ? 0.0f : g_best_depth);
                /* The eye is only wide enough to read through for the last
                 * four seconds of the wake, which is not long enough for
                 * three lines. So the vitals climb while it opens, and the
                 * rest waits until the game has stopped moving and turns
                 * over slowly, for as long as it takes to be read. */
                if (w < 1.0f)
                    sprintf(line, "BIS %d", 40 + (int)(bright * 60.0f));
                else {
                    int k = (int)((w - 1.0f) * 4.6f) % 3;
                    if (k == 0)      sprintf(line, "심정지 %d분 %02d초", secs / 60, secs % 60);
                    else if (k == 1) sprintf(line, "들은 목소리 %d / %d", g_heard_n, BRANCHES);
                    else             sprintf(line, "CLICK TO BEGIN AGAIN");
                }
            }
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
        if (now > g_title_pulse) title_sweep(now);
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
                /* the floor is low so the sweep has something to be brighter
                 * than: at 0.30 the resting word already clipped white and
                 * the wave crossing it did not read */
                glUniform1f(u_base, 0.17f + 0.55f * sw);
                glUniform1f(u_grey, 0.0f);
            }
            glUniform1f(u_monster, 0.0f);
            /* 0, not 1: with persistence on, every letter sat at the 0.95
             * a remembered surface gets, which is above anything uBase or the
             * wavefront could add -- so the swell this code has always claimed
             * to do has never once been visible. Off, the word is exactly what
             * uBase says it is, and the sweep can be seen crossing it. */
            glUniform1f(u_persist, 0.0f);
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

    /* move -- except while the shock is carrying you back up the tunnel */
    if (g_back <= 0.0f) {
        if (in->fwd)   { mx += f[0]; my += f[1]; mz += f[2]; }
        if (in->back)  { mx -= f[0]; my -= f[1]; mz -= f[2]; }
        if (in->right) { mx += r[0]; my += r[1]; mz += r[2]; }
        if (in->left)  { mx -= r[0]; my -= r[1]; mz -= r[2]; }
    }

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
            if (now >= g_stroke_next) {   /* an arm pulling through it */
                audio_stroke();
                g_stroke_next = now + 0.78f + hash1(now * 7.0f) * 0.25f;
            }
        }
        g_vy += SWIM_LIFT * dt;
        g_vx *= SWIM_DRAG; g_vy *= SWIM_DRAG; g_vz *= SWIM_DRAG;
        try_move(g_vx * dt, g_vy * dt, g_vz * dt);
        g_travelled += (float)sqrt((g_px-ox)*(g_px-ox) + (g_py-oy)*(g_py-oy)
                                 + (g_pz-oz)*(g_pz-oz));
    } else if (len > 0.0001f) {
        float s = MOVE_SPEED * dt / len;
        {   float ox = g_px, oy = g_py, oz = g_pz;
            float moved;
            try_move(mx * s, my * s, mz * s);
            moved = (float)sqrt((g_px-ox)*(g_px-ox) + (g_py-oy)*(g_py-oy)
                              + (g_pz-oz)*(g_pz-oz));
            g_travelled += moved;
            /* Your feet, everywhere you are still walking on something.
             * They dry out over about a dozen paces after the water. */
            if (g_wetfeet > 0.0f) {
                g_wetfeet -= moved / 14.0f;
                if (g_wetfeet < 0.0f) g_wetfeet = 0.0f;
            }
            g_step_acc += moved;
            if (g_step_acc > 1.05f) {
                g_step_acc = 0.0f;
                audio_step(g_wetfeet);
                g_steps++;
            }
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
            audio_splash();               /* going under, and coming back out */
            /* and you climb out of it soaked */
            if (!wet) { g_wetfeet = 1.0f; g_step_acc = 0.9f; }
            g_vx = g_vy = g_vz = 0.0f;
        }
    }
    if (g_wet) motes_refresh(now);
    if (-g_pz > GATE_1 - 30.0f && -g_pz < GATE_1 + 24.0f) water_refresh(now);

    if (-g_pz > g_best_depth) g_best_depth = -g_pz;

    {   /* thresholds passed, and the one that ends it */
        static const float GATES[4] = { GATE_1, GATE_2, GATE_3, GATE_END };
        while (g_stage < 4 && -g_pz >= GATES[g_stage]) {
            g_stage++;
            g_stage_flash = 1.0f;
            /* three metres past it, so arriving does not re-cross it */
            g_check  = GATES[g_stage - 1] + 3.0f;
            g_gate_t = 2.8f;
            g_gate_n = g_stage;
            if (g_stage >= 4) {
                audio_hum();                   /* the exit is somewhere in this */
            } else if (g_stage == 1) {
                /* Gate one is the waterline. No interlude - you go in.
                   The plunge is a shove downward, a flash of foam, and the
                   ears closing over; depth_is_wet picks it up from there. */
                g_dive = 1.0f;
                g_vx = 0.0f; g_vy = -6.5f; g_vz = -2.0f;
                g_flash = 0.35f;
                g_note = "\xEC\x82\xB0\xEC\x86\x8C\xED\x8F\xAC\xED\x99\x94\xEB\x8F\x84\xEA\xB0\x80 \xEB\x96\xA8\xEC\x96\xB4\xEC\xA0\xB8\xEC\x9A\x94";
                g_note_t = 3.4f;
            } else {
                /* Gates two and three are ambushes. You come up out of the
                 * water (or round the bend), sound the room, and it is full
                 * of them - and behind you is no way back. What saves you is
                 * not yours: the crash team fires, and in here that arrives
                 * as a wave that burns the horde down and lights the way. */
                int hi;
                float ang0 = (float)atan2(g_px - 0.0f, 1.0f);
                g_ev = 1; g_ev_t = 1.55f;
                g_mon_count = MAX_MON;
                for (hi = 0; hi < MAX_MON; hi++) {
                    Monster *hm = &g_mon[hi];
                    float a = ang0 + (-0.9f + 1.8f * (float)hi / (float)(MAX_MON - 1));
                    float pos[3], nn[3];
                    wall_spot(g_pz - 7.0f - hash1((float)hi * 3.3f) * 9.0f, a, pos);
                    hm->x = pos[0]; hm->y = pos[1]; hm->z = pos[2];
                    cave_normal(hm->x, hm->y, hm->z, nn);
                    hm->nx = nn[0]; hm->ny = nn[1]; hm->nz = nn[2];
                    hm->seed = 11.7f + (float)hi * 1.618f;
                    mon_make(hm, hi % 3, depth_k(g_pz));
                    hm->tx = g_px; hm->ty = g_py; hm->tz = g_pz;
                    hm->state = MON_CHARGING;
                    hm->timer = 8.0f; hm->travel = 0.0f;
                    hm->dash = 0.0f; hm->moving = 1; hm->group = hi & 1;
                    {   int q;
                        for (q = 0; q < 8; q++) {
                            hm->foot[q][0] = hm->x - nn[0] * WALL_HUG;
                            hm->foot[q][1] = hm->y - nn[1] * WALL_HUG;
                            hm->foot[q][2] = hm->z - nn[2] * WALL_HUG;
                            hm->ft[q] = 1.0f;
                        }
                    }
                    if (hi < 3) audio_roar(hm->type);
                }
            }
        }
    }
    if (g_stage_flash > 0.0f) g_stage_flash -= dt * 0.34f;   /* the card's length */
    g_stagef += ((float)g_stage - g_stagef) * (1.0f - (float)exp(-dt * 1.6f));
    if (g_dive > 0.0f) g_dive -= dt * 0.8f;
    if (g_note_t > 0.0f) g_note_t -= dt;
    g_pulse -= dt * 1.7f; if (g_pulse < 0.0f) g_pulse = 0.0f;

    /* the ambush, and the shock that answers it */
    if (g_ev == 1) {
        g_ev_t -= dt;
        if (g_ev_t <= 0.0f) {
            int di;
            g_ev = 2; g_ev_t = 3.6f;
            g_shock++;
            g_flash = 1.4f;
            audio_defib();
            emit_defib(now);
            for (di = 0; di < g_mon_count; di++)
                if (g_mon[di].state == MON_CHARGING || g_mon[di].state == MON_WAKING) {
                    g_mon[di].state = MON_BURST;
                    g_mon[di].timer = BURST_TIME;
                }
        }
    } else if (g_ev == 2) {
        g_ev_t -= dt;
        if (g_ev_t <= 0.0f) g_ev = 0;
    }
    g_shockf += ((float)g_shock - g_shockf) * (1.0f - (float)exp(-dt * 0.55f));

    /* after the first shock the heart paces you toward the door */
    if (g_shock >= 1 && g_ev == 0 && now >= g_guide_next) {
        float ddx = g_px, ddz = g_pz + WAKE_Z;
        float dd  = (float)sqrt(ddx * ddx + ddz * ddz);
        emit_guide(now);
        g_pulse = 1.0f;
        g_guide_next = now + 1.2f + dd * 0.045f;
        if (dd < 26.0f && g_note_t <= 0.0f) {
            g_note = "\xEB\x93\xA4\xEB\xA0\xA4? \xEC\x9D\xB4\xEC\xAA\xBD\xEC\x9D\xB4\xEC\x95\xBC";
            g_note_t = 2.6f;
        }
    }

    /* the door at the end of the hall: an endless road behind it */
    if (-g_pz >= WAKE_Z - 1.6f && in->ping) {
        g_state = ST_ROAD;
        g_road  = 0.0f;
        audio_beep();
        return;
    }

    /* ping - not during the ambush; that moment is not yours to light */
    if (g_ev != 1 && in->ping && now >= g_ping_ready) {
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

    if (g_back > 0.0f) {
        /* Two seconds of being somewhere else. First it is on you: whatever
         * reached you comes the rest of the way in and takes the screen,
         * because a life leaving as a number in the corner is not a thing
         * that happened to anybody. Then the team downstairs has you, the
         * room goes white, and you are back at the last threshold you got
         * past -- which is the only reason the map you built is worth
         * anything. */
        g_back -= dt;
        if (g_maul_i >= 0) {
            Monster *m = &g_mon[g_maul_i];
            float e = 1.0f - g_back / 1.9f;
            if (e > 1.0f) e = 1.0f;
            e = smoothstep01(0.0f, 0.34f, e);       /* in fast, then it stays */
            /* It reaches you at about a metre, which is already close, so
             * the last of it has to be big rather than far: it comes to
             * arm's length and opens. The feet stay where they were standing
             * and the legs stretch back off the edges of the screen, which
             * is the part that reads as being held. */
            {   /* The head sits a quarter of a body ahead of the origin, so
                 * aiming the origin at your face buries the head behind it.
                 * Aim the head instead: it ends up where your eye is, and
                 * the rest of it closes over from there. */
                float sc  = g_maul_sc * (1.0f + 3.2f * e);
                float tgt = 0.16f + 0.26f * sc;
                m->scale = sc;
                m->x = g_maul_x + (g_px + f[0] * tgt - g_maul_x) * e;
                m->y = g_maul_y + (g_py + f[1] * tgt - g_maul_y) * e;
                m->z = g_maul_z + (g_pz + f[2] * tgt - g_maul_z) * e;
            }
            /* facing you, so what fills the screen is the front of it */
            m->dx = -f[0]; m->dy = -f[1]; m->dz = -f[2];
        }
        if (g_back <= 0.0f) {
            g_back = 0.0f;
            g_maul_i = -1;
            respawn(now);
        }
    } else for (i = 0; i < g_mon_count; i++) {
        if (mon_step(&g_mon[i], dt, now)) {
            g_lives--;
            audio_hit();
            if (g_lives <= 0) {
                g_state = ST_FLATLINE;
                g_flat  = 0.0f;
                audio_flatline();
            } else {
                g_back  = 1.9f;
                g_flash = 1.7f;
                g_maul_i  = i;
                g_maul_x  = g_mon[i].x;
                g_maul_y  = g_mon[i].y;
                g_maul_z  = g_mon[i].z;
                g_maul_sc = g_mon[i].scale;
                g_gate_t  = 0.0f;   /* a threshold card would be talking over it */
                audio_roar(g_mon[i].type);
                audio_defib();          /* what actually pulls you back */
            }
            break;
        }
    }

    g_flash -= dt * 1.6f;
    if (g_flash < 0.0f) g_flash = 0.0f;

    /* draw */
    {   /* A threshold is a change of place, so the place changes: the dark
         * the whole game is built on blanches to a cold ward white and comes
         * back over about three seconds, under the card. Small enough at the
         * old 0.05 that you could cross a gate and not know you had. */
        float gf = g_stage_flash * g_stage_flash;   /* holds, then lets go */
        glClearColor(0.008f + g_flash * 0.30f + gf * 0.34f
                            + (g_dive > 0.0f ? g_dive * 0.10f : 0.0f),
                     0.012f + gf * 0.38f,
                     0.020f + gf * 0.42f, 1.0f);
    }
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
        float sm  = surface_mix(g_pz);
        float lit = 0.0f;
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
            {   /* One shock buys a sheen; the second buys the light.
                 * Between soundings the dark still rules until the end. */
                float ls = smoothstep01(1.35f, 2.0f, g_shockf);
                float dim = sm * 0.05f + smoothstep01(0.4f, 1.1f, g_shockf) * 0.10f;
                lit = dim > ls ? dim : ls;
                glUniform1f(c_light, lit);
            }
            glUniform1f(c_wet, g_wet ? 1.0f : 0.0f);
            {   /* each shock hardens the rock into hospital */
                float rd2 = smoothstep01(GATE_3 + 8.0f, GATE_END - 4.0f, -g_pz);
                float rs  = smoothstep01(0.3f, 2.0f, g_shockf);
                glUniform1f(c_room, rd2 > rs ? rd2 : rs);
            }
            glUniform3fv(c_bra, 16, bra);
            glUniform3fv(c_brb, 16, brb);
            glUniform1f(c_pulse, g_pulse);
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
        /* and the points step back as it arrives. Twice over: once for the
         * rock resolving, and again for the lights coming on. Under the ward
         * lights a sounding returns a hundred thousand points onto a wall you
         * can already see, and they read as dirt on the lens rather than as a
         * map. Letting them go is the whole turn of the game -- the instrument
         * you have leaned on for eight minutes stops being what you see with,
         * because there was never anything to sound. */
        glUniform1f(u_fade, (1.0f - sm * 0.82f) * (1.0f - lit * 0.76f));
        {   /* grey rises with depth, but a crossed gate pulls it ahead, so
             * the change is felt at the boundary and not only along the way */
            float gd = smoothstep01(GATE_2, GATE_3 + 16.0f, -g_pz);
            float gs = smoothstep01(0.3f, 1.9f, g_shockf);
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

    if (-g_pz > GATE_1 - 30.0f && -g_pz < GATE_1 + 24.0f) {
        /* the pool, glinting on its own */
        glUniform1f(u_persist, 0.0f);
        glUniform1f(u_base, g_wet ? 0.10f : 0.30f);
        glBindVertexArray(g_wtvao);
        glDrawArrays(GL_POINTS, 0, WATER_PTS);
        glUniform1f(u_base, 0.0f);
    }

    /* the wave itself, crossing open air. Visible only while the front is on
     * it, so it sweeps through and leaves the map exactly as it found it. */
    if (g_wcount > 0) {
        glUniform1f(u_persist, 0.0f);
        glBindVertexArray(g_wvao);
        glDrawArrays(GL_POINTS, 0, g_wcount);
    }

    /* Everything the sounding drew dims under the lights; the thing walking
     * towards you does not. In the ward it is the only thing on the screen
     * that still has to be read off a point cloud, which is worse. */
    glUniform1f(u_fade, 1.0f);
    glUniform1f(u_persist, 0.0f);
    glBindVertexArray(g_mvao);
    for (i = 0; i < g_mon_count; i++) {
        if (!mon_visible(&g_mon[i])) continue;
        mon_emit_points(&g_mon[i], now);
        /* 1, 2, 3: the shader gives each its own red */
        glUniform1f(u_monster, 1.0f + (float)g_mon[i].type);
        glDrawArrays(GL_POINTS, 0, MON_POINTS);
    }
    glUniform1f(u_monster, 0.0f);

    if (g_back > 1.15f) {
        /* while it has you there is nothing else on the screen */
        g_hud_n = 0;
        g_hud_cache[0] = 0;
    } else if (g_gate_t > 0.0f || g_back > 0.0f) {
        /* A threshold has to land as an event, not as a digit changing in
         * the corner. The readout gets out of the way and the screen says
         * where you are, in the same lettering the monitor uses. */
        static const char *NAME[5] = { "", "수몰 구간", "병원이 되기 시작한다",
                                       "백룸", "리미널 홀" };
        char big[24];
        int  st = (g_back > 0.0f) ? g_stage : g_gate_n;
        if (st < 0) st = 0; if (st > 4) st = 4;
        sprintf(big, "STAGE %d", st + 1);
        hud_build_wake(big, (g_back > 0.0f) ? "다시 내려간다" : NAME[st]);
        if (g_gate_t > 0.0f) {
            /* the card is a still picture; the hand is not, so it has to be
               rebuilt every frame rather than served from the cache */
            g_hud_cache[0] = 0;
            hud_hand((float)st * 0.25f, 2.8f - g_gate_t);
            g_gate_t -= dt;
        }
    } else {   /* the readout */
        char left[40], right[40];
        const char *hint = (g_note_t > 0.0f) ? g_note
                         : (g_ev == 2)
                         ? (g_shock >= 2 ? "\xEB\x8F\x8C\xEC\x95\x84\xEC\x99\x80, \xEC\xa1\xb0\xea\xb8\x88\xeb\xa7\x8c \xeb\x8d\x94"
                                         : "\xEC\xA0\x95\xEC\x8B\xA0 \xEC\xB0\xA8\xEB\xA0\xA4")
                         : ((-g_pz >= WAKE_Z - 7.0f) ? "CLICK TO OPEN"
                         : (g_has_moved ? "" : "WASD TO MOVE"));
        {   /* Past the second shock the readout starts slipping. For about
             * half a second at a time it shows what it has actually been
             * counting, and then it is metres again -- often enough to be
             * seen, rarely enough to be sure of. Nobody explains it. The
             * number simply contradicts itself in front of you, and the
             * voices in the side passages have been saying the same thing
             * out loud since the two-minute mark. */
            float dep  = -g_pz < 0.0f ? 0.0f : -g_pz;
            int   secs = (int)dep;
            float slip = smoothstep01(GATE_3, GATE_END, dep);
            if (slip > 0.02f
                && hash1((float)floor(now * 1.7f) + 11.0f) < slip * 0.17f)
                sprintf(left, "STAGE %d   %d:%02d", g_stage + 1,
                        secs / 60, secs % 60);
            else
                sprintf(left, g_wet ? "STAGE %d   %5.1f M  SUBMERGED"
                                    : "STAGE %d   %5.1f M", g_stage + 1, dep);
        }
        sprintf(right, "LIFE %s", g_lives >= 3 ? "* * *"
                                : (g_lives == 2 ? "* *" : (g_lives == 1 ? "*" : "-")));
        hud_build(left, right, hint);
    }

    if (g_hud_n > 0) {          /* readout or card, whichever was just built */
        glUniform1f(u_monster, 0.0f);
        glUniform1f(u_persist, 1.0f);
        glUniform1f(u_fade, 1.0f);
        /* the card arrives lit rather than waiting to be sounded */
        glUniform1f(u_base, (g_gate_t > 0.0f || g_back > 0.0f) ? 0.95f : 0.0f);
        glUniform1f(u_flat, 1.0f);
        glBindVertexArray(g_hvao);
        glDrawArrays(GL_POINTS, 0, g_hud_n);
        glUniform1f(u_flat, 0.0f);
        glUniform1f(u_base, 0.0f);
    }
}

/* -shot only: empty the cave. A scripted walk has no idea it is being
 * hunted, so without this every long run ends the same way and never gets
 * far enough to photograph what it was sent to photograph. */
void game_debug_calm(void)
{
    g_calm = 1;
    g_mon_count = 0;
}

/* -shot only: aim. The spawn heading follows the passage, which in the hall
 * means facing a row wall three metres off -- so every photograph of the
 * place came back as one flat surface, and no amount of detail on it could
 * be told from no detail at all. The long views in there run across the
 * lanes, not down them. */
void game_debug_yaw(float deg)
{
    g_yaw = deg * 0.0174532925f;
}

/* -shot only: drop a spider right in front and wake it, so the thing can be
 * looked at without waiting for one to come along. */
void game_debug_spider(float now, int type)
{
    Monster *m = &g_mon[0];
    float pos[3], n[3];
    g_mon_count = 1;
    m->seed = 4.0f + (float)type;
    /* straight ahead and at eye level: the point of this is to look at it,
     * and on the ceiling six metres up it was out of frame */
    wall_spot(g_pz - 3.2f, 3.1415927f, pos);
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

/* -shot only: steer.
 *
 * The harness walks a fixed heading, which is fine in a tunnel and useless in
 * the hall -- seven metres in it meets a row wall and stands there. This looks
 * a few metres ahead along a fan of headings, keeps the ones with air in them,
 * and picks whichever gets nearest the way out. In the hall the way out is the
 * gap in the next row of wall, which it reads out of the field rather than
 * finding; in the tunnel it is simply the door.
 *
 * What it is for: putting a capture somewhere a fixed heading cannot reach.
 * What it is NOT: proof the hall can be walked. It only ever goes forward, so
 * a corner it turns into is a corner it stays in, and in practice it gets a
 * few rows into the hall before it wedges. Traversal is mazecheck's job, and
 * for a capture deeper than that, spawn there with -depth. */
void game_debug_autopilot(float dt)
{
    static const float sweep[] = { 0.0f, -0.35f, 0.35f, -0.70f, 0.70f,
                                  -1.05f, 1.05f, -1.45f, 1.45f, -1.95f,
                                   1.95f, -2.60f, 2.60f };
    static float ox, oz, watch, panic, lean;
    int   n = (int)(sizeof sweep / sizeof sweep[0]);
    float best = -1e9f, best_yaw = g_yaw, turn, lim;
    float gx, gz, tx, tz, gl, d;
    int   i, s;

    if (g_state != ST_PLAY) return;

    /* Where it got to, not how far it walked: circling a pillar at full speed
     * covers plenty of ground and arrives nowhere. When the last stretch ends
     * up where it started, stop aiming for a while and just get out. */
    watch += dt;
    if (watch > 1.4f) {
        float net = (float)sqrt((g_px - ox) * (g_px - ox) + (g_pz - oz) * (g_pz - oz));
        if (net < 1.0f) {
            if (panic <= 0.0f) lean = (lean > 0.0f) ? -1.0f : 1.0f;
            panic = 2.6f;
        }
        ox = g_px; oz = g_pz; watch = 0.0f;
    }
    if (panic > 0.0f) panic -= dt;

    d = -g_pz;
    if (d > GATE_2 + 6.0f && d < WAKE_Z - 4.0f) {
        float ri = (float)floor(d / 7.0f) + 1.0f;      /* the wall ahead of us */
        float wz = ri * 7.0f;
        if (wz - d < 0.6f) { ri += 1.0f; wz = ri * 7.0f; }
        gx = tunnel_gapx(ri);
        /* line up with the gap first, walk through it second: cutting the
         * corner just means arriving at the wall beside the opening */
        gz = ((float)fabs(g_px - gx) > 0.7f) ? g_pz : -(wz + 1.4f);
    } else {
        tunnel_centre(-WAKE_Z + 4.0f, &gx, &gl);
        gz = -WAKE_Z + 2.0f;
    }
    tx = gx - g_px;
    tz = gz - g_pz;
    gl = (float)sqrt(tx * tx + tz * tz);
    if (gl > 0.001f) { tx /= gl; tz /= gl; }

    for (i = 0; i < n; i++) {
        float y     = g_yaw + sweep[i];
        float fx    =  (float)sin(y);
        float fz    = -(float)cos(y);
        float reach = 0.0f;
        float score;
        for (s = 1; s <= 10; s++) {
            float t = (float)s * 0.55f;
            if (cave_sdf(g_px + fx * t, g_py, g_pz + fz * t) < PLAYER_R) break;
            reach = t;
        }
        /* Past a few metres more air ahead is not a better way to go: the hall
         * is full of long empty rows that lead nowhere. */
        if (reach > 2.8f) reach = 2.8f;
        if (panic > 0.0f)
            score = reach * 1.4f + lean * sweep[i] * 2.5f + (fx * tx + fz * tz) * 0.4f;
        else
            score = reach * 0.9f + (fx * tx + fz * tz) * 3.0f
                  - (float)fabs(sweep[i]) * 0.12f;
        if (score > best) { best = score; best_yaw = y; }
    }

    turn = best_yaw - g_yaw;
    lim  = 2.4f * dt;                    /* a head turns; it does not snap */
    if (turn >  lim) turn =  lim;
    if (turn < -lim) turn = -lim;
    g_yaw += turn;
}

int   game_point_count(void) { return g_count; }
float game_depth(void)       { return -g_pz; }
int   game_lives(void)       { return g_lives; }
int   game_monsters(void)    { return g_mon_count; }
int   game_stage(void)       { return g_stage; }
int   game_quit(void)        { return g_quit; }
int   game_state(void)       { return g_state; }
float game_px(void)          { return g_px; }
float game_py(void)          { return g_py; }
float game_pz(void)          { return g_pz; }
float game_travelled(void)   { return g_travelled; }

/* The numbers a scripted run needs to prove anything about the back half of
 * the game. They were a throwaway fork once; keeping them costs a few bytes
 * and saves rebuilding it every time something in here has to be checked. */
float game_shockf(void)      { return g_shockf; }
/* The three rolls that ARE the cave. game_init's seed does not identify it:
 * the click that starts a run rerolls, so the only way to say which cave a
 * capture walked -- or to hand it to mazecheck -- is to read them out. */
void  game_cave(float *seed, float *wander, float *rough)
{ *seed = g_seed; *wander = g_wander; *rough = g_rough; }
/* how well the player fits where they are standing: PLAYER_R or more is
 * clear of the rock, less means partly inside it */
float game_fit(void)         { return cave_sdf(g_px, g_py, g_pz); }
float game_pulse(void)       { return g_pulse; }
float game_note_t(void)      { return g_note_t; }
int   game_event(void)       { return g_ev; }
int   game_steps(void)       { return g_steps; }
int   game_heard(void)       { return g_heard_n; }
int   game_menu_sel(void)    { return g_menu_sel; }
int   game_menu_mode(void)   { return g_menu_mode; }
