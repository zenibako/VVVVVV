/* Opt-in host execution check, not a performance measurement. Including
 * the real script lets this check observe its transitions and results. */
#include "../src/pocket_bench.cpp"
#include <assert.h>
#include <stdlib.h>

extern "C"
{
Uint32 POCKET_bench_keys;
bool POCKET_saves_volatile;
#ifndef POCKET_SMOKE_RENDER
bool POCKET_tint_runs;
bool POCKET_scroll_fast = true;
bool POCKET_warp_tiles_fast = true;
bool POCKET_copy_fast = true;
bool POCKET_column_ring = true;
bool POCKET_tower_grid_fast = true;
/* Host rendering has no Pocket-only text cache or transparent-run index. */
void POCKET_CountSeeThrough(SDL_Texture*, int, int, int, int, int* clear, int* partial)
{
    *clear = *partial = 0;
}
#endif
/* Advance a test clock instead of waiting several real minutes. */
Uint64 SDL_GetTicks(void) { static Uint64 ticks; return ticks += 17; }
void SDL_Delay(Uint32) {}
unsigned int of_time_us(void) { static unsigned int ticks; return ++ticks; }
unsigned int of_time_ms(void) { return of_time_us() / 1000; }
}

static bool saw_enemy, saw_active, captured_room, captured_tower;
static int max_enemies, renders, result_frames, captured_pages;
static PocketBenchProfile results_profile;
static bool near_end_sampled;
static Uint32 near_end_present_calls;
static Uint32 sent_keys;
static bool save_capture(SDL_Surface* surface, const char* path);

#include "clock_pixels.h"
#include "background_pixels.h"
#include "tower_pixels.h"
#include "font_alpha_pixels.h"

static void check_window_copy(void)
{
#ifdef POCKET_SMOKE_RENDER
    const bool enabled = POCKET_copy_fast;
    POCKET_copy_fast = false;
    window_copy();
    SDL_Surface* original = SDL_RenderReadPixels(gameScreen.m_renderer, NULL);
    POCKET_copy_fast = true;
    window_copy();
    SDL_Surface* optimized = SDL_RenderReadPixels(gameScreen.m_renderer, NULL);
    assert(original && optimized && same_pixels(original, optimized));
    SDL_DestroySurface(original);
    SDL_DestroySurface(optimized);
    Uint64 elapsed[2];
    for (int mode = 0; mode < 2; mode++)
    {
        POCKET_copy_fast = mode != 0;
        const Uint64 start = SDL_GetPerformanceCounter();
        for (int i = 0; i < 1000; i++) window_copy();
        elapsed[mode] = (SDL_GetPerformanceCounter() - start) * 1000000 / SDL_GetPerformanceFrequency() / 1000;
    }
    POCKET_copy_fast = enabled;
    printf("Window copy pixels passed; host wall time us/copy: rows %llu, packed %llu (not Pocket timing)\n",
        (unsigned long long) elapsed[0], (unsigned long long) elapsed[1]);
#endif
}

static bool smoke_present(SDL_Renderer* renderer)
{
    assert(!game.over30mode);
    if (getenv("POCKET_TEST_FONT_ALPHA") != NULL)
    {
        check_font_alpha_pixels();
        exit(0);
    }
    assert(++renders < 30000);
    /* Exercise the real keyboard input path for the existing script. */
    static const SDL_Keycode keys[] = {SDLK_LEFT, SDLK_RIGHT, SDLK_Z, SDLK_RETURN};
    for (int i = 0; i < 4; i++)
    {
        if ((sent_keys ^ POCKET_bench_keys) & (1u << i))
        {
            SDL_Event event;
            SDL_zero(event);
            event.type = POCKET_bench_keys & (1u << i) ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            event.key.key = keys[i];
            event.key.down = event.type == SDL_EVENT_KEY_DOWN;
            SDL_PushEvent(&event);
        }
    }
    sent_keys = POCKET_bench_keys;
    if (!captured_tower && part >= 0 && part < PART_COUNT && parts[part].name != NULL
        && SDL_strcmp(parts[part].name, "tower") == 0 && step >= 300)
    {
        SDL_Surface* screen = SDL_RenderReadPixels(renderer, NULL);
        assert(screen && save_capture(screen, "tower-room.bmp"));
        SDL_DestroySurface(screen);
        captured_tower = true;
    }
    if (game.swnmode && game.swngame == SWN_GRAVITRON)
    {
        saw_active = true;
        assert(game.roomx == 53 && game.roomy == 50);
        assert(map.background == 3);
        assert(map.finalmode && !map.towermode && map.invincibility);
        assert(game.deathseq == -1);
        int enemies = 0;
        for (size_t i = 0; i < obj.entities.size(); i++)
            enemies += obj.entities[i].type == EntityType_GRAVITRON_ENEMY;
        saw_enemy |= enemies > 0;
        if (enemies > max_enemies) max_enemies = enemies;
        if (game.swntimer <= 30)
        {
            const Uint32 present_calls = POCKET_grav_profile.calls[POCKET_GRAV_DRAW_PRESENT];
            if (near_end_sampled)
                assert(present_calls > near_end_present_calls);
            near_end_present_calls = present_calls;
            near_end_sampled = true;
        }
        if (!captured_room && game.swntimer < 900)
        {
            SDL_Surface* screen = SDL_RenderReadPixels(renderer, NULL);
            assert(screen != NULL && save_capture(screen, "gravitron-room.bmp"));
            SDL_DestroySurface(screen);
            captured_room = true;
        }
    }
    if (finished)
    {
        assert(saw_active && saw_enemy && captured_room && captured_tower);
        const PartTimes& gravitron = times[PART_COUNT - 1];
        assert(gravitron.frames > 0);
        assert(gravitron.room[0] == 53 && gravitron.room[1] == 50);
        for (int i = 0; i < POCKET_GRAV_DRAW_END; i++)
        {
            assert(gravitron.grav.calls[i] > 0);
            assert(gravitron.grav.us[i] > 0);
        }
        assert(near_end_sampled);
        assert(gravitron.draw_overlap == 0);
        assert(POCKET_GravDrawTime(&gravitron.grav) + gravitron.draw_other == gravitron.sum.draw);
        assert(!POCKET_grav_draw_active);
        if (result_frames == 0)
            results_profile = POCKET_grav_profile;
        else
            for (int i = 0; i < POCKET_GRAV_PHASES; i++)
            {
                assert(POCKET_grav_profile.us[i] == results_profile.us[i]);
                assert(POCKET_grav_profile.calls[i] == results_profile.calls[i]);
            }
        assert(game.swntimer <= 1);
        if (result_frames % 3 == 0)
        {
            assert(report.page() == captured_pages);
            SDL_Surface* screen = SDL_RenderReadPixels(renderer, NULL);
            assert(screen != NULL);
            char path[32];
            SDL_snprintf(path, sizeof(path), "results-%d.bmp", ++captured_pages);
            assert(save_capture(screen, path));
            SDL_DestroySurface(screen);
        }
        if ((result_frames % 3 == 0 && captured_pages < report.pages()) || result_frames % 3 == 1)
        {
            SDL_Event event;
            SDL_zero(event);
            event.type = result_frames % 3 == 0 ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            event.key.key = SDLK_SPACE;
            event.key.down = event.type == SDL_EVENT_KEY_DOWN;
            SDL_PushEvent(&event);
        }
        if (++result_frames == report.pages() * 3)
        {
            assert(captured_pages == report.pages());
            check_clock_pixels();
            check_background_pixels();
            check_tower_pixels();
            check_window_copy();
            printf("Draw accounting passed: %d rows, %d pages, no overlap; results do not change counters\n",
                report.size(), report.pages());
            printf("Gravitron smoke passed: %d frames, max %d enemies, timer %d, room %d,%d\n",
                gravitron.frames, max_enemies, game.swntimer, game.roomx, game.roomy);
            exit(0);
        }
    }
    return true;
}

#ifdef POCKET_SMOKE_RENDER
extern "C" Uint16* POCKET_FrameBuffer(int* pitch)
{
    static Uint16 pixels[320 * 240];
    *pitch = 320;
    return pixels;
}
extern "C" void POCKET_PresentFrame(void) { smoke_present(gameScreen.m_renderer); }
/* Native SDL's BMP writer expects its own surface bookkeeping. Wrap the
 * port's public pixel buffer without taking ownership of those pixels. */
#undef SDL_CreateSurfaceFrom
#undef SDL_DestroySurface
extern "C" SDL_Surface* SDL_CreateSurfaceFrom(int, int, SDL_PixelFormat, void*, int);
extern "C" void SDL_DestroySurface(SDL_Surface*);
static bool save_capture(SDL_Surface* surface, const char* path)
{
    SDL_Surface* native = SDL_CreateSurfaceFrom(surface->w, surface->h, surface->format, surface->pixels, surface->pitch);
    if (native == NULL) return false;
    const bool ok = SDL_SaveBMP(native, path);
    SDL_DestroySurface(native);
    return ok;
}
#else
extern "C" bool SDL_RenderPresent(SDL_Renderer* renderer) { return smoke_present(renderer); }
static bool save_capture(SDL_Surface* surface, const char* path) { return SDL_SaveBMP(surface, path); }
#endif
