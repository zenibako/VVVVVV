#ifdef POCKET_SMOKE_RENDER
struct POCKET_TileLayer;
extern "C" {
POCKET_TileLayer* POCKET_CreateTileLayer(int, int);
void POCKET_DestroyTileLayer(POCKET_TileLayer*);
bool POCKET_PutTileGrid(POCKET_TileLayer*, SDL_Texture*, const int*, int, int, int);
bool POCKET_RecolorTileLayer(POCKET_TileLayer*, SDL_Texture*, int, int, int);
bool POCKET_DrawTileLayer(SDL_Renderer*, POCKET_TileLayer*, int);
}
extern bool POCKET_tower_recolor_fast;
extern Uint32 POCKET_tower_recolors;
#endif

static void check_tower_recolor(void)
{
#ifdef POCKET_SMOKE_RENDER
    SDL_Renderer* renderer = gameScreen.m_renderer;
    SDL_Texture* target = SDL_GetRenderTarget(renderer);
    SDL_Texture* sheet = graphics.grphx.im_tiles3;
    SDL_Texture* image = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_TARGET, 240, 8);
    POCKET_TileLayer* layer = POCKET_CreateTileLayer(240, 8);
    assert(image && layer);
    int ids[30], cases = 0;
    for (int from = 0; from < 30; from++)
        for (int to = 0; to < 30; to++)
        {
            for (int i = 0; i < 30; i++) ids[i] = from * 30 + i;
            assert(POCKET_PutTileGrid(layer, sheet, ids, 30, 1, 0));
            assert(POCKET_RecolorTileLayer(layer, sheet, from * 30, to * 30, 30));
            graphics.set_render_target(image);
            graphics.fill_rect(17, 39, 89);
            assert(POCKET_DrawTileLayer(renderer, layer, 0));
            SDL_Surface* remapped = SDL_RenderReadPixels(renderer, NULL);
            graphics.fill_rect(17, 39, 89);
            for (int i = 0; i < 30; i++) graphics.drawtile3(i * 8, 0, i, to);
            SDL_Surface* original = SDL_RenderReadPixels(renderer, NULL);
            assert(original && remapped && same_pixels(original, remapped));
            SDL_DestroySurface(original);
            SDL_DestroySurface(remapped);
            cases++;
        }
    POCKET_DestroyTileLayer(layer);
    graphics.set_render_target(target);
    SDL_DestroyTexture(image);

    /* Exercise the Graphics cache, including colour+scroll in one frame,
     * fractional tile offsets, room changes and large camera jumps. */
    const Uint32 hits = POCKET_tower_recolors;
    const int rooms[][2] = {{49, 52}, {51, 53}};
    for (int room = 0; room < 2; room++)
    {
        map.gotoroom(rooms[room][0], rooms[room][1]);
        for (int frame = 0; frame < 60; frame++)
        {
            SDL_Surface* pictures[2];
            for (int mode = 0; mode < 2; mode++)
            {
                POCKET_tower_recolor_fast = mode != 0;
                graphics.set_render_target(graphics.tempShakeTexture);
                map.ypos = map.oldypos = (frame * 7) % 569;
                graphics.towerbg.colstate = frame % 30;
                graphics.fill_rect(17, 39, 89);
                graphics.drawtowermap();
                map.ypos = map.oldypos = frame % 17 == 0 ? 568 : (frame * 7 + 9) % 569;
                graphics.towerbg.colstate = (frame + 1) % 30;
                graphics.fill_rect(17, 39, 89);
                graphics.drawtowermap();
                pictures[mode] = SDL_RenderReadPixels(renderer, NULL);
            }
            assert(pictures[0] && pictures[1] && same_pixels(pictures[0], pictures[1]));
            SDL_DestroySurface(pictures[0]);
            SDL_DestroySurface(pictures[1]);
        }
    }
    POCKET_tower_recolor_fast = true;
    assert(POCKET_tower_recolors > hits);

    /* Same real mini-tower geometry for both timed upload strategies. */
    int base[40 * 31], colored[40 * 31];
    for (int j = 0; j < 31; j++)
    {
        const short* row = map.tower.row(j, 192);
        for (int i = 0; i < 40; i++) base[j * 40 + i] = row[i] > 0 ? row[i] : -1;
    }
    layer = POCKET_CreateTileLayer(320, 248);
    assert(layer);
    Uint64 elapsed[2];
    for (int mode = 0; mode < 2; mode++)
    {
        assert(POCKET_PutTileGrid(layer, sheet, base, 40, 31, 0));
        const Uint64 start = SDL_GetPerformanceCounter();
        for (int frame = 1; frame <= 120; frame++)
        {
            const int to = frame % 30, from = (frame - 1) % 30;
            if (mode)
                assert(POCKET_RecolorTileLayer(layer, sheet, from * 30, to * 30, 30));
            else
            {
                for (int i = 0; i < 40 * 31; i++) colored[i] = base[i] < 0 ? -1 : base[i] + to * 30;
                assert(POCKET_PutTileGrid(layer, sheet, colored, 40, 31, 0));
            }
        }
        elapsed[mode] = SDL_GetPerformanceCounter() - start;
    }
    const double scale = 1000000.0 / SDL_GetPerformanceFrequency() / 120;
    printf("Tower palette pixels passed: %d state pairs and 120 colour/scroll/jump frames; %u retained palette updates\n",
        cases, POCKET_tower_recolors - hits);
    printf("HOST tower colour update us: upload %.1f colour-only %.1f (not Pocket timing)\n",
        elapsed[0] * scale, elapsed[1] * scale);
    POCKET_DestroyTileLayer(layer);
    graphics.set_render_target(target);
#endif
}
