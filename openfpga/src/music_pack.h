/* vvvvvv-music.bin: the game's music, converted once on a computer by
 * pocket-music (github.com/zenibako/pocket-music) to mono IMA ADPCM at the
 * Pocket's output rate, so the device never decodes Vorbis.  It is an OFM2
 * pack:
 *
 *   char     magic[4]    "OFM2"
 *   uint32_t track_count
 *   track_count 76-byte entries, in the order the game numbers its tracks
 *   data
 *
 * An entry, little-endian: name[48], NUL padded; offset of the data;
 * sample_count; sample_rate (0 marks a stored file, which this port has
 * none of); loop_start; loop_end; int16 loop_predictor; uint8
 * loop_step_index; uint8 flags; uint32 reserved.
 *
 * A track's name is "pppppp/<id>" or "mmmmmm/<id>", where <id> is the
 * game's music id, such as "0levelcomplete".
 *
 * Each track is one ADPCM stream, 4 bits a sample, low nibble first,
 * starting from predictor 0 and step index 0, sample_count / 2 bytes long.
 * sample_count, loop_start and loop_end are even, so every point a track
 * starts or loops from is a byte boundary.  A looping track plays to
 * loop_end, then carries on from loop_start with the decoder state the
 * encoder had there, loop_predictor and loop_step_index. */
#ifndef MUSIC_PACK_H
#define MUSIC_PACK_H

#include <stdint.h>

#define MUSIC_PACK_MAGIC "OFM2"
#define MUSIC_PACK_FILE "vvvvvv-music.bin"
#define MUSIC_PACK_NAME_LEN 48
#define MUSIC_PACK_RATE 48000
#define MUSIC_PACK_ENTRY_SIZE 76

typedef struct
{
    char name[MUSIC_PACK_NAME_LEN]; /* NUL padded */
    uint32_t offset;                /* file offset of the ADPCM data */
    uint32_t sample_count;
    uint32_t sample_rate;
    uint32_t loop_start;
    uint32_t loop_end;
    int16_t loop_predictor;
    uint8_t loop_step_index;
} MusicPackTrack;

typedef struct
{
    int32_t predictor;
    int32_t step_index;
} AdpcmState;

static const int8_t adpcm_index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8,
};

static const int16_t adpcm_step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
    253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767,
};

/* Decodes one 4-bit code and returns the new sample. */
static inline int16_t adpcm_decode(AdpcmState *state, uint32_t code)
{
    const int32_t step = adpcm_step_table[state->step_index];
    int32_t diff = step >> 3;
    if (code & 4)
    {
        diff += step;
    }
    if (code & 2)
    {
        diff += step >> 1;
    }
    if (code & 1)
    {
        diff += step >> 2;
    }
    if (code & 8)
    {
        diff = -diff;
    }
    int32_t predictor = state->predictor + diff;
    if (predictor > 32767)
    {
        predictor = 32767;
    }
    if (predictor < -32768)
    {
        predictor = -32768;
    }
    state->predictor = predictor;
    int32_t index = state->step_index + adpcm_index_table[code];
    if (index < 0)
    {
        index = 0;
    }
    if (index > 88)
    {
        index = 88;
    }
    state->step_index = index;
    return (int16_t) predictor;
}

/* Picks the code that brings the decoder closest to sample, and decodes it
 * so state follows what the device will hear. */
static inline uint32_t adpcm_encode(AdpcmState *state, int32_t sample)
{
    const int32_t step = adpcm_step_table[state->step_index];
    int32_t diff = sample - state->predictor;
    uint32_t code = 0;
    if (diff < 0)
    {
        code = 8;
        diff = -diff;
    }
    int32_t part = step;
    for (uint32_t bit = 4; bit != 0; bit >>= 1)
    {
        if (diff >= part)
        {
            code |= bit;
            diff -= part;
        }
        part >>= 1;
    }
    adpcm_decode(state, code);
    return code;
}

#endif /* MUSIC_PACK_H */
