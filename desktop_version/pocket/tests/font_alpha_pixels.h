/* The fixtures deliberately mix binary and partial alpha in one font, and
 * supply partial-alpha digits and letters through another font's fallback. */
static void check_font_alpha_pixels(void)
{
    const char* names[] = {"cachealpha", "cachefallback"};
    const char* texts[] = {"AAA", "1.11", "\xe2\x98\x83\xe2\x98\x83\xe2\x98\x83", "BBB"};
    SDL_Texture* target = SDL_GetRenderTarget(gameScreen.m_renderer);
    graphics.set_render_target(graphics.tempShakeTexture);
    int cases = 0;
    for (int f = 0; f < 2; f++)
    {
        uint8_t idx;
        assert(font::find_main_font_by_name(names[f], &idx));
        for (int clock = 0; clock < 2; clock++)
            for (int border = 0; border < 3; border++)
                for (int flip = 0; flip < 2; flip++)
                    for (int t = 0; t < 4; t++)
                    {
                        graphics.flipmode = flip != 0;
                        const uint32_t flags = PR_FONT_IDX(idx, false) | PR_2X
                            | (border == 1 ? PR_BOR : border == 2 ? PR_FULLBOR : 0);
                        POCKET_text_cache = false;
                        SDL_Surface* expected = draw_case(flags, 20, 40, texts[t], 190, 220, 255, clock);
                        POCKET_text_cache = true;
                        const Uint32 hits = POCKET_text_hits;
                        for (int repeat = 0; repeat < 3; repeat++)
                        {
                            SDL_Surface* actual = draw_case(flags, 20, 40, texts[t], 190, 220, 255, clock);
                            if (!same_pixels(expected, actual))
                                fprintf(stderr, "Partial-alpha mismatch: %s %s clock %d border %d flip %d repeat %d\n",
                                    names[f], texts[t], clock, border, flip, repeat);
                            assert(same_pixels(expected, actual));
                            SDL_DestroySurface(actual);
                        }
                        if (t == 3)
                            assert(POCKET_text_hits > hits); /* Binary glyphs still cache. */
                        SDL_DestroySurface(expected);
                        cases++;
                    }
    }
    graphics.flipmode = false;
    uint8_t shaped_idx;
    assert(font::find_main_font_by_name("cacheshaped", &shaped_idx));
    const uint32_t shaped_flags = PR_FONT_IDX(shaped_idx, true) | PR_2X | PR_BOR;
    POCKET_text_cache = false;
    SDL_Surface* shaped = draw_case(shaped_flags, 20, 40, "\xd8\xa8", 190, 220, 255);
    POCKET_text_cache = true;
    for (int i = 0; i < 3; i++)
    {
        SDL_Surface* actual = draw_case(shaped_flags, 20, 40, "\xd8\xa8", 190, 220, 255);
        assert(same_pixels(shaped, actual));
        SDL_DestroySurface(actual);
    }
    SDL_DestroySurface(shaped);
    cases++;
    graphics.set_render_target(target);
    printf("Partial-alpha fonts: %d repeated-string, clock and fallback pixel cases passed\n", cases);
}
