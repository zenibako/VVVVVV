/* Actual host wall timings, with an accelerated simulation clock. They are
 * host measurements of the game/renderer, never Pocket timing estimates. */
#include "../src/pocket_final_sweep.cpp"
#include <assert.h>
extern "C" {
Uint32 POCKET_bench_keys;
bool POCKET_saves_volatile;
Uint64 SDL_GetTicks(void) { static Uint64 ticks; return ticks += 17; }
void SDL_Delay(Uint32) {}
unsigned int of_time_us(void)
{
    const Uint64 counter = SDL_GetPerformanceCounter();
    const Uint64 frequency = SDL_GetPerformanceFrequency();
    return (unsigned int)((counter / frequency) * 1000000 + (counter % frequency) * 1000000 / frequency);
}
unsigned int of_time_ms(void) { return of_time_us() / 1000; }
}
static bool sweep_smoke_present(SDL_Renderer*)
{
    static int frames;
    assert(++frames < 100000);
    assert(sweep_logic_sum + sweep_draw_sum <= sweep_sum);
    if (!sweep_done) return true;
    assert(!sweep_failed);
    assert(sweep_cases == 104);
    assert(sweep_scroll_cases == 8);
    for (int mode = 0; mode < 2; mode++)
        for (int i = 0; i < SWEEP_ROOMS; i++)
        {
            const SweepResult& r = sweep_results[mode * SWEEP_ROOMS + i];
            assert(r.views == (final_rooms[i].tower ? 10 : 1));
            printf("SWEEP_RESULT,%d,%d,%d,%d,%d,%u,%u,%u,%u,\"%s\"\n",mode,
                final_rooms[i].x,final_rooms[i].y,r.entry,r.camera,r.average,r.logic,r.draw,r.worst,final_rooms[i].name);
        }
    assert(sweep_report.size() == 80 && sweep_report.pages() == 3);
    puts("Final sweep passed: all 34 rooms, both states, 104 views, 25920 timed frames; HOST wall time only");
    exit(0);
}
#ifdef POCKET_SMOKE_RENDER
extern "C" Uint16* POCKET_FrameBuffer(int* pitch)
{
    static Uint16 pixels[320 * 240];
    *pitch = 320;
    return pixels;
}
extern "C" void POCKET_PresentFrame(void) { sweep_smoke_present(gameScreen.m_renderer); }
#else
extern "C" bool SDL_RenderPresent(SDL_Renderer* renderer) { return sweep_smoke_present(renderer); }
#endif
