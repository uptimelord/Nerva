// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Paul Odantabao II

#include "nerva_bench.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    NervaBenchReport report;
    const char *bench_log = "experiments/v10_benchmark_suite/bench.log";
    const char *trace_log = "experiments/v10_benchmark_suite/trace.log";

    int rc = nerva_bench_run_all(&report, bench_log, trace_log);
    if (rc != 0 && rc != 1) {
        fprintf(stderr, "benchmark runner failed\n");
        return 2;
    }

    for (uint32_t i = 0; i < report.count; ++i) {
        const NervaBenchResult *r = &report.items[i];
        printf("benchmark=%-24s pass=%d ticks=%u peak_queue=%u %s\n", r->name, r->pass, r->ticks,
               r->peak_queue, r->notes);
    }
    printf("summary all_pass=%d peak_queue=%u ticks_per_sec=%.0f est_ram_mb=%.2f fluid_routine_pct=%.1f\n",
           report.all_pass, report.peak_queue, report.ticks_per_sec, report.est_ram_mb,
           report.fluid_routine_pct);

    return report.all_pass ? 0 : 1;
}
