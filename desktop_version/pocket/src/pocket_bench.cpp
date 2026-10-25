/* See pocket_bench.h. */
#include "pocket_bench.h"

#ifdef POCKET_BENCH

#include "pocket_bench_report.h"
#include "pocket_bench_profile.h"

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
#include "../../src/KeyPoll.h"
#include "../../src/Xoshiro.h"

extern "C" Uint32 POCKET_bench_keys;
extern "C" bool POCKET_saves_volatile;
extern "C" bool POCKET_tint_runs;
extern "C" bool POCKET_scroll_fast;
extern bool POCKET_text_cache;
extern Uint32 POCKET_print_us, POCKET_print_calls, POCKET_text_hits, POCKET_text_kept;
extern char POCKET_last_kept[48];
extern Uint32 POCKET_bg_us, POCKET_bg_tiles;
extern "C" void POCKET_CountSeeThrough(SDL_Texture* texture, int x, int y, int w, int h, int* clear, int* partial);
PocketBenchProfile POCKET_grav_profile;
bool POCKET_grav_draw_active;

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
    GO_TO_TOWER,
    START_GRAVITRON
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
    /* Wait for the real introduction to finish, then time the complete
     * story-room countdown, including its increasingly dense waves. */
    {NULL, 0, START_GRAVITRON, KEYS_NONE},
    {"gravitron", 1799, START_NONE, KEYS_NONE},
};
#define PART_COUNT ((int) SDL_arraysize(parts))

static int part = -1;     /* -1 until the title screen is up */
static int step;          /* steps into the part */
static bool finished;
static PocketBenchReport report;

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
    Uint32 text_us, prints, text_hits, text_kept; /* font::print's, summed */
    Uint32 bg_us, bg_tiles; /* the tower background's update, summed */
    PocketBenchProfile grav;
    Uint32 draw_other, draw_overlap;
    char last_kept[48];
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
#define SLOW_KEPT 1
static SlowFrame slowest[SLOW_KEPT];
static int slow_count;

static FrameTimes frame_us;
static Uint32 loop_start, step_start, draw_start;
static int colstate_start;
static Uint32 print_us_start, prints_start, hits_start, kept_start;
static Uint32 bg_us_start, bg_tiles_start;
static PocketBenchProfile grav_start;
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
    case START_GRAVITRON:
    {
        game.intimetrial = false;
        game.inintermission = true;
        game.gravitycontrol = 0;
        game.swnstate = game.swnstate2 = game.swnstate3 = game.swnstate4 = 0;
        map.invincibility = true;
        map.finalmode = true;
        map.finalstretch = false;
        map.final_colormode = false;
        map.final_mapcol = 0;
        map.gotoroom(53, 50);
        const int player = obj.getplayer();
        if (INBOUNDS_VEC(player, obj.entities))
        {
            entclass& p = obj.entities[player];
            p.xp = p.lerpoldxp = 148;
            p.yp = p.lerpoldyp = 100;
            p.vx = p.vy = p.ax = p.ay = 0;
        }
        xoshiro_seed(7);
        game.setstate(10); /* Ordinary Gravitron: lines, intro and 60s clock. */
        game.setstatedelay(0);
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
    else if (parts[part].steps == 0
        ? game.swngame == SWN_GRAVITRON
        : ++step >= parts[part].steps)
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
    POCKET_grav_draw_active = !finished && part == PART_COUNT - 1;
    draw_start = of_time_us();
}

void POCKET_BenchDrawEnd(void)
{
    frame_us.draw += of_time_us() - draw_start;
    POCKET_grav_draw_active = false;
}

void POCKET_BenchLoopStart(void)
{
    loop_start = of_time_us();
    POCKET_grav_draw_active = false;
    SDL_zero(frame_us);
    colstate_start = graphics.towerbg.colstate;
    print_us_start = POCKET_print_us;
    prints_start = POCKET_print_calls;
    hits_start = POCKET_text_hits;
    kept_start = POCKET_text_kept;
    bg_us_start = POCKET_bg_us;
    bg_tiles_start = POCKET_bg_tiles;
    grav_start = POCKET_grav_profile;
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
    t->text_us += POCKET_print_us - print_us_start;
    t->prints += POCKET_print_calls - prints_start;
    t->text_hits += POCKET_text_hits - hits_start;
    t->text_kept += POCKET_text_kept - kept_start;
    t->bg_us += POCKET_bg_us - bg_us_start;
    t->bg_tiles += POCKET_bg_tiles - bg_tiles_start;
    const Uint32 draw_before = POCKET_GravDrawTime(&t->grav);
    for (int i = 0; i < POCKET_GRAV_PHASES; i++)
    {
        t->grav.us[i] += POCKET_grav_profile.us[i] - grav_start.us[i];
        t->grav.calls[i] += POCKET_grav_profile.calls[i] - grav_start.calls[i];
    }
    const Uint32 draw_accounted = POCKET_GravDrawTime(&t->grav) - draw_before;
    if (draw_accounted <= frame_us.draw)
        t->draw_other += frame_us.draw - draw_accounted;
    else
        t->draw_overlap += draw_accounted - frame_us.draw;
    if (POCKET_text_kept != kept_start)
    {
        SDL_strlcpy(t->last_kept, POCKET_last_kept, sizeof(t->last_kept));
    }
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


/* Whether text copied from its cached texture is the same, pixel for
 * pixel, as text drawn glyph by glyph: each case is drawn the slow way,
 * then three times with the cache on. */
static int cache_cases, cache_same;

static SDL_Surface* draw_case(uint32_t flags, int x, int y, const char* text, uint8_t r, uint8_t g, uint8_t b, bool clock = false)
{
    graphics.fill_rect(40, 60, 90);
    if (clock)
        font::print_clock(flags, x, y, text, r, g, b);
    else
        font::print(flags, x, y, text, r, g, b);
    return SDL_RenderReadPixels(gameScreen.m_renderer, NULL);
}

static bool same_pixels(const SDL_Surface* a, const SDL_Surface* b)
{
    if (a == NULL || b == NULL || a->w != b->w || a->h != b->h)
    {
        return false;
    }
    for (int y = 0; y < a->h; y++)
    {
        if (SDL_memcmp((const Uint8*) a->pixels + (size_t) y * a->pitch,
            (const Uint8*) b->pixels + (size_t) y * b->pitch, (size_t) a->w * 4) != 0)
        {
            return false;
        }
    }
    return true;
}

static void check_case(uint32_t flags, int x, int y, const char* text, uint8_t r, uint8_t g, uint8_t b, bool clock = false)
{
    POCKET_text_cache = false;
    SDL_Surface* slow = draw_case(flags, x, y, text, r, g, b);
    POCKET_text_cache = true;
    /* Drawn once as it always was, then into the cache, then from it. */
    SDL_Surface* first = draw_case(flags, x, y, text, r, g, b, clock);
    SDL_Surface* again = draw_case(flags, x, y, text, r, g, b, clock);
    SDL_Surface* third = draw_case(flags, x, y, text, r, g, b, clock);
    cache_cases++;
    if (same_pixels(slow, first) && same_pixels(slow, again) && same_pixels(slow, third))
    {
        cache_same++;
    }
    else
    {
        printf("text cache differs: %08x \"%s\"\n", (unsigned) flags, text);
    }
    SDL_DestroySurface(slow);
    SDL_DestroySurface(first);
    SDL_DestroySurface(again);
    SDL_DestroySurface(third);
}

static void check_text_cache(void)
{
    graphics.set_render_target(graphics.tempShakeTexture);
    for (int flip = 0; flip < 2; flip++)
    {
        const bool flipmode = graphics.flipmode;
        graphics.flipmode = flip != 0;
        check_case(0, 10, 10, "Hello, Viridian", 255, 255, 255);
        check_case(0, 10, 10, "Hello, Viridian", 120, 200, 64);
        check_case(PR_BOR, 6, 18, "TIME: 0:12.34", 196, 196, 255);
        check_case(PR_BOR | PR_RIGHT, 300, 30, "DEATH: 7", 255, 255, 255);
        check_case(PR_FULLBOR, 50, 100, "The Tower", 100, 200, 50);
        check_case(PR_2X | PR_BOR | PR_CEN, -1, 20, "SUPER GRAVITRON", 200, 200, 245);
        check_case(PR_3X | PR_BOR | PR_CEN, -1, 200, "GO!", 220, 220, 255);
        check_case(PR_BOR | PR_BRIGHTNESS(128), 2, 0, "Dimmed", 255, 128, 64);
        check_case(PR_BOR, -4, 236, "Edge of the screen", 10, 20, 30);
        const char* clocks[] = {"59.96", "10.00", "9.96", "0.00"};
        for (size_t c = 0; c < SDL_arraysize(clocks); c++)
            check_case(PR_2X | PR_CEN | PR_BOR, -1, 20, clocks[c], 196, 196, 255, true);
        graphics.flipmode = flipmode;
    }
    graphics.set_render_target(NULL);
}

static void cached_letter(void) { font::print(0, 8, 8, "A", 196, 196, 196); }
static void cached_bordered(void) { font::print(PR_BOR, 8, 8, "ABCDEFGHIJ", 196, 196, 196); }

static int clock_micro_tick;
static void original_clock(void)
{
    const std::string text = help.timestring(1799 - clock_micro_tick++ % 60);
    font::print(PR_2X | PR_CEN | PR_BOR, -1, 20, text, 196, 196, 255);
}
static void cached_clock(void)
{
    const std::string text = help.timestring(1799 - clock_micro_tick++ % 60);
    font::print_clock(PR_2X | PR_CEN | PR_BOR, -1, 20, text.c_str(), 196, 196, 255);
}

static void warp_background(void) { graphics.updatebackground(3); }

void POCKET_BenchDraw(void)
{
    if (!finished)
    {
        return;
    }
    if (report.size() == 0)
    {
        report.add("VVVVVV benchmark, ms a frame");
        report.add("part      fr  frame logic  draw   col");
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
            report.add("%-9s%4d %s %s %s %s",
                parts[i].name, t->frames, total, logic, draw, col);
        }
        report.add("a frame: tower bg update ms, tiles");
        for (int i = 0; i < PART_COUNT; i++)
        {
            const PartTimes* t = &times[i];
            if (parts[i].name == NULL || t->frames == 0)
            {
                continue;
            }
            char bg[8];
            ms(bg, sizeof(bg), t->bg_us, t->frames);
            report.add("%-9s %s %5u", parts[i].name, bg,
                (unsigned) (t->bg_tiles / t->frames));
        }
        report.add("a frame: text ms, prints, cached, new");
        for (int i = 0; i < PART_COUNT; i++)
        {
            const PartTimes* t = &times[i];
            if (parts[i].name == NULL || t->frames == 0)
            {
                continue;
            }
            char text[8];
            ms(text, sizeof(text), t->text_us, t->frames);
            /* Counts a frame, to a tenth. */
            const unsigned prints = t->prints * 10 / t->frames;
            const unsigned hits = t->text_hits * 10 / t->frames;
            const unsigned kept = t->text_kept * 10 / t->frames;
            report.add("%-9s %s %3u.%u %3u.%u %2u.%u", parts[i].name, text,
                prints / 10, prints % 10, hits / 10, hits % 10, kept / 10, kept % 10);
        }
        report.add("Gravitron split: ms/frame, calls/frame");
        static const char* phase_names[] = {
            "bg update", "bg draw", "enemy spawn", "enemy move",
            "enemy collide", "enemy animate", "enemy draw"
        };
        const PartTimes* grav = &times[PART_COUNT - 1];
        for (int i = 0; i < POCKET_GRAV_DETAIL_PHASES; i++)
        {
            char elapsed[8];
            ms(elapsed, sizeof(elapsed), grav->grav.us[i], grav->frames);
            const unsigned calls = grav->frames ? grav->grav.calls[i] * 10 / grav->frames : 0;
            report.add("%-14s %s %3u.%u", phase_names[i], elapsed, calls / 10, calls % 10);
        }
        report.add("split times are included in frame totals");
        report.add("Gravitron draw: ms/frame, no overlap");
        static const char* draw_names[] = {
            "clear/setup", "background", "map/foreground", "all entities",
            "HUD/overlays", "gameplay copy", "window/effects", "present API", "post clear"
        };
        for (int i = POCKET_GRAV_DRAW_SETUP; i < POCKET_GRAV_DRAW_END; i++)
        {
            char elapsed[8];
            ms(elapsed, sizeof(elapsed), grav->grav.us[i], grav->frames);
            report.add("%-15s %s", draw_names[i - POCKET_GRAV_DRAW_SETUP], elapsed);
        }
        char accounted[8], total_draw[8], other[8], overlap[8], flip[8], audio[8];
        ms(accounted, sizeof(accounted), POCKET_GravDrawTime(&grav->grav), grav->frames);
        ms(total_draw, sizeof(total_draw), grav->sum.draw, grav->frames);
        ms(other, sizeof(other), grav->draw_other, grav->frames);
        ms(overlap, sizeof(overlap), grav->draw_overlap, grav->frames);
        ms(flip, sizeof(flip), grav->grav.us[POCKET_GRAV_VIDEO_FLIP], grav->frames);
        ms(audio, sizeof(audio), grav->grav.us[POCKET_GRAV_AUDIO], grav->frames);
        report.add("draw sum %s / total %s", accounted, total_draw);
        report.add("other %s overlap %s", other, overlap);
        report.add("in present: flip %s audio %s", flip, audio);
        report.add("flip/audio are inside present API");
        report.add("slowest:   frame logic draw colour");
        for (int i = 0; i < slow_count; i++)
        {
            const SlowFrame* f = &slowest[i];
            report.add("%-9s#%-4d %3u %3u %3u %2d%s",
                parts[f->part].name, f->frame, (unsigned) (f->us.total / 1000),
                (unsigned) (f->us.logic / 1000), (unsigned) (f->us.draw / 1000),
                f->colstate, f->colour_change ? "*" : "");
        }
        /* What text costs: drawing it is most of a frame with the trial's
         * clock and counters up. Glyph by glyph first, then cached. */
        POCKET_text_cache = false;
        const Uint32 tinted = text_us(grey_word, 100, 10);
        const Uint32 letter = text_us(grey_letter, 1000, 1);
        const Uint32 tile = text_us(grey_tile, 1000, 1);
        const Uint32 copy = text_us(grey_copy, 1000, 1);
        const Uint32 bordered = text_us(bordered_word, 20, 1);
        POCKET_text_cache = true;
        const Uint32 cached_word = text_us(grey_word, 100, 1);
        const Uint32 cached_one = text_us(cached_letter, 1000, 1);
        const Uint32 cached_bor = text_us(cached_bordered, 100, 1);
        check_text_cache();
        report.add("glyph us %u, 1 letter %u tile %u copy %u",
            (unsigned) tinted, (unsigned) letter, (unsigned) tile, (unsigned) copy);
        report.add("cached word %u (%u) letter %u (%u)",
            (unsigned) cached_word, (unsigned) (tinted * 10), (unsigned) cached_one, (unsigned) letter);
        report.add("bordered word %u, cached %u us",
            (unsigned) bordered, (unsigned) cached_bor);
        report.add("text cache same: %d of %d",
            cache_same, cache_cases);
        clock_micro_tick = 0;
        const Uint32 clock_old = text_us(original_clock, 60, 1);
        clock_micro_tick = 0;
        const Uint32 clock_new = text_us(cached_clock, 60, 1);
        report.add("changing clock us: old %u new %u", (unsigned) clock_old, (unsigned) clock_new);
        POCKET_scroll_fast = false;
        graphics.backgrounddrawn = false;
        const Uint32 background_old = text_us(warp_background, 60, 1);
        POCKET_scroll_fast = true;
        graphics.backgrounddrawn = false;
        const Uint32 background_new = text_us(warp_background, 60, 1);
        report.add("warp bg us: old %u new %u", (unsigned) background_old, (unsigned) background_new);
        /* Whether the tower's background tiles could be copied as they
         * are, needing nothing under them. */
        int tiles = 0, clear_tiles = 0, partial_tiles = 0;
        int sheet_w, sheet_h;
        if (graphics.query_texture(graphics.grphx.im_tiles3, NULL, NULL, &sheet_w, &sheet_h))
        {
            for (int y = 0; y + 8 <= sheet_h; y += 8)
            {
                for (int x = 0; x + 8 <= sheet_w; x += 8)
                {
                    int clear, partial;
                    POCKET_CountSeeThrough(graphics.grphx.im_tiles3, x, y, 8, 8, &clear, &partial);
                    tiles++;
                    clear_tiles += clear > 0 && clear < 64;
                    partial_tiles += partial > 0;
                }
            }
        }
        report.add("tiles3 %d: %d part clear, %d see-thru",
            tiles, clear_tiles, partial_tiles);
        report.add("new: %.38s", times[2].last_kept);
        report.add("full speed is 34 ms; screenshot me");
        for (int i = 0; i < report.size(); i++)
        {
            printf("%s\n", report.line(i));
        }
    }
    /* The frame has been copied to the window already, so the results are
     * drawn there. */
    graphics.set_render_target(NULL);
    graphics.fill_rect(0, 0, 0);
    report.next(key.isDown(SDL_GAMEPAD_BUTTON_SOUTH) || key.isDown(SDLK_SPACE));
    for (int i = report.first(); i < report.end(); i++)
    {
        font::print(0, 4, 2 + (i - report.first()) * 8, report.line(i), 255, 255, 255);
    }
    char footer[48];
    SDL_snprintf(footer, sizeof(footer), "page %d/%d  A: next  screenshot each page",
        report.page() + 1, report.pages());
    font::print(0, 4, 232, footer, 255, 255, 255);
}

#endif
