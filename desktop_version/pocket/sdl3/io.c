/* SDL3 I/O streams and filesystem calls on musl stdio.
 *
 * openfpgaOS resolves fopen() by basename against the core's data slots
 * and save files, so the base and preference paths are both "/". The
 * game's writable directories are kept in one save file by savefs.c. */
#include <SDL3/SDL.h>

#include "savefs.h"

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
    FILE *f;
    if (SAVEFS_owns(file))
    {
        return SAVEFS_open(file, mode);
    }
    f = fopen(file, mode);
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

/* PCM WAV files, 8 or 16 bits: the game's sound effects. */
static Uint32 wav_u32(const Uint8 *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((Uint32) p[3] << 24);
}

static Uint16 wav_u16(const Uint8 *p)
{
    return (Uint16) (p[0] | (p[1] << 8));
}

bool SDL_LoadWAV_IO(SDL_IOStream *src, bool closeio, SDL_AudioSpec *spec, Uint8 **audio_buf, Uint32 *audio_len)
{
    bool ok = false;
    Uint8 *file = NULL;
    size_t size = 0;
    if (audio_buf != NULL)
    {
        *audio_buf = NULL;
    }
    if (audio_len != NULL)
    {
        *audio_len = 0;
    }
    if (src == NULL || spec == NULL || audio_buf == NULL || audio_len == NULL)
    {
        SDL_SetError("Parameter is invalid");
        goto done;
    }
    {
        const Sint64 length = SDL_GetIOSize(src);
        if (length < 0)
        {
            goto done;
        }
        size = (size_t) length;
        file = (Uint8 *) SDL_malloc(size ? size : 1);
        if (file == NULL || SDL_ReadIO(src, file, size) != size)
        {
            SDL_SetError("Can't read WAV file");
            goto done;
        }
    }
    if (size < 12 || SDL_memcmp(file, "RIFF", 4) != 0 || SDL_memcmp(file + 8, "WAVE", 4) != 0)
    {
        SDL_SetError("Not a WAV file");
        goto done;
    }
    {
        const Uint8 *fmt = NULL;
        Uint32 fmt_size = 0;
        const Uint8 *data = NULL;
        Uint32 data_size = 0;
        size_t at = 12;
        while (at + 8 <= size)
        {
            const Uint32 chunk = wav_u32(file + at + 4);
            const size_t body = at + 8;
            const Uint32 have = (Uint32) SDL_min((size_t) chunk, size - body);
            if (SDL_memcmp(file + at, "fmt ", 4) == 0 && have >= 16)
            {
                fmt = file + body;
                fmt_size = have;
            }
            else if (SDL_memcmp(file + at, "data", 4) == 0)
            {
                data = file + body;
                data_size = have;
            }
            at = body + chunk + (chunk & 1);
        }
        if (fmt == NULL || data == NULL)
        {
            SDL_SetError("WAV file has no fmt or data chunk");
            goto done;
        }
        Uint16 tag = wav_u16(fmt);
        const Uint16 channels = wav_u16(fmt + 2);
        const Uint32 rate = wav_u32(fmt + 4);
        const Uint16 bits = wav_u16(fmt + 14);
        /* WAVE_FORMAT_EXTENSIBLE: the real format leads its subformat. */
        if (tag == 0xFFFE && fmt_size >= 26 && wav_u16(fmt + 16) >= 22)
        {
            tag = wav_u16(fmt + 24);
        }
        if (tag != 1 || (bits != 8 && bits != 16) || channels == 0 || rate == 0)
        {
            SDL_SetError("Only 8 and 16-bit PCM WAV files are supported");
            goto done;
        }
        Uint8 *pcm = (Uint8 *) SDL_malloc(data_size ? data_size : 1);
        if (pcm == NULL)
        {
            goto done;
        }
        SDL_memcpy(pcm, data, data_size);
        spec->format = bits == 8 ? SDL_AUDIO_U8 : SDL_AUDIO_S16LE;
        spec->channels = channels;
        spec->freq = (int) rate;
        *audio_buf = pcm;
        *audio_len = data_size;
        ok = true;
    }
done:
    SDL_free(file);
    if (closeio && src != NULL)
    {
        SDL_CloseIO(src);
    }
    return ok;
}

/* Filesystem */

const char *SDL_GetBasePath(void) { return "/"; }

char *SDL_GetPrefPath(const char *org, const char *app)
{
    (void) org;
    (void) app;
    return SDL_strdup("/");
}

/* PHYSFS_init needs a home folder; the card's root stands in for it. */
const char *SDL_GetUserFolder(SDL_Folder folder)
{
    if (folder == SDL_FOLDER_HOME)
    {
        return "/";
    }
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
    if (SAVEFS_owns(path))
    {
        return SAVEFS_stat(path, info);
    }
    /* The game mounts the base path "/" itself; openfpgaOS may not stat it. */
    if (path[strspn(path, "/.")] == '\0')
    {
        info->type = SDL_PATHTYPE_DIRECTORY;
        return true;
    }
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
    DIR *dir;
    if (SAVEFS_owns(path))
    {
        return SAVEFS_enumerate(path, callback, userdata);
    }
    dir = opendir(path);
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
    if (SAVEFS_owns(path))
    {
        return SAVEFS_mkdir(path);
    }
    if (mkdir(path, 0770) != 0 && errno != EEXIST)
    {
        return SDL_SetError("Can't create directory %s", path);
    }
    return true;
}

bool SDL_RemovePath(const char *path)
{
    if (SAVEFS_owns(path))
    {
        return SAVEFS_remove(path);
    }
    if (remove(path) != 0)
    {
        return SDL_SetError("Can't remove %s", path);
    }
    return true;
}
