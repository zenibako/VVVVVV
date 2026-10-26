#include "../src/pocket_bench_profile.h"
#include <assert.h>
#include <stdio.h>

PocketBenchProfile POCKET_grav_profile;
bool POCKET_grav_draw_active;
static uint32_t now;
extern "C" unsigned int of_time_us(void) { return now; }

int main(void)
{
    { PocketBenchScope scope(POCKET_GRAV_BG_DRAW, true); now += 17; }
    assert(POCKET_grav_profile.us[POCKET_GRAV_BG_DRAW] == 17);
    assert(POCKET_grav_profile.calls[POCKET_GRAV_BG_DRAW] == 1);
    { PocketBenchScope scope(POCKET_GRAV_ENEMY_DRAW, false); now += 23; }
    assert(POCKET_grav_profile.us[POCKET_GRAV_ENEMY_DRAW] == 0);
    assert(POCKET_grav_profile.calls[POCKET_GRAV_ENEMY_DRAW] == 0);
    now = 0xfffffff8u;
    { PocketBenchScope scope(POCKET_GRAV_BG_DRAW, true); now = 7; }
    assert(POCKET_grav_profile.us[POCKET_GRAV_BG_DRAW] == 32);
    {
        PocketBenchScope scope(POCKET_GRAV_DRAW_SETUP, true);
        now += 5;
        scope.switch_to(POCKET_GRAV_DRAW_MAP);
        now += 7;
        scope.switch_to(POCKET_GRAV_DRAW_SETUP);
        now += 11;
        scope.finish();
        scope.finish(); /* Explicit finish and destruction must not count twice. */
    }
    assert(POCKET_grav_profile.us[POCKET_GRAV_DRAW_SETUP] == 16);
    assert(POCKET_grav_profile.us[POCKET_GRAV_DRAW_MAP] == 7);
    assert(POCKET_grav_profile.calls[POCKET_GRAV_DRAW_SETUP] == 2);
    POCKET_grav_draw_active = true;
    POCKET_GRAV_TIMER_START(flip);
    now += 3;
    POCKET_grav_draw_active = false; /* Finish uses the sampled enable flag. */
    POCKET_GRAV_TIMER_END(flip, POCKET_GRAV_VIDEO_FLIP);
    assert(POCKET_grav_profile.us[POCKET_GRAV_VIDEO_FLIP] == 3);
    assert(POCKET_GravDrawTime(&POCKET_grav_profile) == 23);
    /* Nested diagnostics and presentation details are excluded from the sum. */
    assert(POCKET_grav_profile.us[POCKET_GRAV_BG_DRAW] == 32);
    puts("Benchmark profiling: phase isolation, disabled scopes and clock wrap pass");
}
