/* The flip/audio adapter is C, and shares recording with the C++ timers. */
#include "../src/pocket_bench_profile.h"
#include <assert.h>
#include <stdio.h>

PocketBenchProfile POCKET_grav_profile;
bool POCKET_grav_draw_active;
static uint32_t now;
unsigned int of_time_us(void) { return now; }

int main(void)
{
    POCKET_grav_draw_active = true;
    POCKET_GRAV_TIMER_START(flip);
    now = 19;
    POCKET_grav_draw_active = false;
    POCKET_GRAV_TIMER_END(flip, POCKET_GRAV_VIDEO_FLIP);
    assert(POCKET_grav_profile.us[POCKET_GRAV_VIDEO_FLIP] == 19);
    assert(POCKET_grav_profile.calls[POCKET_GRAV_VIDEO_FLIP] == 1);
    POCKET_GRAV_TIMER_START(audio);
    now += 7;
    POCKET_grav_draw_active = true;
    POCKET_GRAV_TIMER_END(audio, POCKET_GRAV_AUDIO);
    assert(POCKET_grav_profile.calls[POCKET_GRAV_AUDIO] == 0);
    assert(POCKET_GravDrawTime(&POCKET_grav_profile) == 0);
    puts("Benchmark C timers: captured enable flags and nested accounting pass");
    return 0;
}
