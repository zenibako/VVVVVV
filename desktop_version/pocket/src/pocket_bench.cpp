/* See pocket_bench.h. */
#include "pocket_bench.h"

#ifdef POCKET_BENCH

#include <SDL3/SDL.h>

#include <stdio.h>

#include "of_timer.h"

#include "../../src/Enums.h"
#include "../../src/Font.h"
#include "../../src/Game.h"
#include "../../src/Graphics.h"
#include "../../src/Map.h"
#include "../../src/Entity.h"
#include "../../src/Screen.h"
#include "../../src/Script.h"
#include "../../src/UtilityClass.h"

extern "C" Uint32 POCKET_bench_keys;
extern "C" bool POCKET_saves_volatile;
extern "C" bool POCKET_tint_runs;

/* Keys held down, as input.c sends them (bit order there). */
enum
{
    KEY_LEFT = 1,
    KEY_RIGHT = 2,
    KEY_FLIP = 4,
    KEY_MAP = 8
};

enum Start
{
    START_NONE,
    START_TOWER,
    HIDE_TRIAL,
    GO_TO_TOWER
};

enum Keys
{
    KEYS_NONE,
    KEYS_MAP,
    KEYS_CLIMB
};

/* Each part of the run lasts so many steps of game logic, 30 a second.
 * The tower's colour changes every 50 steps, five times in a row. */
static const struct
{
    const char* name; /* NULL for a part that is not timed */
    int steps;
    enum Start start;
    enum Keys keys;
}
parts[] = {
    {NULL, 90, START_NONE, KEYS_NONE},      /* the title screen */
    {NULL, 170, START_TOWER, KEYS_NONE},    /* fade in and countdown */
    /* The Tower's time trial starts in Teleporter Divot (108,109). Its
     * clock and counters are drawn over the room until the trial is
     * dropped, after which the run is ordinary play. */
    {"divot+hud", 150, START_NONE, KEYS_NONE},
    {"divot", 300, HIDE_TRIAL, KEYS_NONE},
    {"tower", 600, GO_TO_TOWER, KEYS_NONE},
    {"map", 480, START_NONE, KEYS_MAP},
    {"climb", 600, START_NONE, KEYS_CLIMB},
};
#define PART_COUNT ((int) SDL_arraysize(parts))

static int part = -1;     /* -1 until the title screen is up */
static int step;          /* steps into the part */
static bool finished;

/* Microseconds per frame in game logic, in drawing (the game's drawing
 * and the renderer's, then showing the frame) and in all of the frame. */
typedef struct
{
    Uint32 logic, draw, total;
}
FrameTimes;

typedef struct
{
    int frames;
    FrameTimes sum;
    Uint32 max;
    int colour_frames;
    Uint32 colour_total;
    int room[2]; /* where the part ended, to show it ran where it meant to */
}
PartTimes;
static PartTimes times[PART_COUNT];

typedef struct
{
    int part, frame, colstate;
    FrameTimes us;
    bool colour_change;
}
SlowFrame;
#define SLOW_KEPT 8
static SlowFrame slowest[SLOW_KEPT];
static int slow_count;

static FrameTimes frame_us;
static Uint32 loop_start, step_start, draw_start;
static int colstate_start;
static int steps_this_loop;

static void start_part(void)
{
    step = 0;
    switch (parts[part].start)
    {
    case START_NONE:
        break;
    case START_TOWER:
        /* Spikes do not end the run; the trial is only marked as helped. */
        map.invincibility = true;
        script.startgamemode(Start_TIMETRIAL_TOWER);
        break;
    case HIDE_TRIAL:
        game.intimetrial = false;
        break;
    case GO_TO_TOWER:
    {
        /* Into The Tower's ground floor, as walking right out of the Divot
         * does: loading (109,109) moves the player down to the tower's
         * bottom and puts the camera there. */
        const int player = obj.getplayer();
        if (INBOUNDS_VEC(player, obj.entities))
        {
            obj.entities[player].xp = 0;
            obj.entities[player].lerpoldxp = 0;
            obj.entities[player].lerpoldyp = obj.entities[player].yp;
        }
        game.gravitycontrol = 0;
        map.gotoroom(109, 109);
        break;
    }
    }
}

static Uint32 keys_for_step(void)
{
    switch (parts[part].keys)
    {
    case KEYS_NONE:
        break;
    case KEYS_MAP:
        /* Open the map, wait, close it, wait. */
        return step % 60 < 2 ? KEY_MAP : 0;
    case KEYS_CLIMB:
        /* Run right, into the tower, flipping every 40 steps. */
        return KEY_RIGHT | (step % 40 == 0 ? KEY_FLIP : 0);
    }
    return 0;
}

void POCKET_BenchStep(void)
{
    step_start = of_time_us();
    if (finished)
    {
        POCKET_bench_keys = 0;
        return;
    }
    POCKET_saves_volatile = true;
    steps_this_loop++;
    if (part < 0)
    {
        if (game.gamestate != TITLEMODE)
        {
            return;
        }
        part = 0;
        start_part();
    }
    else if (++step >= parts[part].steps)
    {
        times[part].room[0] = game.roomx;
        times[part].room[1] = game.roomy;
        if (++part == PART_COUNT)
        {
            finished = true;
            POCKET_bench_keys = 0;
            return;
        }
        start_part();
    }
    POCKET_bench_keys = keys_for_step();
}

void POCKET_BenchStepEnd(void)
{
    frame_us.logic += of_time_us() - step_start;
}

void POCKET_BenchDrawStart(void)
{
    draw_start = of_time_us();
}

void POCKET_BenchDrawEnd(void)
{
    frame_us.draw += of_time_us() - draw_start;
}

void POCKET_BenchLoopStart(void)
{
    loop_start = of_time_us();
    SDL_zero(frame_us);
    colstate_start = graphics.towerbg.colstate;
    steps_this_loop = 0;
}

static void keep_slow(const SlowFrame* frame)
{
    if (slow_count == SLOW_KEPT && slowest[SLOW_KEPT - 1].us.total >= frame->us.total)
    {
        return;
    }
    int at = slow_count < SLOW_KEPT ? slow_count++ : SLOW_KEPT - 1;
    while (at > 0 && slowest[at - 1].us.total < frame->us.total)
    {
        slowest[at] = slowest[at - 1];
        at--;
    }
    slowest[at] = *frame;
}

void POCKET_BenchLoopEnd(void)
{
    if (finished || part < 0 || parts[part].name == NULL || steps_this_loop == 0)
    {
        return;
    }
    frame_us.total = of_time_us() - loop_start;
    PartTimes* t = &times[part];
    const bool colour_change = graphics.towerbg.colstate != colstate_start;
    t->frames++;
    t->sum.logic += frame_us.logic;
    t->sum.draw += frame_us.draw;
    t->sum.total += frame_us.total;
    t->max = SDL_max(t->max, frame_us.total);
    if (colour_change)
    {
        t->colour_frames++;
        t->colour_total += frame_us.total;
    }
    SlowFrame frame;
    frame.part = part;
    frame.frame = t->frames;
    frame.colstate = graphics.towerbg.colstate;
    frame.us = frame_us;
    frame.colour_change = colour_change;
    keep_slow(&frame);
}

/* Milliseconds to a tenth, from a total of microseconds over count frames. */
static void ms(char* out, size_t size, Uint32 total_us, int count)
{
    if (count == 0)
    {
        SDL_snprintf(out, size, "    -");
        return;
    }
    const Uint32 value = total_us / 100 / count;
    SDL_snprintf(out, size, "%3u.%u", (unsigned) (value / 10), (unsigned) (value % 10));
}

/* Microseconds for one pass of draw, repeated count times, over each of
 * per things drawn a pass. It draws into a scratch texture made opaque
 * first, as the game's own screen is, and draws once untimed so a texture
 * drawn for the first time is not timed building its caches. */
static Uint32 text_us(void (*draw)(void), int count, int per)
{
    graphics.set_render_target(graphics.tempShakeTexture);
    graphics.fill_rect(0, 0, 0);
    draw();
    const Uint32 start = of_time_us();
    for (int i = 0; i < count; i++)
    {
        draw();
    }
    const Uint32 took = of_time_us() - start;
    graphics.set_render_target(NULL);
    return took / count / per;
}

static void grey_word(void) { font::print(0, 8, 8, "ABCDEFGHIJ", 196, 196, 196); }
static void white_word(void) { font::print(0, 8, 8, "ABCDEFGHIJ", 255, 255, 255); }
static void grey_letter(void) { font::print(0, 8, 8, "A", 196, 196, 196); }
static void bordered_word(void) { font::print(PR_BOR, 8, 8, "ABCDEFGHIJ", 196, 196, 196); }

/* An 8x8 corner of a sprite, a sheet with transparency as the font's is:
 * through the game's tile drawing, and straight to the renderer. */
static void grey_tile(void)
{
    graphics.draw_grid_tile(graphics.grphx.im_sprites, 0, 8, 8, 8, 8, 196, 196, 196);
}

static void grey_copy(void)
{
    static const SDL_FRect part = {0, 0, 8, 8};
    static const SDL_FRect to = {8, 8, 8, 8};
    SDL_SetTextureColorMod(graphics.grphx.im_sprites, 196, 196, 196);
    SDL_RenderTexture(gameScreen.m_renderer, graphics.grphx.im_sprites, &part, &to);
    SDL_SetTextureColorMod(graphics.grphx.im_sprites, 255, 255, 255);
}

static void nothing(void) {}

void POCKET_BenchDraw(void)
{
    if (!finished)
    {
        return;
    }
    static char lines[30][48];
    static int line_count;
    if (line_count == 0)
    {
        SDL_snprintf(lines[line_count++], 48, "VVVVVV benchmark, ms a frame");
        SDL_snprintf(lines[line_count++], 48, "part      fr  frame logic  draw   col");
        for (int i = 0; i < PART_COUNT; i++)
        {
            if (parts[i].name == NULL)
            {
                continue;
            }
            const PartTimes* t = &times[i];
            char total[8], logic[8], draw[8], col[8];
            ms(total, sizeof(total), t->sum.total, t->frames);
            ms(logic, sizeof(logic), t->sum.logic, t->frames);
            ms(draw, sizeof(draw), t->sum.draw, t->frames);
            ms(col, sizeof(col), t->colour_total, t->colour_frames);
            SDL_snprintf(lines[line_count++], 48, "%-9s%4d %s %s %s %s",
                parts[i].name, t->frames, total, logic, draw, col);
        }
        SDL_snprintf(lines[line_count++], 48, "col: frames the colour changed");
        SDL_snprintf(lines[line_count++], 48, "max ms, room each part ended in:");
        for (int i = 0; i < PART_COUNT; i++)
        {
            if (parts[i].name != NULL)
            {
                SDL_snprintf(lines[line_count++], 48, "%-9s %4u %d,%d", parts[i].name,
                    (unsigned) (times[i].max / 1000), times[i].room[0], times[i].room[1]);
            }
        }
        SDL_snprintf(lines[line_count++], 48, "slowest:   frame logic draw colour");
        for (int i = 0; i < slow_count; i++)
        {
            const SlowFrame* f = &slowest[i];
            SDL_snprintf(lines[line_count++], 48, "%-9s#%-4d %3u %3u %3u %2d%s",
                parts[f->part].name, f->frame, (unsigned) (f->us.total / 1000),
                (unsigned) (f->us.logic / 1000), (unsigned) (f->us.draw / 1000),
                f->colstate, f->colour_change ? "*" : "");
        }
        /* What text costs: drawing it is most of a frame with the trial's
         * clock and counters up. The slow path is timed both before and
         * after the fast one, so neither gains from going first. */
        POCKET_tint_runs = false;
        const Uint32 slow_before = text_us(grey_word, 100, 10);
        POCKET_tint_runs = true;
        const Uint32 tinted = text_us(grey_word, 100, 10);
        POCKET_tint_runs = false;
        const Uint32 slow_after = text_us(grey_word, 100, 10);
        POCKET_tint_runs = true;
        const Uint32 white = text_us(white_word, 100, 10);
        const Uint32 letter = text_us(grey_letter, 1000, 1);
        const Uint32 tile = text_us(grey_tile, 1000, 1);
        const Uint32 copy = text_us(grey_copy, 1000, 1);
        const Uint32 empty = text_us(nothing, 1000, 1);
        const Uint32 bordered = text_us(bordered_word, 20, 1);
        SDL_snprintf(lines[line_count++], 48, "glyph us: %u, old way %u/%u, white %u",
            (unsigned) tinted, (unsigned) slow_before, (unsigned) slow_after, (unsigned) white);
        SDL_snprintf(lines[line_count++], 48, "1 letter %u tile %u copy %u none %u",
            (unsigned) letter, (unsigned) tile, (unsigned) copy, (unsigned) empty);
        SDL_snprintf(lines[line_count++], 48, "bordered word %u us", (unsigned) bordered);
        SDL_snprintf(lines[line_count++], 48, "full speed is 34 ms; screenshot me");
        for (int i = 0; i < line_count; i++)
        {
            printf("%s\n", lines[i]);
        }
    }
    /* The frame has been copied to the window already, so the results are
     * drawn there. */
    graphics.set_render_target(NULL);
    graphics.fill_rect(0, 0, 0);
    const int spacing = line_count > 26 ? 8 : 9; /* fits the 240-pixel screen */
    for (int i = 0; i < line_count; i++)
    {
        font::print(0, 4, 2 + i * spacing, lines[i], 255, 255, 255);
    }
}

#endif
