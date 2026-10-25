/* Runs inside the host smoke after the real game has loaded its fonts. */
static int clock_pixel_cases;

/* This uses the host's real counter, independent of the accelerated game
 * clock. It compares implementations, not physical Pocket frame times. */
static Uint64 host_clock_cost(int mode)
{
    POCKET_clock_border_cache = mode == 2;
    graphics.fill_rect(40, 60, 90);
    const Uint64 start = SDL_GetPerformanceCounter();
    for (int i = 0; i < 600; i++)
    {
        const std::string text = help.timestring(1799 - i);
        if (mode != 0)
            font::print_clock(PR_2X | PR_CEN | PR_BOR, -1, 20, text.c_str(), 196, 196, 255);
        else
            font::print(PR_2X | PR_CEN | PR_BOR, -1, 20, text, 196, 196, 255);
    }
    return (SDL_GetPerformanceCounter() - start) * 1000000 / SDL_GetPerformanceFrequency();
}

static void clock_pixel_case(uint32_t flags, int x, int y, const char* text,
    uint8_t r, uint8_t g, uint8_t b)
{
    POCKET_text_cache = false;
    SDL_Surface* original = draw_case(flags, x, y, text, r, g, b);
    graphics.fill_rect(40, 60, 90);
    POCKET_text_cache = true;
    font::print_clock(flags, x, y, text, r, g, b);
    SDL_Surface* optimized = SDL_RenderReadPixels(gameScreen.m_renderer, NULL);
    if (!same_pixels(original, optimized))
        fprintf(stderr, "Clock pixels differ: flags %08x, flip %d, outlines %d, text %s\n",
            (unsigned) flags, graphics.flipmode, !graphics.notextoutline, text);
    assert(same_pixels(original, optimized));
    SDL_DestroySurface(original);
    SDL_DestroySurface(optimized);
    clock_pixel_cases++;
}

static void check_clock_pixels(void)
{
    SDL_Texture* target = SDL_GetRenderTarget(gameScreen.m_renderer);
    graphics.set_render_target(graphics.tempShakeTexture);
    const bool flip = graphics.flipmode, no_outline = graphics.notextoutline;
    const uint32_t clock_flags = PR_2X | PR_CEN | PR_BOR;
    for (int f = 0; f < 2; f++)
    {
        graphics.flipmode = f != 0;
        graphics.notextoutline = false;
        for (int tick = 0; tick < 1800; tick++)
        {
            std::string text = help.timestring(tick);
            clock_pixel_case(clock_flags, -1, 20, text.c_str(),
                220 - tick % 30, 220 - tick % 30, 255 - tick % 15);
        }
        const uint32_t styles[] = {clock_flags, PR_3X | PR_FULLBOR | PR_RIGHT,
            PR_2X | PR_BOR | PR_BRIGHTNESS(128), PR_2X | PR_BOR | PR_RTL_FORCE,
            PR_2X | PR_BOR | PR_FULLBOR, PR_BOR | PR_RIGHT, PR_3X | PR_BOR};
        for (int no = 0; no < 2; no++)
        {
            graphics.notextoutline = no != 0;
            for (size_t s = 0; s < SDL_arraysize(styles); s++)
            {
                clock_pixel_case(styles[s], 30, 20, "59.96", 120, 200, 64);
                clock_pixel_case(styles[s], -4, 236, "0.00", 255, 255, 255);
                clock_pixel_case(styles[s], 315, -4, "1:23.45", 0, 0, 0);
                clock_pixel_case(styles[s], 50, 20, "Current Time", 196, 196, 255);
                clock_pixel_case(styles[s], 50, 20, "\xd9\xa1.\xd9\xa0\xd9\xa0", 196, 196, 255);
            }
            for (int i = 0; i < font::font_idx_options_n; i++)
                clock_pixel_case(PR_2X | PR_BOR | PR_FONT_IDX(font::font_idx_options[i], false),
                    10, 40, "12.34", 190, 200, 250);
        }
    }
    graphics.flipmode = false;
    graphics.notextoutline = false;
    POCKET_text_cache = true;
    /* A border-only clock entry must never replace a complete text entry. */
    font::print_clock(clock_flags, -1, 20, "1", 200, 200, 240);
    POCKET_text_cache = false;
    SDL_Surface* complete = draw_case(clock_flags, -1, 20, "1", 200, 200, 240);
    POCKET_text_cache = true;
    for (int i = 0; i < 2; i++)
    {
        SDL_Surface* cached = draw_case(clock_flags, -1, 20, "1", 200, 200, 240);
        assert(same_pixels(complete, cached));
        SDL_DestroySurface(cached);
    }
    SDL_DestroySurface(complete);
    font::print_clock(clock_flags, -1, 20, "0123456789.:", 200, 200, 240);
    const Uint32 kept = POCKET_text_kept;
    for (int i = 0; i < 60; i++)
    {
        std::string text = help.timestring(1799 - i);
        font::print_clock(clock_flags, -1, 20, text.c_str(), 200, 200, 240);
    }
    assert(POCKET_text_kept == kept); /* Changing values reuse the same glyphs. */
    Uint64 old_us = 0, glyph_us = 0, new_us = 0;
    for (int round = 0; round < 3; round++)
    {
        old_us += host_clock_cost(0);
        glyph_us += host_clock_cost(1);
        new_us += host_clock_cost(2);
    }
    printf("Clock host wall time, us/draw: original %llu, glyphs %llu, border pairs %llu (not Pocket timing)\n",
        (unsigned long long) (old_us / 1800), (unsigned long long) (glyph_us / 1800),
        (unsigned long long) (new_us / 1800));
    graphics.flipmode = flip;
    graphics.notextoutline = no_outline;
    graphics.set_render_target(target);
    printf("Clock pixels passed: %d cases; changing clocks created no new cache entries\n", clock_pixel_cases);
}
