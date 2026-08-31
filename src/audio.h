/* audio.h - runtime-synthesised sound. No audio files anywhere.
 * Samples are generated into a rotating set of waveOut buffers, so the whole
 * soundtrack costs a few hundred bytes of code and nothing on disk. */
#ifndef AUDIO_H
#define AUDIO_H

void audio_init(void);
void audio_update(void);   /* call once per frame: refills finished buffers */
void audio_shutdown(void);

void audio_ping(void);     /* the chirp you send out */
void audio_roar(int type); /* it heard you - each kind in its own voice */
void audio_hum(void);      /* the hall: fluorescent, endless */
void audio_hit(void);      /* it reached you */
void audio_beep(void);     /* the monitor, from the other side */
void audio_splash(void);   /* going under */
void audio_stroke(void);   /* an arm pulling through the water */
void audio_submerged(int under);  /* everything above 500 Hz leaves */
void audio_flatline(void);        /* the monitor giving up */
void audio_defib(void);           /* the crash team, from the other side */
void audio_step(float wet);       /* a footfall; 1 = straight out of the water */
void audio_set_volume(float v);   /* the settings slider, 0..1 */
float audio_get_volume(void);

#endif /* AUDIO_H */
