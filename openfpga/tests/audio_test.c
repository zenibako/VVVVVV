/* Host checks for the Pocket sound player (src/pocket_audio.c) and the WAV
 * loader in sdl3/io.c, on a music pack written here. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "music_pack.h"
#include "of_audio.h"
#include "pocket_audio.h"

static int failures;

#define CHECK(cond)                                                    \
    do                                                                 \
    {                                                                  \
        if (!(cond))                                                   \
        {                                                              \
            printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                \
        }                                                              \
    } while (0)

/* The OS output: everything written, and how much of it has played. */
#define CAPACITY 131072
#define MAX_OUTPUT (48000 * 20)
static int16_t output[MAX_OUTPUT * 2];
static int written;
static int played;

void of_audio_init(void)
{
    written = 0;
    played = 0;
}

int of_audio_write(const int16_t *samples, int count)
{
    const int room = CAPACITY - (written - played);
    if (count > room)
    {
        count = room;
    }
    if (written + count > MAX_OUTPUT)
    {
        count = MAX_OUTPUT - written;
    }
    memcpy(output + written * 2, samples, sizeof(int16_t) * 2 * count);
    written += count;
    return count;
}

int of_audio_free(void)
{
    return CAPACITY - (written - played);
}

/* One 34 ms frame: the device plays, then the game tops up. */
static void frame(void)
{
    played += 48000 * 34 / 1000;
    if (played > written)
    {
        played = written;
    }
    pocket_audio_update();
}

/* A test track: a tone whose pitch rises, so a wrong loop shows. */
#define TRACK_SAMPLES 48000
#define LOOP_START 12000
#define LOOP_END 40000

static uint8_t adpcm[2][TRACK_SAMPLES / 2];
static MusicPackTrack headers[2];

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
}

static void write_pack(void)
{
    for (int t = 0; t < 2; t++)
    {
        AdpcmState state = {0, 0};
        MusicPackTrack *h = &headers[t];
        memset(h, 0, sizeof(*h));
        snprintf(h->name, sizeof(h->name), t == 0 ? "pppppp/0levelcomplete" : "pppppp/1pushingonwards");
        h->sample_count = TRACK_SAMPLES;
        h->loop_start = t == 0 ? LOOP_START : 0;
        h->loop_end = t == 0 ? LOOP_END : TRACK_SAMPLES;
        double phase = 0.0;
        for (int i = 0; i < TRACK_SAMPLES; i++)
        {
            if (i == (int) h->loop_start)
            {
                h->loop_predictor = (int16_t) state.predictor;
                h->loop_step_index = (uint8_t) state.step_index;
            }
            phase += 2.0 * M_PI * (200.0 + i * 0.01 + t * 100) / 48000.0;
            const uint32_t code = adpcm_encode(&state, (int32_t) (8000.0 * sin(phase)));
            adpcm[t][i / 2] |= (uint8_t) (i & 1 ? code << 4 : code);
        }
    }
    FILE *f = fopen(POCKET_MUSIC_PACK, "wb");
    uint8_t header[8];
    memcpy(header, MUSIC_PACK_MAGIC, 4);
    put_u32(header + 4, 2);
    fwrite(header, 1, 8, f);
    uint32_t offset = 8 + 2 * MUSIC_PACK_ENTRY_SIZE;
    for (int t = 0; t < 2; t++)
    {
        uint8_t entry[MUSIC_PACK_ENTRY_SIZE] = {0};
        memcpy(entry, headers[t].name, MUSIC_PACK_NAME_LEN);
        put_u32(entry + 48, offset);
        put_u32(entry + 52, headers[t].sample_count);
        put_u32(entry + 56, MUSIC_PACK_RATE);
        put_u32(entry + 60, headers[t].loop_start);
        put_u32(entry + 64, headers[t].loop_end);
        entry[68] = (uint8_t) headers[t].loop_predictor;
        entry[69] = (uint8_t) ((uint16_t) headers[t].loop_predictor >> 8);
        entry[70] = headers[t].loop_step_index;
        fwrite(entry, 1, sizeof(entry), f);
        offset += TRACK_SAMPLES / 2;
    }
    fwrite(adpcm, 1, sizeof(adpcm), f);
    fclose(f);
}

/* What track t should sound like from its start, looping or not. */
static int16_t *expected_music(int t, bool loop, int count)
{
    int16_t *out = calloc(count, sizeof(int16_t));
    const MusicPackTrack *h = &headers[t];
    const uint32_t end = loop ? h->loop_end : h->sample_count;
    AdpcmState state = {0, 0};
    uint32_t pos = 0;
    for (int i = 0; i < count; i++)
    {
        if (pos >= end)
        {
            if (!loop)
            {
                break;
            }
            pos = h->loop_start;
            state.predictor = h->loop_predictor;
            state.step_index = h->loop_step_index;
        }
        const uint8_t byte = adpcm[t][pos / 2];
        out[i] = adpcm_decode(&state, pos & 1 ? byte >> 4 : byte & 0x0F);
        pos++;
    }
    return out;
}

/* Output channel samples from..from+count against want, scaled by
 * volume / 128. */
static int mismatches(int from, const int16_t *want, int count, int volume)
{
    int bad = 0;
    for (int i = 0; i < count; i++)
    {
        const int16_t w = (int16_t) ((want[i] * volume) >> 7);
        if (output[(from + i) * 2] != w || output[(from + i) * 2 + 1] != w)
        {
            if (bad == 0)
            {
                printf("  first difference at %d: %d, want %d\n", from + i, output[(from + i) * 2], w);
            }
            bad++;
        }
    }
    return bad;
}

static void test_looping_music(void)
{
    pocket_audio_init();
    CHECK(pocket_music_count() == 2);
    CHECK(strcmp(pocket_music_name(0), "pppppp/0levelcomplete") == 0);
    CHECK(pocket_music_halted());
    CHECK(pocket_music_play(0, true));
    CHECK(!pocket_music_halted() && !pocket_music_paused());
    const int start = written;
    for (int i = 0; i < 200; i++)
    {
        frame();
    }
    /* Six and a half seconds: several loops, played without a gap. */
    const int count = written - start;
    CHECK(count > 48000 * 6);
    int16_t *want = expected_music(0, true, count);
    CHECK(mismatches(start, want, count, 128) == 0);
    free(want);
    pocket_audio_shutdown();
}

static void test_music_controls(void)
{
    pocket_audio_init();
    CHECK(pocket_music_play(1, false));
    pocket_music_set_volume(64);
    const int start = written;
    for (int i = 0; i < 60; i++)
    {
        frame();
    }
    /* Played once at half volume, then silent, but not halted. */
    int16_t *want = expected_music(1, false, written - start);
    CHECK(mismatches(start, want, written - start, 64) == 0);
    free(want);
    CHECK(!pocket_music_halted());

    /* Paused music stops where it is and carries on from there. */
    CHECK(pocket_music_play(0, true));
    pocket_music_set_volume(128);
    const int before = written;
    frame();
    const int heard = written - before;
    pocket_music_pause();
    CHECK(pocket_music_paused());
    const int silent_from = written;
    for (int i = 0; i < 5; i++)
    {
        frame();
    }
    int nonzero = 0;
    for (int i = silent_from; i < written; i++)
    {
        nonzero += output[i * 2] != 0;
    }
    CHECK(nonzero == 0);
    pocket_music_resume();
    const int resumed = written;
    frame();
    want = expected_music(0, true, heard + (written - resumed));
    CHECK(mismatches(resumed, want + heard, written - resumed, 128) == 0);
    free(want);

    pocket_music_halt();
    CHECK(pocket_music_halted() && pocket_music_paused());
    CHECK(!pocket_music_play(5, true));
    pocket_audio_shutdown();
}

static void test_sounds(void)
{
    pocket_audio_init();
    /* 24 kHz 16-bit stereo: kept as mono at twice as many samples. */
    int16_t pcm[100 * 2];
    for (int i = 0; i < 100; i++)
    {
        pcm[i * 2] = 1000;
        pcm[i * 2 + 1] = 3000;
    }
    const int sound = pocket_sound_load(pcm, sizeof(pcm), 24000, 2, 16);
    CHECK(sound >= 0);
    const int start = written;
    pocket_sound_play(sound);
    frame();
    CHECK(output[start * 2] == 2000);
    CHECK(output[(start + 199) * 2] == 2000);
    CHECK(output[(start + 200) * 2] == 0);

    /* Eight voices at most, as FAudio had; and a volume for them all. */
    pocket_sound_set_volume(64);
    const int many = written;
    for (int i = 0; i < 10; i++)
    {
        pocket_sound_play(sound);
    }
    frame();
    CHECK(output[many * 2] == 8 * 1000);

    /* 8-bit unsigned mono at the output rate. */
    uint8_t pcm8[10];
    memset(pcm8, 128 + 64, sizeof(pcm8));
    const int sound8 = pocket_sound_load(pcm8, sizeof(pcm8), 48000, 1, 8);
    pocket_sound_set_volume(128);
    const int at8 = written;
    pocket_sound_play(sound8);
    frame();
    CHECK(output[at8 * 2] == 64 << 8);
    CHECK(output[(at8 + 10) * 2] == 0);

    /* Interpolating between the extremes of 16-bit PCM stays in range. */
    {
        const int16_t edge[2] = {-32768, 32767};
        const int sound_edge = pocket_sound_load(edge, sizeof(edge), 22050, 1, 16);
        const int at_edge = written;
        CHECK(sound_edge >= 0);
        pocket_sound_play(sound_edge);
        frame();
        CHECK(output[at_edge * 2] == -32768);
        CHECK(output[(at_edge + 1) * 2] > -32768 && output[(at_edge + 1) * 2] < 32767);
        pocket_sound_free(sound_edge);
    }

    /* Paused sounds hold their place. */
    const int paused_at = written;
    pocket_sound_play(sound);
    pocket_sound_pause_all();
    frame();
    CHECK(output[paused_at * 2] == 0);
    pocket_sound_resume_all();
    const int resumed = written;
    frame();
    CHECK(output[resumed * 2] == 2000);
    pocket_audio_shutdown();
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = v;
    p[1] = v >> 8;
}

static void test_wav(void)
{
    uint8_t wav[44 + 8 + 8];
    memcpy(wav, "RIFF", 4);
    put_u32(wav + 4, sizeof(wav) - 8);
    memcpy(wav + 8, "WAVEfmt ", 8);
    put_u32(wav + 16, 16);
    put16(wav + 20, 1);
    put16(wav + 22, 2);
    put_u32(wav + 24, 22050);
    put_u32(wav + 28, 22050 * 4);
    put16(wav + 32, 4);
    put16(wav + 34, 16);
    /* A chunk the loader skips, then the samples. */
    memcpy(wav + 36, "LIST", 4);
    put_u32(wav + 40, 0);
    memcpy(wav + 44, "data", 4);
    put_u32(wav + 48, 8);
    for (int i = 0; i < 8; i++)
    {
        wav[52 + i] = (uint8_t) i;
    }
    SDL_AudioSpec spec;
    Uint8 *buf;
    Uint32 len;
    CHECK(SDL_LoadWAV_IO(SDL_IOFromConstMem(wav, sizeof(wav)), true, &spec, &buf, &len));
    CHECK(spec.freq == 22050 && spec.channels == 2 && spec.format == SDL_AUDIO_S16LE);
    CHECK(len == 8 && buf[7] == 7);
    SDL_free(buf);

    /* WAVE_FORMAT_EXTENSIBLE with no room for its extension is refused,
     * not read past: here the 16-byte fmt chunk ends the file. */
    {
        uint8_t ext[12 + 10 + 24];
        memcpy(ext, "RIFF", 4);
        put_u32(ext + 4, sizeof(ext) - 8);
        memcpy(ext + 8, "WAVEdata", 8);
        put_u32(ext + 16, 2);
        ext[20] = ext[21] = 0;
        memcpy(ext + 22, "fmt ", 4);
        put_u32(ext + 26, 16);
        memcpy(ext + 30, wav + 20, 16);
        put16(ext + 30, 0xFFFE);
        CHECK(!SDL_LoadWAV_IO(SDL_IOFromConstMem(ext, sizeof(ext)), true, &spec, &buf, &len));
    }

    put16(wav + 20, 3); /* float */
    CHECK(!SDL_LoadWAV_IO(SDL_IOFromConstMem(wav, sizeof(wav)), true, &spec, &buf, &len));
    CHECK(!SDL_LoadWAV_IO(SDL_IOFromConstMem("RIFF", 4), true, &spec, &buf, &len));
}

int main(void)
{
    write_pack();
    test_looping_music();
    test_music_controls();
    test_sounds();
    test_wav();
    remove(POCKET_MUSIC_PACK);
    if (failures != 0)
    {
        printf("%d audio checks failed\n", failures);
        return 1;
    }
    printf("audio checks passed\n");
    return 0;
}
