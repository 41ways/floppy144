/* audio.h - runtime-synthesised sound. No audio files anywhere.
 * Samples are generated into a rotating set of waveOut buffers, so the whole
 * soundtrack costs a few hundred bytes of code and nothing on disk. */
#ifndef AUDIO_H
#define AUDIO_H

void audio_init(void);
void audio_update(void);   /* call once per frame: refills finished buffers */
void audio_shutdown(void);

void audio_ping(void);     /* the chirp you send out */
void audio_roar(void);     /* it heard you */
void audio_hit(void);      /* it reached you */

#endif /* AUDIO_H */
