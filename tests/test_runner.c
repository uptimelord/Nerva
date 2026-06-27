// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Paul Odantabao II

#include <stdio.h>

int test_graph_run(void);
int test_event_run(void);
int test_trace_run(void);
int test_learning_run(void);
int test_prediction_run(void);
int test_exception_run(void);
int test_schema_run(void);
int test_memory_run(void);
int test_routing_run(void);
int test_parse_run(void);
int test_persist_run(void);
int test_bench_run(void);
int test_tagworld_run(void);

int main(void) {
    int failures = 0;
    failures += test_graph_run();
    failures += test_event_run();
    failures += test_trace_run();
    failures += test_learning_run();
    failures += test_prediction_run();
    failures += test_exception_run();
    failures += test_schema_run();
    failures += test_memory_run();
    failures += test_routing_run();
    failures += test_parse_run();
    failures += test_persist_run();
    failures += test_bench_run();
    failures += test_tagworld_run();

    if (failures == 0) {
        printf("All tests passed.\n");
        return 0;
    }

    fprintf(stderr, "%d test failure(s).\n", failures);
    return 1;
}
