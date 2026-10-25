/* SDL3 events and gamepad on openfpgaOS.
 *
 * The Pocket has one built-in controller. It is reported as a single
 * gamepad that is added on the first poll, and its buttons and left stick
 * become SDL gamepad events. A and B follow the Pocket's convention of A
 * to confirm and B to go back: A (right) is SDL's south button, which the
 * game uses to confirm and flip, and B (bottom) is east, its back button.
 * X (top) and Y (left) map by position. The game shows Pocket button glyphs
 * for them (ButtonGlyphs.cpp, openfpga/fonts). */
#include <SDL3/SDL.h>

#include "of_input.h"

#define POCKET_JOYSTICK_ID 1

/* openfpgaOS bits to SDL's positional gamepad buttons. */
static const struct
{
    uint32_t mask;
    SDL_GamepadButton button;
} button_map[] = {
    {OF_BTN_A, SDL_GAMEPAD_BUTTON_SOUTH},
    {OF_BTN_B, SDL_GAMEPAD_BUTTON_EAST},
    {OF_BTN_Y, SDL_GAMEPAD_BUTTON_WEST},
    {OF_BTN_X, SDL_GAMEPAD_BUTTON_NORTH},
    {OF_BTN_SELECT, SDL_GAMEPAD_BUTTON_BACK},
    {OF_BTN_START, SDL_GAMEPAD_BUTTON_START},
    {OF_BTN_L3, SDL_GAMEPAD_BUTTON_LEFT_STICK},
    {OF_BTN_R3, SDL_GAMEPAD_BUTTON_RIGHT_STICK},
    {OF_BTN_L1, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},
    {OF_BTN_R1, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
    {OF_BTN_UP, SDL_GAMEPAD_BUTTON_DPAD_UP},
    {OF_BTN_DOWN, SDL_GAMEPAD_BUTTON_DPAD_DOWN},
    {OF_BTN_LEFT, SDL_GAMEPAD_BUTTON_DPAD_LEFT},
    {OF_BTN_RIGHT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
};

#define BUTTON_COUNT (sizeof(button_map) / sizeof(button_map[0]))

#ifdef POCKET_BENCH
/* Keys the benchmark holds down (pocket_bench.cpp), a bit each. */
Uint32 POCKET_bench_keys;
static const SDL_Keycode bench_keys[] = {SDLK_LEFT, SDLK_RIGHT, SDLK_Z, SDLK_RETURN};
#define BENCH_KEY_COUNT (sizeof(bench_keys) / sizeof(bench_keys[0]))
#else
#define BENCH_KEY_COUNT 0
#endif

/* One poll yields at most every button changing, two stick axes and the
 * added event. */
#define QUEUE_SIZE (BUTTON_COUNT + 3 + BENCH_KEY_COUNT)

struct SDL_Gamepad
{
    int open;
};

static SDL_Gamepad pocket_gamepad;

static SDL_Event queue[QUEUE_SIZE];
static int queue_len;
static int queue_pos;

static int added;
static Sint16 last_lx;
static Sint16 last_ly;

static SDL_Event *push(SDL_EventType type)
{
    SDL_Event *event = &queue[queue_len++];
    SDL_zerop(event);
    event->type = type;
    event->common.timestamp = SDL_GetTicks() * 1000000;
    return event;
}

static void push_axis(SDL_GamepadAxis axis, Sint16 value)
{
    SDL_Event *event = push(SDL_EVENT_GAMEPAD_AXIS_MOTION);
    event->gaxis.which = POCKET_JOYSTICK_ID;
    event->gaxis.axis = (Uint8) axis;
    event->gaxis.value = value;
}

static void fill_queue(void)
{
    size_t i;

    queue_len = 0;
    queue_pos = 0;

    if (!added)
    {
        added = 1;
        push(SDL_EVENT_GAMEPAD_ADDED)->gdevice.which = POCKET_JOYSTICK_ID;
    }

    of_input_poll_p0();

    for (i = 0; i < BUTTON_COUNT; i++)
    {
        const uint32_t mask = button_map[i].mask;
        int down;
        SDL_Event *event;

        if (of_btn_pressed(mask))
        {
            down = 1;
        }
        else if (of_btn_released(mask))
        {
            down = 0;
        }
        else
        {
            continue;
        }
        event = push(down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP);
        event->gbutton.which = POCKET_JOYSTICK_ID;
        event->gbutton.button = (Uint8) button_map[i].button;
        event->gbutton.down = down;
    }

    if (_of_input_p0.joy_lx != last_lx)
    {
        last_lx = _of_input_p0.joy_lx;
        push_axis(SDL_GAMEPAD_AXIS_LEFTX, last_lx);
    }
    if (_of_input_p0.joy_ly != last_ly)
    {
        last_ly = _of_input_p0.joy_ly;
        push_axis(SDL_GAMEPAD_AXIS_LEFTY, last_ly);
    }

#ifdef POCKET_BENCH
    {
        static Uint32 sent;
        for (i = 0; i < BENCH_KEY_COUNT; i++)
        {
            const Uint32 bit = 1u << i;
            if ((POCKET_bench_keys ^ sent) & bit)
            {
                const bool down = (POCKET_bench_keys & bit) != 0;
                SDL_Event *event = push(down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP);
                event->key.key = bench_keys[i];
                event->key.down = down;
            }
        }
        sent = POCKET_bench_keys;
    }
#endif
}

/* The game drains the queue once per frame. The first call of a frame reads
 * the controller; the call that finds the queue empty ends the frame. */
bool SDL_PollEvent(SDL_Event *event)
{
    if (queue_pos == queue_len)
    {
        static int drained;
        if (drained)
        {
            drained = 0;
            return false;
        }
        fill_queue();
        if (queue_len == 0)
        {
            return false;
        }
        drained = 1;
    }

    /* A NULL event only asks whether one is waiting, and leaves it. */
    if (event != NULL)
    {
        *event = queue[queue_pos++];
    }
    return true;
}

SDL_Gamepad *SDL_OpenGamepad(SDL_JoystickID instance_id)
{
    if (instance_id != POCKET_JOYSTICK_ID)
    {
        SDL_SetError("No gamepad with ID %u", (unsigned) instance_id);
        return NULL;
    }
    pocket_gamepad.open = 1;
    return &pocket_gamepad;
}

void SDL_CloseGamepad(SDL_Gamepad *gamepad)
{
    if (gamepad != NULL)
    {
        gamepad->open = 0;
    }
}

/* The gamepad doubles as its own joystick handle. */
SDL_Joystick *SDL_GetGamepadJoystick(SDL_Gamepad *gamepad)
{
    return (SDL_Joystick *) gamepad;
}

SDL_JoystickID SDL_GetJoystickID(SDL_Joystick *joystick)
{
    return joystick != NULL ? POCKET_JOYSTICK_ID : 0;
}

const char *SDL_GetGamepadName(SDL_Gamepad *gamepad)
{
    (void) gamepad;
    return "Analogue Pocket";
}

SDL_GamepadType SDL_GetGamepadType(SDL_Gamepad *gamepad)
{
    (void) gamepad;
    return SDL_GAMEPAD_TYPE_STANDARD;
}

Uint16 SDL_GetGamepadVendor(SDL_Gamepad *gamepad)
{
    (void) gamepad;
    return 0;
}

Uint16 SDL_GetGamepadProduct(SDL_Gamepad *gamepad)
{
    (void) gamepad;
    return 0;
}

/* The mapping is fixed; there is no gamecontrollerdb to load. */
int SDL_AddGamepadMappingsFromFile(const char *file)
{
    (void) file;
    return 0;
}
