/* A scripted run through the scenes that are slowest on the Pocket, timing
 * every frame: built with `make BENCH=1`. It starts from the title screen
 * by itself, drives the game with key presses of its own, and shows its
 * results on the screen when it is done. Saves are kept in memory only, so
 * the card's save is not written. */
#ifndef POCKET_BENCH_H
#define POCKET_BENCH_H

#ifdef POCKET_BENCH

/* After settings are read, before loading fonts and localized resources. */
void POCKET_BenchPrepare(void);

/* main.cpp: once a pass of the main loop, around its work (not the wait
 * for the next frame), and before each step of game logic. */
void POCKET_BenchLoopStart(void);
void POCKET_BenchLoopEnd(void);
void POCKET_BenchStep(void);
void POCKET_BenchStepEnd(void);
/* main.cpp: around drawing a frame and showing it. */
void POCKET_BenchDrawStart(void);
void POCKET_BenchDrawEnd(void);
/* main.cpp: after a frame is drawn, before it is shown; draws the results
 * over it once the run is over. */
void POCKET_BenchDraw(void);

#endif

#endif
