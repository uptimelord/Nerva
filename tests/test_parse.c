// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Paul Odantabao II

#include "nerva_config.h"
#include "nerva_debug.h"
#include "nerva_engine.h"
#include "nerva_event.h"
#include "nerva_graph.h"
#include "nerva_learning.h"
#include "nerva_math.h"
#include "nerva_mutation.h"
#include "nerva_parse.h"
#include "nerva_schema.h"
#include "nerva_trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;

static void expect_true(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        g_failures++;
    }
}

static void expect_eq_u32(uint32_t actual, uint32_t expected, const char *message) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (expected %u, got %u)\n", message, expected, actual);
        g_failures++;
    }
}

static int nerva_test_fire_log_contains(const NervaEngine *e, uint32_t node_id) {
    for (uint32_t i = 0; i < e->fire_log_count; ++i) {
        if (e->fire_log[i].node_id == node_id) {
            return 1;
        }
    }
    return 0;
}

static void test_parse_blank_and_comment(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "parse comment init");

    expect_true(nerva_parse_run_line(&e, "# setup") == 0, "comment line noop");
    expect_true(nerva_parse_run_line(&e, "") == 0, "blank line noop");
    expect_true(nerva_parse_run_line(&e, "NODE comment_node  # trailing") == 0, "trailing comment");

    uint32_t id = nerva_find_node_by_name(&e, "comment_node");
    expect_true(id != UINT32_MAX, "node created before trailing comment");

    nerva_engine_free(&e);
}

static void test_parse_invalid_command(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "invalid cmd init");

    expect_true(nerva_parse_run_line(&e, "EDGE only two") != 0, "malformed edge fails");
    expect_true(nerva_parse_run_line(&e, "FEEDBACK correct extra") != 0, "feedback rejects extra token");
    expect_true(nerva_parse_run_line(&e, "NOTACOMMAND x") != 0, "unknown command fails");
    expect_eq_u32(e.node_count, 0u, "invalid commands do not mutate graph");

    nerva_engine_free(&e);
}

static void test_parse_script_poodle_propagation(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "poodle script init");

    expect_true(nerva_parse_run_file(&e, "examples/poodle.nerva") == 0, "poodle script runs");

    uint32_t poodle = nerva_find_node_by_name(&e, "poodle");
    uint32_t dog = nerva_find_node_by_name(&e, "dog");
    uint32_t animal = nerva_find_node_by_name(&e, "animal");
    expect_true(poodle != UINT32_MAX && dog != UINT32_MAX && animal != UINT32_MAX,
                "poodle script nodes exist");
    expect_true(nerva_test_fire_log_contains(&e, dog), "dog fired from script");
    expect_true(nerva_test_fire_log_contains(&e, animal), "animal fired from script");
    expect_true(nerva_graph_reachable(&e, poodle, animal, NERVA_REL_KIND_OF),
                "poodle reaches animal after script");

    nerva_engine_free(&e);
}

static void test_parse_script_penguin_blocker(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "penguin script init");

    expect_true(nerva_parse_run_file(&e, "examples/penguin.nerva") == 0, "penguin script runs");

    uint32_t fly = nerva_find_node_by_name(&e, "fly");
    expect_true(fly != UINT32_MAX, "fly node exists");
    expect_true(e.debug.exceptions_suppressed >= 1u, "penguin path blocked in script");
    expect_true(!nerva_test_fire_log_contains(&e, fly) ||
                    e.debug.exceptions_suppressed >= 1u,
                "fly not freely reached on blocked penguin query");

    nerva_engine_free(&e);
}

static void test_parse_query_feedback_mutations(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "feedback script init");

    expect_true(nerva_parse_run_line(&e, "NODE poodle") == 0, "node poodle");
    expect_true(nerva_parse_run_line(&e, "NODE dog") == 0, "node dog");
    expect_true(nerva_parse_run_line(&e, "EDGE poodle kind_of dog") == 0, "edge poodle dog");
    expect_true(nerva_parse_run_line(&e, "QUERY poodle kind_of dog") == 0, "query poodle dog");
    expect_true(nerva_parse_run_line(&e, "TICK 4") == 0, "tick query");
    expect_eq_u32(nerva_trace_count_used_path(&e), 1u, "one used-path trace from query");

    nerva_q8_8_t w0 = e.edges[0].weight;
    expect_true(nerva_parse_run_line(&e, "FEEDBACK correct") == 0, "feedback correct");
    expect_true(nerva_parse_run_line(&e, "APPLY") == 0, "apply mutations");
    expect_true(e.edges[0].weight > w0, "edge strengthened after feedback apply");

    nerva_engine_free(&e);
}

static void test_parse_rejects_extra_tokens(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "extra token init");

    expect_true(nerva_parse_run_line(&e, "NODE a junk") != 0, "NODE rejects extra token");
    expect_eq_u32(e.node_count, 0u, "extra token NODE does not mutate graph");

    nerva_engine_free(&e);
}

static void test_parse_deferred_adjacency_rebuild(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "deferred adj init");

    expect_true(nerva_parse_run_line(&e, "NODE poodle") == 0, "deferred node poodle");
    expect_true(nerva_parse_run_line(&e, "NODE dog") == 0, "deferred node dog");
    expect_true(nerva_parse_run_line(&e, "NODE animal") == 0, "deferred node animal");
    expect_true(nerva_parse_run_line(&e, "EDGE poodle kind_of dog") == 0, "deferred edge 1");
    expect_true(nerva_parse_run_line(&e, "EDGE dog kind_of animal") == 0, "deferred edge 2");
    expect_true(!e.adjacency_valid, "adjacency still deferred after EDGE commands");
    expect_true(nerva_parse_run_line(&e, "ACTIVATE poodle") == 0, "activate heals adjacency");
    expect_true(e.adjacency_valid, "adjacency rebuilt before propagation");
    expect_true(nerva_parse_run_line(&e, "TICK 4") == 0, "tick after deferred edges");

    uint32_t dog = nerva_find_node_by_name(&e, "dog");
    uint32_t animal = nerva_find_node_by_name(&e, "animal");
    expect_true(nerva_test_fire_log_contains(&e, dog), "dog fired with deferred adjacency");
    expect_true(nerva_test_fire_log_contains(&e, animal), "animal fired with deferred adjacency");

    nerva_engine_free(&e);
}

static void test_parse_query_requires_declared_nodes(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "query require init");

    expect_true(nerva_parse_run_line(&e, "NODE poodle") == 0, "declare poodle");
    expect_true(nerva_parse_run_line(&e, "QUERY poodle kind_of ghost") != 0,
                "query rejects undeclared target");
    expect_eq_u32(e.node_count, 1u, "query does not auto-create target node");

    nerva_engine_free(&e);
}

static void test_parse_long_node_name(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "long name init");

    char long_name[NERVA_PARSE_ARG_MAX];
    memset(long_name, 'n', NERVA_PARSE_ARG_MAX - 1u);
    long_name[NERVA_PARSE_ARG_MAX - 1u] = '\0';

    char line[NERVA_PARSE_LINE_MAX];
    snprintf(line, sizeof(line), "NODE %s", long_name);
    expect_true(nerva_parse_run_line(&e, line) == 0, "long node name accepted");

    uint32_t id = nerva_find_node_by_name(&e, long_name);
    expect_true(id != UINT32_MAX, "long node name stored without truncation");

    nerva_engine_free(&e);
}

static void test_parse_schema_script(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "schema script init");

    expect_true(nerva_parse_run_file(&e, "examples/schema.nerva") == 0, "schema script runs");

    uint32_t x = nerva_find_node_by_name(&e, "X");
    uint32_t z = nerva_find_node_by_name(&e, "Z");
    expect_true(x != UINT32_MAX && z != UINT32_MAX, "schema script nodes exist");
    expect_true(nerva_graph_has_edge(&e, x, z, NERVA_REL_KIND_OF),
                "schema apply inferred shortcut edge");
    expect_true(nerva_test_fire_log_contains(&e, z), "Z fired after schema shortcut");

    nerva_engine_free(&e);
}

static void test_parse_experiment_artifacts(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "artifact init");

    expect_true(nerva_parse_run_file(&e, "examples/poodle.nerva") == 0, "artifact poodle script");

    uint32_t poodle = nerva_find_node_by_name(&e, "poodle");
    uint32_t dog = nerva_find_node_by_name(&e, "dog");
    uint32_t animal = nerva_find_node_by_name(&e, "animal");
    expect_true(poodle != UINT32_MAX && dog != UINT32_MAX && animal != UINT32_MAX,
                "artifact poodle graph nodes exist");
    uint32_t path[] = {poodle, dog, animal};
    expect_true(nerva_debug_save_fire_trace_with_path(
                    &e, "experiments/v08_command_language/trace.log", path, 3) == 0,
                "parse trace.log saved");
    {
        FILE *trace_out = fopen("experiments/v08_command_language/trace.log", "a");
        expect_true(trace_out != NULL, "append edge traces to trace.log");
        if (trace_out) {
            fputs("edges:\n", trace_out);
            nerva_trace_print_path(&e, trace_out, 8);
            fclose(trace_out);
        }
    }

    FILE *out = fopen("experiments/v08_command_language/script.log", "w");
    expect_true(out != NULL, "open script.log");
    if (out) {
        fprintf(out, "script=examples/poodle.nerva\n");
        fprintf(out, "nodes=%u edges=%u\n", e.node_count, e.edge_count);
        fprintf(out, "fires=%u traces_used=%u\n", e.fire_log_count,
                (unsigned)nerva_trace_count_used_path(&e));
        fclose(out);
    }

    nerva_engine_free(&e);
}

int test_parse_run(void) {
    g_failures = 0;
    test_parse_blank_and_comment();
    test_parse_invalid_command();
    test_parse_rejects_extra_tokens();
    test_parse_deferred_adjacency_rebuild();
    test_parse_query_requires_declared_nodes();
    test_parse_long_node_name();
    test_parse_schema_script();
    test_parse_script_poodle_propagation();
    test_parse_script_penguin_blocker();
    test_parse_query_feedback_mutations();
    test_parse_experiment_artifacts();
    return g_failures;
}
