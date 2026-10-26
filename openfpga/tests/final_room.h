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
    assert(times[final_part].room[0] == 42 && times[final_part].room[1] == 52);
    assert(final_colours && (final_colours & (final_colours - 1)) != 0);
    assert(final_phases == 15 && final_shapes_changed);
    assert(SDL_strstr(report.line(report.size() - 1), "full speed") != NULL);
    SDL_DestroySurface(final_obstacles);
    final_obstacles = NULL;
    printf("Final room passed: %d frames, colours %x, phases %x, changing obstacle pixels at equal colour\n",
        final_frames, final_colours, final_phases);
}
