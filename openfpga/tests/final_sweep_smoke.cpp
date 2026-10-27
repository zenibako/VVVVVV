/* Actual host wall timings, with an accelerated simulation clock. They are
 * host measurements of the game/renderer, never Pocket timing estimates. */
#include "../src/pocket_final_sweep.cpp"
#include <assert.h>
extern "C" {
Uint32 POCKET_bench_keys;
bool POCKET_saves_volatile;
#ifndef POCKET_SMOKE_RENDER
Uint32 POCKET_grid_repeat_hits;
#endif
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
#ifdef POCKET_SMOKE_RENDER
static bool sweep_save_surface(SDL_Surface*, const char*);
#endif
static void sweep_check_presented_report(void)
{
    assert(SDL_GetRenderTarget(gameScreen.m_renderer)==NULL);
#ifdef POCKET_SMOKE_RENDER
    SDL_Texture* previous = SDL_GetRenderTarget(gameScreen.m_renderer);
    graphics.set_render_target(NULL);
    SDL_Surface* actual = SDL_RenderReadPixels(gameScreen.m_renderer, NULL);
    char filename[40];
    SDL_snprintf(filename,sizeof(filename),"sweep-results-%d.bmp",sweep_report.page()+1);
    assert(actual && sweep_save_surface(actual,filename));
    SDL_Texture* expected_target = SDL_CreateTexture(gameScreen.m_renderer,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 320, 240);
    assert(actual && expected_target);
    graphics.set_render_target(expected_target);
    graphics.fill_rect(0,0,0);
    for (int i = sweep_report.first(); i < sweep_report.end(); i++)
        font::print(0,4,(i-sweep_report.first())*8,sweep_report.line(i),255,255,255);
    char footer[48];
    SDL_snprintf(footer,sizeof(footer),"page %d/%d  A: next  screenshot each page",sweep_report.page()+1,sweep_report.pages());
    font::print(0,4,232,footer,255,255,255);
    SDL_Surface* expected = SDL_RenderReadPixels(gameScreen.m_renderer, NULL);
    assert(expected && actual->w == expected->w && actual->h == expected->h);
    for (int y=0; y<actual->h; y++)
        assert(SDL_memcmp((Uint8*)actual->pixels+y*actual->pitch,
            (Uint8*)expected->pixels+y*expected->pitch,actual->w*4)==0);
    SDL_DestroySurface(actual);
    SDL_DestroySurface(expected);
    graphics.set_render_target(previous);
    SDL_DestroyTexture(expected_target);
#endif
}
static void sweep_check_presented_progress(void)
{
    assert(SDL_GetRenderTarget(gameScreen.m_renderer)==NULL);
#ifdef POCKET_SMOKE_RENDER
    SDL_Texture* previous = SDL_GetRenderTarget(gameScreen.m_renderer);
    graphics.set_render_target(NULL);
    SDL_Surface* screen = SDL_RenderReadPixels(gameScreen.m_renderer,NULL);
    assert(screen && sweep_save_surface(screen,"sweep-progress.bmp"));
    SDL_DestroySurface(screen);
    const SDL_Rect band={0,0,320,10};
    SDL_Surface* actual=SDL_RenderReadPixels(gameScreen.m_renderer,&band);
    SDL_Texture* expected_target=SDL_CreateTexture(gameScreen.m_renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,320,10);
    graphics.set_render_target(expected_target);
    graphics.fill_rect(0,0,0);
    font::print(0,4,0,sweep_status,255,255,255);
    SDL_Surface* expected=SDL_RenderReadPixels(gameScreen.m_renderer,NULL);
    assert(actual && expected && actual->w==expected->w && actual->h==expected->h);
    for(int y=0;y<actual->h;y++)
        assert(SDL_memcmp((Uint8*)actual->pixels+y*actual->pitch,(Uint8*)expected->pixels+y*expected->pitch,actual->w*4)==0);
    SDL_DestroySurface(actual); SDL_DestroySurface(expected);
    graphics.set_render_target(previous); SDL_DestroyTexture(expected_target);
#endif
}
static void sweep_press_a(bool down)
{
    SDL_Event event;
    SDL_zero(event);
    event.type=down?SDL_EVENT_GAMEPAD_BUTTON_DOWN:SDL_EVENT_GAMEPAD_BUTTON_UP;
    event.gbutton.button=SDL_GAMEPAD_BUTTON_SOUTH;
    event.gbutton.down=down;
    assert(SDL_PushEvent(&event));
    key.Poll();
}
static bool sweep_smoke_present(SDL_Renderer*)
{
    static int frames;
    static bool checked_progress;
    assert(++frames < 100000);
    assert(sweep_logic_sum + sweep_draw_sum <= sweep_sum);
    if (!sweep_done)
    {
        if(sweep_started && !checked_progress && sweep_tick>=0 && sweep_tick<SWEEP_WARMUP)
        { sweep_check_presented_progress(); checked_progress=true; }
        return true;
    }
    assert(!sweep_failed);
    assert(sweep_cases == SWEEP_CASES);
    assert(sweep_scroll_cases == 8);
#ifdef POCKET_SMOKE_RENDER
    assert(POCKET_tower_recolors > 0 && POCKET_grid_repeat_hits > 0);
#endif
    for (int mode = 0; mode < 2; mode++)
        for (int i = 0; i < SWEEP_ROOMS; i++)
        {
            const SweepResult& r = sweep_results[mode * SWEEP_ROOMS + i];
            if(!sweep_selected(i,mode)) { assert(r.views==0); continue; }
            assert(r.views == (final_rooms[i].tower ? SWEEP_VIEWS_PER_TOWER : 1));
            assert(r.peak_logic + r.peak_draw <= r.worst);
            assert(r.peak_bg <= r.peak_logic && r.peak_fg <= r.peak_draw);
            printf("SWEEP_RESULT,%d,%d,%d,%d,%d,%u,%u,%u,%u,\"%s\"\n",mode,
                final_rooms[i].x,final_rooms[i].y,r.entry,r.camera,r.average,r.logic,r.draw,r.worst,final_rooms[i].name);
        }
    assert(sweep_report.size() == SWEEP_REPORT_ROWS);
    sweep_check_presented_report();
    const int pages=sweep_report.pages();
    for(int page=1;page<=pages;page++)
    {
        sweep_press_a(true);
        POCKET_BenchDraw();
        assert(sweep_report.page()==page%pages);
        sweep_check_presented_report();
        POCKET_BenchDraw();
        assert(sweep_report.page()==page%pages); /* Holding A advances only once. */
        sweep_press_a(false);
        POCKET_BenchDraw();
    }
    assert(checked_progress);
#ifdef POCKET_SMOKE_RENDER
    puts("Sweep display passed: progress and all report pages match the presented window; gamepad A cycles pages");
#else
    puts("Sweep native SDL presentation target and gamepad A paging passed");
#endif
    printf("Final sweep passed: %d cases, %d scrolling cases; HOST wall time only\n",sweep_cases,sweep_scroll_cases);
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
#undef SDL_CreateSurfaceFrom
#undef SDL_DestroySurface
extern "C" SDL_Surface* SDL_CreateSurfaceFrom(int,int,SDL_PixelFormat,void*,int);
extern "C" void SDL_DestroySurface(SDL_Surface*);
static bool sweep_save_surface(SDL_Surface* surface,const char* path)
{
    SDL_Surface* native=SDL_CreateSurfaceFrom(surface->w,surface->h,surface->format,surface->pixels,surface->pitch);
    if(!native)return false;
    const bool ok=SDL_SaveBMP(native,path);
    SDL_DestroySurface(native);
    return ok;
}
#else
extern "C" bool SDL_RenderPresent(SDL_Renderer* renderer) { return sweep_smoke_present(renderer); }
#endif
