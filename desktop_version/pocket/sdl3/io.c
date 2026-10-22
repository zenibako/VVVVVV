/* SDL3 I/O streams and filesystem calls on musl stdio.
 *
 * openfpgaOS resolves fopen() by basename against the core's data slots
 * and save files, so the base and preference paths are both "/". */
#include <SDL3/SDL.h>

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct SDL_IOStream
{
    SDL_IOStreamInterface iface;
    void *userdata;
    SDL_IOStatus status;
};

SDL_IOStream *SDL_OpenIO(const SDL_IOStreamInterface *iface, void *userdata)
{
    SDL_IOStream *io = (SDL_IOStream *) SDL_calloc(1, sizeof(*io));
    if (io == NULL)
    {
        SDL_SetError("Out of memory");
        return NULL;
    }
    io->iface = *iface;
    io->userdata = userdata;
    io->status = SDL_IO_STATUS_READY;
    return io;
}

bool SDL_CloseIO(SDL_IOStream *io)
{
    bool result = true;
    if (io == NULL)
    {
        return SDL_SetError("Parameter 'context' is invalid");
    }
    if (io->iface.close != NULL)
    {
        result = io->iface.close(io->userdata);
    }
    SDL_free(io);
    return result;
}

SDL_IOStatus SDL_GetIOStatus(SDL_IOStream *io) { return io->status; }

Sint64 SDL_GetIOSize(SDL_IOStream *io)
{
    if (io->iface.size == NULL)
    {
        return SDL_SetError("Not supported"), -1;
    }
    return io->iface.size(io->userdata);
}

Sint64 SDL_SeekIO(SDL_IOStream *io, Sint64 offset, SDL_IOWhence whence)
{
    if (io->iface.seek == NULL)
    {
        return SDL_SetError("Not supported"), -1;
    }
    return io->iface.seek(io->userdata, offset, whence);
}

Sint64 SDL_TellIO(SDL_IOStream *io) { return SDL_SeekIO(io, 0, SDL_IO_SEEK_CUR); }

size_t SDL_ReadIO(SDL_IOStream *io, void *ptr, size_t size)
{
    size_t got;
    if (io->iface.read == NULL)
    {
        io->status = SDL_IO_STATUS_WRITEONLY;
        return 0;
    }
    io->status = SDL_IO_STATUS_READY;
    if (size == 0)
    {
        return 0;
    }
    got = io->iface.read(io->userdata, ptr, size, &io->status);
    if (got == 0 && io->status == SDL_IO_STATUS_READY)
    {
        io->status = SDL_IO_STATUS_EOF;
    }
    return got;
}

size_t SDL_WriteIO(SDL_IOStream *io, const void *ptr, size_t size)
{
    if (io->iface.write == NULL)
    {
        io->status = SDL_IO_STATUS_READONLY;
        return 0;
    }
    io->status = SDL_IO_STATUS_READY;
    if (size == 0)
    {
        return 0;
    }
    return io->iface.write(io->userdata, ptr, size, &io->status);
}

bool SDL_FlushIO(SDL_IOStream *io)
{
    io->status = SDL_IO_STATUS_READY;
    if (io->iface.flush == NULL)
    {
        return true;
    }
    return io->iface.flush(io->userdata, &io->status);
}

/* Files */

static Sint64 SDLCALL file_size(void *userdata)
{
    FILE *f = (FILE *) userdata;
    const long here = ftell(f);
    long end;
    if (here < 0 || fseek(f, 0, SEEK_END) != 0)
    {
        return -1;
    }
    end = ftell(f);
    fseek(f, here, SEEK_SET);
    return end;
}

static Sint64 SDLCALL file_seek(void *userdata, Sint64 offset, SDL_IOWhence whence)
{
    FILE *f = (FILE *) userdata;
    int stdio_whence = SEEK_SET;
    if (whence == SDL_IO_SEEK_CUR)
    {
        stdio_whence = SEEK_CUR;
    }
    else if (whence == SDL_IO_SEEK_END)
    {
        stdio_whence = SEEK_END;
    }
    if (fseek(f, (long) offset, stdio_whence) != 0)
    {
        return SDL_SetError("Couldn't seek"), -1;
    }
    return ftell(f);
}

static size_t SDLCALL file_read(void *userdata, void *ptr, size_t size, SDL_IOStatus *status)
{
    FILE *f = (FILE *) userdata;
    const size_t got = fread(ptr, 1, size, f);
    if (got < size)
    {
        *status = ferror(f) ? SDL_IO_STATUS_ERROR : SDL_IO_STATUS_EOF;
    }
    return got;
}

static size_t SDLCALL file_write(void *userdata, const void *ptr, size_t size, SDL_IOStatus *status)
{
    FILE *f = (FILE *) userdata;
    const size_t put = fwrite(ptr, 1, size, f);
    if (put < size)
    {
        *status = SDL_IO_STATUS_ERROR;
    }
    return put;
}

static bool SDLCALL file_flush(void *userdata, SDL_IOStatus *status)
{
    if (fflush((FILE *) userdata) != 0)
    {
        *status = SDL_IO_STATUS_ERROR;
        return false;
    }
    return true;
}

static bool SDLCALL file_close(void *userdata)
{
    return fclose((FILE *) userdata) == 0;
}

SDL_IOStream *SDL_IOFromFile(const char *file, const char *mode)
{
    SDL_IOStreamInterface iface;
    SDL_IOStream *io;
    FILE *f = fopen(file, mode);
    if (f == NULL)
    {
        SDL_SetError("Couldn't open %s: %s", file, strerror(errno));
        return NULL;
    }
    SDL_INIT_INTERFACE(&iface);
    iface.size = file_size;
    iface.seek = file_seek;
    iface.read = file_read;
    iface.write = file_write;
    iface.flush = file_flush;
    iface.close = file_close;
    io = SDL_OpenIO(&iface, f);
    if (io == NULL)
    {
        fclose(f);
    }
    return io;
}

/* Read-only memory */

typedef struct
{
    const Uint8 *base;
    size_t size;
    size_t pos;
} MemStream;

static Sint64 SDLCALL mem_size(void *userdata) { return ((MemStream *) userdata)->size; }

static Sint64 SDLCALL mem_seek(void *userdata, Sint64 offset, SDL_IOWhence whence)
{
    MemStream *m = (MemStream *) userdata;
    Sint64 target = offset;
    if (whence == SDL_IO_SEEK_CUR)
    {
        target += m->pos;
    }
    else if (whence == SDL_IO_SEEK_END)
    {
        target += m->size;
    }
    if (target < 0 || (size_t) target > m->size)
    {
        return SDL_SetError("Seek out of range"), -1;
    }
    m->pos = (size_t) target;
    return target;
}

static size_t SDLCALL mem_read(void *userdata, void *ptr, size_t size, SDL_IOStatus *status)
{
    MemStream *m = (MemStream *) userdata;
    size_t n = m->size - m->pos;
    if (n > size)
    {
        n = size;
    }
    memcpy(ptr, m->base + m->pos, n);
    m->pos += n;
    if (n < size)
    {
        *status = SDL_IO_STATUS_EOF;
    }
    return n;
}

static bool SDLCALL mem_close(void *userdata)
{
    SDL_free(userdata);
    return true;
}

SDL_IOStream *SDL_IOFromConstMem(const void *mem, size_t size)
{
    SDL_IOStreamInterface iface;
    SDL_IOStream *io;
    MemStream *m = (MemStream *) SDL_calloc(1, sizeof(*m));
    if (m == NULL)
    {
        return NULL;
    }
    m->base = (const Uint8 *) mem;
    m->size = size;
    SDL_INIT_INTERFACE(&iface);
    iface.size = mem_size;
    iface.seek = mem_seek;
    iface.read = mem_read;
    iface.close = mem_close;
    io = SDL_OpenIO(&iface, m);
    if (io == NULL)
    {
        SDL_free(m);
    }
    return io;
}

/* WAV loading is replaced by the converted sound pack (phase 4). */
bool SDL_LoadWAV_IO(SDL_IOStream *src, bool closeio, SDL_AudioSpec *spec, Uint8 **audio_buf, Uint32 *audio_len)
{
    (void) spec;
    if (audio_buf != NULL)
    {
        *audio_buf = NULL;
    }
    if (audio_len != NULL)
    {
        *audio_len = 0;
    }
    if (closeio && src != NULL)
    {
        SDL_CloseIO(src);
    }
    return SDL_SetError("WAV loading is not supported");
}

/* Filesystem */

const char *SDL_GetBasePath(void) { return "/"; }

char *SDL_GetPrefPath(const char *org, const char *app)
{
    (void) org;
    (void) app;
    return SDL_strdup("/");
}

const char *SDL_GetUserFolder(SDL_Folder folder)
{
    (void) folder;
    SDL_SetError("No user folders");
    return NULL;
}

bool SDL_GetPathInfo(const char *path, SDL_PathInfo *info)
{
    struct stat st;
    SDL_PathInfo dummy;
    if (info == NULL)
    {
        info = &dummy;
    }
    SDL_zerop(info);
    if (stat(path, &st) == 0)
    {
        info->type = S_ISDIR(st.st_mode) ? SDL_PATHTYPE_DIRECTORY : SDL_PATHTYPE_FILE;
        info->size = (Uint64) st.st_size;
        return true;
    }
    /* Data slots are always openable even where stat() is not wired up. */
    {
        FILE *f = fopen(path, "rb");
        if (f != NULL)
        {
            info->type = SDL_PATHTYPE_FILE;
            if (fseek(f, 0, SEEK_END) == 0)
            {
                info->size = (Uint64) ftell(f);
            }
            fclose(f);
            return true;
        }
    }
    return SDL_SetError("Can't stat %s", path);
}

bool SDL_EnumerateDirectory(const char *path, SDL_EnumerateDirectoryCallback callback, void *userdata)
{
    struct dirent *entry;
    bool result = true;
    DIR *dir = opendir(path);
    if (dir == NULL)
    {
        return SDL_SetError("Can't open directory %s", path);
    }
    while ((entry = readdir(dir)) != NULL)
    {
        SDL_EnumerationResult r;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
            continue;
        }
        r = callback(userdata, path, entry->d_name);
        if (r == SDL_ENUM_FAILURE)
        {
            result = false;
            break;
        }
        if (r == SDL_ENUM_SUCCESS)
        {
            break;
        }
    }
    closedir(dir);
    return result;
}

bool SDL_CreateDirectory(const char *path)
{
    if (mkdir(path, 0770) != 0 && errno != EEXIST)
    {
        return SDL_SetError("Can't create directory %s", path);
    }
    return true;
}

bool SDL_RemovePath(const char *path)
{
    if (remove(path) != 0)
    {
        return SDL_SetError("Can't remove %s", path);
    }
    return true;
}
