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
/* The rooms stop here; from CORR_Z to WAKE_Z it is one straight corridor
 * with the door at the far end, so the last twenty metres are a walk toward
 * something you can see rather than one more turn. */
#define WAKE_Z        (GATE_END + 66.0f)
#define CORR_Z        (WAKE_Z - 20.0f)
#define LAMP_Z        (WAKE_Z + 26.0f)   /* the room past the door */
#define GATE_R           9.5f  /* how wide a threshold chamber opens */
#define GATE_W           5.0f  /* and how long it runs
 */

/* The debug spawn used to put a monster off the edge of the frame, so nobody
 * had ever actually looked at one: 2,800 points over eight double-jointed legs
 * is six specks per ring, and what came back on a sounding was a dotted
 * outline. A shape you have to finish in your head is not frightening, it is
 * a puzzle. Doubled, and the extra goes into the parts that carry the horror
 * -- the mass of the body, the hung head, and legs solid enough to be legs.
 * 120 KB of RAM, nothing on disk, and the frame already draws 1.2 M points. */
/* The most any one of them uses is 3,724 (ribs and hands push the walkers
 * past the listener). Everything above what is written gets gain 0 and is
 * still handed to the card, so the slack is drawn and thrown away. */
#define MON_POINTS      3900
/* Eight legs at ten points a ring over fifty-eight rings is 4,640 of the
 * 5,500 this animal is made of -- six points of leg for every one of body.
 * Drawing the body alone proved it: the ribcage, the spine and the split head
 * are all there and all perfectly legible, and with the legs on they vanish
 * into the tangle. Changing the body did almost nothing to the picture (1,429
 * pixels in 921,600) because the body was never what you were looking at.
 * Legs thinner and sparser, body richer, and now the ribs carry the shape. */
#define LEG_TUBE           7   /* points around the curve at each sample */
#define LEG_RINGS         48   /* samples along it */
/* Points add. Twice as many of them at the old per-point gain turned the whole
 * animal into one solid white blot with no legs, no head and no hollow under
 * the body -- brighter, and less of a monster. This gives the extra density
 * back as shape instead of as light. */
#define MON_GAIN       0.47f
#define MAX_MON           12
#define MON_KILL_DIST   1.05f
#define WALL_HUG       0.34f   /* how far off the rock it rides */
#define WEAVE_RATE      1.8f   /* how fast it swings side to side */
#define WEAVE_AMT      0.52f   /* and how far - too much and it circles instead of closing */
#define STRIDE         0.44f   /* how far a foot drifts before it is picked up */
#define SWING_TIME     0.11f   /* and how long the step through the air takes */
#define DASH_ON        0.22f   /* it runs in bursts, not at a constant speed */
#define DASH_OFF       0.15f
/* In the cave a thing gives up after this far and comes apart. In the
 * building nothing comes apart: it follows until the light stops it or you
 * are through the door. */
/* In the cave a thing gives up after this far and comes apart. In the
 * building nothing comes apart: it follows until the light stops it or you
 * are through the door. */
#define MON_RANGE      24.0f   /* how far it will chase before it comes apart */
#define BURST_TIME     0.55f   /* and how long it takes to scatter */

/* --- point cloud -------------------------------------------------------- */

typedef struct { float x, y, z, reveal, gain; } Point;   /* 20 bytes */

static Point *g_pts;
static int    g_count;   /* how many of the ring are worth drawing */
static int    g_head;    /* where the next mark goes */
static Point *g_wpts;          /* the wave in flight, a ring that nobody reads back */
static int    g_wcount;
static GLuint g_vao, g_vbo, g_wvao, g_wvbo, g_mvao, g_mvbo, g_hvao, g_hvbo,
              g_prog, g_wake_prog, g_wvao_full;
static GLint  u_vp, u_cam, u_time, u_monster, u_persist, u_flat, u_base, u_ink;
static GLint  w_run;
static GLint  w_res, w_time, w_open, w_bright, w_sharp, w_lamp, w_lampb;
static GLuint g_cave_prog;
static GLint  c_res, c_cam, c_fwd, c_right, c_up, c_seed, c_wander,
              c_rough, c_time, c_light, c_wet, c_room, c_road, c_white, c_wakez,
              c_ward, c_hand,
              c_pulse, c_hospy, c_blink, c_corrx, c_dooru, c_lampout,
              c_monn, c_monp, c_mond, c_bra, c_brb;
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
static float g_hosp_y;      /* the one height the hospital is built at */
static float g_corr_x;      /* the corridor, and the door, run down this line */
static float g_door_open;   /* you opened it; it stays open */
static float g_lamp_out;    /* the bulb, once you are far enough in */
static float g_lamp_t;
static float g_beat;        /* phase of the heart, 0..1 per beat */
static float g_bpm;         /* 50 at the far end, 80 at the door */
static float g_blink;       /* 1 on the beat, falling - the lights, and the blow */
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
    {   /* The side passages are cave, and where the cave has become corridor
         * they were still being cut at full width -- a three and a half metre
         * round tube punched through a suspended ceiling and a tiled floor,
         * which is the one thing down there that cannot be read as a building
         * however well it is lit. Narrowed as the room takes over. Still open,
         * so the voices at their tips are still reachable and still worth a
         * sounding; a hole you go through rather than a bubble the corridor
         * turns out to be inside of.
         *
         * Off the sampled depth and nothing else, because the shader carves
         * these too and the two have to agree -- the pool room spent a while
         * being a square room in the points and a round chamber in the rock. */
        float rk = BRANCH_RAD * (1.0f - 0.42f
                 * smoothstep01(GATE_3 + 8.0f, GATE_END - 4.0f, -z));
        return rk - seg_dist(x, y, z,
                             g_br[i][0], g_br[i][1], g_br[i][2],
                             g_br[i][3], g_br[i][4], g_br[i][5]);
    }
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


/* --- the backrooms ------------------------------------------------------
 *
 * A grid of rooms seven metres on a side. Every cell keeps two bits: whether
 * the wall on its north side is open, and whether the wall on its west side
 * is open. Both come from the seed, but not freely - a cell always opens at
 * least one of them, so no cell is ever sealed off, and the north opening is
 * biased so the way out exists without being a corridor.
 *
 * That gives what the reference gives: rooms that repeat, turns that end in
 * nothing, and openings you can read at a glance because they are doorways
 * in flat walls rather than gaps between pillars.
 */
#define CELL     7.0f
#define WALL_T   0.22f
#define DOOR_W   1.35f

static int cell_open_n(int cx, int cz)     /* the wall at -z of this cell */
{
    float h;
    /* The first rows always let you in. The cave narrows to about two metres
     * as the building blends over it, so you arrive at one x with no room to
     * go sideways, and if that cell's north wall happened to be shut the whole
     * hospital was behind it -- about one seed in twenty, unwinnable, and it
     * would have looked exactly like the maze being hard. */
    if ((float)cz * CELL > -(GATE_2 + 26.0f)) return 1;
    h = hash1((float)cx * 13.31f + (float)cz * 7.77f + g_seed);
    return h < 0.62f;
}

static int cell_open_w(int cx, int cz)     /* the wall at -x of this cell */
{
    float h = hash1((float)cx * 5.19f - (float)cz * 11.03f + g_seed * 3.0f);
    if (h < 0.34f) return 1;
    if (!cell_open_n(cx, cz)) return 1;    /* never seal a cell completely */
    return 0;
}

/* Positive in air. A wall is a slab across the cell boundary with a doorway
 * cut out of it; the doorway sits a little off centre so the place does not
 * read as a lattice. */
static float rooms_air(float x, float y, float z, float floor_y)
{
    float slab = 1.55f - (float)fabs(y - floor_y - 1.30f);
    float air  = slab;
    int   cx   = (int)floorf(x / CELL);
    int   cz   = (int)floorf(z / CELL);
    int   k;

    /* A wall slab here has no extent along itself -- `fabs(z - bz) - WALL_T`
     * is the whole wall line, not this cell's piece of it -- and the doorway
     * is punched by a test on x. So which cell's doorway gets punched matters
     * absolutely, and the old loop ran both walls over both neighbours: the
     * -z plane was applied once carrying cell cx's doorway and once carrying
     * cell cx+1's, seven metres away, and the second copy closed the first.
     * Every wall in the building was solid across its whole length. Nothing
     * past the second gate could be walked to -- the last 270 metres of the
     * game, the hospital, the corridor, the door and the ending, all sealed
     * behind the first row of rooms.
     *
     * A wall's doorway belongs to the cell the point is in, so the -z walls
     * take their x from cx and only cx, and vary over the two z-edges; the
     * -x walls take their z from cz and vary over the two x-edges. */
    for (k = 0; k <= 1; k++) {          /* this cell's two walls across z */
        int   az = cz + k;
        float bz = (float)az * CELL, bx = (float)cx * CELL;
        float d  = (float)fabs(z - bz) - WALL_T;
        float o  = hash1((float)cx * 3.7f + (float)az * 9.1f + g_seed) - 0.5f;
        float dc = bx + CELL * 0.5f + o * (CELL - DOOR_W * 2.4f);
        if (!cell_open_n(cx, az) || (float)fabs(x - dc) > DOOR_W)
            if (d < air) air = d;
    }
    for (k = 0; k <= 1; k++) {          /* and its two across x */
        int   ax = cx + k;
        float bx = (float)ax * CELL, bz = (float)cz * CELL;
        float d  = (float)fabs(x - bx) - WALL_T;
        float o  = hash1((float)ax * 8.3f - (float)cz * 2.9f + g_seed) - 0.5f;
        float dc = bz + CELL * 0.5f + o * (CELL - DOOR_W * 2.4f);
        if (!cell_open_w(ax, cz) || (float)fabs(z - dc) > DOOR_W)
            if (d < air) air = d;
    }
    return air;
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
            /* The building. Rooms on a grid with doorways in flat walls -
             * see rooms_air. The tunnel's wander dies out over the first
             * fifteen metres so the floor is one height and the walls are
             * straight, because a hospital that undulates is a cave with
             * paint on it. */
            float st2  = smoothstep01(GATE_2 + 4.0f, GATE_2 + 19.0f, -z);
            float cyh  = cy * (1.0f - st2) + g_hosp_y * st2;
            float hall = rooms_air(x, y, z, cyh - 1.35f);
            float dep  = -z;
            if (dep > CORR_Z - 3.0f) {
                /* A T. One cross-corridor along the end of the rooms, which
                 * every part of the maze can reach, running into one thin
                 * corridor that goes to the door.
                 *
                 * The problem is that the building has no outer wall -- its
                 * rooms run sideways for ever while the way out sits at one x
                 * -- so a slot on that line is only reachable from the rooms
                 * that happen to sit on it, and a quarter of seeds sealed the
                 * ending off. The first answer was to make the corridor
                 * funnel: a hundred metres wide where the rooms end. That
                 * reaches everybody and tells them nothing. You came out of
                 * the maze onto an open floor with no way to guess which way
                 * the door was, which is worse than a wall -- a wall at least
                 * says go somewhere else.
                 *
                 * A corridor across the end of it says the same thing a
                 * hospital says: you are in a building, and buildings have
                 * corridors, and corridors go somewhere. Walk it either way
                 * and you meet the one that turns off toward the door. */
                float fl2 = cyh - 1.35f;
                float ch  = 1.55f - (float)fabs(y - fl2 - 1.30f);
                float cw  = 1.9f - (float)fabs(x - g_corr_x);
                float cor = cw < ch ? cw : ch;
                float lz  = 1.9f - (float)fabs(dep - CORR_Z);
                float lat = lz < ch ? lz : ch;
                /* Unioned into the rooms, not blended with them. A lerp
                 * between the maze and a slot on one line is what sealed the
                 * ending: where the maze is open and the slot is not, the mix
                 * of the two is a wall. */
                if (lat > hall) hall = lat;
                if (cor > hall) hall = cor;
                {   /* and past the junction the rooms stop and it is corridor
                     * the rest of the way */
                    float kk2 = smoothstep01(CORR_Z + 3.0f, CORR_Z + 9.0f, dep);
                    hall = hall * (1.0f - kk2) + cor * kk2;
                }
            }
            if (dep > WAKE_Z + 1.2f) {
                /* and past the door, the room with the lamp: wide, low, and
                 * empty enough that there is nothing to measure it against */
                float fl3 = cyh - 1.35f;
                float rw  = 15.0f - (float)fabs(x - g_corr_x);
                float rh  = 1.75f - (float)fabs(y - fl3 - 1.45f);
                float re  = (LAMP_Z + 14.0f) - dep;
                float rm  = rw < rh ? rw : rh;
                if (re < rm) rm = re;
                hall = rm;
            }
            float k2   = ((-z) - (GATE_2 + 4.0f)) / 6.0f;
            if (k2 > 1.0f) k2 = 1.0f;
            main_air = main_air * (1.0f - k2) + hall * k2;
        }
        {   /* the far side of the lamp room is the end of the world */
            float wall = z + (LAMP_Z + 14.0f);
            if (wall < main_air) main_air = wall;
        }
        if (-z > WAKE_Z - 0.35f && -z < WAKE_Z + 0.35f) {
            /* the door itself: shut until you open it */
            float leaf = (float)fabs(-z - WAKE_Z) - 0.18f;
            float gapx2 = 1.05f - (float)fabs(x - g_corr_x);
            float d3 = leaf;
            if (g_door_open > 0.5f && gapx2 > 0.0f) d3 = 1000.0f;
            if (gapx2 < 0.0f) d3 = 1000.0f;   /* the frame is the corridor wall */
            if (d3 < main_air) main_air = d3;
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
    float speed, warn, hear;
    float stun;      /* the lights just went, and it felt it */
    /* the archetype, as three numbers */
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
    if (m->stun > 0.0f) {
        /* The lights just went. Whatever that is to a thing made of
         * this place, it stops it dead for half a second - which is
         * the half second you have to be somewhere else. */
        m->stun -= dt;
        m->moving = 0;
        return 0;
    }
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
        } else if ((m->travel > MON_RANGE || m->timer <= 0.0f) && g_stage < 3) {
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

#define PUT(A, B, C, G)     do { if (w < MON_POINTS) {         g_mpts[w].x = m->x + f[0]*(A) + sd[0]*(B) + n[0]*(C);         g_mpts[w].y = m->y + f[1]*(A) + sd[1]*(B) + n[1]*(C);         g_mpts[w].z = m->z + f[2]*(A) + sd[2]*(B) + n[2]*(C);         g_mpts[w].reveal = now; g_mpts[w].gain = (G) * MON_GAIN; w++; } } while (0)

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

    /* Why the old one was cute, and what the good creature designers do
     * instead.
     *
     * It was a ball with a smaller ball on it. Those are the two roundest
     * shapes there are, at the two sizes that read as a body and a head --
     * which is the geometry of a mouse. Bilaterally tidy, small enough to
     * take in at a glance, no negative space anywhere. Every one of those is
     * a cuteness lever, and it had all of them pulled.
     *
     * What the well-liked monsters do, and what survives being drawn as a
     * point cloud (structure reads; shading does not):
     *
     *  - Silent Hill: horror by recognition, not by strangeness. The thing
     *    is a person, wrongly assembled. Hence a RIBCAGE -- and parallel
     *    arcs happen to be the one structure a sparse cloud draws perfectly.
     *  - Amnesia: the head is a gape, not a face. A split skull with a hole
     *    down the middle of it instead of a sphere.
     *  - RE4's Regenerador: elongation. Body dragged back, neck run long,
     *    head carried far out in front of the mass.
     *  - The Thing, Ito: asymmetry. Cute is symmetrical; this is built
     *    lopsided, one side longer than the other, head cocked off-axis.
     *  - and one human hand, on the front legs, because a hand where a foot
     *    should be is the cheapest and worst tell there is.
     */
    {
    float asym = (hash1(m->seed * 11.3f) - 0.5f) * 0.55f;   /* built lopsided */
    float cock = asym * 0.10f * sc;                         /* head off-axis */

    /* the abdomen: dragged out behind, not a ball under the head. The
     * outward bias makes it a shell, so the ribs read over a hollow. */
    /* Thin, and dim. Dense and bright it came back as one solid white mass
     * with the ribs buried inside it -- and a featureless blot is where the
     * eye goes first, so the animal was a blot with legs. The flesh is the
     * quiet part; the bones are what you are meant to read. */
    for (i = 0; i < (m->type == T_LISTENER ? 230 : 210); i++) {
        float a = hash1((float)i * 1.7f + m->seed) * 6.2831853f;
        float b = hash1((float)i * 3.1f + m->seed + 4.0f) * 3.1415927f;
        float r = 0.235f * sc * br * (float)pow(hash1((float)i * 5.3f + m->seed), 0.20);
        PUT(-0.20f * sc + r*(float)sin(b)*(float)cos(a) * 1.55f,
             r*(float)sin(b)*(float)sin(a) * 0.74f,
             bob + r*(float)cos(b) * 0.66f,
             m->type == T_LISTENER ? 1.0f : 0.40f);
    }
    /* The ribs, and they are the body -- not a detail buried under it.
     *
     * First go put a proper shell on and the arcs behind it, and at the seven
     * metres you actually meet one of these the whole thing came back as a
     * red smear with limbs. Fine internal detail does not survive a sparse
     * cloud at range; a silhouette does. So the flesh is nearly gone and six
     * bright arcs carry the shape. A ribcage with nothing on it is a stronger
     * outline than a body with a ribcage hidden inside it, and it is the same
     * trick every good one of these pulls: you are not looking at an animal,
     * you are looking at the inside of a person. */
    if (m->type != T_LISTENER) {
        for (k = 0; k < 6; k++) {
            float xr = (-0.40f + 0.098f * (float)k) * sc;
            float rw = 0.272f * sc * br
                     * (0.50f + 0.50f * (float)sin(0.44f + 2.50f * (float)k / 5.0f));
            for (i = 0; i < 52; i++) {
                float t2 = (-1.04f + 2.08f * (float)i / 51.0f) * 1.44f;
                PUT(xr + (hash1((float)(k*29+i) * 1.9f + m->seed) - 0.5f) * 0.020f * sc,
                    rw * (float)sin(t2) * 0.90f,
                    bob + rw * (float)cos(t2) * 0.84f, 1.45f);
            }
        }
        /* and a spine down the length of them */
        for (i = 0; i < 90; i++) {
            float u = (float)i / 89.0f;
            PUT((-0.46f + 0.66f * u) * sc,
                (hash1((float)i * 3.3f + m->seed) - 0.5f) * 0.020f * sc,
                bob + (0.215f - 0.030f * u) * sc * br, 1.30f);
        }
    }
    for (i = 0; i < 74; i++) {          /* the neck, run long */
        float u = (float)i / 73.0f;
        float a = hash1((float)i * 6.1f + m->seed + 7.0f) * 6.2831853f;
        float r = 0.048f * sc;
        PUT(0.04f * sc + u * 0.36f * sc + r*(float)cos(a),
            u * cock + r*(float)sin(a) * 0.7f,
            bob - u * 0.13f * sc + r*(float)sin(a) * 0.7f, 0.85f);
    }
    /* The head: a long wedge split down the middle, and what is between the
     * halves is nothing. It opens toward you as it tapers, so the front of
     * the animal is a gap rather than a face. */
    for (i = 0; i < 210; i++) {
        float side = (i & 1) ? 1.0f : -1.0f;
        float u    = hash1((float)i * 2.3f + m->seed + 9.0f);
        float a    = hash1((float)i * 4.7f + m->seed + 2.0f) * 6.2831853f;
        float rr   = 0.072f * sc * (1.02f - 0.62f * u);
        float gap  = (0.022f + 0.062f * u) * sc;
        PUT(0.40f * sc + u * 0.30f * sc + rr*(float)cos(a) * 0.55f,
            cock + side * gap + rr*(float)sin(a) * 0.50f,
            bob - 0.13f * sc - u * 0.045f * sc + rr*(float)cos(a) * 0.85f, 1.0f);
    }
    for (i = 0; i < 50; i++) {          /* a pair of them, in front of that */
        float u    = (float)(i % 25) / 24.0f;
        float side = (i < 25) ? 1.0f : -1.0f;
        PUT(0.68f * sc + u * 0.20f * sc,
            cock + side * (0.060f - u * 0.040f) * sc * (1.0f + side * asym),
            bob - 0.16f * sc - u * 0.060f * sc, 1.0f);
    }
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
        /* and the two sides do not match either. Eight legs that disagree
         * with each other individually still arrange themselves into a tidy
         * mirror; a body built longer down one side does not. */
        float aside = (i & 1) ? 1.0f : -1.0f;
        float asym  = 1.0f + aside * (hash1(m->seed * 11.3f) - 0.5f) * 0.55f;
        float k1x, k1y, k1z, k2x, k2y, k2z;
        lv *= asym;
        tk *= (2.0f - asym);
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

        for (k = 0; k < LEG_RINGS; k++) {
            float u   = (float)k / (float)(LEG_RINGS - 1);
            float iu  = 1.0f - u;
            float w3  = 3.0f * iu * iu * u, w2 = 3.0f * iu * u * u, w1 = u * u * u;
            float a   = w3 * k1x + w2 * k2x + w1 * ex;
            float b2  = w3 * k1y + w2 * k2y + w1 * ey;
            float c2  = w3 * k1z + w2 * k2z + w1 * ez + bob * iu * iu;
            float rad = (0.112f * iu * iu + 0.020f) * sc * tk
                      * (m->type == T_LISTENER ? 0.52f : 1.0f);
            int   q;
            for (q = 0; q < LEG_TUBE; q++) {
                float h  = (float)(i * 97 + k * 13 + q) * 1.31f + m->seed;
                float aa = hash1(h) * 6.2831853f;
                float bb = hash1(h + 3.7f) * 3.1415927f;
                float rr = rad * (float)pow(hash1(h + 8.1f), 0.34);
                PUT(a  + rr * (float)sin(bb) * (float)cos(aa),
                    b2 + rr * (float)sin(bb) * (float)sin(aa),
                    c2 + rr * (float)cos(bb),
                    0.40f + 0.32f * iu);
            }
        }
        /* A hand, on the two front legs. Five digits splayed on the rock
         * where a foot should taper to a point. Nothing else on the animal
         * costs ninety points and says as plainly that it used to be
         * somebody -- and it is the part nearest you when it arrives. */
        if (i < 2 && m->type != T_LISTENER) {
            int d, s;
            for (d = 0; d < 5; d++) {
                float sp = (-0.5f + 0.25f * (float)d);          /* fan */
                float dl = (0.115f + 0.048f * (float)(d == 2)) * sc
                         * (0.72f + hash1((float)(i*17+d) * 2.7f + m->seed) * 0.55f);
                for (s = 1; s <= 9; s++) {
                    float t3 = (float)s / 9.0f;
                    float cu = t3 * t3 * 0.4f;                  /* curls under */
                    PUT(ex + dl * t3 * (float)cos(sp) * 1.15f,
                        ey + dl * t3 * (float)sin(sp) * 1.9f,
                        ez + dl * cu * 0.9f, 1.05f);
                }
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

/* The other voices, and the ones that matter.
 *
 * The sixteen in the side passages are the arrest: the last thing this body
 * heard, replayed, and they end with a pulse coming back. They saved the
 * heart. They did not get the person, and that is the whole premise -- it is
 * day fifteen and nobody has been home since.
 *
 * These are now. A ward, a chair that has been sat in every day for two
 * weeks, and people talking to someone who has not answered once. They are
 * the only thing in the game that says plainly what is happening, so they
 * come twice in the cave -- quiet enough to be misheard -- and then keep
 * coming once the building is around you, where there is nothing left to
 * mistake it for. */
/* Some of them do something.
 *
 * A body in a bed is not left alone for fifteen days: it gets turned every
 * couple of hours so it does not break down, the light gets put on when
 * somebody comes in, and somebody holds its hand. None of that stops at the
 * skin. If the place you are lost in is made out of this body, then what is
 * done to the body has to arrive in it -- the room rolls when they roll you,
 * every light in the building comes up when a nurse reaches for a switch,
 * and when a hand closes around yours the things hunting you let go.
 *
 * The line comes first and the effect follows a moment later, so it reads as
 * one causing the other rather than as two things happening at once. */
enum { WF_NONE, WF_TURN, WF_LIGHT, WF_HAND };
typedef struct { float dep; int fx; const char *line; } Ward;
static const Ward WARD[] = {
    {  96.0f, WF_NONE,  "오늘로 열흘째래" },
    { 214.0f, WF_NONE,  "눈은 뜨는데 우릴 못 봐" },
    { 286.0f, WF_HAND,  "손 잡아 드릴게요" },
    { 368.0f, WF_NONE,  "보름째입니다. 오늘로 보름" },
    { 396.0f, WF_TURN,  "돌려 눕힐게요" },
    { 424.0f, WF_NONE,  "이대로 못 깨어나면요" },
    { 452.0f, WF_LIGHT, "불 좀 켤게요" },
    { 478.0f, WF_NONE,  "아빠, 나 매일 올게" },
    { 506.0f, WF_HAND,  "손 잡고 있을게. 여기 있어" },
    { 530.0f, WF_TURN,  "자세 한 번 더 바꿉니다" }
};
#define WARDS ((int)(sizeof WARD / sizeof WARD[0]))
static int   g_ward_n;      /* how many have come, so none comes twice */
static float g_hosp_t;      /* seconds spent inside the building */
static float g_hum_t;       /* the fixtures, kept from ever going quiet */
static float g_mon_beep;    /* the bedside monitor, once a beat */
static float g_roll;        /* the world, rolled, because they rolled you */
static float g_roll_to;     /* where it is rolling to */
static float g_roll_t;      /* how long it stays there */
static float g_wardlit;     /* somebody put the light on out there */
static float g_hand;        /* somebody is holding your hand */
static int   g_fx_kind;     /* what the last line set going */
static float g_fx_wait;     /* and how long until it does it */
static int   g_days;        /* fifteen at its door, and climbing */

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

/* The map is a ring, and it has to be.
 *
 * It used to be a bucket: every append was guarded by g_count < MAX_POINTS and
 * simply stopped when it got there. A sounding costs about seven thousand
 * marks, so a player clicking once a second filled 1.2 M in a little under
 * three minutes -- a third of the way through an eight-minute game -- and from
 * then on the only verb they had did nothing at all. No message, no fade, the
 * click just stopped answering. An autopilot run reached the ceiling at 67 m.
 *
 * Overwriting the oldest is also the better rule on its own terms. What goes
 * is the far end of where you have already been, which you are walking away
 * from; the cave closes up behind you instead of staying lit for ever. */
static void pt_put(float x, float y, float z, float reveal, float gain)
{
    g_pts[g_head].x = x;
    g_pts[g_head].y = y;
    g_pts[g_head].z = z;
    g_pts[g_head].reveal = reveal;
    g_pts[g_head].gain   = gain;
    if (++g_head >= MAX_POINTS) g_head = 0;
    if (g_count < MAX_POINTS) g_count++;
}

/* Push [from, g_head) to the card -- in two goes if it wrapped round the end.
 * from == g_head means nothing was added: a single emitter never lays down a
 * whole ring's worth, so it cannot mean a full lap. */
static void pt_upload(int from)
{
    if (from == g_head) return;
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    if (g_head > from) {
        glBufferSubData(GL_ARRAY_BUFFER, (GLintptr)(from * (int)sizeof(Point)),
                        (GLsizeiptr)((g_head - from) * (int)sizeof(Point)),
                        g_pts + from);
        return;
    }
    glBufferSubData(GL_ARRAY_BUFFER, (GLintptr)(from * (int)sizeof(Point)),
                    (GLsizeiptr)((MAX_POINTS - from) * (int)sizeof(Point)),
                    g_pts + from);
    if (g_head > 0)
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        (GLsizeiptr)(g_head * (int)sizeof(Point)), g_pts);
}

static void ping_work(void)
{
    int b;
    int budget = PING_CHUNK;
    int wall_start = g_head, air_start = g_wcount;
    int air_added;
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
                for (m = 0; m < nmark; m++) {
                    float a = (hash1((float)i * 3.7f + (float)b * 11.0f + (float)m * 2.3f) - 0.5f) * 2.0f * srad;
                    float c = (hash1((float)i * 5.1f + (float)b * 7.0f  + (float)m * 3.9f) - 0.5f) * 2.0f * srad;
                    if (m == 0) { a = 0.0f; c = 0.0f; }      /* one dead on the hit */
                    /* The mark keeps more of itself than the bullet does: a
                     * tired ricochet still proves a wall is there, and the
                     * cave only takes shape if late hits stay readable. */
                    pt_put(ox + t1[0]*a + t2[0]*c,
                           oy + t1[1]*a + t2[1]*c,
                           oz + t1[2]*a + t2[2]*c,
                           g_ping_t0 + travelled / wave_speed(),
                           (0.42f + 0.58f * gain) * (1.0f + 0.35f * g_ping_room));
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


    air_added = g_wcount - air_start;

    pt_upload(wall_start);
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
    int start = g_head;

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
            if (!cave_ray(ox, oy, oz, dx, dy, dz, 46.0f - travelled, &t)) break;
            ox += dx * t; oy += dy * t; oz += dz * t; travelled += t;
            cave_normal(ox, oy, oz, n2);
            {   int m2;
                for (m2 = 0; m2 < 3; m2++) {
                    float h = (float)(i * 31 + b * 7 + m2) * 1.7f;
                    pt_put(ox + (hash1(h) - 0.5f) * 0.3f,
                           oy + (hash1(h + 2.0f) - 0.5f) * 0.3f,
                           oz + (hash1(h + 5.0f) - 0.5f) * 0.3f,
                           now + travelled / 34.0f, gain);
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
    pt_upload(start);
}

/* A quieter cousin: every few seconds after the first shock, a cone of the
 * same borrowed light leaves your chest toward the exit. The heart knows the
 * way out; the marks it leaves fade like the shock's do. The pulses come
 * quicker as the door gets closer, which is itself the hint. */
static void emit_guide(float now)
{
    int i;
    int start = g_head;
    float dxx, dyy, dzz, dl;
    float doorx, doory;
    tunnel_centre(-WAKE_Z + 4.0f, &doorx, &doory);
    dxx = doorx - g_px; dyy = doory - g_py; dzz = (-WAKE_Z + 2.0f) - g_pz;
    dl = (float)sqrt(dxx * dxx + dyy * dyy + dzz * dzz);
    if (dl < 1.0f) return;
    dxx /= dl; dyy /= dl; dzz /= dl;

    for (i = 0; i < 240; i++) {
        float j1 = (hash1((float)i * 1.91f + now) - 0.5f) * 0.55f;
        float j2 = (hash1((float)i * 3.37f + now * 2.0f) - 0.5f) * 0.55f;
        float rx = dxx + j1 * (dzz + 0.3f), ry = dyy + j2, rz = dzz - j1 * (dxx + 0.3f);
        float rl = (float)sqrt(rx * rx + ry * ry + rz * rz), t;
        rx /= rl; ry /= rl; rz /= rl;
        if (!cave_ray(g_px, g_py, g_pz, rx, ry, rz, 26.0f, &t)) continue;
        pt_put(g_px + rx * t, g_py + ry * t, g_pz + rz * t,
               now + t / 30.0f, 2.0f);
    }
    pt_upload(start);
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

/* Roll a basis about its own forward axis. The view rolls; walking does not,
 * because a rolled strafe would drive the player up into the ceiling -- what
 * is turning is the room, not their feet. */
static void roll_basis(const float *f, float *r, float *u)
{
    float c = (float)cos(g_roll), sn = (float)sin(g_roll);
    float r2[3], u2[3];
    int i;
    (void)f;
    for (i = 0; i < 3; i++) { r2[i] = r[i]*c + u[i]*sn; u2[i] = u[i]*c - r[i]*sn; }
    for (i = 0; i < 3; i++) { r[i] = r2[i]; u[i] = u2[i]; }
}

static void mat4_view(float *m, float px, float py, float pz,
                      float yaw, float pitch)
{
    float f[3], r[3], u[3];
    basis(yaw, pitch, f, r, u);
    roll_basis(f, r, u);
    m[0]  =  r[0]; m[4] =  r[1]; m[8]  =  r[2]; m[12] = -(r[0]*px + r[1]*py + r[2]*pz);
    m[1]  =  u[0]; m[5] =  u[1]; m[9]  =  u[2]; m[13] = -(u[0]*px + u[1]*py + u[2]*pz);
    m[2]  = -f[0]; m[6] = -f[1]; m[10] = -f[2]; m[14] =  (f[0]*px + f[1]*py + f[2]*pz);
    m[3]  =  0.0f; m[7] =  0.0f; m[11] =  0.0f; m[15] =  1.0f;
}

#define PLAYER_R 0.62f

/* --- attempts and lives -------------------------------------------------- */

/* defined further down, beside the rest of the building's geometry */
static float hospital_eye_y(float z);

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
    /* Inside the building the floor is one height, not the tunnel's. The axis
     * it used to follow wanders four metres up and down, so a checkpoint past
     * the second gate respawned the player inside the floor or above the
     * ceiling -- fit came back at -0.41 at 514 m, and the search below only
     * ever looks sideways, so nothing could recover it. Every death in the
     * back half of the game landed there. hospital_eye_y is the same height
     * game_probe measures against, so there is one answer to this and not two.
     *
     * Only where the building is: hospital_eye_y sits five centimetres below
     * the axis, which is right for a flat floor and is a change the cave has
     * no reason to wear -- a depth 20 capture stops matching over it. */
    if (g_check > GATE_2 + 4.0f) cy = hospital_eye_y(-g_check);
    g_px = cx; g_py = cy; g_pz = -g_check;
    /* past the rooms there is only the corridor, and it does not run down
       the old tunnel axis - starting on that axis puts you in its wall */
    if (g_check > CORR_Z) g_px = g_corr_x;
    {   /* The tunnel axis is a fine place to stand in a tunnel. Inside the
         * building it is wherever the old axis happens to run, which is as
         * often a wall as a room - and a capture at 433 m came back as one
         * flat surface filling the screen. Look around for somewhere open. */
        if (cave_sdf(g_px, g_py, g_pz) < PLAYER_R + 0.15f) {
            float best = -1e9f, bx = g_px, bz = g_pz;
            int a2, r2;
            for (r2 = 1; r2 <= 8; r2++) {
                for (a2 = 0; a2 < 12; a2++) {
                    float th = (float)a2 * 0.5236f;
                    float tx = g_px + (float)cos(th) * (float)r2 * 0.9f;
                    float tz = g_pz + (float)sin(th) * (float)r2 * 0.9f;
                    float d2 = cave_sdf(tx, g_py, tz);
                    if (d2 > best) { best = d2; bx = tx; bz = tz; }
                }
                if (best > PLAYER_R + 0.35f) break;
            }
            g_px = bx; g_pz = bz;
        }
    }
    {   /* The tunnel bends, so a fixed heading points into the wall as often
         * as not - and in the dark that is indistinguishable from the game
         * being broken. Face the way the passage actually runs. */
        float ax, ay, fx, fz;
        tunnel_centre(-g_check - 3.0f, &ax, &ay);
        fx = ax - cx;
        fz = -3.0f;
        g_yaw = (float)atan2(fx, -fz);
        /* The corridor runs straight and the old tunnel axis does not apply
           to it, so facing along that axis started you walking back the way
           you came - measured, six metres of it. */
        if (g_check > CORR_Z) g_yaw = 0.0f;
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
    {   /* Everything past the second gate is a building, and buildings
         * do not undulate. One height is taken at the entrance and the
         * floor holds it from there on. */
        float hx, hy;
        tunnel_centre(-(GATE_2 + 4.0f), &hx, &hy);
        g_hosp_y = hy;
        {   float qx, qy;
            tunnel_centre(-CORR_Z, &qx, &qy);
            g_corr_x = qx;
        }
    g_beat = 0.0f; g_bpm = 50.0f; g_blink = 0.0f;
    }
    /* Both, or neither. Resetting the count without the cursor left the title's
     * own letters sitting at the front of the ring as the first thing drawn,
     * while every mark the first soundings made landed past the end of what
     * was being drawn -- so a run opened with SOUNDING lying on the floor of
     * the cave and a few seconds of clicking at nothing. */
    g_count  = 0;
    g_head   = 0;
    g_pings  = 0;
    g_heard  = 0;
    g_heard_n = 0;
    g_hosp_t = 0.0f;
    g_hum_t  = 0.0f;
    g_days   = 15;
    g_roll = g_roll_to = g_roll_t = 0.0f;
    g_wardlit = g_hand = 0.0f;
    g_fx_kind = WF_NONE; g_fx_wait = 0.0f;
    /* Anything the start depth is already past has been said. A run that
       opens at 400 m has not just walked in on someone saying it is day ten. */
    for (g_ward_n = 0; g_ward_n < WARDS && WARD[g_ward_n].dep <= g_start_depth;)
        g_ward_n++;
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
    for (i = 0; i < n && g_head < MAX_POINTS; i++) {
        float x = g_px + g_text_xy[i * 2 + 0] * scale;
        float y = g_py - g_text_xy[i * 2 + 1] * scale + yoff;   /* bitmaps run down */
        float z = g_pz - 5.0f;
        /* The word never goes dark and comes back on -- that read as the game
         * restarting. It sits at uBase and a wavefront crosses it, so the
         * title is the mechanic: a sounding sweeps left to right and the
         * letters brighten as it reaches them. delay staggers the lines so
         * the sweep arrives at each in turn. */
        pt_put(x, y, z, now + delay, 1.0f);
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
    /* the title is the one thing that starts the ring over: nothing sounded
     * in a previous run should still be standing behind the word */
    g_count = 0;
    g_head  = 0;
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
    for (i = 0; i < WATER_PTS; i++) {
        float u = hash1((float)i * 1.31f) - 0.5f;
        float v = hash1((float)i * 2.77f + 4.0f);
        float cx, cy, x, z;
        z = -GATE_1 + 3.0f - v * 16.0f;
        /* The floor of the room is cy(z) - 1.05, and cy moves nearly a metre
         * over three metres of tunnel. Taking one cy for the whole sheet made
         * a flat plane through a curved floor: near the player it came out at
         * eye height, edge-on, and invisible. Every point takes the centre at
         * its own z, so the surface sits in the hole the way the hole is. */
        tunnel_centre(z, &cx, &cy);
        x = cx + u * 17.0f;
        float rip = (float)sin(x * 1.7f + now * 1.3f)
                  + (float)sin(z * 2.3f - now * 0.9f);
        g_water[i].x = x;
        g_water[i].y = cy - 1.05f + rip * 0.055f;
        g_water[i].z = z;
        g_water[i].reveal = now - 1000.0f;      /* always lit, via uBase */
        g_water[i].gain   = 0.85f + 0.55f * hash1((float)i * 5.3f + (float)((int)(now * 2.0f)));
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
    w_run    = glGetUniformLocation(g_wake_prog, "uRun");
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
    c_hospy  = glGetUniformLocation(g_cave_prog, "uHospY");
    c_blink  = glGetUniformLocation(g_cave_prog, "uBlink");
    c_corrx  = glGetUniformLocation(g_cave_prog, "uCorrX");
    c_wakez  = glGetUniformLocation(g_cave_prog, "uWakeZ");
    c_ward   = glGetUniformLocation(g_cave_prog, "uWard");
    c_hand   = glGetUniformLocation(g_cave_prog, "uHand");
    c_dooru  = glGetUniformLocation(g_cave_prog, "uDoor");
    c_lampout= glGetUniformLocation(g_cave_prog, "uLampOut");
    c_monn   = glGetUniformLocation(g_cave_prog, "uMonN");
    c_monp   = glGetUniformLocation(g_cave_prog, "uMonP");
    c_mond   = glGetUniformLocation(g_cave_prog, "uMonD");
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


/* Pushing out of the rock kept you out of it but did nothing to help you get
 * anywhere: hold a direction into a wall and you simply stopped. Take the
 * wall-parallel part of the step instead and you slide along it, which is
 * what walking down a bending tunnel in the dark actually requires. */
/* In the building the floor is a known height rather than something to be
 * found by falling, so the eye can simply be told where it belongs. Locking
 * movement to the horizontal without this left the player at whatever height
 * they entered at: in the corridor that was pressed into the ceiling, with
 * exactly PLAYER_R of clearance and nowhere to go. */
static float hospital_eye_y(float z)
{
    float cx, cy, st2, cyh, dep = -z;
    tunnel_centre(z, &cx, &cy);
    st2 = smoothstep01(GATE_2 + 4.0f, GATE_2 + 19.0f, dep);
    cyh = cy * (1.0f - st2) + g_hosp_y * st2;
    if (dep > WAKE_Z + 1.2f) return cyh + 0.10f;   /* the lamp room is taller */
    return cyh - 0.05f;
}

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
        {   /* The eye has to follow the corridor down it. It did not: g_py was
             * left wherever the door was while the passage wandered a couple
             * of metres either way underneath it, so the four seconds that are
             * supposed to be distance opening up were spent inside a slab, or
             * nose-down on a floor with the horizon up at the top of the
             * screen. Nothing was wrong with the road; nobody was on it. */
            float cx, cy, k2 = 1.0f - (float)exp(-dt * 4.0f);
            tunnel_centre(g_pz, &cx, &cy);
            g_px += (cx - g_px) * k2;
            g_py += (cy - g_py) * k2;
        }
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
            glUniform1f(c_hospy, g_hosp_y);
            glUniform1f(c_blink, g_blink);
            glUniform1f(c_corrx, g_corr_x);
            glUniform1f(c_wakez, WAKE_Z);
            glUniform1f(c_ward, g_wardlit);
            glUniform1f(c_hand, g_hand);
            glUniform1f(c_dooru, g_door_open);
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
        /* In, and then out again. Held at full this stayed a white wash over
         * everything for as long as the screen was up, so the room the whole
         * game is walking toward was never actually seen. */
        float bright = smoothstep01(0.60f, 1.00f, w)
                     * (1.0f - 0.82f * smoothstep01(1.06f, 1.60f, w));
        float sharp  = smoothstep01(0.46f, 0.94f, w);
        char line[48];

        /* It keeps counting past one. Every smoothstep above already clamps,
         * so nothing moves any more -- but the overflow is a clock the last
         * screen can be paced by, and the last screen is where the game
         * finally says what it has been counting. */
        /* The monitor, once a beat, in step with the trace on its screen:
         * the sweep runs at 0.34 and carries two beats across, so a beat is
         * every 1.47 seconds. It is the first sound of the room, and it
         * starts before the eye is open. */
        if (room > 0.004f) {
            g_mon_beep -= dt;
            if (g_mon_beep <= 0.0f) { audio_monitor(); g_mon_beep = 1.4706f; }
        }
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
            {   /* He is already on his way when the eye opens -- the room
                 * reacted before you did, which is the whole point of it. */
                /* He has to arrive while the eye is open, or the only
                 * thing that moves in the ending happens behind an eyelid.
                 * The eye opens over 0.36..0.72; he comes in behind it. */
                float run = smoothstep01(0.58f, 1.16f, w);
                glUniform1f(w_run, run);
            }
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
                    int k = (int)((w - 1.0f) * 4.6f) % 5;
                    /* What put you under, how long it kept you, how much of
                     * it you brought back -- and then the only line that was
                     * ever true of the whole thing. */
                    if (k == 0)      sprintf(line, "심정지 %d분 %02d초", secs / 60, secs % 60);
                    else if (k == 1) sprintf(line, "혼수 %d일", g_days);
                    else if (k == 2) sprintf(line, "들은 목소리 %d / %d", g_heard_n, BRANCHES);
                    else if (k == 3) sprintf(line, "모두 기다리고 있었다");
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

    if (!g_wet) {
        /* Looking up used to walk you into the ceiling: the move vector was
         * the look vector. On foot the two are different things - the eye
         * turns, the feet stay on the floor - so the horizontal part is all
         * that moves you. */
        float hl = (float)sqrt(mx * mx + mz * mz);
        if (hl > 0.0001f) {
            float k3 = (float)sqrt(mx * mx + my * my + mz * mz) / hl;
            mx *= k3; mz *= k3;
        }
        my = 0.0f;
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
            if (g_step_acc > 1.55f) {
                g_step_acc = 0.0f;
                audio_step(g_wetfeet);
                g_steps++;
            }
        }
        g_has_moved = 1;
    }

    if (!g_wet && -g_pz > GATE_2 + 4.0f) {
        /* stand on the floor of whatever room this is */
        float want = hospital_eye_y(g_pz);
        g_py += (want - g_py) * (1.0f - (float)exp(-dt * 9.0f));
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
    {   /* The lights are on the heart. Far from the door it is fifty a
         * minute; at the door it is eighty, and the closer you get the more
         * often the room goes white - which is also how often the things
         * chasing you are knocked back. The clock is the ally. */
        float dz2 = g_pz + WAKE_Z, dx2 = g_px;
        float dd2 = (float)sqrt(dx2 * dx2 + dz2 * dz2);
        float t2  = 1.0f - smoothstep01(6.0f, 90.0f, dd2);
        g_bpm = 50.0f + 30.0f * t2;
        g_beat += dt * (g_bpm / 60.0f);
        if (g_beat >= 1.0f) {
            g_beat -= 1.0f;
            if (g_stage >= 3) {
                int mi;
                audio_beat();
                for (mi = 0; mi < g_mon_count; mi++)
                    if (g_mon[mi].state == MON_CHARGING || g_mon[mi].state == MON_BURST)
                        g_mon[mi].stun = 0.55f;
            }
        }
        /* The shape of one beat, taken from the phase rather than snapped on
         * and decayed off. It used to jump to 1 the instant the beat landed
         * and fall in a straight line to nothing in a sixth of a second: at
         * one a second that is a strobe, and it read as a fault in the tube
         * rather than as something alive. A heart is two sounds, the second
         * softer and close behind the first, and neither of them is a step --
         * so this is two smooth swells, lub and dub, and the room breathes
         * instead of stuttering. */
        if (g_stage >= 3) {
            float ph = g_beat;
            float d1 = (ph - 0.10f) / 0.085f;
            float d2 = (ph - 0.32f) / 0.060f;
            g_blink = (float)exp(-d1 * d1) + 0.42f * (float)exp(-d2 * d2);
        } else {
            g_blink = 0.0f;
        }
    }
    if (g_note_t > 0.0f) g_note_t -= dt;
    {   /* The ward, arriving on its own. Nothing the player does brings these
         * -- that is the point of them: the room you are in has no idea you
         * are working, and it keeps talking. */
        if (g_ward_n < WARDS && -g_pz >= WARD[g_ward_n].dep && g_note_t <= 0.0f) {
            g_note    = WARD[g_ward_n].line;
            g_note_t  = 4.0f;
            g_fx_kind = WARD[g_ward_n].fx;
            g_fx_wait = 1.4f;      /* let the line land before the world answers */
            g_ward_n++;
        }
        /* what the ward does, arriving in here */
        if (g_fx_kind != WF_NONE) {
            g_fx_wait -= dt;
            if (g_fx_wait <= 0.0f) {
                if (g_fx_kind == WF_TURN) {
                    /* onto the other side, and left there. They turn a body
                     * every couple of hours; it does not turn back. */
                    g_roll_to = (g_roll_to > 0.0f ? -0.62f : 0.62f);
                    g_roll_t  = 26.0f;
                } else if (g_fx_kind == WF_LIGHT) {
                    g_wardlit = 1.0f;
                    audio_beep();
                } else if (g_fx_kind == WF_HAND) {
                    int mi;
                    g_hand = 1.0f;
                    /* and nothing in here can hold on to you while it lasts */
                    for (mi = 0; mi < g_mon_count; mi++) g_mon[mi].stun = 3.4f;
                }
                g_fx_kind = WF_NONE;
            }
        }
        /* the roll eases in, sits, and eases back when the timer runs out */
        if (g_roll_t > 0.0f) g_roll_t -= dt;
        else g_roll_to = 0.0f;
        g_roll += (g_roll_to - g_roll) * (1.0f - (float)exp(-dt * 0.85f));
        if (g_wardlit > 0.0f) g_wardlit -= dt * 0.085f;
        if (g_hand    > 0.0f) g_hand    -= dt * 0.30f;
        /* and the days keep going while you are in here, because they do.
         * A day every ninety seconds: no failure hangs off it, nothing is
         * taken away. It is only ever the number getting worse while you
         * cannot find the way out, which is the whole feeling. */
        if (-g_pz > GATE_2 + 4.0f) {
            g_hosp_t += dt;
            /* And it hums the whole time. A corridor with nobody in it is not
             * silent -- it is full of the sound of its own lights, and that
             * sound is most of why a photograph of an empty hallway feels the
             * way it does. The voice runs twenty-four seconds; it is started
             * again at twenty-two so it never lapses while you are inside. */
            g_hum_t -= dt;
            if (g_hum_t <= 0.0f) { audio_hum(); g_hum_t = 22.0f; }
        } else g_hum_t = 0.0f;
        g_days = 15 + (int)(g_hosp_t / 90.0f);
    }
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

    /* The door. You have to be at it, and opening it opens it - what is
     * behind is a room you walk into, not a cut to somewhere else. */
    if (g_door_open < 0.5f && -g_pz >= WAKE_Z - 2.2f && in->ping) {
        g_door_open = 1.0f;
        audio_door();
        g_note = "\xEB\xAC\xB8\xEC\x9D\xB4 \xEC\x97\xB4\xEB\xA0\xB8\xEB\x8B\xA4";
        g_note_t = 2.0f;
    }
    /* Far enough into the room, the lamp goes out. Not a fade - out. */
    if (-g_pz > LAMP_Z && g_lamp_out < 0.5f) {
        g_lamp_out = 1.0f;
        g_lamp_t   = 0.0f;
        audio_lampout();
    }
    if (g_lamp_out > 0.5f) {
        g_lamp_t += dt;
        if (g_lamp_t > 1.15f) { g_state = ST_WAKE; g_wake = 0.32f; }
    }

    /* ping - not during the ambush; that moment is not yours to light */
    /* Inside the building there is light, and nothing to sound for. The
     * ping is the cave's instrument and it is left in the cave. */
    if (g_stage < 3 && g_ev != 1 && in->ping && now >= g_ping_ready) {
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
        /* Squared, this held near full for most of a second and then let
         * go all at once, which on top of a wave already crossing the screen
         * was too much moving at once. Cubed it is a swell instead: it comes
         * up through the ping rather than over it. */
        float gf = g_stage_flash * g_stage_flash * g_stage_flash * 0.72f;
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
                /* The building has its own lights and they are on because it
                 * is a building. Hanging them off the shock count meant that
                 * starting inside the place left you in the dark for good,
                 * with the sounding already taken away from you. */
                float ind = smoothstep01(GATE_2 + 2.0f, GATE_2 + 22.0f, -g_pz);
                lit = dim > ls ? dim : ls;
                if (ind > lit) lit = ind;
                glUniform1f(c_light, lit);
            }
            glUniform1f(c_wet, g_wet ? 1.0f : 0.0f);
            {   /* each shock hardens the rock into hospital */
                float rd2 = smoothstep01(GATE_2 + 2.0f, GATE_2 + 22.0f, -g_pz);
                float rs  = smoothstep01(0.3f, 2.0f, g_shockf);
                glUniform1f(c_room, rd2 > rs ? rd2 : rs);
            }
            glUniform3fv(c_bra, 16, bra);
            glUniform3fv(c_brb, 16, brb);
            glUniform1f(c_pulse, g_pulse);
            glUniform1f(c_hospy, g_hosp_y);
            glUniform1f(c_blink, g_blink);
            glUniform1f(c_corrx, g_corr_x);
            glUniform1f(c_wakez, WAKE_Z);
            glUniform1f(c_ward, g_wardlit);
            glUniform1f(c_hand, g_hand);
            glUniform1f(c_dooru, g_door_open);
            glUniform1f(c_lampout, g_lamp_out);
            {   /* Indoors the things are geometry, not returns: the shader
                 * marches them alongside the walls so the same lights land
                 * on them. Six is as many as ever matter at once. */
                float mp[24], md[24];
                int mn = 0, mi;
                if (-g_pz > GATE_2 + 4.0f) {
                    for (mi = 0; mi < g_mon_count && mn < 6; mi++) {
                        Monster *mm = &g_mon[mi];
                        if (!mon_visible(mm)) continue;
                        mp[mn*4+0] = mm->x;
                        /* Indoors they walk the floor rather than cling to
                           rock, and the body rides above it. */
                        mp[mn*4+1] = hospital_eye_y(mm->z) - 0.66f;
                        mp[mn*4+2] = mm->z;
                        mp[mn*4+3] = 1.0f + (float)mm->type;
                        md[mn*4+0] = mm->dx;
                        md[mn*4+1] = mm->dy;
                        md[mn*4+2] = mm->dz;
                        md[mn*4+3] = mm->stun > 0.0f ? 1.0f : 0.0f;
                        mn++;
                    }
                }
                glUniform1i(c_monn, mn);
                if (mn > 0) {
                    glUniform4fv(c_monp, mn, mp);
                    glUniform4fv(c_mond, mn, md);
                }
            }
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
        /* The pool, glinting on its own - and in its own colour. Read in the
         * distance palette it came back the same amber as the rock around it,
         * so the one surface in the game you are meant to recognise as water
         * looked like more floor. Tint 4 is water. */
        glUniform1f(u_persist, 0.0f);
        glUniform1f(u_base, g_wet ? 0.22f : 0.72f);
        glUniform1f(u_monster, 4.0f);
        glBindVertexArray(g_wtvao);
        glDrawArrays(GL_POINTS, 0, WATER_PTS);
        glUniform1f(u_monster, 0.0f);
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
    for (i = 0; -g_pz < GATE_2 + 4.0f && i < g_mon_count; i++) {
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
        /* A threshold is where the game tells you where you are, so these
         * are where it stops being a cave and starts being the truth. */
        static const char *NAME[5] = { "", "숨을 참아라",
                                       "여기, 와 본 적 있다",
                                       "보름째. 아직 못 깨어났다",
                                       "문이 있다. 마지막이다" };
        char big[24];
        int  st = (g_back > 0.0f) ? g_stage : g_gate_n;
        if (st < 0) st = 0;
        if (st > 4) st = 4;
        sprintf(big, "STAGE %d", st + 1);
        hud_build_wake(big, (g_back > 0.0f) ? "아직이다. 다시" : NAME[st]);
        if (g_gate_t > 0.0f) g_gate_t -= dt;
    } else {   /* the readout */
        char left[40], right[40];
        const char *hint = (-g_pz >= WAKE_Z - 2.2f && -g_pz < WAKE_Z + 0.5f
                            && g_door_open < 0.5f) ? "CLICK TO OPEN"
                         : (g_note_t > 0.0f) ? g_note
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
            else if (g_stage >= 3)
                /* Metres stopped being progress at the second gate -- the
                 * building is a maze and the number was just going up while
                 * you got no nearer. This is the number that is actually
                 * running, and it is the one you cannot do anything about. */
                sprintf(left, "STAGE %d   \355\230\274\354\210\230 %d\354\235\274\354\247\270",
                        g_stage + 1, g_days);
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
    float f2[3], r2[3], u2[3], n[3], t, d;
    g_mon_count = 1;
    m->seed = 4.0f + (float)type;
    mon_make(m, type, 0.3f);
    /* Ahead of the camera, standing on the floor, far enough back that the
     * whole animal is in frame.
     *
     * Two goes at this. A fixed angle round the tunnel put it off the left
     * edge; the nearest wall the camera happened to be pointed at put the
     * big one -- which is nearly four times the small one -- half out of the
     * top corner. The distance has to come from how big the thing is. */
    basis(g_yaw, g_pitch, f2, r2, u2);
    d = 3.2f + 2.8f * m->scale;
    if (cave_ray(g_px, g_py, g_pz, f2[0], f2[1], f2[2], d, &t) && t > 0.6f)
        d = t - 0.4f;
    m->x = g_px + f2[0] * d;
    m->y = g_py + f2[1] * d;
    m->z = g_pz + f2[2] * d;
    if (cave_ray(m->x, m->y, m->z, 0.0f, -1.0f, 0.0f, 5.0f, &t))
        m->y -= t - 0.06f;                       /* put its feet on the ground */
    cave_normal(m->x, m->y, m->z, n);
    m->nx = n[0]; m->ny = n[1]; m->nz = n[2];
    m->dx = -f2[0]; m->dy = 0.0f; m->dz = -f2[2];   /* facing you */
    t = (float)sqrt(m->dx*m->dx + m->dz*m->dz);
    if (t > 1e-4f) { m->dx /= t; m->dz /= t; }
    m->travel = 0.0f; m->dash = 0.0f; m->moving = 1;
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
void  game_set_pitch(float p){ g_pitch = p; }
int   game_state(void)       { return g_state; }
int   game_paused(void)      { return g_state == ST_MENU_; }
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

/* Walk the corridor centre line and print the clearance. Guessing where a
 * field closes is how an afternoon goes; measuring it takes a second. */
/* Put one of them in front of the camera, indoors, so the marched version
 * can actually be looked at. The cave has game_debug_spider; this is the
 * same idea for a thing that is geometry rather than returns. */
void game_debug_beast(float now, int type)
{
    Monster *m = &g_mon[0];
    float f[3], r[3], u[3], nn[3];
    basis(g_yaw, g_pitch, f, r, u);
    g_mon_count = 1;
    {   /* Put it where there is room for it: walk out along the view ray
         * and take the most open point, or the thing ends up inside a wall
         * and every capture is a picture of plaster. */
        float bestd = -1e9f, bt = 4.0f, tt;
        for (tt = 5.0f; tt <= 9.0f; tt += 0.25f) {
            float dd = cave_sdf(g_px + f[0]*tt, g_py, g_pz + f[2]*tt);
            if (dd > bestd) { bestd = dd; bt = tt; }
        }
        m->x = g_px + f[0] * bt;
        m->y = g_py;
        m->z = g_pz + f[2] * bt;
    }
    cave_normal(m->x, m->y, m->z, nn);
    m->nx = nn[0]; m->ny = nn[1]; m->nz = nn[2];
    m->seed = 3.1f;
    mon_make(m, type, 1.0f);
    m->state  = MON_CHARGING;
    m->timer  = 60.0f;
    m->travel = 0.0f;
    m->stun   = 0.0f;
    /* three quarters on: head-on, the abdomen hides everything */
    m->dx = -f[0] * 0.5f - f[2] * 0.87f;
    m->dy = 0.0f;
    m->dz = -f[2] * 0.5f + f[0] * 0.87f;
    m->tx = g_px; m->ty = g_py; m->tz = g_pz;
    (void)now;
}

void game_probe(void)
{
    FILE *f = fopen("probe.txt", "w");
    float d;
    if (!f) return;
    fprintf(f, "corr_x %.3f  hosp_y %.3f  CORR_Z %.1f  WAKE_Z %.1f  LAMP_Z %.1f\n",
            g_corr_x, g_hosp_y, (double)CORR_Z, (double)WAKE_Z, (double)LAMP_Z);
    for (d = 495.0f; d <= 560.0f; d += 1.0f) {
        float ey = hospital_eye_y(-d);
        fprintf(f, "dep %6.1f  eye_y %7.3f  air@corr %7.3f  air@axis %7.3f\n",
                d, ey, cave_sdf(g_corr_x, ey, -d), cave_sdf(g_corr_x + 3.0f, ey, -d));
    }
    fclose(f);
}
float game_pulse(void)       { return g_pulse; }
float game_note_t(void)      { return g_note_t; }
int   game_event(void)       { return g_ev; }
int   game_steps(void)       { return g_steps; }
int   game_heard(void)       { return g_heard_n; }
int   game_menu_sel(void)    { return g_menu_sel; }
int   game_menu_mode(void)   { return g_menu_mode; }
