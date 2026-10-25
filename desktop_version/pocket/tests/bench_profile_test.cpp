#include "../src/pocket_bench_profile.h"
#include <assert.h>
#include <stdio.h>

PocketBenchProfile POCKET_grav_profile;
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
    puts("Benchmark profiling: phase isolation, disabled scopes and clock wrap pass");
}
