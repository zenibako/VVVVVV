/* Observe the rendered obstacle band without calling finalat(), which would
 * consume the game's RNG and change the benchmark workload. */
static unsigned final_colours, final_phases;
static int final_frames, final_part = -1, sample_colour, sample_phase;
static bool final_shapes_changed;
static SDL_Surface* final_obstacles;

static void observe_final_room(SDL_Renderer* renderer)
{
    if (part < 0 || part >= PART_COUNT || parts[part].name == NULL
        || SDL_strcmp(parts[part].name, "final") != 0)
        return;
    final_part = part;
    final_frames++;
    assert(game.roomx == 42 && game.roomy == 52);
    assert(map.finalmode && map.finalstretch && map.final_colormode && obj.flags[72]);
    assert(map.background == 6 && map.invincibility && !map.towermode);
    assert(!game.swnmode && game.deathseq == -1);
    assert(!game.noflashingmode && !game.colourblindmode);
    assert(map.final_mapcol >= 0 && map.final_mapcol < 7);
    assert(map.final_aniframe >= 0 && map.final_aniframe < 4);
    final_colours |= 1u << map.final_mapcol;
    final_phases |= 1u << map.final_aniframe;
    if (final_shapes_changed || map.final_mapcol == 1)
        return; /* Colour 1 randomizes tiles; use a regular four-frame phase. */

    SDL_Texture* target = SDL_GetRenderTarget(renderer);
    graphics.set_render_target(graphics.foregroundTexture);
    const SDL_Rect band = {0, 0, 320, 80};
    SDL_Surface* obstacles = SDL_RenderReadPixels(renderer, &band);
    graphics.set_render_target(target);
    assert(obstacles);
    if (final_obstacles == NULL)
    {
        final_obstacles = obstacles;
        sample_colour = map.final_mapcol;
        sample_phase = map.final_aniframe;
        SDL_Surface* screen = SDL_RenderReadPixels(renderer, NULL);
        assert(screen && save_capture(screen, "final-room-a.bmp"));
        SDL_DestroySurface(screen);
        return;
    }
    if (map.final_mapcol == sample_colour && map.final_aniframe != sample_phase
        && !same_pixels(final_obstacles, obstacles))
    {
        final_shapes_changed = true;
        SDL_Surface* screen = SDL_RenderReadPixels(renderer, NULL);
        assert(screen && save_capture(screen, "final-room-b.bmp"));
        SDL_DestroySurface(screen);
    }
    SDL_DestroySurface(obstacles);
}

static void check_final_room(void)
{
    assert(final_part >= 0 && final_frames >= 590);
    assert(times[final_part].frames >= 590);
    assert(times[final_part].final_rebuilds > 0 && times[final_part].final_rebuild_us > 0);
    assert(times[final_part].final_copy_us > 0);
    assert(times[final_part].room[0] == 42 && times[final_part].room[1] == 52);
    assert(final_colours && (final_colours & (final_colours - 1)) != 0);
    assert(final_phases == 15 && final_shapes_changed);
    assert(SDL_strstr(report.line(report.size() - 1), "full speed") != NULL);
    SDL_DestroySurface(final_obstacles);
    final_obstacles = NULL;
    printf("Final room passed: %d frames, colours %x, phases %x, changing obstacle pixels at equal colour\n",
        final_frames, final_colours, final_phases);
}

static void check_final_pixels(void)
{
#ifdef POCKET_SMOKE_RENDER
    SDL_Texture* target = SDL_GetRenderTarget(gameScreen.m_renderer);
    const int rooms[][2] = {{42,52}, {41,52}, {52,53}};
    int cases = 0;
    map.finalmode = map.finalstretch = true;
    obj.flags[72] = true;
    const Uint32 hits = POCKET_final_grid_hits;
    for (int room = 0; room < 3; room++)
    {
        map.gotoroom(rooms[room][0], rooms[room][1]);
        for (int colour = 1; colour <= 6; colour++)
            for (int phase = 0; phase < 4; phase++)
                for (int reduced = 0; reduced < 2; reduced++)
                {
                    map.changefinalcol(colour);
                    map.final_aniframe = phase;
                    game.noflashingmode = reduced != 0;
                    SDL_Surface* images[2];
                    Uint32 next[2];
                    for (int mode = 0; mode < 2; mode++)
                    {
                        POCKET_final_grid_fast = mode != 0;
                        graphics.foregrounddrawn = false;
                        srand(17 + cases); /* finalat uses the C RNG. */
                        graphics.set_render_target(graphics.tempShakeTexture);
                        graphics.fill_rect(17, 39, 89);
                        graphics.drawfinalmap();
                        next[mode] = rand();
                        graphics.set_render_target(graphics.foregroundTexture);
                        images[mode] = SDL_RenderReadPixels(gameScreen.m_renderer, NULL);
                        const Uint32 rebuilds = POCKET_final_rebuilds;
                        srand(71);
                        const int expected = rand();
                        srand(71);
                        graphics.set_render_target(graphics.tempShakeTexture);
                        graphics.drawfinalmap(); /* The retained foreground must not reroll. */
                        assert(rand() == expected && POCKET_final_rebuilds == rebuilds);
                    }
                    if (!same_pixels(images[0], images[1]))
                    {
                        fprintf(stderr, "Final mismatch room %d,%d colour %d phase %d reduced %d\n",
                            rooms[room][0], rooms[room][1], colour, phase, reduced);
                        assert(save_capture(images[0], "final-old.bmp"));
                        assert(save_capture(images[1], "final-new.bmp"));
                    }
                    assert(same_pixels(images[0], images[1]));
                    assert(next[0] == next[1]);
                    SDL_DestroySurface(images[0]);
                    SDL_DestroySurface(images[1]);
                    cases++;
                }
    }
    assert(POCKET_final_grid_hits > hits);
    map.gotoroom(42, 52);
    /* A replacement sheet with partial alpha must use the saved tile
     * indices on fallback, rather than rolling random variants twice. */
    SDL_Texture* sheet = graphics.grphx.im_tiles2;
    int width, height;
    assert(graphics.query_texture(sheet, NULL, NULL, &width, &height));
    SDL_Surface* partial = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_ARGB8888);
    assert(partial);
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
            ((Uint32*)((Uint8*) partial->pixels + y * partial->pitch))[x] = 0x805ab4dc;
    SDL_SetSurfaceBlendMode(partial, SDL_BLENDMODE_BLEND);
    graphics.grphx.im_tiles2 = SDL_CreateTextureFromSurface(gameScreen.m_renderer, partial);
    SDL_DestroySurface(partial);
    assert(graphics.grphx.im_tiles2);
    map.changefinalcol(1);
    game.noflashingmode = false;
    SDL_Surface* fallback[2];
    Uint32 following[2];
    const Uint32 before_fallback = POCKET_final_grid_hits;
    for (int mode = 0; mode < 2; mode++)
    {
        POCKET_final_grid_fast = mode != 0;
        graphics.foregrounddrawn = false;
        srand(91);
        graphics.set_render_target(graphics.tempShakeTexture);
        graphics.drawfinalmap();
        following[mode] = rand();
        graphics.set_render_target(graphics.foregroundTexture);
        fallback[mode] = SDL_RenderReadPixels(gameScreen.m_renderer, NULL);
    }
    assert(POCKET_final_grid_hits == before_fallback);
    assert(same_pixels(fallback[0], fallback[1]) && following[0] == following[1]);
    SDL_DestroySurface(fallback[0]);
    SDL_DestroySurface(fallback[1]);
    SDL_DestroyTexture(graphics.grphx.im_tiles2);
    graphics.grphx.im_tiles2 = sheet;

    for (int style = 0; style < 3; style++)
    {
        const Uint32 before = POCKET_final_grid_hits;
        for (int mode = 0; mode < 2; mode++)
        {
            SDL_SetTextureColorMod(sheet, style == 0 ? 128 : 255, 255, 255);
            SDL_SetTextureAlphaMod(sheet, style == 1 ? 128 : 255);
            SDL_SetTextureBlendMode(sheet, style == 2 ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
            POCKET_final_grid_fast = mode != 0;
            graphics.foregrounddrawn = false;
            srand(91);
            graphics.set_render_target(graphics.tempShakeTexture);
            graphics.drawfinalmap();
            following[mode] = rand();
            graphics.set_render_target(graphics.foregroundTexture);
            fallback[mode] = SDL_RenderReadPixels(gameScreen.m_renderer, NULL);
        }
        assert(same_pixels(fallback[0], fallback[1]) && following[0] == following[1]);
        assert(POCKET_final_grid_hits == before);
        SDL_DestroySurface(fallback[0]);
        SDL_DestroySurface(fallback[1]);
    }
    SDL_SetTextureColorMod(sheet, 255, 255, 255);
    SDL_SetTextureAlphaMod(sheet, 255);
    SDL_SetTextureBlendMode(sheet, SDL_BLENDMODE_BLEND);

    map.changefinalcol(2);
    Uint64 elapsed[2];
    for (int mode = 0; mode < 2; mode++)
    {
        POCKET_final_grid_fast = mode != 0;
        graphics.set_render_target(graphics.tempShakeTexture);
        const Uint64 start = SDL_GetPerformanceCounter();
        for (int i = 0; i < 120; i++)
        {
            graphics.foregrounddrawn = false;
            graphics.drawfinalmap();
        }
        elapsed[mode] = (SDL_GetPerformanceCounter() - start) * 1000000 / SDL_GetPerformanceFrequency() / 120;
    }
    POCKET_final_grid_fast = true;
    graphics.set_render_target(target);
    printf("Final foreground pixels and RNG passed: %d cases, three rooms/all colours/phases/reduced flashing\n", cases);
    printf("Final partial-alpha/modulated fallback pixels/RNG passed; host redraw us: old %llu new %llu (not Pocket timing)\n",
        (unsigned long long) elapsed[0], (unsigned long long) elapsed[1]);
#endif
}
