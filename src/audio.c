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

enum { V_PING, V_ROAR, V_HIT, V_BEEP };

typedef struct {
    int   active;
    int   kind;
    float t;        /* seconds since the voice started */
    float dur;
} Voice;

static HWAVEOUT g_wo;
static WAVEHDR  g_hdr[NBUF];
static short    g_buf[NBUF][BUF_SAMPLES];
static Voice    g_voices[MAX_VOICES];
static unsigned g_rng = 0x1234567u;
static int      g_ready;

static float *g_ra, *g_rb, *g_rc, *g_ap;
static int    g_ia, g_ib, g_ic, g_iap;

static float frand(void)
{
    g_rng = g_rng * 1664525u + 1013904223u;
    return (float)((g_rng >> 9) & 0x7FFF) / 16383.5f - 1.0f;
}

static void voice_start(int kind, float dur)
{
    int i;
    for (i = 0; i < MAX_VOICES; i++) {
        if (!g_voices[i].active) {
            g_voices[i].active = 1;
            g_voices[i].kind   = kind;
            g_voices[i].t      = 0.0f;
            g_voices[i].dur    = dur;
            return;
        }
    }
}

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
    } else if (v->kind == V_ROAR) {
        /* low, detuned, and it slides down as it commits to the charge */
        float f   = 78.0f - 30.0f * x;
        float env = (x < 0.06f ? x / 0.06f : (float)exp(-(x - 0.06f) * 2.6f));
        float saw = (float)fmod(f * v->t, 1.0) * 2.0f - 1.0f;
        float sub = (float)sin(6.2831853 * (f * 0.5) * v->t);
        s = (saw * 0.34f + sub * 0.30f + frand() * 0.16f) * env * 0.55f;
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
void audio_roar(void) { voice_start(V_ROAR, 1.70f); }
void audio_hit(void)  { voice_start(V_HIT,  0.55f); }
void audio_beep(void) { voice_start(V_BEEP, 0.42f); }
