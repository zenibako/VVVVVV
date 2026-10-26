/* Run against the real settings loader's defaults and gamepad event path. */
#include "src/Input.h"
#include "src/Render.h"
#include "src/ButtonGlyphs.h"

static void clear_control_bindings(void)
{
    game.controllerButton_flip.clear();
    game.controllerButton_esc.clear();
    game.controllerButton_map.clear();
    game.controllerButton_restart.clear();
    game.controllerButton_interact.clear();
}

static void control_button(SDL_GamepadButton button, bool down)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
    event.gbutton.button = button;
    event.gbutton.down = down;
    assert(SDL_PushEvent(&event));
    key.Poll();
}

static void check_face_buttons(void)
{
    control_button(SDL_GAMEPAD_BUTTON_SOUTH, true); /* A, right */
    assert(key.isDown(game.controllerButton_flip));
    assert(!key.isDown(game.controllerButton_esc));
    control_button(SDL_GAMEPAD_BUTTON_EAST, true); /* B, bottom */
    control_button(SDL_GAMEPAD_BUTTON_SOUTH, false);
    assert(key.isDown(game.controllerButton_flip));
    assert(!key.isDown(game.controllerButton_esc));
    control_button(SDL_GAMEPAD_BUTTON_EAST, false);
    assert(!key.isDown(game.controllerButton_flip));
    control_button(SDL_GAMEPAD_BUTTON_START, true);
    assert(key.isDown(game.controllerButton_esc));
    assert(!key.isDown(game.controllerButton_flip));
    control_button(SDL_GAMEPAD_BUTTON_START, false);
}

static void check_controls(void)
{
    clear_control_bindings();
    game.setdefaultcontrollerbuttons();
    check_face_buttons();

    /* B backs out once; it cannot also confirm the newly exposed menu. */
    graphics.fademode = FADE_NONE;
    game.createmenu(Menu::mainmenu);
    game.createmenu(Menu::options);
    game.menustart = true;
    game.jumpheld = false;
    control_button(SDL_GAMEPAD_BUTTON_EAST, true);
    titleinput();
    assert(game.currentmenuname == Menu::mainmenu && !game.press_action);
    titleinput();
    assert(game.currentmenuname == Menu::mainmenu);
    control_button(SDL_GAMEPAD_BUTTON_EAST, false);
    titleinput();

    graphics.menuoffset = 0;
    graphics.resumegamemode = false;
    game.mapheld = game.jumpheld = false;
    game.menupage = 30;
    control_button(SDL_GAMEPAD_BUTTON_EAST, true);
    mapinput();
    assert(graphics.resumegamemode && !game.press_action);
    control_button(SDL_GAMEPAD_BUTTON_EAST, false);

    graphics.resumegamemode = false;
    game.mapheld = game.jumpheld = false;
    control_button(SDL_GAMEPAD_BUTTON_EAST, true);
    teleporterinput();
    assert(graphics.resumegamemode && !game.press_action);
    control_button(SDL_GAMEPAD_BUTTON_EAST, false);

    /* Both generations of saved defaults migrate; reloading is idempotent. */
    for (int with_start = 0; with_start < 2; with_start++)
    {
        clear_control_bindings();
        game.controllerButton_flip.push_back(SDL_GAMEPAD_BUTTON_SOUTH);
        game.controllerButton_esc.push_back(SDL_GAMEPAD_BUTTON_EAST);
        if (with_start) game.controllerButton_esc.push_back(SDL_GAMEPAD_BUTTON_START);
        game.setdefaultcontrollerbuttons();
        game.setdefaultcontrollerbuttons();
        check_face_buttons();
    }

    /* An old save with Start deliberately rebound is a custom layout. */
    clear_control_bindings();
    game.controllerButton_flip.push_back(SDL_GAMEPAD_BUTTON_SOUTH);
    game.controllerButton_esc.push_back(SDL_GAMEPAD_BUTTON_EAST);
    game.controllerButton_restart.push_back(SDL_GAMEPAD_BUTTON_START);
    game.setdefaultcontrollerbuttons();
    control_button(SDL_GAMEPAD_BUTTON_START, true);
    assert(key.isDown(game.controllerButton_restart));
    assert(!key.isDown(game.controllerButton_esc));
    control_button(SDL_GAMEPAD_BUTTON_START, false);
    control_button(SDL_GAMEPAD_BUTTON_EAST, true);
    assert(key.isDown(game.controllerButton_esc));
    assert(!key.isDown(game.controllerButton_flip));
    control_button(SDL_GAMEPAD_BUTTON_EAST, false);

    /* Rebinding gameplay flip to B alone must not strand menu navigation. */
    clear_control_bindings();
    game.controllerButton_flip.push_back(SDL_GAMEPAD_BUTTON_EAST);
    game.controllerButton_esc.push_back(SDL_GAMEPAD_BUTTON_START);
    game.setdefaultcontrollerbuttons();
    Action menu_accept;
    menu_accept.Menu = Action_Menu_Accept;
    assert(SDL_strcmp(BUTTONGLYPHS_get_button(ActionSet_Menu, menu_accept, 0), "\xee\xac\xb7") == 0);
    assert(BUTTONGLYPHS_get_button(ActionSet_Menu, menu_accept, 1) == NULL);
    game.menustart = true;
    game.jumpheld = false;
    game.createmenu(Menu::mainmenu);
    control_button(SDL_GAMEPAD_BUTTON_SOUTH, true);
    titleinput();
    assert(game.press_action);
    control_button(SDL_GAMEPAD_BUTTON_SOUTH, false);
    titleinput();

    clear_control_bindings();
    game.controllerButton_flip.push_back(SDL_GAMEPAD_BUTTON_WEST);
    game.controllerButton_esc.push_back(SDL_GAMEPAD_BUTTON_EAST);
    game.setdefaultcontrollerbuttons();
    control_button(SDL_GAMEPAD_BUTTON_EAST, true);
    assert(!key.isDown(game.controllerButton_flip));
    assert(key.isDown(game.controllerButton_esc));
    control_button(SDL_GAMEPAD_BUTTON_EAST, false);
    control_button(SDL_GAMEPAD_BUTTON_WEST, true);
    assert(key.isDown(game.controllerButton_flip));
    control_button(SDL_GAMEPAD_BUTTON_WEST, false);

    game.menustart = false;
    game.jumpheld = false;
    graphics.fademode = FADE_NONE;
    graphics.col_tr = graphics.col_tg = graphics.col_tb = 0;
    game.screenshake = game.flashlight = 0;
    graphics.set_render_target(graphics.gameTexture);
    titlerender();
    graphics.set_render_target(graphics.gameTexture);
    SDL_Surface* image = SDL_RenderReadPixels(gameScreen.m_renderer, NULL);
    assert(image && save_capture(image, "title-controls.bmp"));
    const SDL_PixelFormatDetails* format = SDL_GetPixelFormatDetails(image->format);
    assert(format && format->bytes_per_pixel <= sizeof(Uint32));
    int bright_pixels = 0;
    for (int y = 175; y < 184; y++)
        for (int x = 0; x < 320; x++)
        {
            Uint8 r, g, b, a;
            Uint32 pixel = 0;
            SDL_memcpy(&pixel, (Uint8*) image->pixels + y * image->pitch
                + x * format->bytes_per_pixel, format->bytes_per_pixel);
            SDL_GetRGBA(pixel, format, NULL, &r, &g, &b, &a);
            bright_pixels += r > 220 && g > 220 && b > 220;
        }
    SDL_DestroySurface(image);
    graphics.set_render_target(NULL);
    assert(bright_pixels > 100); /* Visible even during a black title colour. */
    const SDL_GamepadButton title_buttons[] = {
        SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST,
        SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
        SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_BACK,
        SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
        SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN,
        SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT
    };
    for (size_t i = 0; i < SDL_arraysize(title_buttons); i++)
    {
        game.createmenu(Menu::mainmenu);
        game.menustart = false;
        game.jumpheld = false;
        control_button(title_buttons[i], true);
        titleinput();
        assert(game.menustart); /* Every game button continues, regardless of bindings. */
        const Menu::MenuName menu = game.currentmenuname;
        titleinput();
        assert(game.menustart && game.currentmenuname == menu);
        control_button(title_buttons[i], false);
        titleinput();
    }
    puts("Pocket controls: both face buttons, held release, saved defaults, custom bindings and all 12 title buttons pass");
}
