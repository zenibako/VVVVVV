#ifndef POCKET_BENCH_PROFILE_H
#define POCKET_BENCH_PROFILE_H

#ifdef POCKET_BENCH
#include <stdint.h>
#include "of_timer.h"

enum PocketBenchPhase
{
    POCKET_GRAV_BG_UPDATE, POCKET_GRAV_BG_DRAW,
    POCKET_GRAV_ENEMY_SPAWN, POCKET_GRAV_ENEMY_UPDATE,
    POCKET_GRAV_ENEMY_COLLIDE, POCKET_GRAV_ENEMY_ANIMATE,
    POCKET_GRAV_ENEMY_DRAW, POCKET_GRAV_PHASES
};
struct PocketBenchProfile
{
    uint32_t us[POCKET_GRAV_PHASES], calls[POCKET_GRAV_PHASES];
};
extern PocketBenchProfile POCKET_grav_profile;

class PocketBenchScope
{
    PocketBenchPhase phase;
    bool enabled;
    uint32_t start;
public:
    PocketBenchScope(PocketBenchPhase p, bool active)
        : phase(p), enabled(active), start(active ? of_time_us() : 0) {}
    ~PocketBenchScope()
    {
        if (!enabled) return;
        POCKET_grav_profile.us[phase] += of_time_us() - start;
        POCKET_grav_profile.calls[phase]++;
    }
};
#define POCKET_GRAV_SCOPE(phase, condition) \
    PocketBenchScope pocket_grav_scope(phase, game.swnmode && game.swngame == SWN_GRAVITRON && (condition))
#else
#define POCKET_GRAV_SCOPE(phase, condition) ((void) 0)
#endif
#endif
