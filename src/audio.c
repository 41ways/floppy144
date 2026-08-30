/* audio.c - a tiny synthesiser on top of waveOut.
 *
 * waveOut is part of Windows, so this adds no redistributable and no assets.
 * Buffers are polled from the main loop rather than refilled from a callback:
 * waveOut callbacks run on their own thread with real restrictions on what
 * they may call, and at 23 ms per buffer polling at frame rate is plenty. */

#include <windows.h>
#include <mmsystem.h>
#include <math.h>
#include "audio.h"

#define SR           22050
#define BUF_SAMPLES  512
#define NBUF         8
#define MAX_VOICES   8

/* Two combs of coprime length. One alone rings like a pipe; two beat against
 * each other and read as a space with walls somewhere out there. */
#define REV_A        6113
#define REV_B        8971

enum { V_PING, V_ROAR, V_HIT };

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

static float g_reva[REV_A], g_revb[REV_B];
static int   g_ia, g_ib;

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

/* One sample of one voice. Everything is an envelope times an oscillator;
 * there is no sample data to load because there are no samples. */
static float voice_sample(Voice *v)
{
    float x = v->t / v->dur;          /* 0..1 through the voice */
    float s = 0.0f;

    if (v->kind == V_PING) {
        /* A drop into still water. The pitch RISES as the cavity collapses -
         * that rise is the whole reason a bloop reads as water and a falling
         * chirp reads as a machine. */
        float f    = 330.0f + 1150.0f * x * x;
        float body = (float)sin(6.2831853 * f * v->t) * (float)exp(-x * 9.0);
        float slap = frand() * (float)exp(-x * 55.0) * 0.35f;
        s = body * 0.42f + slap;
    } else if (v->kind == V_ROAR) {
        /* low, detuned, and it slides down as it commits to the charge */
        float f   = 78.0f - 30.0f * x;
        float env = (x < 0.06f ? x / 0.06f : (float)exp(-(x - 0.06f) * 2.6f));
        float saw = (float)fmod(f * v->t, 1.0) * 2.0f - 1.0f;
        float sub = (float)sin(6.2831853 * (f * 0.5) * v->t);
        s = (saw * 0.34f + sub * 0.30f + frand() * 0.16f) * env * 0.55f;
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
        float dry = 0.0f, wet, mix;

        for (j = 0; j < MAX_VOICES; j++) {
            Voice *v = &g_voices[j];
            if (!v->active) continue;
            dry += voice_sample(v);
            v->t += 1.0f / (float)SR;
            if (v->t >= v->dur) v->active = 0;
        }

        /* The ping should not stop at the edge of the screen. Feeding it back
         * through two delays makes it travel out, hit something, and come
         * back - which is the sound of the mechanic, not decoration. */
        wet = g_reva[g_ia] * 0.62f + g_revb[g_ib] * 0.48f;
        g_reva[g_ia] = dry + g_revb[g_ib] * 0.34f;
        g_revb[g_ib] = dry + g_reva[g_ia] * 0.30f;
        if (++g_ia >= REV_A) g_ia = 0;
        if (++g_ib >= REV_B) g_ib = 0;

        mix = dry + wet * 0.55f;
        if (mix >  1.0f) mix =  1.0f;
        if (mix < -1.0f) mix = -1.0f;
        out[i] = (short)(mix * 27000.0f);
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

void audio_ping(void) { voice_start(V_PING, 0.55f); }
void audio_roar(void) { voice_start(V_ROAR, 1.70f); }
void audio_hit(void)  { voice_start(V_HIT,  0.55f); }
