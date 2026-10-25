/* The benchmark must reach the title without a first-run language prompt,
 * including when the save archive is absent or rejected as truncated. */
#include "../src/pocket_bench.h"
#include "src/Localization.h"
#include "src/Game.h"
#include <assert.h>
#include <stdio.h>

namespace loc
{
    std::string lang;
    int lang_set;
}

Game game;

int main(void)
{
    const char* languages[] = {"en", "fr", "ja"};
    const int versions[] = {0, 1, 2};
    for (int i = 0; i < 3; i++)
    {
        loc::lang = languages[i];
        loc::lang_set = versions[i];
        game.over30mode = true;
        POCKET_BenchPrepare();
        assert(!game.over30mode);
        assert(loc::lang == "en");
        assert(loc::lang_set >= loc::lang_set_current);
    }
    puts("Benchmark startup: fresh and existing settings pass");
    return 0;
}
