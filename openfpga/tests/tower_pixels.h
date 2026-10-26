/* Compare full 336-square Tower backgrounds, including hidden black bands. */
static void check_tower_pixels(void)
{
#ifdef POCKET_SMOKE_RENDER
    SDL_Texture* target = SDL_GetRenderTarget(gameScreen.m_renderer);
    const bool enabled = POCKET_tower_grid_fast;
    TowerBG bg[2] = {graphics.towerbg, graphics.towerbg};
    bg[0].texture = SDL_CreateTexture(gameScreen.m_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 336, 336);
    bg[1].texture = SDL_CreateTexture(gameScreen.m_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 336, 336);
    assert(bg[0].texture && bg[1].texture);
    int cases = 0;
    const int bases[] = {-8, 0, 8, 952, 960};
    for (int colour = 0; colour < 30; colour++)
        for (int direction = 0; direction < 2; direction++)
            for (size_t base = 0; base < SDL_arraysize(bases); base++)
                for (int offset = 0; offset < 8; offset++)
                {
                    SDL_Surface* pictures[2];
                    for (int mode = 0; mode < 2; mode++)
                    {
                        bg[mode].bypos = bases[base] + offset;
                        bg[mode].bscroll = direction ? 3 + offset : -3;
                        bg[mode].scrolldir = direction;
                        bg[mode].colstate = colour;
                        bg[mode].tdrawback = true;
                        POCKET_tower_grid_fast = mode != 0;
                        graphics.set_render_target(target);
                        graphics.updatetowerbackground(bg[mode]);
                        assert(SDL_GetRenderTarget(gameScreen.m_renderer) == target);
                        assert(!bg[mode].tdrawback);
                        graphics.set_render_target(bg[mode].texture);
                        pictures[mode] = SDL_RenderReadPixels(gameScreen.m_renderer, NULL);
                    }
                    if (!same_pixels(pictures[0], pictures[1]))
                        fprintf(stderr, "Tower pixels differ: colour %d direction %d ypos %d\n", colour, direction, bases[base] + offset);
                    assert(same_pixels(pictures[0], pictures[1]));
                    assert(bg[0].bypos == bg[1].bypos);
                    SDL_DestroySurface(pictures[0]);
                    SDL_DestroySurface(pictures[1]);
                    cases++;
                }
    /* Alternate steady scrolling with forced redraws, colour changes and jumps. */
    int position = 947;
    for (int frame = 0; frame < 160; frame++)
    {
        const int direction = (frame / 17) % 2;
        const int scroll = direction ? 3 : -3;
        position = frame % 29 == 0 ? (frame * 23) % 960 : position + scroll;
        SDL_Surface* pictures[2];
        for (int mode = 0; mode < 2; mode++)
        {
            bg[mode].bypos = position;
            bg[mode].bscroll = scroll;
            bg[mode].scrolldir = direction;
            bg[mode].colstate = (frame / 4) % 30;
            bg[mode].tdrawback = frame % 4 != 3;
            POCKET_tower_grid_fast = mode != 0;
            graphics.set_render_target(target);
            graphics.updatetowerbackground(bg[mode]);
            assert(SDL_GetRenderTarget(gameScreen.m_renderer) == target);
            graphics.set_render_target(bg[mode].texture);
            pictures[mode] = SDL_RenderReadPixels(gameScreen.m_renderer, NULL);
        }
        assert(same_pixels(pictures[0], pictures[1]));
        SDL_DestroySurface(pictures[0]);
        SDL_DestroySurface(pictures[1]);
        cases++;
    }
    Uint64 elapsed[2];
    for (int mode = 0; mode < 2; mode++)
    {
        POCKET_tower_grid_fast = mode != 0;
        bg[mode].bypos = 491;
        bg[mode].bscroll = 3;
        bg[mode].scrolldir = 1;
        bg[mode].colstate = 10;
        const Uint64 start = SDL_GetPerformanceCounter();
        for (int i = 0; i < 100; i++)
        {
            bg[mode].tdrawback = true;
            graphics.updatetowerbackground(bg[mode]);
        }
        elapsed[mode] = (SDL_GetPerformanceCounter() - start) * 1000000 / SDL_GetPerformanceFrequency() / 100;
    }
    graphics.set_render_target(target);
    SDL_DestroyTexture(bg[0].texture);
    SDL_DestroyTexture(bg[1].texture);
    POCKET_tower_grid_fast = enabled;
    printf("Tower pixels passed: %d full-texture cases, all colours/directions/offsets and redraw transitions\n", cases);
    printf("Tower redraw host wall time us: tile calls %llu, grid %llu (not Pocket timing)\n",
        (unsigned long long) elapsed[0], (unsigned long long) elapsed[1]);
#endif
}
