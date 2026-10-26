#include "pocket_bench.h"
#if defined(POCKET_BENCH) && defined(POCKET_FINAL_SWEEP)
#include "pocket_final_rooms.h"
#include "pocket_bench_report.h"
#include "pocket_bench_profile.h"
#include "of_timer.h"
#include "src/Game.h"
#include "src/Map.h"
#include "src/Entity.h"
#include "src/Graphics.h"
#include "src/Screen.h"
#include "src/Script.h"
#include "src/Enums.h"
#include "src/Font.h"
#include "src/KeyPoll.h"
#include "src/Xoshiro.h"
#include "src/UtilityClass.h"
#include <stdlib.h>
#include <stdio.h>

extern "C" Uint32 POCKET_bench_keys;
extern "C" bool POCKET_saves_volatile;
extern Uint32 POCKET_bg_scroll;
PocketBenchProfile POCKET_grav_profile;
bool POCKET_grav_draw_active;

enum { SWEEP_ROOMS = sizeof(final_rooms) / sizeof(final_rooms[0]),
    SWEEP_WARMUP = 12, SWEEP_FRAMES = 240 };
static const int sweep_cameras[] = {0, 192, 384, 568, -1};
struct SweepResult
{
    Uint32 average, logic, draw, worst, peak_logic, peak_draw;
    int camera, entry, views;
};
static SweepResult sweep_results[SWEEP_ROOMS * 2];
static PocketBenchReport sweep_report;
static char sweep_status[48];
static int sweep_room, sweep_mode, sweep_variant, sweep_tick = -1;
static int sweep_x, sweep_y, sweep_camera, sweep_entry_y, sweep_cases, sweep_scroll_cases;
static int sweep_scroll_x[72], sweep_scroll_y[72];
static bool sweep_scrolling;
static int sweep_previous_camera, sweep_scroll_frames;
static unsigned sweep_colours, sweep_phases;
static Uint32 sweep_scroll_calls_start;
static bool sweep_started, sweep_done, sweep_failed, sweep_stepped;
static Uint32 sweep_loop_start, sweep_logic_start, sweep_draw_start, sweep_logic_us, sweep_draw_us;
static Uint32 sweep_overlay_us;
static Uint64 sweep_sum, sweep_logic_sum, sweep_draw_sum;
static Uint32 sweep_worst, sweep_peak_logic, sweep_peak_draw;
static int sweep_frames;
#ifdef POCKET_FINAL_SWEEP_ALL
enum { SWEEP_CASES = 104, SWEEP_VIEWS_PER_TOWER = 10, SWEEP_REPORT_ROWS = 80 };
static bool sweep_selected(int, int) { return true; }
#else
enum { SWEEP_CASES = 14, SWEEP_VIEWS_PER_TOWER = 2, SWEEP_REPORT_ROWS = 26 };
static bool sweep_selected(int room, int mode)
{ return final_rooms[room].focus && (final_rooms[room].tower || mode == 1); }
#endif

static bool sweep_select_room(void)
{
    while (sweep_mode < 2 && !sweep_selected(sweep_room,sweep_mode))
        if (++sweep_room == SWEEP_ROOMS) { sweep_room=0; sweep_mode++; }
    if (sweep_mode == 2) { sweep_done=true; return false; }
#ifndef POCKET_FINAL_SWEEP_ALL
    if(final_rooms[sweep_room].tower) sweep_variant=4;
#endif
    return true;
}

static bool sweep_position_clear(int x, int y)
{
    const int player = obj.getplayer();
    if (!INBOUNDS_VEC(player, obj.entities)) return false;
    const entclass& p = obj.entities[player];
    SDL_Rect box = {x + p.cx - 8, y + p.cy - 8, p.w + 16, p.h + 16};
    for (size_t i = 0; i < obj.blocks.size(); i++)
    {
        const blockclass& b = obj.blocks[i];
        if (b.type == TRIGGER && box.x < b.rect.x + b.rect.w && box.x + box.w > b.rect.x
            && box.y < b.rect.y + b.rect.h && box.y + box.h > b.rect.y)
            return false;
    }
    for (int ty = (y + p.cy) / 8; ty <= (y + p.cy + p.h - 1) / 8; ty++)
        for (int tx = (x + p.cx) / 8; tx <= (x + p.cx + p.w - 1) / 8; tx++)
            if (map.collide(tx, ty, true)) return false;
    return true;
}

static bool sweep_choose_position(int camera, int& px, int& py)
{
    int best = 100000;
    for (int y = camera + 24; y <= camera + 208; y += 8)
        for (int x = 16; x <= 288; x += 8)
            if (sweep_position_clear(x, y))
            {
                const int distance = abs(x - 150) + abs(y - camera - 120);
                if (distance < best) { best = distance; px = x; py = y; }
            }
    return best != 100000;
}

static int sweep_duration(void) { return sweep_scrolling ? 360 : SWEEP_FRAMES; }

static void sweep_pin_player(void)
{
    const int player = obj.getplayer();
    if (!INBOUNDS_VEC(player, obj.entities)) return;
    entclass& p = obj.entities[player];
    p.xp = p.lerpoldxp = sweep_x;
    p.yp = p.lerpoldyp = sweep_y;
    p.vx = p.vy = p.ax = p.ay = 0;
    if (map.towermode)
    {
        map.ypos = map.oldypos = sweep_scrolling ? sweep_previous_camera : sweep_camera;
        const int delta = sweep_camera - sweep_previous_camera;
        map.cameramode = sweep_scrolling && delta != 0 ? 1 : 0;
        if (sweep_scrolling && delta != 0)
            graphics.towerbg.scrolldir = delta > 0 ? 1 : 0;
        map.setbgobjlerp(graphics.towerbg);
    }
}

static void sweep_load(void)
{
    const FinalSweepRoom& room = final_rooms[sweep_room];
    game.gamestate = GAMEMODE;
    game.intimetrial = game.inintermission = game.swnmode = false;
    game.swngame = SWN_NONE;
    game.supercrewmate = false;
    game.companion = 0;
    game.gravitycontrol = 0;
    game.hascontrol = true;
    game.startscript = script.running = false;
    game.noflashingmode = game.colourblindmode = false;
    game.blackout = game.completestop = false;
    game.deathseq = -1;
    game.flashlight = game.screenshake = 0;
    game.framecounter = 0;
    game.setstate(0);
    game.setstatedelay(0);
    graphics.fademode = FADE_NONE;
    graphics.fadeamount = graphics.oldfadeamount = 0;
    map.invincibility = map.finalmode = true;
    map.finalstretch = map.final_colormode = sweep_mode != 0;
    map.final_mapcol = map.final_colorframe = map.final_colorframedelay = 0;
    map.final_aniframe = 0;
    map.final_aniframedelay = 2;
    map.colstatedelay = map.colsuperstate = 0;
    graphics.towerbg.bypos = 0;
    obj.flags[72] = sweep_mode != 0;
    srand(7);
    xoshiro_seed(7);
    sweep_camera = room.tower ? sweep_cameras[sweep_variant % 5] : 0;
    sweep_scrolling = sweep_camera == -1;
    sweep_entry_y = room.y + (room.tower && sweep_variant >= 5 ? 1 : 0);
    map.gotoroom(room.x, sweep_entry_y);
    if (sweep_scrolling)
    {
        for (int i = 0; i < 72; i++)
            if (!sweep_choose_position(i * 8, sweep_scroll_x[i], sweep_scroll_y[i]))
            { sweep_failed = sweep_done = true; return; }
        sweep_camera = sweep_variant >= 5 ? 568 : 0;
        sweep_x = sweep_scroll_x[sweep_camera/8]; sweep_y = sweep_scroll_y[sweep_camera/8];
    }
    else if (!sweep_choose_position(sweep_camera, sweep_x, sweep_y))
    { sweep_failed = sweep_done = true; return; }
    sweep_previous_camera = sweep_camera;
    sweep_pin_player();
    sweep_tick = -1;
    sweep_sum = sweep_logic_sum = sweep_draw_sum = 0;
    sweep_worst = sweep_peak_logic = sweep_peak_draw = 0;
    sweep_frames = 0;
    sweep_colours = sweep_phases = 0;
    sweep_scroll_frames = 0;
    sweep_scroll_calls_start = POCKET_bg_scroll;
}

static void sweep_finish_case(void)
{
    if (sweep_frames != sweep_duration()
        || (sweep_scrolling && (sweep_scroll_frames < 350 || POCKET_bg_scroll - sweep_scroll_calls_start < 250))
        || (sweep_mode && ((sweep_colours & 126) != 126 || sweep_phases != 15)))
    {
        sweep_failed = sweep_done = true;
        return;
    }
    SweepResult& result = sweep_results[sweep_mode * SWEEP_ROOMS + sweep_room];
    const Uint32 average = (Uint32) (sweep_sum / sweep_frames);
    if (result.views == 0 || average > result.average)
    {
        result.average = average;
        result.logic = (Uint32) (sweep_logic_sum / sweep_frames);
        result.draw = (Uint32) (sweep_draw_sum / sweep_frames);
        result.camera = sweep_scrolling ? -1 : sweep_camera;
        result.entry = sweep_entry_y;
    }
    if (result.views == 0 || sweep_worst > result.worst)
    {
        result.worst = sweep_worst;
        result.peak_logic = sweep_peak_logic;
        result.peak_draw = sweep_peak_draw;
    }
    result.views++;
    sweep_cases++;
    if (sweep_scrolling) sweep_scroll_cases++;
#ifdef OF_PC
    printf("SWEEP_VIEW,%d,%d,%d,%d,%d,%d,%u,%u,%u,%u,\"%s\"\n",
        sweep_mode, final_rooms[sweep_room].x, final_rooms[sweep_room].y,
        sweep_entry_y, sweep_scrolling ? -1 : sweep_camera, sweep_frames, average,
        (unsigned)(sweep_logic_sum / sweep_frames), (unsigned)(sweep_draw_sum / sweep_frames),
        sweep_worst, final_rooms[sweep_room].name);
    fflush(stdout);
#endif
}

void POCKET_BenchStep(void)
{
    POCKET_saves_volatile = true;
    POCKET_bench_keys = 0;
    if (sweep_done) return;
    if (!sweep_started)
    {
        if (game.gamestate != TITLEMODE) return;
        script.startgamemode(Start_TIMETRIAL_TOWER);
        sweep_started = true;
        if(!sweep_select_room())return;
        sweep_load();
    }
    if (++sweep_tick == SWEEP_WARMUP + sweep_duration())
    {
        sweep_finish_case();
        if (sweep_failed) return;
        bool next_room;
#ifdef POCKET_FINAL_SWEEP_ALL
        next_room = ++sweep_variant == (final_rooms[sweep_room].tower ? 10 : 1);
#else
        next_room = !final_rooms[sweep_room].tower || sweep_variant == 9;
        if(!next_room)sweep_variant=9;
#endif
        if (next_room)
        {
            sweep_variant = 0;
            if (++sweep_room == SWEEP_ROOMS)
            {
                sweep_room = 0;
                if (++sweep_mode == 2) { sweep_done = true; return; }
            }
            if(!sweep_select_room())return;
        }
        sweep_load();
        sweep_tick = 0;
    }
    if (sweep_scrolling)
    {
        const int position = SDL_max(0, sweep_tick - SWEEP_WARMUP) * 2 % 1136;
        sweep_camera = position <= 568 ? position : 1136 - position;
        if (sweep_variant >= 5) sweep_camera = 568 - sweep_camera;
        sweep_x = sweep_scroll_x[sweep_camera / 8];
        sweep_y = sweep_scroll_y[sweep_camera / 8];
    }
    sweep_pin_player();
    sweep_logic_start = of_time_us();
    /* Cover all six palettes at the native 40-step cadence, rather than
     * depend on a random short run happening to choose the expensive one. */
    if (sweep_mode && sweep_tick >= SWEEP_WARMUP)
    {
        const int colour = 1 + ((sweep_tick - SWEEP_WARMUP) / 40) % 6;
        if (colour != map.final_mapcol) map.changefinalcol(colour);
    }
    sweep_stepped = true;
}

void POCKET_BenchStepEnd(void)
{
    if (!sweep_stepped || sweep_done) return;
    sweep_logic_us += of_time_us() - sweep_logic_start;
    const FinalSweepRoom& room = final_rooms[sweep_room];
    if (game.roomx != room.x || game.roomy != room.y || game.state != 0
        || script.running || game.startscript || game.gamestate != GAMEMODE || game.deathseq != -1
        || map.towermode != room.tower || (room.tower && map.ypos != sweep_camera))
    {
#ifdef OF_PC
        fprintf(stderr, "Unsafe sweep fixture %d,%d view %d tick %d: actual %d,%d state %d script %d\n",
            room.x, room.y, sweep_camera, sweep_tick, game.roomx, game.roomy, game.state, script.running);
#endif
        sweep_failed = sweep_done = true;
    }
    sweep_previous_camera = map.ypos;
}
void POCKET_BenchLoopStart(void)
{
    sweep_loop_start = of_time_us();
    sweep_stepped = false;
    sweep_logic_us = sweep_draw_us = 0;
    sweep_overlay_us = 0;
}
void POCKET_BenchLoopEnd(void)
{
    if (!sweep_stepped || sweep_done || sweep_tick < SWEEP_WARMUP) return;
    const Uint32 elapsed = of_time_us() - sweep_loop_start - sweep_overlay_us;
    sweep_sum += elapsed;
    sweep_logic_sum += sweep_logic_us;
    sweep_draw_sum += sweep_draw_us;
    if (elapsed > sweep_worst)
    {
        sweep_worst = elapsed;
        sweep_peak_logic = sweep_logic_us;
        sweep_peak_draw = sweep_draw_us;
    }
    sweep_frames++;
    if (sweep_scrolling && graphics.towerbg.bscroll != 0) sweep_scroll_frames++;
    sweep_colours |= 1u << map.final_mapcol;
    sweep_phases |= 1u << map.final_aniframe;
}
void POCKET_BenchDrawStart(void) { sweep_draw_start = of_time_us(); }
void POCKET_BenchDrawEnd(void) { sweep_draw_us += of_time_us() - sweep_draw_start - sweep_overlay_us; }
void POCKET_BenchDraw(void)
{
    if (!sweep_done)
    {
        if (!sweep_started) return;
        const Uint32 overlay_start = of_time_us();
        const int progress = SDL_max(0,sweep_tick-SWEEP_WARMUP+1)/20*20;
        SDL_snprintf(sweep_status,sizeof(sweep_status),"Sweep v4 %3d/%d %c %02d,%02d %3d/%d",
            sweep_cases+1,SWEEP_CASES,sweep_mode?'+':'-',final_rooms[sweep_room].x,final_rooms[sweep_room].y,
            progress,sweep_duration());
        graphics.set_render_target(NULL);
        SDL_SetRenderDrawColor(gameScreen.m_renderer,0,0,0,255);
        const SDL_FRect band = {0,0,320,10};
        SDL_RenderFillRect(gameScreen.m_renderer,&band);
        font::print(0,4,0,sweep_status,255,255,255);
        sweep_overlay_us += of_time_us()-overlay_start;
        return;
    }
    if (sweep_report.size() == 0)
    {
        sweep_report.add(SWEEP_CASES==104?"Full sweep v4: 34 rooms, worst view first":"Final focus v4: 8 problem rooms");
        sweep_report.add("+ stabilizer off; - on; times in ms");
        sweep_report.add("room +/- mean peak name");
        if (sweep_failed) sweep_report.add("INVALID FIXTURE: no ranking available");
        else
        {
            int order[SWEEP_ROOMS * 2], count=0;
            for (int i = 0; i < SWEEP_ROOMS * 2; i++)
            {
                if(sweep_results[i].views==0)continue;
                int at = count++;
                while (at > 0 && sweep_results[order[at-1]].average < sweep_results[i].average)
                { order[at] = order[at-1]; at--; }
                order[at] = i;
            }
            for (int i = 0; i < count; i++)
            {
                const int id = order[i];
                const FinalSweepRoom& room = final_rooms[id % SWEEP_ROOMS];
                const SweepResult& r = sweep_results[id];
                sweep_report.add("%02d,%02d %c%5.1f %5.1f %.24s", room.x, room.y,
                    id >= SWEEP_ROOMS ? '+' : '-', r.average/1000.0, r.worst/1000.0, room.name);
            }
            sweep_report.add("Top 3: logic/draw ms; y=-1 is scrolling");
            for (int i = 0; i < 3; i++)
            {
                const int id = order[i];
                const FinalSweepRoom& room = final_rooms[id % SWEEP_ROOMS];
                const SweepResult& r = sweep_results[id];
                sweep_report.add("%02d,%02d %c L%4.1f D%4.1f y%3d", room.x, room.y,
                    id >= SWEEP_ROOMS ? '+' : '-', r.logic/1000.0, r.draw/1000.0, r.camera);
            }
#ifndef POCKET_FINAL_SWEEP_ALL
            sweep_report.add("Top 3 peak frames: logic/draw ms");
            for (int i = 0; i < 3; i++)
            {
                const int id = order[i];
                const FinalSweepRoom& room = final_rooms[id % SWEEP_ROOMS];
                const SweepResult& r = sweep_results[id];
                sweep_report.add("%02d,%02d %c L%4.1f D%4.1f", room.x, room.y,
                    id >= SWEEP_ROOMS ? '+' : '-', r.peak_logic/1000.0, r.peak_draw/1000.0);
            }
#endif
            sweep_report.add("The Final Challenge: logic/draw ms");
            for (int mode = 0; mode < 2; mode++)
                for (int i = 0; i < SWEEP_ROOMS; i++)
                    if (final_rooms[i].x == 51 && final_rooms[i].y == 53)
                    {
                        const SweepResult& r = sweep_results[mode * SWEEP_ROOMS + i];
                        sweep_report.add("%c L%4.1f D%4.1f y%3d", mode ? '+' : '-',
                            r.logic/1000.0, r.draw/1000.0, r.camera);
                    }
        }
        sweep_report.add(SWEEP_CASES==104?"both tower entrances; static + scroll views":"both towers/states/entrances; scrolling");
        sweep_report.add("240/360 frames/view; budget 34 ms; A: next");
    }
    sweep_report.next(key.isDown(SDL_GAMEPAD_BUTTON_SOUTH) || key.isDown(SDLK_SPACE));
    /* gamerender has already copied the frame to the window. Match the
     * ordinary benchmark: draw the report onto the presented backbuffer. */
    graphics.set_render_target(NULL);
    graphics.fill_rect(0,0,0);
    int y = 0;
    for (int i = sweep_report.first(); i < sweep_report.end(); i++, y += 8)
        font::print(0, 4, y, sweep_report.line(i), 255,255,255);
    char footer[48];
    SDL_snprintf(footer,sizeof(footer),"page %d/%d  A: next  screenshot each page",sweep_report.page()+1,sweep_report.pages());
    font::print(0,4,232,footer,255,255,255);
}
#endif
