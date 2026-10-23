/* The OS audio output for host tests: tests/audio_test.c records what is
 * written and decides how much room the queue has. */
#ifndef OF_AUDIO_H
#define OF_AUDIO_H

#include <stdint.h>

#define OF_AUDIO_RATE 48000

void of_audio_init(void);
int of_audio_write(const int16_t *samples, int count);
int of_audio_free(void);

#endif /* OF_AUDIO_H */
