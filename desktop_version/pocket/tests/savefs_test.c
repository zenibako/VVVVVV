/* Host checks for the save archive behind the game's writable directories.
 * savefs.c is included directly so a test can forget the loaded archive and
 * read it back from the save file, as the next boot would. */
#include "../sdl3/savefs.c"

#include <stdlib.h>

static int failures;

#define CHECK(cond)                                                     \
    do                                                                  \
    {                                                                   \
        if (!(cond))                                                    \
        {                                                               \
            printf("%s:%d: check failed: %s (%s)\n", __FILE__, __LINE__, \
                #cond, SDL_GetError());                                 \
            failures++;                                                 \
        }                                                               \
    } while (0)

static void reboot(void)
{
    Uint32 i;
    for (i = 0; i < file_count; i++)
    {
        SDL_free(files[i].data);
    }
    file_count = 0;
    made_dir_count = 0;
    loaded = 0;
}

static bool put(const char *path, const char *mode, const char *text)
{
    SDL_IOStream *io = SDL_IOFromFile(path, mode);
    const size_t len = strlen(text);
    if (io == NULL)
    {
        return false;
    }
    if (SDL_WriteIO(io, text, len) != len)
    {
        SDL_CloseIO(io);
        return false;
    }
    return SDL_CloseIO(io);
}

/* Reads the whole file into buf, or returns false. */
static bool get(const char *path, char *buf, size_t size)
{
    SDL_IOStream *io = SDL_IOFromFile(path, "rb");
    size_t got;
    if (io == NULL)
    {
        return false;
    }
    got = SDL_ReadIO(io, buf, size - 1);
    buf[got] = '\0';
    return SDL_CloseIO(io);
}

typedef struct
{
    char names[256];
    int count;
} Listing;

static SDL_EnumerationResult SDLCALL collect(void *userdata, const char *dirname, const char *fname)
{
    Listing *listing = (Listing *) userdata;
    (void) dirname;
    if (listing->count++ != 0)
    {
        SDL_strlcat(listing->names, " ", sizeof(listing->names));
    }
    SDL_strlcat(listing->names, fname, sizeof(listing->names));
    return SDL_ENUM_CONTINUE;
}

static Listing list(const char *path)
{
    Listing listing;
    SDL_zero(listing);
    CHECK(SDL_EnumerateDirectory(path, collect, &listing));
    return listing;
}

int main(void)
{
    char buf[256];
    SDL_PathInfo info;
    Listing listing;

    remove(POCKET_SAVE_FILE);

    /* PHYSFS_init asks for the base, home and pref folders. */
    CHECK(strcmp(SDL_GetBasePath(), "/") == 0);
    CHECK(SDL_GetUserFolder(SDL_FOLDER_HOME) && strcmp(SDL_GetUserFolder(SDL_FOLDER_HOME), "/") == 0);

    /* Paths the archive owns, as the game and PhysFS spell them. */
    CHECK(SAVEFS_owns("/saves/unlock.vvv"));
    CHECK(SAVEFS_owns("./saves//tsave.vvv"));
    CHECK(SAVEFS_owns("/levels"));
    CHECK(SAVEFS_owns("/screenshots/1x/"));
    CHECK(!SAVEFS_owns("/data.zip"));
    CHECK(!SAVEFS_owns("/savesx/a"));
    CHECK(!SAVEFS_owns("/"));

    /* A first boot has no save file and empty directories. */
    CHECK(!get("/saves/unlock.vvv", buf, sizeof(buf)));
    CHECK(SDL_GetPathInfo("/saves/", &info) && info.type == SDL_PATHTYPE_DIRECTORY);
    CHECK(!SDL_GetPathInfo("/saves/unlock.vvv", &info));
    listing = list("/saves");
    CHECK(listing.count == 0);

    /* Writing a file stores it and survives a reboot. */
    CHECK(put("/saves/unlock.vvv", "wb", "<unlock/>"));
    CHECK(put("/saves/settings.vvv", "wb", "<settings/>"));
    reboot();
    CHECK(get("/saves/unlock.vvv", buf, sizeof(buf)) && strcmp(buf, "<unlock/>") == 0);
    CHECK(SDL_GetPathInfo("/saves/settings.vvv", &info) &&
        info.type == SDL_PATHTYPE_FILE && info.size == 11);
    listing = list("/saves/");
    CHECK(listing.count == 2 && strcmp(listing.names, "unlock.vvv settings.vvv") == 0);

    /* "wb" replaces, "ab" appends. */
    CHECK(put("/saves/unlock.vvv", "wb", "new"));
    CHECK(put("/saves/unlock.vvv", "ab", "er"));
    reboot();
    CHECK(get("/saves/unlock.vvv", buf, sizeof(buf)) && strcmp(buf, "newer") == 0);

    /* Created directories exist until reboot; files make their own. */
    CHECK(SDL_CreateDirectory("/screenshots/1x"));
    CHECK(SDL_GetPathInfo("/screenshots/1x", &info) && info.type == SDL_PATHTYPE_DIRECTORY);
    CHECK(put("/levels/mine/level.vvv", "wb", "x"));
    CHECK(SDL_GetPathInfo("/levels/mine", &info) && info.type == SDL_PATHTYPE_DIRECTORY);
    listing = list("/levels");
    CHECK(listing.count == 0);
    listing = list("/levels/mine");
    CHECK(listing.count == 1 && strcmp(listing.names, "level.vvv") == 0);

    /* Removing a file takes it out of the save file. */
    CHECK(SDL_RemovePath("/saves/settings.vvv"));
    reboot();
    CHECK(!SDL_GetPathInfo("/saves/settings.vvv", &info));
    CHECK(get("/saves/unlock.vvv", buf, sizeof(buf)) && strcmp(buf, "newer") == 0);
    listing = list("/saves");
    CHECK(listing.count == 1);

    /* A save file that is not an archive (a fresh, blank slot) reads as
     * empty. */
    {
        FILE *f = fopen(POCKET_SAVE_FILE, "wb");
        static const char zeros[64];
        fwrite(zeros, 1, sizeof(zeros), f);
        fclose(f);
    }
    reboot();
    CHECK(!get("/saves/unlock.vvv", buf, sizeof(buf)));
    CHECK(put("/saves/unlock.vvv", "wb", "again"));
    reboot();
    CHECK(get("/saves/unlock.vvv", buf, sizeof(buf)) && strcmp(buf, "again") == 0);

    remove(POCKET_SAVE_FILE);
    if (failures != 0)
    {
        printf("savefs checks: %d failed\n", failures);
        return 1;
    }
    printf("savefs checks passed\n");
    return 0;
}
