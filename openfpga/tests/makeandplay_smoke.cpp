/* Real-engine integration check for the public Make and Play edition.
 * Link instead of bench_smoke.cpp, with shared SDL3 and MAKEANDPLAY.
 * Run in a private capture directory with an empty, explicit -basedir:
 * VVV_MAKEANDPLAY_SMOKE_BASEDIR=/absolute/private/basedir/ \
 * SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software \
 * ./vvvvvv -assets /verified/makeandplay/data.zip -basedir /absolute/private/basedir/
 * These accelerated host frames are correctness evidence, not Pocket timings.
 */
#ifndef MAKEANDPLAY
#error The Make and Play smoke must compile the MAKEANDPLAY engine edition.
#endif
#ifdef POCKET_BENCH
#error The Make and Play smoke must not use the campaign benchmark.
#endif

#include <SDL3/SDL.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <physfs.h>

#include "../../desktop_version/src/ButtonGlyphs.h"
#include "../../desktop_version/src/CustomLevels.h"
#include "../../desktop_version/src/Entity.h"
#include "../../desktop_version/src/FileSystemUtils.h"
#include "../../desktop_version/src/Game.h"
#include "../../desktop_version/src/Graphics.h"
#include "../../desktop_version/src/KeyPoll.h"
#include "../../desktop_version/src/Localization.h"
#include "../../desktop_version/src/Map.h"
#include "../../desktop_version/src/Screen.h"
#include "../../desktop_version/src/ScreenSettings.h"

static const char* const public_levels[] = {
    "levels/333333_easy.vvvvvv", "levels/4kvvvv.vvvvvv",
    "levels/a_new_dimension.vvvvvv", "levels/linewrap.vvvvvv",
    "levels/pyramid.vvvvvv", "levels/quantumtunnel12.vvvvvv",
    "levels/roadtrip.vvvvvv", "levels/seasons.vvvvvv",
    "levels/soulsearching.vvvvvv", "levels/the_dual_challenge.vvvvvv",
    "levels/towerofpower.vvvvvv", "levels/variationventure.vvvvvv",
    "levels/varietyshow.vvvvvv", "levels/vertexvortex.vvvvvv",
    "levels/vertiginousviridian.vvvvvv", "levels/victuals.vvvvvv",
    "levels/vvvvvvgoldenspiral.vvvvvv"
};

enum SmokeStage {
    BOOT, TITLE, MAIN_MENU, PLAYER_MENU, CHECK_DISABLED_EDITOR,
    LEVEL_LIST, PLAYING, PAUSE_MENU, QUIT_PROMPT, RETURN_TO_LEVELS
};
static SmokeStage stage = BOOT;
static int renders, stage_frames, selected_level, active_frames;
static int controller_selections, played_levels;
static int loading_frames;
static size_t loading_found;
static bool saw_language, saw_title, saw_menu, captured_levels, checked_editor, saved_progress;
static bool saw_controller_motion;
static int last_x, last_y, last_rx, last_ry;
static int initial_clock;
static SDL_GamepadButton held_button = SDL_GAMEPAD_BUTTON_INVALID;
static SDL_GamepadButton tap_button = SDL_GAMEPAD_BUTTON_INVALID;
static int tap_frames, settle_frames;
static bool save_capture(SDL_Surface* surface, const char* path);

static void require(bool condition, const char* message)
{
    if (condition) return;
    fprintf(stderr, "Make and Play smoke failed: %s (frame=%d stage=%d state=%d menu=%d option=%d page=%d fade=%d)\n",
        message, renders, (int) stage, (int) game.gamestate,
        (int) game.currentmenuname, game.currentmenuoption, game.menupage,
        (int) graphics.fademode);
    abort();
}

static void button_event(SDL_GamepadButton button, bool down)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
    event.gbutton.button = (Uint8) button;
    event.gbutton.down = down;
    require(SDL_PushEvent(&event), "enqueue real controller event");
}

static void hold(SDL_GamepadButton button)
{
    if (button == held_button) return;
    if (held_button != SDL_GAMEPAD_BUTTON_INVALID) button_event(held_button, false);
    held_button = button;
    if (held_button != SDL_GAMEPAD_BUTTON_INVALID) button_event(held_button, true);
}

static void tap(SDL_GamepadButton button)
{
    require(tap_button == SDL_GAMEPAD_BUTTON_INVALID && settle_frames == 0,
        "controller taps must not overlap");
    tap_button = button;
    tap_frames = 2;
    button_event(button, true);
}

static bool wait_for_release(void)
{
    if (tap_button != SDL_GAMEPAD_BUTTON_INVALID)
    {
        if (--tap_frames == 0)
        {
            button_event(tap_button, false);
            tap_button = SDL_GAMEPAD_BUTTON_INVALID;
            settle_frames = 3;
        }
        return true;
    }
    if (settle_frames > 0)
    {
        --settle_frames;
        return true;
    }
    return false;
}

static void next_stage(SmokeStage next)
{
    stage = next;
    stage_frames = 0;
}

static void capture(SDL_Renderer* renderer, const char* name)
{
    SDL_Surface* surface = SDL_RenderReadPixels(renderer, NULL);
    require(surface != NULL && surface->w > 0 && surface->h > 0, "read rendered engine frame");
    require(save_capture(surface, name), "save engine BMP capture");
    SDL_DestroySurface(surface);
}

static void check_isolation(void)
{
    const char* expected = getenv("VVV_MAKEANDPLAY_SMOKE_BASEDIR");
    require(expected != NULL && expected[0] == '/' && strlen(expected) > 1,
        "set VVV_MAKEANDPLAY_SMOKE_BASEDIR to the same private absolute -basedir");
    const std::string base = std::string(expected) + (expected[strlen(expected) - 1] == '/' ? "" : "/");
    require(base == PHYSFS_getWriteDir(), "engine write directory must be the explicit private namespace");
    require(base + "saves/" == FILESYSTEM_getUserSaveDirectory(), "private engine save directory");
    require(base + "levels/" == FILESYSTEM_getUserLevelDirectory(), "private engine level directory");
}

static bool option_is(size_t index, const char* text)
{
    return index < game.menuoptions.size()
        && SDL_strcmp(game.menuoptions[index].text, loc::gettext(text)) == 0;
}

static void check_inventory(void)
{
    require(cl.ListOfMetaData.size() == SDL_arraysize(public_levels), "exactly 17 public bundled levels");
    bool found[SDL_arraysize(public_levels)] = {false};
    for (size_t i = 0; i < cl.ListOfMetaData.size(); ++i)
    {
        const LevelMetaData& metadata = cl.ListOfMetaData[i];
        require(!metadata.title.empty() && !metadata.creator.empty(), "engine parsed bundled level metadata");
        bool matched = false;
        for (size_t j = 0; j < SDL_arraysize(public_levels); ++j)
        {
            if (metadata.filename == public_levels[j])
            {
                require(!found[j], "no duplicate public level");
                matched = found[j] = true;
                break;
            }
        }
        require(matched, "no unexpected or original campaign level in the public inventory");
    }
    require(!FILESYSTEM_levelDirHasError(), "bundled level discovery has no filesystem error");
}

static void check_save_reload(void)
{
    check_isolation();
    const std::string filename = cl.ListOfMetaData[game.playcustomlevel].filename;
    const int x = game.savex, y = game.savey, rx = game.saverx, ry = game.savery;
    const int gravity = game.savegc, deaths = game.deathcounts;
    require(game.customsavequick(filename), "save real custom-level progress");
    const std::string path = "saves/" + filename.substr(7) + ".vvv";
    require(FILESYSTEM_isFile(path.c_str()), "custom-level save exists in private write directory");
    game.savex = game.savey = game.saverx = game.savery = -12345;
    game.savegc = game.deathcounts = -12345;
    game.customloadquick(filename);
    require(game.savex == x && game.savey == y && game.saverx == rx && game.savery == ry
        && game.savegc == gravity && game.deathcounts == deaths,
        "real loader restores saved checkpoint and progress after in-memory values are destroyed");

    const bool previous_flashing = game.noflashingmode;
    game.noflashingmode = !previous_flashing;
    require(game.savesettings(), "save engine settings");
    game.noflashingmode = previous_flashing;
    ScreenSettings settings;
    SDL_zero(settings);
    ScreenSettings_default(&settings);
    game.loadsettings(&settings);
    require(game.noflashingmode != previous_flashing, "engine reload restores the persisted setting");
    game.noflashingmode = previous_flashing;
    require(game.savesettings(), "restore ordinary setting after roundtrip");
    game.updatecustomlevelstats(filename.substr(7), 1);
    game.customlevelstats.clear();
    game.loadcustomlevelstats();
    require(game.customlevelstats.count(filename.substr(7)) != 0
        && game.customlevelstats[filename.substr(7)] == 1, "level progress stats persist and reload");
    saved_progress = true;
}

static void check_live_player(void)
{
    require(game.gamestate == GAMEMODE && map.custommode && map.custommodeforreal,
        "actual player-level gameplay, not campaign or editor playtesting");
    require(cl.mapwidth > 0 && cl.mapheight > 0, "real loaded level dimensions");
    const int player = obj.getplayer();
    require(player >= 0 && (size_t) player < obj.entities.size(), "live player entity");
    if (active_frames == 0)
    {
        last_x = obj.entities[player].xp;
        last_y = obj.entities[player].yp;
        last_rx = game.roomx;
        last_ry = game.roomy;
        initial_clock = game.frames + 30 * (game.seconds + 60 * (game.minutes + 60 * game.hours));
    }
    else if (active_frames > 10 && held_button != SDL_GAMEPAD_BUTTON_INVALID && game.hascontrol)
    {
        saw_controller_motion |= obj.entities[player].xp != last_x
            || obj.entities[player].yp != last_y || game.roomx != last_rx || game.roomy != last_ry;
    }
}

static void drive(SDL_Renderer* renderer)
{
    if (stage == LEVEL_LIST && game.currentmenuname == Menu::playerworlds)
    {
        const size_t found = cl.ListOfMetaData.size();
        if (loading_frames == 0)
        {
            require(found == 0, "loading feedback is presented before metadata reads");
            capture(renderer, "makeandplay-loading-start.bmp");
        }
        require(found >= loading_found && found <= SDL_arraysize(public_levels),
            "loading feedback follows real metadata progress");
        if (found == 8) capture(renderer, "makeandplay-loading-progress.bmp");
        loading_found = found;
        ++loading_frames;
        return; /* Intermediate loading presents must not drive controller input. */
    }
    ++renders;
    ++stage_frames;
    require(renders < 20000 && stage_frames < 1500, "bounded real-engine state transition");
    require(game.gamestate != EDITORMODE, "Pocket editor must remain unavailable");
    if (wait_for_release()) return;
    switch (stage)
    {
    case BOOT:
        if (game.gamestate != TITLEMODE || graphics.fademode != FADE_NONE) return;
        check_isolation();
        if (game.currentmenuname == Menu::language)
        {
            require(loc::pre_title_lang_menu, "initial language selection belongs to startup");
            saw_language = true;
            int english = -1;
            for (size_t i = 0; i < loc::languagelist.size(); ++i)
                if (loc::languagelist[i].code == "en") english = (int) i;
            require(english >= 0, "English appears in the real language selector");
            const int column = ((int) game.menuoptions.size() + 1) / 2;
            if ((english < column) != (game.currentmenuoption < column))
                tap(SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
            else if (game.currentmenuoption != english)
                tap(SDL_GAMEPAD_BUTTON_DPAD_DOWN);
            else
                tap(SDL_GAMEPAD_BUTTON_SOUTH);
            return;
        }
        require(loc::lang == "en", "real language selection uses English");
        next_stage(TITLE);
        break;
    case TITLE:
        require(game.currentmenuname == Menu::mainmenu && !game.menustart, "initial title prompt");
        if (graphics.fademode != FADE_NONE || stage_frames < 10) return;
        capture(renderer, "makeandplay-title.bmp");
        saw_title = true;
        tap(SDL_GAMEPAD_BUTTON_SOUTH);
        next_stage(MAIN_MENU);
        break;
    case MAIN_MENU:
        require(game.currentmenuname == Menu::mainmenu && game.menustart, "controller advances title into menu");
        require(option_is(0, "levels") && option_is(1, "options"), "M&P has no campaign play entry");
        for (size_t i = 0; i < game.menuoptions.size(); ++i)
            require(!option_is(i, "play"), "original campaign cannot be selected");
        capture(renderer, "makeandplay-menu.bmp");
        saw_menu = true;
        tap(SDL_GAMEPAD_BUTTON_SOUTH);
        next_stage(PLAYER_MENU);
        break;
    case PLAYER_MENU:
        require(game.currentmenuname == Menu::playerworlds, "controller opens player worlds");
        require(game.editor_disabled && !BUTTONGLYPHS_keyboard_is_available(), "Pocket has no keyboard editor");
        require(option_is(0, "play a level") && option_is(1, "level editor")
            && !game.menuoptions[1].active && option_is(2, "return") && game.menuoptions.size() == 3,
            "disabled editor and absent desktop folder actions");
        tap(SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        next_stage(CHECK_DISABLED_EDITOR);
        break;
    case CHECK_DISABLED_EDITOR:
        require(game.currentmenuname == Menu::playerworlds, "editor action cannot leave player menu");
        if (!checked_editor)
        {
            require(game.currentmenuoption == 1, "controller highlights disabled editor");
            checked_editor = true;
            tap(SDL_GAMEPAD_BUTTON_SOUTH);
        }
        else if (game.currentmenuoption != 0)
            tap(SDL_GAMEPAD_BUTTON_DPAD_UP);
        else
        {
            tap(SDL_GAMEPAD_BUTTON_SOUTH);
            next_stage(LEVEL_LIST);
        }
        break;
    case LEVEL_LIST:
    {
        require(game.currentmenuname == Menu::levellist, "controller opens real bundled level list");
        require(loading_frames > 1 && loading_found == SDL_arraysize(public_levels),
            "visible loading feedback precedes the level list and reaches the real count");
        check_inventory();
        if (graphics.fademode != FADE_NONE) return;
        if (!captured_levels)
        {
            capture(renderer, "makeandplay-levels.bmp");
            captured_levels = true;
        }
        const int target_page = selected_level / 8;
        const int target_option = game.levelpage == target_page
            ? selected_level % 8 : (int) game.menuoptions.size() - 3;
        if (game.currentmenuoption != target_option)
            tap(SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        else if (game.levelpage != target_page)
            tap(SDL_GAMEPAD_BUTTON_SOUTH);
        else
        {
            tap(SDL_GAMEPAD_BUTTON_SOUTH);
            ++controller_selections;
            active_frames = 0;
            next_stage(PLAYING);
        }
        break;
    }
    case PLAYING:
        if (game.gamestate == TITLEMODE && game.currentmenuname == Menu::quickloadlevel)
        {
            /* A prior isolated run or checkpoint created by this test is valid. */
            tap(SDL_GAMEPAD_BUTTON_SOUTH);
            return;
        }
        if (game.gamestate != GAMEMODE || graphics.fademode != FADE_NONE) return;
        require(game.playcustomlevel == selected_level, "controller launches the selected public level");
        require(game.customlevelfilename == cl.ListOfMetaData[selected_level].filename,
            "engine gameplay identity matches selected metadata");
        check_live_player();
        if (active_frames == 0)
        {
            printf("Make and Play controller launch %d/17: %s (%s)\n", selected_level + 1,
                game.customlevelfilename.c_str(), game.customleveltitle.c_str());
            fflush(stdout);
        }
        if (active_frames < 25) hold(SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
        else if (active_frames < 50) hold(SDL_GAMEPAD_BUTTON_DPAD_LEFT);
        else hold(SDL_GAMEPAD_BUTTON_INVALID);
        ++active_frames;
        if (active_frames == 60)
        {
            require(game.frames + 30 * (game.seconds + 60 * (game.minutes + 60 * game.hours)) != initial_clock,
                "real gameplay clock advances");
            char name[64];
            SDL_snprintf(name, sizeof(name), "makeandplay-level-%02d.bmp", selected_level + 1);
            capture(renderer, name);
            if (selected_level == 0)
            {
                capture(renderer, "makeandplay-gameplay.bmp");
                check_save_reload();
            }
        }
        if (active_frames >= 90)
        {
            ++played_levels;
            tap(SDL_GAMEPAD_BUTTON_START);
            next_stage(PAUSE_MENU);
        }
        break;
    case PAUSE_MENU:
        if (game.gamestate != MAPMODE || graphics.menuoffset != 0) return;
        if (game.menupage != 32) tap(SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        else
        {
            tap(SDL_GAMEPAD_BUTTON_SOUTH);
            next_stage(QUIT_PROMPT);
        }
        break;
    case QUIT_PROMPT:
        require(game.gamestate == MAPMODE, "real quit confirmation is in the map/pause screen");
        if (game.menupage != 11) tap(SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        else
        {
            tap(SDL_GAMEPAD_BUTTON_SOUTH);
            next_stage(RETURN_TO_LEVELS);
        }
        break;
    case RETURN_TO_LEVELS:
        if (game.gamestate != TITLEMODE || graphics.fademode != FADE_NONE) return;
        require(game.currentmenuname == Menu::levellist, "controller quits back to level list");
        if (++selected_level < (int) SDL_arraysize(public_levels)) next_stage(LEVEL_LIST);
        else
        {
            check_isolation();
            require(saw_title && saw_menu && captured_levels && checked_editor && saved_progress && saw_controller_motion,
                "complete title/menu/editor/save/controller-motion coverage");
            require(controller_selections == 17 && played_levels == 17, "every public level entered actual gameplay");
            printf("Make and Play smoke passed: 17 public levels launched by controller, active gameplay, "
                "campaign absent, editor disabled, private settings/checkpoint/stats reloaded; "
                "initial language selector %s; %d loading feedback frames; %d rendered frames. Host correctness only.\n",
                saw_language ? "exercised" : "already configured", loading_frames, renders);
            fflush(stdout);
            exit(0);
        }
        break;
    }
}

extern "C" Uint64 SDL_GetTicks(void)
{
    static Uint64 ticks;
    return ticks += 17;
}
extern "C" void SDL_Delay(Uint32) {}

#ifdef POCKET_SMOKE_RENDER
extern "C" Uint16* POCKET_FrameBuffer(int* pitch)
{
    static Uint16 pixels[320 * 240];
    *pitch = 320;
    return pixels;
}

extern "C" void POCKET_PresentFrame(void)
{
    drive(gameScreen.m_renderer);
}

/* SDL's BMP writer requires a native surface. Borrow the Pocket surface's
 * RGB565 pixels without transferring ownership of its allocation. */
#undef SDL_CreateSurfaceFrom
#undef SDL_DestroySurface
extern "C" SDL_Surface* SDL_CreateSurfaceFrom(int, int, SDL_PixelFormat, void*, int);
extern "C" void SDL_DestroySurface(SDL_Surface*);

static bool save_capture(SDL_Surface* surface, const char* path)
{
    SDL_Surface* native = SDL_CreateSurfaceFrom(
        surface->w, surface->h, surface->format, surface->pixels, surface->pitch);
    if (native == NULL) return false;
    const bool ok = SDL_SaveBMP(native, path);
    SDL_DestroySurface(native);
    return ok;
}
#else
extern "C" bool SDL_RenderPresent(SDL_Renderer* renderer)
{
    drive(renderer);
    return true;
}

static bool save_capture(SDL_Surface* surface, const char* path)
{
    return SDL_SaveBMP(surface, path);
}
#endif
