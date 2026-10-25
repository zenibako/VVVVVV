#ifndef POCKET_BENCH_PROFILE_H
#define POCKET_BENCH_PROFILE_H

#ifdef POCKET_BENCH
#include <stdint.h>
#include <stdbool.h>
#include "of_timer.h"

typedef enum PocketBenchPhase
{
    POCKET_GRAV_BG_UPDATE, POCKET_GRAV_BG_DRAW,
    POCKET_GRAV_ENEMY_SPAWN, POCKET_GRAV_ENEMY_UPDATE,
    POCKET_GRAV_ENEMY_COLLIDE, POCKET_GRAV_ENEMY_ANIMATE,
    POCKET_GRAV_ENEMY_DRAW, POCKET_GRAV_DETAIL_PHASES,
    /* Only this contiguous range is added to the draw accounting sum. */
    POCKET_GRAV_DRAW_SETUP = POCKET_GRAV_DETAIL_PHASES,
    POCKET_GRAV_DRAW_BACKGROUND, POCKET_GRAV_DRAW_MAP,
    POCKET_GRAV_DRAW_ENTITIES, POCKET_GRAV_DRAW_HUD,
    POCKET_GRAV_DRAW_COPY, POCKET_GRAV_DRAW_EFFECTS,
    POCKET_GRAV_DRAW_PRESENT, POCKET_GRAV_DRAW_POST_CLEAR,
    POCKET_GRAV_DRAW_END,
    /* Nested inside DRAW_PRESENT; do not add these to the flat sum. */
    POCKET_GRAV_VIDEO_FLIP = POCKET_GRAV_DRAW_END,
    POCKET_GRAV_AUDIO, POCKET_GRAV_PHASES
} PocketBenchPhase;
typedef struct PocketBenchProfile
{
    uint32_t us[POCKET_GRAV_PHASES], calls[POCKET_GRAV_PHASES];
} PocketBenchProfile;
#ifdef __cplusplus
extern "C" {
#endif
extern PocketBenchProfile POCKET_grav_profile;
extern bool POCKET_grav_draw_active;

static inline uint32_t POCKET_BenchProfileLap(PocketBenchPhase phase, uint32_t start)
{
    const uint32_t end = of_time_us();
    POCKET_grav_profile.us[phase] += end - start;
    POCKET_grav_profile.calls[phase]++;
    return end;
}

static inline uint32_t POCKET_GravDrawTime(const PocketBenchProfile* profile)
{
    uint32_t total = 0;
    for (int i = POCKET_GRAV_DRAW_SETUP; i < POCKET_GRAV_DRAW_END; i++)
        total += profile->us[i];
    return total;
}

/* C entry points use the same clock and recording code as the RAII timer. */
#define POCKET_GRAV_TIMER_START(name) \
    const bool name##_active = POCKET_grav_draw_active; \
    const uint32_t name = name##_active ? of_time_us() : 0
#define POCKET_GRAV_TIMER_END(name, phase) \
    do { if (name##_active) POCKET_BenchProfileLap(phase, name); } while (0)

#ifdef __cplusplus
}

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
        finish();
    }
    void switch_to(PocketBenchPhase next)
    {
        if (!enabled) return;
        start = POCKET_BenchProfileLap(phase, start);
        phase = next;
    }
    void finish()
    {
        if (!enabled) return;
        POCKET_BenchProfileLap(phase, start);
        enabled = false;
    }
};
#define POCKET_GRAV_SCOPE(phase, condition) \
    PocketBenchScope pocket_grav_scope(phase, game.swnmode && game.swngame == SWN_GRAVITRON && (condition))
#define POCKET_GRAV_DRAW_SCOPE(phase) \
    PocketBenchScope pocket_grav_draw_scope(phase, POCKET_grav_draw_active)
#define POCKET_GRAV_DRAW_NEXT(phase) pocket_grav_draw_scope.switch_to(phase)
#define POCKET_GRAV_DRAW_FINISH() pocket_grav_draw_scope.finish()
#endif
#else
#define POCKET_GRAV_SCOPE(phase, condition) ((void) 0)
#define POCKET_GRAV_DRAW_SCOPE(phase) ((void) 0)
#define POCKET_GRAV_DRAW_NEXT(phase) ((void) 0)
#define POCKET_GRAV_DRAW_FINISH() ((void) 0)
#define POCKET_GRAV_TIMER_START(name) ((void) 0)
#define POCKET_GRAV_TIMER_END(name, phase) ((void) 0)
#endif
#endif
