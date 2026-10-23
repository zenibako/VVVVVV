/* The game's sound on openfpgaOS, in place of FAudio: music streamed from
 * the converted pack (music_pack.h) and sound effects held in memory,
 * mixed in software into the OS's 48 kHz stereo output.
 *
 * The OS doesn't call back for more sound, so pocket_audio_update() keeps
 * the output topped up; the port calls it from each presented frame and
 * from SDL_Delay. */
#ifndef POCKET_AUDIO_H
#define POCKET_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Volumes run from 0 to POCKET_AUDIO_MAX_VOLUME, as the game's do. */
#define POCKET_AUDIO_MAX_VOLUME 128
#define POCKET_AUDIO_SOUND_VOICES 8

bool pocket_audio_init(void);
void pocket_audio_shutdown(void);
void pocket_audio_update(void);

/* Music tracks are the pack's, numbered in its order. */
int pocket_music_count(void);
const char *pocket_music_name(int track);
bool pocket_music_valid(int track);
bool pocket_music_play(int track, bool loop);
void pocket_music_halt(void);
bool pocket_music_halted(void);
void pocket_music_pause(void);
void pocket_music_resume(void);
bool pocket_music_paused(void);
void pocket_music_set_volume(int volume);

/* A sound is 16-bit or 8-bit PCM, mono or stereo, at any rate; it is kept
 * as mono at the output rate.  Returns the sound's number, or -1. */
int pocket_sound_load(const void *pcm, uint32_t bytes, int rate, int channels, int bits);
void pocket_sound_free(int sound);
/* Plays on a free voice, if there is one, as FAudio's eight did. */
void pocket_sound_play(int sound);
void pocket_sound_pause_all(void);
void pocket_sound_resume_all(void);
void pocket_sound_set_volume(int volume);

#ifdef __cplusplus
}
#endif

#endif /* POCKET_AUDIO_H */
