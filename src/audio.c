/* audio.c - a tiny synthesiser on top of waveOut.
 *
 * waveOut is part of Windows, so this adds no redistributable and no assets.
 * Buffers are polled from the main loop rather than refilled from a callback:
 * waveOut callbacks run on their own thread with real restrictions on what
 * they may call, and at 23 ms per buffer polling at frame rate is plenty. */

#include <windows.h>
#include <mmsystem.h>
#include <math.h>
#include <stdlib.h>
#include "audio.h"

#define SR           22050
#define BUF_SAMPLES  512
#define NBUF         8
#define MAX_VOICES   8

/* Three combs at coprime lengths, then an allpass to smear them. One comb
 * alone rings like a pipe; three beat against each other and the allpass
 * blurs the individual echoes into a space with no obvious walls. Lengths
 * are 0.31, 0.42 and 0.56 seconds - a room you cannot see the end of. */
#define REV_A        6899
#define REV_B        9257
#define REV_C       12409
#define AP_LEN       1051

enum { V_PING, V_ROAR, V_ROAR_HI, V_ROAR_LO, V_HIT, V_BEEP, V_SPLASH, V_FLAT, V_HUM,
       V_STROKE, V_DEFIB, V_STEP };

typedef struct {
    int   active;
    int   kind;
    float t;        /* seconds since the voice started */
    float dur;
    float p;        /* one number the voice reads: a footfall's wetness */
} Voice;

static HWAVEOUT g_wo;
static WAVEHDR  g_hdr[NBUF];
static short    g_buf[NBUF][BUF_SAMPLES];
static Voice    g_voices[MAX_VOICES];
static unsigned g_rng = 0x1234567u;
static int      g_ready;

static float *g_ra, *g_rb, *g_rc, *g_ap;
/* One pole is all it takes. Drop the coefficient and the top of the spectrum
 * simply leaves, and there is no other thing a person hears that as except
 * being under water. */
static float g_lp_target = 1.0f, g_lp = 1.0f, g_lp_z;
static float g_vol = 0.8f;
static int    g_ia, g_ib, g_ic, g_iap;

static float frand(void)
{
    g_rng = g_rng * 1664525u + 1013904223u;
    return (float)((g_rng >> 9) & 0x7FFF) / 16383.5f - 1.0f;
}

static void voice_startp(int kind, float dur, float p)
{
    int i;
    for (i = 0; i < MAX_VOICES; i++) {
        if (!g_voices[i].active) {
            g_voices[i].active = 1;
            g_voices[i].kind   = kind;
            g_voices[i].t      = 0.0f;
            g_voices[i].dur    = dur;
            g_voices[i].p      = p;
            return;
        }
    }
}

static void voice_start(int kind, float dur) { voice_startp(kind, dur, 0.0f); }

/* A struck bowl rather than a drop in water. Partials sit at non-integer
 * ratios, which is what separates a bell from a note and reads as somewhere
 * other than here; each one decays at its own rate so the sound darkens as it
 * fades. Two copies four thousandths apart beat slowly against each other, so
 * it never quite settles. */
static const float BOWL_R[5] = { 1.000f, 1.503f, 2.011f, 2.978f, 4.213f };
static const float BOWL_A[5] = { 1.000f, 0.520f, 0.300f, 0.170f, 0.095f };
static const float BOWL_D[5] = { 1.000f, 1.650f, 2.350f, 3.300f, 4.600f };

static float voice_sample(Voice *v)
{
    float x = v->t / v->dur;          /* 0..1 through the voice */
    float s = 0.0f;

    if (v->kind == V_PING) {
        float f0  = 132.0f;
        float att = 1.0f - (float)exp(-v->t / 0.050);   /* swells, never clicks */
        int k;
        for (k = 0; k < 5; k++) {
            float f = f0 * BOWL_R[k];
            float e = (float)exp(-x * BOWL_D[k]);
            s += BOWL_A[k] * e
               * ((float)sin(6.2831853 * f * v->t)
                + (float)sin(6.2831853 * f * 1.004 * v->t)) * 0.5f;
        }
        s = s * att * 0.52f + frand() * (float)exp(-x * 22.0f) * 0.045f;
    } else if (v->kind == V_STROKE) {
        /* a swell of turbulence: noise through a resonance that rises and
         * falls with the pull. The underwater lowpass does the rest. */
        float sw  = (float)sin(3.1415927 * x);
        float res = (float)sin(6.2831853 * (150.0 + 90.0 * sw) * v->t);
        s = (frand() * 0.7f + res * 0.3f) * sw * sw * 0.34f;
    } else if (v->kind == V_STEP) {
        /* A shoe on vinyl: a soft knock and the tiniest squeak of it.
         * Wet, it also has to let go of the floor -- the knock is duller,
         * there is a scatter of water in it, and the tail is longer and
         * dirtier. Coming out of the flooded stretch every step carries a
         * little less of that until, about a dozen paces later, it is a
         * dry shoe again and you have stopped noticing. */
        float w   = v->p;
        float dry = (frand() * (float)exp(-x * 26.0)
           + (float)sin(6.2831853 * 95.0 * v->t) * (float)exp(-x * 18.0) * 0.5f
           + (float)sin(6.2831853 * 1450.0 * v->t) * (float)exp(-x * 40.0) * 0.06f) * 0.30f;
        float wet = (frand() * (float)exp(-x * 6.5)
           + (float)sin(6.2831853 * 2400.0 * v->t) * (float)exp(-x * 13.0) * 0.22f
           + (float)sin(6.2831853 * 62.0 * v->t) * (float)exp(-x * 9.0) * 0.35f) * 0.30f;
        s = dry * (1.0f - 0.35f * w) + wet * w;
    } else if (v->kind == V_DEFIB) {
        /* the whine of the charge, the thump of it landing, and two beats of
         * a heart deciding to continue */
        float t = v->t;
        if (t < 0.55f) {
            float f2 = 320.0f + 900.0f * (t / 0.55f);
            s = (float)sin(6.2831853 * f2 * t) * 0.16f * (t / 0.55f);
        } else if (t < 0.80f) {
            float u2 = t - 0.55f;
            s = ((float)sin(6.2831853 * 55.0 * u2) * 0.9f + frand() * 0.5f)
              * (float)exp(-u2 * 11.0f);
        } else {
            float u2 = (float)fmod(t - 0.80f, 0.72f);
            float amp = (t < 2.4f) ? 0.5f : 0.0f;
            s = (float)sin(6.2831853 * 60.0 * u2) * (float)exp(-u2 * 16.0f) * amp;
        }
    } else if (v->kind == V_HUM) {
        /* sixty-cycle light fixtures, forever */
        float env = (x < 0.10f ? x / 0.10f : (x > 0.9f ? (1.0f - x) / 0.1f : 1.0f));
        s = ((float)sin(6.2831853 * 120.0 * v->t) * 0.5f
           + (float)sin(6.2831853 * 240.0 * v->t) * 0.2f
           + frand() * 0.06f) * env * 0.055f;
    } else if (v->kind == V_ROAR || v->kind == V_ROAR_HI || v->kind == V_ROAR_LO) {
        /* the same throat at three depths: the small one yelps, the big one
         * drops the floor, the tall one keens */
        float f0  = v->kind == V_ROAR_LO ? 52.0f
                  : v->kind == V_ROAR_HI ? 164.0f : 96.0f;
        float f   = f0 * (1.0f - 0.38f * x);
        float env = (x < 0.06f ? x / 0.06f : (float)exp(-(x - 0.06f) * 2.6f));
        float saw = (float)fmod(f * v->t, 1.0) * 2.0f - 1.0f;
        float sub = (float)sin(6.2831853 * (f * 0.5) * v->t);
        s = (saw * 0.34f + sub * 0.30f + frand() * 0.16f) * env * 0.55f;
    } else if (v->kind == V_SPLASH) {
        /* going under: a slap, then everything closing over the top of it */
        float env  = (float)exp(-x * 5.0);
        float band = frand() * (float)exp(-x * 14.0);
        float glug = (float)sin(6.2831853 * (240.0 - 180.0 * x) * v->t);
        s = (band * 0.55f + glug * 0.34f) * env * 0.75f;
    } else if (v->kind == V_FLAT) {
        /* two last beeps, then the tone that does not stop */
        float t = v->t;
        if (t < 0.42f)      s = (float)sin(6.2831853 * 1046.0 * t)
                              * (t < 0.05f ? t / 0.05f : (float)exp(-(t - 0.05f) * 7.0f)) * 0.30f;
        else if (t < 1.05f) { float u = t - 0.63f;
            if (u > 0.0f) s = (float)sin(6.2831853 * 1046.0 * u)
                            * (u < 0.05f ? u / 0.05f : (float)exp(-(u - 0.05f) * 7.0f)) * 0.30f; }
        else {
            float u = t - 1.05f;
            float env = u < 0.10f ? u / 0.10f : 1.0f;
            if (x > 0.86f) env *= (1.0f - x) / 0.14f;    /* let it die at the end */
            s = (float)sin(6.2831853 * 988.0 * v->t) * env * 0.16f;
        }
    } else if (v->kind == V_BEEP) {
        /* the flat blip of a bedside monitor, heard from under */
        float env = (x < 0.05f ? x / 0.05f : (float)exp(-(x - 0.05f) * 7.0f));
        s = ((float)sin(6.2831853 * 1046.0 * v->t)
           + (float)sin(6.2831853 * 2092.0 * v->t) * 0.25f) * env * 0.30f;
    } else { /* V_HIT */
        float env = (float)exp(-x * 9.0f);
        s = (frand() * 0.8f + (float)sin(6.2831853 * 120.0 * v->t) * 0.4f) * env * 0.6f;
    }
    return s;
}

static void render(short *out, int n)
{
    int i, j;
    for (i = 0; i < n; i++) {
        float dry = 0.0f, comb, apin, apout, mix;

        for (j = 0; j < MAX_VOICES; j++) {
            Voice *v = &g_voices[j];
            if (!v->active) continue;
            dry += voice_sample(v);
            v->t += 1.0f / (float)SR;
            if (v->t >= v->dur) v->active = 0;
        }

        /* The sound should not stop at the edge of the screen: it should go
         * out into the dark and keep coming back for a while. */
        comb = g_ra[g_ia] + g_rb[g_ib] + g_rc[g_ic];
        g_ra[g_ia] = dry + g_ra[g_ia] * 0.71f;
        g_rb[g_ib] = dry + g_rb[g_ib] * 0.67f;
        g_rc[g_ic] = dry + g_rc[g_ic] * 0.63f;
        if (++g_ia >= REV_A) g_ia = 0;
        if (++g_ib >= REV_B) g_ib = 0;
        if (++g_ic >= REV_C) g_ic = 0;

        /* an allpass smears the three into one another so no single echo
         * stands out and the tail stops sounding like a corridor */
        apin  = comb * 0.230f;
        apout = g_ap[g_iap] - apin * 0.62f;
        g_ap[g_iap] = apin + apout * 0.62f;
        if (++g_iap >= AP_LEN) g_iap = 0;

        mix = dry * 0.78f + apout * 0.80f;

        g_lp += (g_lp_target - g_lp) * 0.0006f;      /* slide, never switch */
        g_lp_z += (mix - g_lp_z) * g_lp;
        mix = g_lp_z * (1.0f + (1.0f - g_lp) * 1.30f);
        mix *= g_vol;
        if (mix >  1.0f) mix =  1.0f;
        if (mix < -1.0f) mix = -1.0f;
        out[i] = (short)(mix * 26000.0f);
    }
}

void audio_init(void)
{
    WAVEFORMATEX wf;
    int i;

    ZeroMemory(&wf, sizeof wf);
    wf.wFormatTag      = WAVE_FORMAT_PCM;
    wf.nChannels       = 1;
    wf.nSamplesPerSec  = SR;
    wf.wBitsPerSample  = 16;
    wf.nBlockAlign     = (WORD)(wf.nChannels * wf.wBitsPerSample / 8);
    wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;

    g_ra = (float *)calloc(REV_A, sizeof(float));
    g_rb = (float *)calloc(REV_B, sizeof(float));
    g_rc = (float *)calloc(REV_C, sizeof(float));
    g_ap = (float *)calloc(AP_LEN, sizeof(float));
    if (!g_ra || !g_rb || !g_rc || !g_ap) { g_ready = 0; return; }

    if (waveOutOpen(&g_wo, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        g_ready = 0;     /* no sound device: the game still plays, silently */
        return;
    }

    for (i = 0; i < NBUF; i++) {
        ZeroMemory(&g_hdr[i], sizeof(WAVEHDR));
        ZeroMemory(g_buf[i], sizeof g_buf[i]);
        g_hdr[i].lpData         = (LPSTR)g_buf[i];
        g_hdr[i].dwBufferLength = sizeof g_buf[i];
        waveOutPrepareHeader(g_wo, &g_hdr[i], sizeof(WAVEHDR));
        waveOutWrite(g_wo, &g_hdr[i], sizeof(WAVEHDR));
    }
    g_ready = 1;
}

void audio_update(void)
{
    int i;
    if (!g_ready) return;
    for (i = 0; i < NBUF; i++) {
        if (g_hdr[i].dwFlags & WHDR_DONE) {
            waveOutUnprepareHeader(g_wo, &g_hdr[i], sizeof(WAVEHDR));
            render(g_buf[i], BUF_SAMPLES);
            g_hdr[i].dwFlags        = 0;
            g_hdr[i].dwBufferLength = sizeof g_buf[i];
            waveOutPrepareHeader(g_wo, &g_hdr[i], sizeof(WAVEHDR));
            waveOutWrite(g_wo, &g_hdr[i], sizeof(WAVEHDR));
        }
    }
}

void audio_shutdown(void)
{
    int i;
    if (!g_ready) return;
    waveOutReset(g_wo);
    for (i = 0; i < NBUF; i++)
        waveOutUnprepareHeader(g_wo, &g_hdr[i], sizeof(WAVEHDR));
    waveOutClose(g_wo);
    g_ready = 0;
}

void audio_ping(void) { voice_start(V_PING, 2.60f); }
void audio_roar(int type)
{
    voice_start(type == 1 ? V_ROAR_LO : (type == 2 ? V_ROAR_HI : V_ROAR),
                type == 2 ? 2.30f : 1.70f);
}
void audio_hum(void)  { voice_start(V_HUM, 24.0f); }
void audio_stroke(void) { voice_start(V_STROKE, 0.62f); }
void audio_defib(void)  { voice_start(V_DEFIB, 2.60f); }
/* wet is 0 for a dry shoe and 1 straight out of the water */
void audio_step(float wet)
{ voice_startp(V_STEP, 0.16f + 0.26f * wet, wet); }
void audio_set_volume(float v) { g_vol = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
float audio_get_volume(void)   { return g_vol; }
void audio_hit(void)  { voice_start(V_HIT,  0.55f); }
void audio_beep(void) { voice_start(V_BEEP, 0.42f); }

void audio_submerged(int under)
{
    g_lp_target = under ? 0.055f : 1.0f;
}

void audio_splash(void)
{
    voice_start(V_SPLASH, 1.10f);
}

void audio_flatline(void)
{
    voice_start(V_FLAT, 6.5f);
}
