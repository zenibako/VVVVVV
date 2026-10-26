#ifndef POCKET_BENCH_REPORT_H
#define POCKET_BENCH_REPORT_H

#include <stdarg.h>
#include <stdio.h>

/* Keep results within the 240-pixel display, with room for a page footer. */
class PocketBenchReport
{
    enum { CAPACITY = 64, WIDTH = 48, PAGE_LINES = 27 };
    char rows[CAPACITY][WIDTH];
    int count, current;
    bool held;
public:
    PocketBenchReport() : count(0), current(0), held(false) {}
    void add(const char* format, ...)
    {
        if (count == CAPACITY) return;
        va_list args;
        va_start(args, format);
        vsnprintf(rows[count++], WIDTH, format, args);
        va_end(args);
    }
    int size() const { return count; }
    int pages() const { return count == 0 ? 1 : (count + PAGE_LINES - 1) / PAGE_LINES; }
    int page() const { return current; }
    int first() const { return current * PAGE_LINES; }
    int end() const { return count < first() + PAGE_LINES ? count : first() + PAGE_LINES; }
    const char* line(int i) const { return i >= 0 && i < count ? rows[i] : ""; }
    void next(bool down)
    {
        if (down && !held) current = (current + 1) % pages();
        held = down;
    }
};

#endif
