/* The game's writable directories, kept in one openfpgaOS save file.
 *
 * openfpgaOS has no writable directory. It has fixed save files, named in
 * the core's instance JSON, that the Pocket writes back to the SD card when
 * the core closes. The game writes small XML files under saves/ (unlock.vvv,
 * tsave.vvv, qsave.vvv, settings.vvv, levelstats.vvv and custom level
 * saves) and creates levels/ and screenshots/.
 *
 * Everything under those directories lives in one save file as a small
 * archive: a header, a table of names and sizes, then the contents. It is
 * read into memory on first use, and the whole archive is rewritten each
 * time a file is closed after writing. The layout follows Butterscotch's
 * save archive (of_save_format.h) with its own magic and longer names. */
#include "savefs.h"

#include <stdio.h>
#include <string.h>

#ifndef POCKET_SAVE_FILE
#define POCKET_SAVE_FILE "vvvvvv.sav"
#endif

#define SAVEFS_MAGIC 0x31565656u /* "VVV1" */
#define SAVEFS_MAX_FILES 32
#define SAVEFS_NAME_LEN 64
/* Stay well inside the 256 KB save file. */
#define SAVEFS_MAX_BYTES (192 * 1024)

typedef struct
{
    Uint32 magic;
    Uint32 file_count;
    Uint32 total_bytes; /* the whole archive, header included */
    Uint32 reserved;
} SaveHeader;

typedef struct
{
    char name[SAVEFS_NAME_LEN];
    Uint32 size;
    Uint32 offset; /* from the start of the archive */
} SaveEntry;

typedef struct
{
    char name[SAVEFS_NAME_LEN];
    Uint8 *data;
    Uint32 size;
} SaveFile;

static const char *const roots[] = {"saves", "levels", "screenshots"};

static SaveFile files[SAVEFS_MAX_FILES];
static Uint32 file_count;
static int loaded;

/* Directories the game created that hold no files yet (screenshots/1x).
 * They are not stored; files imply their own directories. */
#define SAVEFS_MAX_DIRS 8
static char made_dirs[SAVEFS_MAX_DIRS][SAVEFS_NAME_LEN];
static Uint32 made_dir_count;

/* "/saves//x.vvv" and "./saves/x.vvv" both become "saves/x.vvv". Returns
 * false if the result does not fit. */
static bool normalize(const char *path, char *out, size_t out_size)
{
    size_t len = 0;
    while (*path != '\0')
    {
        if (*path == '/')
        {
            path++;
            continue;
        }
        if (path[0] == '.' && (path[1] == '/' || path[1] == '\0'))
        {
            path++;
            continue;
        }
        if (len != 0)
        {
            if (len + 1 >= out_size)
            {
                return false;
            }
            out[len++] = '/';
        }
        while (*path != '\0' && *path != '/')
        {
            if (len + 1 >= out_size)
            {
                return false;
            }
            out[len++] = *path++;
        }
    }
    out[len] = '\0';
    return true;
}

/* True if name is dir itself or inside it. */
static bool under(const char *name, const char *dir)
{
    const size_t len = strlen(dir);
    return strncmp(name, dir, len) == 0 && (name[len] == '\0' || name[len] == '/');
}

bool SAVEFS_owns(const char *path)
{
    char name[SAVEFS_NAME_LEN];
    size_t i;
    if (!normalize(path, name, sizeof(name)))
    {
        return false;
    }
    for (i = 0; i < SDL_arraysize(roots); i++)
    {
        if (under(name, roots[i]))
        {
            return true;
        }
    }
    return false;
}

static void load(void)
{
    SaveHeader header;
    Uint8 *archive;
    const SaveEntry *entries;
    Uint32 i;
    FILE *slot;

    if (loaded)
    {
        return;
    }
    loaded = 1;

    slot = fopen(POCKET_SAVE_FILE, "rb");
    if (slot == NULL)
    {
        printf("Saves: no save data yet\n");
        return;
    }
    if (fread(&header, sizeof(header), 1, slot) != 1 ||
        header.magic != SAVEFS_MAGIC ||
        header.file_count > SAVEFS_MAX_FILES ||
        header.total_bytes > SAVEFS_MAX_BYTES ||
        header.total_bytes < sizeof(header) + header.file_count * sizeof(SaveEntry))
    {
        /* A save file that has never been written reads as blank. */
        printf("Saves: no save data yet\n");
        fclose(slot);
        return;
    }

    archive = (Uint8 *) SDL_malloc(header.total_bytes);
    if (archive == NULL ||
        fseek(slot, 0, SEEK_SET) != 0 ||
        fread(archive, 1, header.total_bytes, slot) != header.total_bytes)
    {
        printf("Saves: could not read the save file\n");
        SDL_free(archive);
        fclose(slot);
        return;
    }
    fclose(slot);

    entries = (const SaveEntry *) (archive + sizeof(SaveHeader));
    for (i = 0; i < header.file_count; i++)
    {
        const SaveEntry *entry = &entries[i];
        SaveFile *file;
        if ((Uint64) entry->offset + entry->size > header.total_bytes)
        {
            printf("Saves: save data is damaged; ignoring the rest of it\n");
            break;
        }
        file = &files[file_count];
        file->data = (Uint8 *) SDL_malloc(entry->size + 1);
        if (file->data == NULL)
        {
            break;
        }
        SDL_memcpy(file->name, entry->name, SAVEFS_NAME_LEN);
        file->name[SAVEFS_NAME_LEN - 1] = '\0';
        SDL_memcpy(file->data, archive + entry->offset, entry->size);
        file->size = entry->size;
        file_count++;
    }
    SDL_free(archive);
    printf("Saves: loaded %u files\n", (unsigned) file_count);
}

static bool store(void)
{
    Uint32 total = (Uint32) (sizeof(SaveHeader) + file_count * sizeof(SaveEntry));
    Uint32 offset = total;
    Uint8 *archive;
    SaveHeader *header;
    SaveEntry *entries;
    Uint32 i;
    FILE *slot;
    bool ok;

    for (i = 0; i < file_count; i++)
    {
        total += files[i].size;
    }
    if (total > SAVEFS_MAX_BYTES)
    {
        return SDL_SetError("Saves: %u bytes will not fit in the save file", (unsigned) total);
    }

    archive = (Uint8 *) SDL_calloc(1, total);
    if (archive == NULL)
    {
        return SDL_SetError("Out of memory");
    }
    header = (SaveHeader *) archive;
    header->magic = SAVEFS_MAGIC;
    header->file_count = file_count;
    header->total_bytes = total;
    entries = (SaveEntry *) (archive + sizeof(SaveHeader));
    for (i = 0; i < file_count; i++)
    {
        SDL_memcpy(entries[i].name, files[i].name, SAVEFS_NAME_LEN);
        entries[i].size = files[i].size;
        entries[i].offset = offset;
        SDL_memcpy(archive + offset, files[i].data, files[i].size);
        offset += files[i].size;
    }

    slot = fopen(POCKET_SAVE_FILE, "wb");
    ok = slot != NULL && fwrite(archive, 1, total, slot) == total;
    if (slot != NULL && fclose(slot) != 0)
    {
        ok = false;
    }
    SDL_free(archive);
    if (!ok)
    {
        return SDL_SetError("Saves: could not write the save file");
    }
    return true;
}

static SaveFile *find(const char *name)
{
    Uint32 i;
    for (i = 0; i < file_count; i++)
    {
        if (strcmp(files[i].name, name) == 0)
        {
            return &files[i];
        }
    }
    return NULL;
}

static bool is_directory(const char *name)
{
    Uint32 i;
    size_t r;
    for (r = 0; r < SDL_arraysize(roots); r++)
    {
        if (strcmp(name, roots[r]) == 0)
        {
            return true;
        }
    }
    for (i = 0; i < made_dir_count; i++)
    {
        if (strcmp(made_dirs[i], name) == 0)
        {
            return true;
        }
    }
    for (i = 0; i < file_count; i++)
    {
        if (strcmp(files[i].name, name) != 0 && under(files[i].name, name))
        {
            return true;
        }
    }
    return false;
}

bool SAVEFS_mkdir(const char *path)
{
    char name[SAVEFS_NAME_LEN];

    load();
    if (!normalize(path, name, sizeof(name)))
    {
        return SDL_SetError("Path too long: %s", path);
    }
    if (find(name) != NULL)
    {
        return SDL_SetError("%s is a file", path);
    }
    if (is_directory(name))
    {
        return true;
    }
    if (made_dir_count == SAVEFS_MAX_DIRS)
    {
        return SDL_SetError("Too many directories to create %s", path);
    }
    SDL_strlcpy(made_dirs[made_dir_count++], name, SAVEFS_NAME_LEN);
    return true;
}

bool SAVEFS_stat(const char *path, SDL_PathInfo *info)
{
    char name[SAVEFS_NAME_LEN];
    const SaveFile *file;

    load();
    SDL_zerop(info);
    if (!normalize(path, name, sizeof(name)))
    {
        return SDL_SetError("Path too long: %s", path);
    }
    file = find(name);
    if (file != NULL)
    {
        info->type = SDL_PATHTYPE_FILE;
        info->size = file->size;
        return true;
    }
    if (is_directory(name))
    {
        info->type = SDL_PATHTYPE_DIRECTORY;
        return true;
    }
    return SDL_SetError("Can't stat %s", path);
}

bool SAVEFS_enumerate(const char *path, SDL_EnumerateDirectoryCallback callback, void *userdata)
{
    char dir[SAVEFS_NAME_LEN];
    const size_t dir_len = normalize(path, dir, sizeof(dir)) ? strlen(dir) : 0;
    Uint32 i;

    load();
    if (dir_len == 0)
    {
        return SDL_SetError("Path too long: %s", path);
    }
    for (i = 0; i < file_count; i++)
    {
        const char *child = files[i].name + dir_len + 1;
        SDL_EnumerationResult r;
        /* Only direct children; files further down are listed through
         * their own directory. */
        if (!under(files[i].name, dir) || files[i].name[dir_len] != '/' ||
            strchr(child, '/') != NULL)
        {
            continue;
        }
        r = callback(userdata, path, child);
        if (r == SDL_ENUM_FAILURE)
        {
            return false;
        }
        if (r == SDL_ENUM_SUCCESS)
        {
            break;
        }
    }
    return true;
}

bool SAVEFS_remove(const char *path)
{
    char name[SAVEFS_NAME_LEN];
    SaveFile *file;

    load();
    if (!normalize(path, name, sizeof(name)))
    {
        return SDL_SetError("Path too long: %s", path);
    }
    file = find(name);
    if (file == NULL)
    {
        /* Removing an empty virtual directory always works. */
        return is_directory(name) ? true : SDL_SetError("Can't remove %s", path);
    }
    SDL_free(file->data);
    *file = files[--file_count];
    return store();
}

/* Writing: the stream collects the whole file and stores it on close. */

typedef struct
{
    char name[SAVEFS_NAME_LEN];
    Uint8 *data;
    size_t size;
    size_t capacity;
    size_t pos;
    bool writable;
} SaveStream;

static bool reserve(SaveStream *s, size_t size)
{
    Uint8 *grown;
    size_t capacity = s->capacity != 0 ? s->capacity : 1024;
    if (size <= s->capacity)
    {
        return true;
    }
    while (capacity < size)
    {
        capacity *= 2;
    }
    grown = (Uint8 *) SDL_realloc(s->data, capacity);
    if (grown == NULL)
    {
        return false;
    }
    s->data = grown;
    s->capacity = capacity;
    return true;
}

static Sint64 SDLCALL stream_size(void *userdata)
{
    return (Sint64) ((SaveStream *) userdata)->size;
}

static Sint64 SDLCALL stream_seek(void *userdata, Sint64 offset, SDL_IOWhence whence)
{
    SaveStream *s = (SaveStream *) userdata;
    Sint64 target = offset;
    if (whence == SDL_IO_SEEK_CUR)
    {
        target += (Sint64) s->pos;
    }
    else if (whence == SDL_IO_SEEK_END)
    {
        target += (Sint64) s->size;
    }
    if (target < 0 || (size_t) target > s->size)
    {
        return SDL_SetError("Seek out of range"), -1;
    }
    s->pos = (size_t) target;
    return target;
}

static size_t SDLCALL stream_read(void *userdata, void *ptr, size_t size, SDL_IOStatus *status)
{
    SaveStream *s = (SaveStream *) userdata;
    size_t n = s->size - s->pos;
    if (n > size)
    {
        n = size;
    }
    SDL_memcpy(ptr, s->data + s->pos, n);
    s->pos += n;
    if (n < size)
    {
        *status = SDL_IO_STATUS_EOF;
    }
    return n;
}

static size_t SDLCALL stream_write(void *userdata, const void *ptr, size_t size, SDL_IOStatus *status)
{
    SaveStream *s = (SaveStream *) userdata;
    if (!s->writable)
    {
        *status = SDL_IO_STATUS_READONLY;
        return 0;
    }
    if (!reserve(s, s->pos + size))
    {
        *status = SDL_IO_STATUS_ERROR;
        return 0;
    }
    SDL_memcpy(s->data + s->pos, ptr, size);
    s->pos += size;
    if (s->pos > s->size)
    {
        s->size = s->pos;
    }
    return size;
}

static bool SDLCALL stream_close(void *userdata)
{
    SaveStream *s = (SaveStream *) userdata;
    bool ok = true;
    if (s->writable)
    {
        SaveFile *file = find(s->name);
        Uint8 *old = NULL;
        if (file == NULL)
        {
            if (file_count == SAVEFS_MAX_FILES)
            {
                ok = SDL_SetError("Saves: too many files to store %s", s->name);
            }
            else
            {
                file = &files[file_count++];
                SDL_memcpy(file->name, s->name, SAVEFS_NAME_LEN);
            }
        }
        else
        {
            old = file->data;
        }
        if (file != NULL)
        {
            /* The archive owns the buffer from here on. */
            file->data = s->data;
            file->size = (Uint32) s->size;
            s->data = NULL;
            SDL_free(old);
            ok = store();
        }
    }
    SDL_free(s->data);
    SDL_free(s);
    return ok;
}

/* Only the modes the game and PhysFS use: "rb", "wb" and "ab". */
SDL_IOStream *SAVEFS_open(const char *path, const char *mode)
{
    SDL_IOStreamInterface iface;
    SDL_IOStream *io;
    SaveStream *s;
    const SaveFile *file;
    const bool write = mode[0] == 'w' || mode[0] == 'a';

    load();
    s = (SaveStream *) SDL_calloc(1, sizeof(*s));
    if (s == NULL)
    {
        SDL_SetError("Out of memory");
        return NULL;
    }
    if (!normalize(path, s->name, sizeof(s->name)))
    {
        SDL_free(s);
        SDL_SetError("Path too long: %s", path);
        return NULL;
    }

    file = find(s->name);
    if (file == NULL && !write)
    {
        SDL_free(s);
        SDL_SetError("Couldn't open %s", path);
        return NULL;
    }
    if (file != NULL && mode[0] != 'w')
    {
        if (!reserve(s, file->size + 1))
        {
            SDL_free(s);
            SDL_SetError("Out of memory");
            return NULL;
        }
        SDL_memcpy(s->data, file->data, file->size);
        s->size = file->size;
    }
    s->writable = write;
    s->pos = mode[0] == 'a' ? s->size : 0;

    SDL_INIT_INTERFACE(&iface);
    iface.size = stream_size;
    iface.seek = stream_seek;
    iface.read = stream_read;
    iface.write = stream_write;
    iface.close = stream_close;
    io = SDL_OpenIO(&iface, s);
    if (io == NULL)
    {
        SDL_free(s->data);
        SDL_free(s);
    }
    return io;
}
