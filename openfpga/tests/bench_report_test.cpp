#include "../src/pocket_bench_report.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    PocketBenchReport report;
    for (int i = 0; i < 31; i++) report.add("row %d", i);
    assert(report.size() == 31);
    assert(report.pages() == 2);
    assert(report.first() == 0 && report.end() == 27);
    assert(strcmp(report.line(26), "row 26") == 0);
    report.next(true);
    assert(report.page() == 1 && report.first() == 27 && report.end() == 31);
    assert(strcmp(report.line(30), "row 30") == 0);
    report.next(true); /* Holding A must not repeatedly change page. */
    assert(report.page() == 1);
    report.next(false);
    report.next(true);
    assert(report.page() == 0);
    for (int i = 0; i < 100; i++) report.add("extra %d", i);
    assert(report.size() == 80);
    assert(report.pages() == 3);
    report.next(false);
    report.next(true);
    report.next(false);
    report.next(true);
    assert(report.page() == 2 && report.first() == 54 && report.end() == 80);
    assert(strcmp(report.line(79), "extra 48") == 0);
    report.next(false);
    report.next(true);
    assert(report.page() == 0);
    puts("Benchmark report: page boundaries, held input and capacity pass");
    return 0;
}
