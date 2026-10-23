/* SDL3 init, time, threads and desktop-only services on openfpgaOS. */
#include <SDL3/SDL.h>

#include <stdio.h>
#include <unistd.h>

#include "of_timer.h"
#include "pocket_audio.h"

bool SDL_Init(SDL_InitFlags flags)
{
    (void) flags;
    return true;
}

void SDL_Quit(void) {}

/* of_time_ms() is 32-bit; widen it so SDL_GetTicks never wraps. */
Uint64 SDL_GetTicks(void)
{
    static Uint32 last;
    static Uint64 high;
    const Uint32 now = of_time_ms();
    if (now < last)
    {
        high += (Uint64) 1 << 32;
    }
    last = now;
    return high | now;
}

void SDL_Delay(Uint32 ms)
{
    /* The main loop waits here for its next step: top up the sound
     * first, since nothing else will meanwhile. */
    pocket_audio_update();
    usleep((useconds_t) ms * 1000);
}

/* The game runs on one thread; PhysFS still asks for mutexes. */
SDL_Mutex *SDL_CreateMutex(void)
{
    static int token;
    return (SDL_Mutex *) &token;
}

void SDL_DestroyMutex(SDL_Mutex *mutex) { (void) mutex; }
void SDL_LockMutex(SDL_Mutex *mutex) { (void) mutex; }
void SDL_UnlockMutex(SDL_Mutex *mutex) { (void) mutex; }
SDL_ThreadID SDL_GetCurrentThreadID(void) { return 1; }

bool SDL_OpenURL(const char *url)
{
    (void) url;
    return SDL_SetError("No web browser");
}

char *SDL_GetClipboardText(void) { return SDL_strdup(""); }

bool SDL_SetClipboardText(const char *text)
{
    (void) text;
    return SDL_SetError("No clipboard");
}

/* Message boxes go to the OS console; the first button is the answer. */
bool SDL_ShowMessageBox(const SDL_MessageBoxData *data, int *buttonid)
{
    printf("%s: %s\n", data->title ? data->title : "", data->message ? data->message : "");
    if (buttonid != NULL)
    {
        *buttonid = data->numbuttons > 0 ? data->buttons[0].buttonID : -1;
    }
    return true;
}

bool SDL_ShowSimpleMessageBox(SDL_MessageBoxFlags flags, const char *title, const char *message, SDL_Window *window)
{
    (void) flags;
    (void) window;
    printf("%s: %s\n", title ? title : "", message ? message : "");
    return true;
}

bool SDL_EnableScreenSaver(void) { return true; }
bool SDL_DisableScreenSaver(void) { return true; }
bool SDL_ShowCursor(void) { return true; }
bool SDL_HideCursor(void) { return true; }
bool SDL_CursorVisible(void) { return false; }

/* No keyboard: text entry (level editor, name entry) is off. */
bool SDL_StartTextInput(SDL_Window *window) { (void) window; return true; }
bool SDL_StopTextInput(SDL_Window *window) { (void) window; return true; }
bool SDL_TextInputActive(SDL_Window *window) { (void) window; return false; }

bool SDL_SetTextInputArea(SDL_Window *window, const SDL_Rect *rect, int cursor)
{
    (void) window;
    (void) rect;
    (void) cursor;
    return true;
}

bool SDL_HasRectIntersection(const SDL_Rect *a, const SDL_Rect *b)
{
    if (a == NULL || b == NULL || a->w <= 0 || a->h <= 0 || b->w <= 0 || b->h <= 0)
    {
        return false;
    }
    return a->x < b->x + b->w && b->x < a->x + a->w && a->y < b->y + b->h && b->y < a->y + a->h;
}
