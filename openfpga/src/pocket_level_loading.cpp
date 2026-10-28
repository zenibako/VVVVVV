#include "pocket_level_loading.h"

#include "../../desktop_version/src/Constants.h"
#include "../../desktop_version/src/Font.h"
#include "../../desktop_version/src/Graphics.h"
#include "../../desktop_version/src/Localization.h"
#include "../../desktop_version/src/Screen.h"
#include "../../desktop_version/src/VFormat.h"

void POCKET_ShowLevelLoading(size_t found)
{
    SDL_Texture* previous_target = SDL_GetRenderTarget(gameScreen.m_renderer);
    graphics.set_render_target(graphics.gameTexture);
    graphics.set_blendmode(SDL_BLENDMODE_NONE);
    graphics.fill_rect(0, 0, 0);

    font::print(PR_CEN | PR_CJK_HIGH, -1, 102,
        loc::gettext("Loading levels..."), 128, 255, 255);
    char progress[SCREEN_WIDTH_CHARS + 1];
    vformat_buf(progress, sizeof(progress), loc::gettext("Levels found: {count}"),
        "count:int", (int) found);
    font::print(PR_CEN | PR_CJK_LOW, -1, 122, progress, 255, 255, 255);

    /* Input is still scanning synchronously, so the normal frame loop cannot
     * present this screen for us. Do it before the next file is read. */
    graphics.render();
    gameScreen.RenderPresent();
    graphics.set_render_target(previous_target);
}
