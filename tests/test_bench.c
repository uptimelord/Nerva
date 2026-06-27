// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Paul Odantabao II

#include "nerva_bench.h"

#include <stdio.h>

static int g_failures = 0;

static void expect_true(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        g_failures++;
    }
}

static void test_bench_all_pass(void) {
    NervaBenchReport report;
    const char *bench_log = "experiments/v10_benchmark_suite/bench.log";
    const char *trace_log = "experiments/v10_benchmark_suite/trace.log";

    int rc = nerva_bench_run_all(&report, bench_log, trace_log);
    expect_true(rc == 0, "all benchmarks pass");
    expect_true(report.all_pass, "report all_pass flag");
    expect_true(report.count == NERVA_BENCH_COUNT, "benchmark count");
    expect_true(report.ticks_per_sec >= 1000.0, "ticks per second threshold");
    expect_true(report.est_ram_mb < 7168.0, "estimated RAM under cap");
    expect_true(report.fluid_routine_pct < 5.0, "routine fluid rate under 5%");

    for (uint32_t i = 0; i < report.count; ++i) {
        if (!report.items[i].pass) {
            fprintf(stderr, "FAIL: benchmark %s\n", report.items[i].name);
            g_failures++;
        }
    }
}

int test_bench_run(void) {
    g_failures = 0;
    test_bench_all_pass();
    return g_failures;
}
