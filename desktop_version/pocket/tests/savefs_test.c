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
    load_failed = false;
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

/* Like the game's level-save reset: removes each file as it is listed. */
static SDL_EnumerationResult SDLCALL remove_each(void *userdata, const char *dirname, const char *fname)
{
    char path[SAVEFS_NAME_LEN];
    (*(int *) userdata)++;
    SDL_snprintf(path, sizeof(path), "%s/%s", dirname, fname);
    return SDL_RemovePath(path) ? SDL_ENUM_CONTINUE : SDL_ENUM_FAILURE;
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

    /* Removing files while listing them still visits every one. */
    {
        int visited = 0;
        CHECK(put("/levels/a.vvvvvv.vvv", "wb", "a"));
        CHECK(put("/levels/b.vvvvvv.vvv", "wb", "b"));
        CHECK(put("/levels/c.vvvvvv.vvv", "wb", "c"));
        CHECK(SDL_EnumerateDirectory("/levels", remove_each, &visited));
        CHECK(visited == 3);
        listing = list("/levels");
        CHECK(listing.count == 0);
    }

    /* A file too big for the save file is refused, and leaves the archive
     * as it was, so smaller saves still work. */
    {
        char *big = (char *) SDL_malloc(SAVEFS_MAX_BYTES + 1);
        SDL_memset(big, 'x', SAVEFS_MAX_BYTES);
        big[SAVEFS_MAX_BYTES] = '\0';
        CHECK(!put("/screenshots/big.png", "wb", big));
        CHECK(!SDL_GetPathInfo("/screenshots/big.png", &info));
        CHECK(!put("/saves/unlock.vvv", "wb", big));
        CHECK(get("/saves/unlock.vvv", buf, sizeof(buf)) && strcmp(buf, "newer") == 0);
        SDL_free(big);
        CHECK(put("/saves/qsave.vvv", "wb", "<quick/>"));
        reboot();
        CHECK(get("/saves/qsave.vvv", buf, sizeof(buf)) && strcmp(buf, "<quick/>") == 0);
        CHECK(get("/saves/unlock.vvv", buf, sizeof(buf)) && strcmp(buf, "newer") == 0);
        CHECK(SDL_RemovePath("/saves/qsave.vvv"));
    }

    /* A save file that is not an archive (a fresh, blank slot) reads as
     * empty. */
    {
        FILE *f = fopen(POCKET_SAVE_FILE, "wb");
        static const char zeros[64];
        CHECK(f != NULL);
        if (f != NULL)
        {
            fwrite(zeros, 1, sizeof(zeros), f);
            fclose(f);
        }
    }
    reboot();
    CHECK(!get("/saves/unlock.vvv", buf, sizeof(buf)));
    CHECK(put("/saves/unlock.vvv", "wb", "again"));
    reboot();
    CHECK(get("/saves/unlock.vvv", buf, sizeof(buf)) && strcmp(buf, "again") == 0);

    /* Save data that could not be read is not written over. */
    {
        const SaveHeader header = {SAVEFS_MAGIC, 1, 1000, 0};
        FILE *f = fopen(POCKET_SAVE_FILE, "wb");
        CHECK(f != NULL);
        if (f != NULL)
        {
            fwrite(&header, 1, sizeof(header), f);
            fclose(f);
        }
        reboot();
        CHECK(!put("/saves/settings.vvv", "wb", "<settings/>"));
        f = fopen(POCKET_SAVE_FILE, "rb");
        CHECK(f != NULL && fseek(f, 0, SEEK_END) == 0 && ftell(f) == (long) sizeof(header));
        if (f != NULL)
        {
            fclose(f);
        }
    }

    remove(POCKET_SAVE_FILE);
    if (failures != 0)
    {
        printf("savefs checks: %d failed\n", failures);
        return 1;
    }
    printf("savefs checks passed\n");
    return 0;
}
