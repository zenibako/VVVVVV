/* Opt-in host execution check, not a performance measurement. Including
 * the real script lets this check observe its transitions and results. */
#include "../src/pocket_bench.cpp"
#include <assert.h>
#include <stdlib.h>

extern "C"
{
Uint32 POCKET_bench_keys;
bool POCKET_saves_volatile;
bool POCKET_tint_runs;
/* Host rendering has no Pocket-only text cache or transparent-run index. */
void POCKET_CountSeeThrough(SDL_Texture*, int, int, int, int, int* clear, int* partial)
{
    *clear = *partial = 0;
}
/* Advance a test clock instead of waiting several real minutes. */
Uint64 SDL_GetTicks(void) { static Uint64 ticks; return ticks += 17; }
void SDL_Delay(Uint32) {}
unsigned int of_time_us(void) { static unsigned int ticks; return ++ticks; }
unsigned int of_time_ms(void) { return of_time_us() / 1000; }
}

static bool saw_enemy, saw_active, captured_room;
static int max_enemies, renders, result_frames;
static Uint32 sent_keys;

extern "C" bool SDL_RenderPresent(SDL_Renderer* renderer)
{
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
    if (game.swnmode && game.swngame == SWN_GRAVITRON)
    {
        saw_active = true;
        assert(game.roomx == 53 && game.roomy == 50);
        assert(map.finalmode && !map.towermode && map.invincibility);
        assert(game.deathseq == -1);
        int enemies = 0;
        for (size_t i = 0; i < obj.entities.size(); i++)
            enemies += obj.entities[i].type == EntityType_GRAVITRON_ENEMY;
        saw_enemy |= enemies > 0;
        if (enemies > max_enemies) max_enemies = enemies;
        if (!captured_room && game.swntimer < 900)
        {
            SDL_Surface* screen = SDL_RenderReadPixels(renderer, NULL);
            assert(screen != NULL && SDL_SaveBMP(screen, "gravitron-room.bmp"));
            SDL_DestroySurface(screen);
            captured_room = true;
        }
    }
    if (finished)
    {
        assert(saw_active && saw_enemy && captured_room);
        const PartTimes& gravitron = times[PART_COUNT - 1];
        assert(gravitron.frames > 0);
        assert(gravitron.room[0] == 53 && gravitron.room[1] == 50);
        assert(game.swntimer <= 1);
        if (result_frames == 0 || result_frames == 3)
        {
            SDL_Surface* screen = SDL_RenderReadPixels(renderer, NULL);
            assert(screen != NULL);
            assert(SDL_SaveBMP(screen, result_frames == 0 ? "results-1.bmp" : "results-2.bmp"));
            SDL_DestroySurface(screen);
        }
        if (result_frames == 0)
        {
            SDL_Event event;
            SDL_zero(event);
            event.type = SDL_EVENT_KEY_DOWN;
            event.key.key = SDLK_SPACE;
            event.key.down = true;
            SDL_PushEvent(&event);
        }
        if (++result_frames == 4)
        {
            printf("Gravitron smoke passed: %d frames, max %d enemies, timer %d, room %d,%d\n",
                gravitron.frames, max_enemies, game.swntimer, game.roomx, game.roomy);
            exit(0);
        }
    }
    return true;
}
