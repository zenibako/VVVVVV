#include "pocket_bench.h"

#ifdef POCKET_BENCH
#include "../../src/Localization.h"

void POCKET_BenchPrepare(void)
{
    /* The benchmark has an independent save slot, which may be empty or
     * unreadable. Never wait for first-run input, and keep the same text
     * and fonts in every run even if settings were saved previously. */
    loc::lang = "en";
    loc::lang_set = loc::lang_set_current;
}
#endif
