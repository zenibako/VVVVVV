/* See pocket_audio.h. */
#include "pocket_audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "music_pack.h"
#include "of_audio.h"

#ifndef POCKET_MUSIC_PACK
#define POCKET_MUSIC_PACK MUSIC_PACK_FILE
#endif

/* Output queued ahead of what is playing.  A frame is 34 ms, so this rides
 * out a frame three times as long; it is also how late a sound starts. */
#define QUEUE_TARGET_PAIRS (OF_AUDIO_RATE / 10)
#define MIX_CHUNK_PAIRS 256

/* Music read ahead from the card: 64 KB is 2.7 s.  A read is at most a
 * piece, one a call, so a frame is never held up long. */
#define RING_BYTES 65536
#define READ_PIECE 8192

#define MAX_SOUNDS 64

typedef struct
{
    FILE *file;
    MusicPackTrack *tracks;
    int count;

    /* The playing track. */
    int track;
    bool loop;
    bool paused;
    bool finished;
    int volume;
    uint32_t position; /* next sample to decode */
    uint32_t end;      /* where it loops or stops */
    AdpcmState state;
    uint8_t byte;      /* the byte whose high nibble is next */

    uint8_t ring[RING_BYTES];
    uint32_t ring_read;  /* bytes taken, counted from the start */
    uint32_t ring_write; /* bytes put in */
    uint32_t file_byte;  /* track byte the next read starts at */
    bool reading;
} Music;

typedef struct
{
    int16_t *samples;
    uint32_t count;
} Sound;

typedef struct
{
    int sound;
    uint32_t position;
} Voice;

static bool started;
static int queue_capacity;
static Music music;
static Sound sounds[MAX_SOUNDS];
static Voice voices[POCKET_AUDIO_SOUND_VOICES];
static bool sounds_paused;
static int sound_volume = POCKET_AUDIO_MAX_VOLUME;

static uint32_t read_u32(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t) p[3] << 24);
}

static void open_pack(void)
{
    FILE *f = fopen(POCKET_MUSIC_PACK, "rb");
    if (f == NULL)
    {
        printf("[INFO] No music: %s is missing\n", POCKET_MUSIC_PACK);
        return;
    }
    uint8_t header[8];
    if (fread(header, 1, 8, f) != 8 || memcmp(header, MUSIC_PACK_MAGIC, 4) != 0)
    {
        printf("[WARN] No music: %s is not a music pack\n", POCKET_MUSIC_PACK);
        fclose(f);
        return;
    }
    const uint32_t count = read_u32(header + 4);
    if (count > 1024)
    {
        fclose(f);
        return;
    }
    music.tracks = (MusicPackTrack *) calloc(count ? count : 1, sizeof(MusicPackTrack));
    if (music.tracks == NULL)
    {
        fclose(f);
        return;
    }
    for (uint32_t i = 0; i < count; i++)
    {
        uint8_t entry[MUSIC_PACK_ENTRY_SIZE];
        if (fread(entry, 1, sizeof(entry), f) != sizeof(entry))
        {
            free(music.tracks);
            music.tracks = NULL;
            fclose(f);
            return;
        }
        MusicPackTrack *t = &music.tracks[i];
        memcpy(t->name, entry, MUSIC_PACK_NAME_LEN);
        t->name[MUSIC_PACK_NAME_LEN - 1] = '\0';
        t->offset = read_u32(entry + 48);
        t->sample_count = read_u32(entry + 52) & ~1u;
        t->sample_rate = read_u32(entry + 56);
        t->loop_start = read_u32(entry + 60) & ~1u;
        t->loop_end = read_u32(entry + 64) & ~1u;
        t->loop_predictor = (int16_t) (entry[68] | (entry[69] << 8));
        t->loop_step_index = entry[70] > 88 ? 88 : entry[70];
        /* Music is converted to the output rate; anything else is skipped,
         * silent, keeping the numbering. */
        if (t->sample_rate != MUSIC_PACK_RATE)
        {
            t->sample_count = 0;
        }
        if (t->loop_end > t->sample_count || t->loop_start >= t->loop_end)
        {
            t->loop_start = 0;
            t->loop_end = t->sample_count;
            t->loop_predictor = 0;
            t->loop_step_index = 0;
        }
    }
    music.file = f;
    music.count = (int) count;
    printf("[INFO] Music: %d tracks\n", music.count);
}

bool pocket_audio_init(void)
{
    if (started)
    {
        return true;
    }
    of_audio_init();
    /* Nothing is queued yet, so all of the queue is free. */
    queue_capacity = of_audio_free();
    music.track = -1;
    music.volume = POCKET_AUDIO_MAX_VOLUME;
    for (int i = 0; i < POCKET_AUDIO_SOUND_VOICES; i++)
    {
        voices[i].sound = -1;
    }
    open_pack();
    started = true;
    return true;
}

void pocket_audio_shutdown(void)
{
    if (music.file != NULL)
    {
        fclose(music.file);
    }
    free(music.tracks);
    memset(&music, 0, sizeof(music));
    for (int i = 0; i < MAX_SOUNDS; i++)
    {
        pocket_sound_free(i);
    }
    started = false;
}

/* Music */

int pocket_music_count(void)
{
    return music.count;
}

const char *pocket_music_name(int track)
{
    return track >= 0 && track < music.count ? music.tracks[track].name : NULL;
}

bool pocket_music_valid(int track)
{
    return track >= 0 && track < music.count && music.tracks[track].sample_count > 0;
}

/* Tops up the read-ahead with at most one piece. */
static void read_music(void)
{
    if (music.track < 0 || !music.reading || music.file == NULL)
    {
        return;
    }
    const MusicPackTrack *t = &music.tracks[music.track];
    const uint32_t end_byte = music.end / 2;
    if (music.file_byte >= end_byte)
    {
        if (!music.loop)
        {
            music.reading = false;
            return;
        }
        music.file_byte = t->loop_start / 2;
    }
    uint32_t room = RING_BYTES - (music.ring_write - music.ring_read);
    if (room < READ_PIECE)
    {
        return;
    }
    uint32_t want = READ_PIECE;
    if (want > end_byte - music.file_byte)
    {
        want = end_byte - music.file_byte;
    }
    /* Into the ring, in up to two parts where it wraps. */
    uint8_t piece[READ_PIECE];
    if (fseek(music.file, (long) (t->offset + music.file_byte), SEEK_SET) != 0)
    {
        music.reading = false;
        return;
    }
    const size_t got = fread(piece, 1, want, music.file);
    if (got == 0)
    {
        music.reading = false;
        return;
    }
    const uint32_t at = music.ring_write % RING_BYTES;
    const uint32_t first = got < RING_BYTES - at ? (uint32_t) got : RING_BYTES - at;
    memcpy(music.ring + at, piece, first);
    memcpy(music.ring, piece + first, got - first);
    music.ring_write += (uint32_t) got;
    music.file_byte += (uint32_t) got;
}

bool pocket_music_play(int track, bool loop)
{
    if (!pocket_music_valid(track))
    {
        return false;
    }
    const MusicPackTrack *t = &music.tracks[track];
    music.track = track;
    music.loop = loop;
    music.paused = false;
    music.finished = false;
    music.position = 0;
    music.end = loop ? t->loop_end : t->sample_count;
    music.state.predictor = 0;
    music.state.step_index = 0;
    music.ring_read = 0;
    music.ring_write = 0;
    music.file_byte = 0;
    music.reading = true;
    /* Enough to start on; the rest follows a piece a frame. */
    read_music();
    read_music();
    return true;
}

void pocket_music_halt(void)
{
    music.track = -1;
    music.reading = false;
    music.paused = true;
}

bool pocket_music_halted(void)
{
    return music.track < 0;
}

void pocket_music_pause(void)
{
    if (music.track >= 0)
    {
        music.paused = true;
    }
}

void pocket_music_resume(void)
{
    if (music.track >= 0)
    {
        music.paused = false;
    }
}

bool pocket_music_paused(void)
{
    return music.paused || music.track < 0;
}

void pocket_music_set_volume(int volume)
{
    music.volume = volume < 0 ? 0 : volume > POCKET_AUDIO_MAX_VOLUME ? POCKET_AUDIO_MAX_VOLUME : volume;
}

/* Adds count samples of music to mix, or as many as are read ahead. */
static void mix_music(int32_t *mix, int count)
{
    if (music.track < 0 || music.paused || music.finished)
    {
        return;
    }
    const MusicPackTrack *t = &music.tracks[music.track];
    const int32_t volume = music.volume;
    for (int i = 0; i < count; i++)
    {
        if (music.position >= music.end)
        {
            if (!music.loop)
            {
                music.finished = true;
                return;
            }
            music.position = t->loop_start;
            music.state.predictor = t->loop_predictor;
            music.state.step_index = t->loop_step_index;
        }
        uint32_t code;
        if (music.position & 1)
        {
            code = music.byte >> 4;
        }
        else
        {
            if (music.ring_read == music.ring_write)
            {
                /* The card fell behind: a gap, not a skip. */
                return;
            }
            music.byte = music.ring[music.ring_read % RING_BYTES];
            music.ring_read++;
            code = music.byte & 0x0F;
        }
        music.position++;
        mix[i] += (adpcm_decode(&music.state, code) * volume) >> 7;
    }
}

/* Sounds */

/* One sample of a frame of PCM, mixed down and scaled to 16 bits. */
static int32_t pcm_sample(const uint8_t *pcm, uint32_t frame, int channels, int bits)
{
    int32_t sum = 0;
    for (int c = 0; c < channels; c++)
    {
        const uint32_t i = frame * channels + c;
        if (bits == 8)
        {
            sum += ((int32_t) pcm[i] - 128) << 8;
        }
        else
        {
            sum += (int16_t) (pcm[i * 2] | (pcm[i * 2 + 1] << 8));
        }
    }
    return sum / channels;
}

int pocket_sound_load(const void *pcm, uint32_t bytes, int rate, int channels, int bits)
{
    if (pcm == NULL || rate <= 0 || channels < 1 || channels > 2 || (bits != 8 && bits != 16))
    {
        return -1;
    }
    int slot = -1;
    for (int i = 0; i < MAX_SOUNDS; i++)
    {
        if (sounds[i].samples == NULL)
        {
            slot = i;
            break;
        }
    }
    if (slot < 0)
    {
        return -1;
    }
    const uint32_t frames = bytes / (uint32_t) (channels * (bits / 8));
    const uint32_t count = (uint32_t) (((uint64_t) frames * OF_AUDIO_RATE) / (uint32_t) rate);
    int16_t *out = (int16_t *) malloc(sizeof(int16_t) * (count ? count : 1));
    if (out == NULL)
    {
        return -1;
    }
    /* Linear interpolation to the output rate, in 16.16 steps. */
    const uint64_t step = ((uint64_t) rate << 16) / OF_AUDIO_RATE;
    uint64_t at = 0;
    for (uint32_t i = 0; i < count; i++, at += step)
    {
        const uint32_t frame = (uint32_t) (at >> 16);
        const int32_t frac = (int32_t) (at & 0xFFFF);
        const int32_t a = pcm_sample((const uint8_t *) pcm, frame, channels, bits);
        const int32_t b = frame + 1 < frames ? pcm_sample((const uint8_t *) pcm, frame + 1, channels, bits) : a;
        out[i] = (int16_t) (a + (int32_t) (((int64_t) (b - a) * frac) >> 16));
    }
    sounds[slot].samples = out;
    sounds[slot].count = count;
    return slot;
}

void pocket_sound_free(int sound)
{
    if (sound < 0 || sound >= MAX_SOUNDS)
    {
        return;
    }
    for (int i = 0; i < POCKET_AUDIO_SOUND_VOICES; i++)
    {
        if (voices[i].sound == sound)
        {
            voices[i].sound = -1;
        }
    }
    free(sounds[sound].samples);
    sounds[sound].samples = NULL;
    sounds[sound].count = 0;
}

void pocket_sound_play(int sound)
{
    if (sound < 0 || sound >= MAX_SOUNDS || sounds[sound].samples == NULL)
    {
        return;
    }
    for (int i = 0; i < POCKET_AUDIO_SOUND_VOICES; i++)
    {
        if (voices[i].sound < 0)
        {
            voices[i].sound = sound;
            voices[i].position = 0;
            return;
        }
    }
}

void pocket_sound_pause_all(void)
{
    sounds_paused = true;
}

void pocket_sound_resume_all(void)
{
    sounds_paused = false;
}

void pocket_sound_set_volume(int volume)
{
    sound_volume = volume < 0 ? 0 : volume > POCKET_AUDIO_MAX_VOLUME ? POCKET_AUDIO_MAX_VOLUME : volume;
}

static void mix_sounds(int32_t *mix, int count)
{
    if (sounds_paused)
    {
        return;
    }
    const int32_t volume = sound_volume;
    for (int v = 0; v < POCKET_AUDIO_SOUND_VOICES; v++)
    {
        Voice *voice = &voices[v];
        if (voice->sound < 0)
        {
            continue;
        }
        const Sound *s = &sounds[voice->sound];
        uint32_t left = s->count - voice->position;
        const int n = left < (uint32_t) count ? (int) left : count;
        const int16_t *in = s->samples + voice->position;
        for (int i = 0; i < n; i++)
        {
            mix[i] += (in[i] * volume) >> 7;
        }
        voice->position += (uint32_t) n;
        if (voice->position >= s->count)
        {
            voice->sound = -1;
        }
    }
}

/* Output */

static void mix_and_write(int pairs)
{
    static int32_t mix[MIX_CHUNK_PAIRS];
    static int16_t out[MIX_CHUNK_PAIRS * 2];
    while (pairs > 0)
    {
        const int chunk = pairs < MIX_CHUNK_PAIRS ? pairs : MIX_CHUNK_PAIRS;
        memset(mix, 0, sizeof(int32_t) * chunk);
        mix_music(mix, chunk);
        mix_sounds(mix, chunk);
        for (int i = 0; i < chunk; i++)
        {
            int32_t s = mix[i];
            s = s > 32767 ? 32767 : s < -32768 ? -32768 : s;
            out[i * 2] = (int16_t) s;
            out[i * 2 + 1] = (int16_t) s;
        }
        const int accepted = of_audio_write(out, chunk);
        if (accepted < chunk)
        {
            return;
        }
        pairs -= chunk;
    }
}

void pocket_audio_update(void)
{
    if (!started)
    {
        return;
    }
    read_music();
    const int queued = queue_capacity - of_audio_free();
    int target = QUEUE_TARGET_PAIRS;
    if (target > queue_capacity * 3 / 4)
    {
        target = queue_capacity * 3 / 4;
    }
    if (queued < target)
    {
        mix_and_write(target - queued);
    }
}
