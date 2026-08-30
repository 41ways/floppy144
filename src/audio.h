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
void audio_submerged(int under);  /* everything above 500 Hz leaves */
void audio_flatline(void);        /* the monitor giving up */

#endif /* AUDIO_H */
