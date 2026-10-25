/* Profiling tests provide a deterministic clock, without the device SDK. */
#ifndef OF_TIMER_H
#define OF_TIMER_H
#ifdef __cplusplus
extern "C" {
#endif
unsigned int of_time_us(void);
#ifdef __cplusplus
}
#endif
#endif
