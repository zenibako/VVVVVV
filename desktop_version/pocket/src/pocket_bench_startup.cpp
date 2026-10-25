#include "pocket_bench.h"

#ifdef POCKET_BENCH
#include "../../src/Localization.h"
#include "../../src/Game.h"

void POCKET_BenchPrepare(void)
{
    /* The benchmark has an independent save slot, which may be empty or
     * unreadable. Never wait for first-run input, and keep the same text
     * and fonts in every run even if settings were saved previously. */
    loc::lang = "en";
    loc::lang_set = loc::lang_set_current;
    /* Measure one presentation per logic timestep. Render-only loops would
     * otherwise be omitted from the benchmark's per-frame accounting. */
    game.over30mode = false;
}
#endif
