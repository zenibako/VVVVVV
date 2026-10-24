/* See pocket_bench.h. */
#include "pocket_bench.h"

#ifdef POCKET_BENCH

#include <SDL3/SDL.h>

#include <stdio.h>

#include "../../src/Enums.h"
#include "../../src/Font.h"
#include "../../src/Game.h"
#include "../../src/Graphics.h"
#include "../../src/Map.h"
#include "../../src/Entity.h"
#include "../../src/Script.h"
#include "../../src/UtilityClass.h"

extern "C" Uint32 POCKET_bench_keys;
extern "C" bool POCKET_saves_volatile;
extern "C" Uint32 POCKET_render_us;

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
    START_FINAL_LEVEL,
    GO_TO_DIVOT
};

/* Each part of the run lasts so many steps of game logic, 30 a second.
 * The tower's colour changes every 50 steps, five times in a row. */
static const struct
{
    const char* name; /* NULL for a part that is not timed */
    int steps;
    enum Start start;
}
parts[] = {
    {NULL, 90, START_NONE},                 /* the title screen */
    {NULL, 170, START_TOWER},               /* fade in and countdown */
    {"tower still", 600, START_NONE},
    {"tower climb", 600, START_NONE},
    {"map", 480, START_NONE},
    {NULL, 170, START_FINAL_LEVEL},
    {"divot", 450, GO_TO_DIVOT},
};
#define PART_COUNT ((int) SDL_arraysize(parts))

static int part = -1;     /* -1 until the title screen is up */
static int step;          /* steps into the part */
static bool finished;

typedef struct
{
    int frames;
    Uint32 ms;
    int max_ms;
    int over_50;
    int colour_frames;
    Uint32 colour_ms;
}
PartTimes;
static PartTimes times[PART_COUNT];

typedef struct
{
    int part, frame, ms, draw_ms, colstate, steps;
    bool colour_change;
}
SlowFrame;
#define SLOW_KEPT 8
static SlowFrame slowest[SLOW_KEPT];
static int slow_count;

static Uint64 loop_start;
static Uint32 render_start;
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
    case START_FINAL_LEVEL:
        script.startgamemode(Start_TIMETRIAL_FINALLEVEL);
        break;
    case GO_TO_DIVOT:
    {
        const int player = obj.getplayer();
        if (INBOUNDS_VEC(player, obj.entities))
        {
            obj.entities[player].xp = 152;
            obj.entities[player].yp = 100;
            obj.entities[player].lerpoldxp = 152;
            obj.entities[player].lerpoldyp = 100;
        }
        game.gravitycontrol = 0;
        map.gotoroom(108, 109);
        break;
    }
    }
}

static Uint32 keys_for_step(void)
{
    if (parts[part].name == NULL)
    {
        return 0;
    }
    if (SDL_strcmp(parts[part].name, "tower climb") == 0)
    {
        /* Flip every 40 steps, running right then left by turns. */
        const Uint32 run = (step / 120) % 2 == 0 ? KEY_RIGHT : KEY_LEFT;
        return run | (step % 40 == 0 ? KEY_FLIP : 0);
    }
    if (SDL_strcmp(parts[part].name, "map") == 0)
    {
        /* Open the map, wait, close it, wait. */
        return step % 60 < 2 ? KEY_MAP : 0;
    }
    return 0;
}

void POCKET_BenchStep(void)
{
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

void POCKET_BenchLoopStart(void)
{
    loop_start = SDL_GetTicks();
    render_start = POCKET_render_us;
    colstate_start = graphics.towerbg.colstate;
    steps_this_loop = 0;
}

static void keep_slow(const SlowFrame* frame)
{
    if (slow_count == SLOW_KEPT && slowest[SLOW_KEPT - 1].ms >= frame->ms)
    {
        return;
    }
    int at = slow_count < SLOW_KEPT ? slow_count++ : SLOW_KEPT - 1;
    while (at > 0 && slowest[at - 1].ms < frame->ms)
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
    PartTimes* t = &times[part];
    const int ms = (int) (SDL_GetTicks() - loop_start);
    const bool colour_change = graphics.towerbg.colstate != colstate_start;
    t->frames++;
    t->ms += ms;
    t->max_ms = SDL_max(t->max_ms, ms);
    if (ms > 50)
    {
        t->over_50++;
    }
    if (colour_change)
    {
        t->colour_frames++;
        t->colour_ms += ms;
    }
    SlowFrame frame;
    frame.part = part;
    frame.frame = t->frames;
    frame.ms = ms;
    frame.draw_ms = (int) ((POCKET_render_us - render_start) / 1000);
    frame.colstate = graphics.towerbg.colstate;
    frame.steps = steps_this_loop;
    frame.colour_change = colour_change;
    keep_slow(&frame);
}

static void tenths(char* out, size_t size, Uint32 total, int count)
{
    if (count == 0)
    {
        SDL_snprintf(out, size, "    -");
        return;
    }
    const Uint32 value = total * 10 / count;
    SDL_snprintf(out, size, "%3u.%u", (unsigned) (value / 10), (unsigned) (value % 10));
}

void POCKET_BenchDraw(void)
{
    if (!finished)
    {
        return;
    }
    static char lines[24][48];
    static int line_count;
    if (line_count == 0)
    {
        SDL_snprintf(lines[line_count++], 48, "VVVVVV benchmark, ms a frame");
        SDL_snprintf(lines[line_count++], 48, "            frames  avg  col max >50");
        for (int i = 0; i < PART_COUNT; i++)
        {
            if (parts[i].name == NULL)
            {
                continue;
            }
            char avg[8], col[8];
            tenths(avg, sizeof(avg), times[i].ms, times[i].frames);
            tenths(col, sizeof(col), times[i].colour_ms, times[i].colour_frames);
            SDL_snprintf(lines[line_count++], 48, "%-11s %4d %s%s %3d %3d",
                parts[i].name, times[i].frames, avg, col, times[i].max_ms, times[i].over_50);
        }
        SDL_snprintf(lines[line_count++], 48, "col: frames the tower colour changed");
        SDL_snprintf(lines[line_count++], 48, "slowest: ms draw colour steps");
        for (int i = 0; i < slow_count; i++)
        {
            const SlowFrame* f = &slowest[i];
            SDL_snprintf(lines[line_count++], 48, "%-11s #%-4d %3d %3d %2d%s %d",
                parts[f->part].name, f->frame, f->ms, f->draw_ms, f->colstate,
                f->colour_change ? "*" : " ", f->steps);
        }
        SDL_snprintf(lines[line_count++], 48, "full speed is 34 ms a frame");
        SDL_snprintf(lines[line_count++], 48, "done: take a screenshot");
        for (int i = 0; i < line_count; i++)
        {
            printf("%s\n", lines[i]);
        }
    }
    graphics.set_render_target(graphics.gameTexture);
    graphics.fill_rect(0, 0, 0);
    for (int i = 0; i < line_count; i++)
    {
        font::print(0, 4, 4 + i * 10, lines[i], 255, 255, 255);
    }
}

#endif
