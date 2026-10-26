/* First-run settings may be written before the first benchmark step. */
#include "../sdl3/savefs.c"
#include <assert.h>

int main(void)
{
    SDL_IOStream *io;
    char settings[8] = {0};
    remove(POCKET_SAVE_FILE);
    assert(POCKET_saves_volatile);
    io = SDL_IOFromFile("saves/settings.vvv", "wb");
    assert(io != NULL);
    assert(SDL_WriteIO(io, "english", 7) == 7);
    assert(SDL_CloseIO(io));
    assert(POCKET_saves_written == 0);
    assert(access(POCKET_SAVE_FILE, F_OK) != 0);
    io = SDL_IOFromFile("saves/settings.vvv", "rb");
    assert(io != NULL);
    assert(SDL_ReadIO(io, settings, 7) == 7);
    assert(SDL_CloseIO(io));
    assert(strcmp(settings, "english") == 0);
    puts("Benchmark saves: startup writes remain in memory");
    return 0;
}
