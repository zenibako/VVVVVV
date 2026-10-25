/* Compare the actual warp tiles and scroll phases against the original
 * temporary-texture path, including all offscreen pixels. */
static void check_background_pixels(void)
{
#ifdef POCKET_SMOKE_RENDER
    SDL_Texture* target = SDL_GetRenderTarget(gameScreen.m_renderer);
    SDL_Texture* background = graphics.backgroundTexture;
    SDL_Texture* temp = graphics.tempScrollingTexture;
    const int colour = graphics.rcol, offset = graphics.backoffset;
    const bool drawn = graphics.backgrounddrawn, enabled = POCKET_scroll_fast;
    SDL_Texture* original = SDL_CreateTexture(gameScreen.m_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 336, 336);
    SDL_Texture* optimized = SDL_CreateTexture(gameScreen.m_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 336, 336);
    graphics.tempScrollingTexture = SDL_CreateTexture(gameScreen.m_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 336, 336);
    assert(original && optimized && graphics.tempScrollingTexture);
    int cases = 0;
    for (int axis = 3; axis <= 4; axis++)
        for (int c = 0; c <= 6; c++)
        {
            int previous_offset = 0;
            for (int frame = 0; frame < 40; frame++)
            {
                graphics.rcol = c;
                graphics.backoffset = previous_offset;
                graphics.backgrounddrawn = frame != 0;
                graphics.backgroundTexture = original;
                POCKET_scroll_fast = false;
                graphics.set_render_target(target);
                graphics.updatebackground(axis);
                const int next_offset = graphics.backoffset;
                assert(SDL_GetRenderTarget(gameScreen.m_renderer) == target);
                graphics.set_render_target(original);
                SDL_Surface* a = SDL_RenderReadPixels(gameScreen.m_renderer, NULL);

                graphics.backoffset = previous_offset;
                graphics.backgrounddrawn = frame != 0;
                graphics.backgroundTexture = optimized;
                POCKET_scroll_fast = true;
                graphics.set_render_target(target);
                graphics.updatebackground(axis);
                assert(graphics.backoffset == next_offset);
                assert(SDL_GetRenderTarget(gameScreen.m_renderer) == target);
                graphics.set_render_target(optimized);
                SDL_Surface* b = SDL_RenderReadPixels(gameScreen.m_renderer, NULL);
                if (!same_pixels(a, b))
                    fprintf(stderr, "Background pixels differ: mode %d colour %d frame %d\n", axis, c, frame);
                assert(same_pixels(a, b));
                SDL_DestroySurface(a);
                SDL_DestroySurface(b);
                previous_offset = next_offset;
                cases++;
            }
        }
    graphics.rcol = 6;
    Uint64 elapsed[2];
    for (int mode = 0; mode < 2; mode++)
    {
        POCKET_scroll_fast = mode != 0;
        graphics.backgrounddrawn = false;
        graphics.updatebackground(3);
        const Uint64 start = SDL_GetPerformanceCounter();
        for (int i = 0; i < 300; i++) graphics.updatebackground(3);
        elapsed[mode] = (SDL_GetPerformanceCounter() - start) * 1000000 / SDL_GetPerformanceFrequency() / 300;
    }
    graphics.set_render_target(target);
    SDL_DestroyTexture(original);
    SDL_DestroyTexture(optimized);
    SDL_DestroyTexture(graphics.tempScrollingTexture);
    graphics.backgroundTexture = background;
    graphics.tempScrollingTexture = temp;
    graphics.rcol = colour;
    graphics.backoffset = offset;
    graphics.backgrounddrawn = drawn;
    POCKET_scroll_fast = enabled;
    printf("Warp background pixels passed: %d full-texture cases, both axes\n", cases);
    printf("Background host wall time, us/update: original %llu, shared scroll %llu (not Pocket timing)\n",
        (unsigned long long) elapsed[0], (unsigned long long) elapsed[1]);
#endif
}
