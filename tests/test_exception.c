// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Paul Odantabao II

#include "nerva_config.h"
#include "nerva_debug.h"
#include "nerva_engine.h"
#include "nerva_event.h"
#include "nerva_exception.h"
#include "nerva_graph.h"
#include "nerva_math.h"
#include "nerva_mutation.h"
#include "nerva_trace.h"

#include <stdio.h>
#include <stdlib.h>

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

static void expect_eq_u64(uint64_t actual, uint64_t expected, const char *message) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (expected %llu, got %llu)\n", message,
                (unsigned long long)expected, (unsigned long long)actual);
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

static int nerva_test_find_blocker_edge(const NervaEngine *e, uint32_t source, uint32_t target) {
    for (uint32_t i = 0; i < e->edge_count; ++i) {
        const NervaEdge *ed = &e->edges[i];
        if (ed->flags & NERVA_EDGE_DELETED) {
            continue;
        }
        if (!(ed->flags & NERVA_EDGE_BLOCKER)) {
            continue;
        }
        if (ed->source == source && ed->target == target) {
            return 1;
        }
    }
    return 0;
}

static void nerva_test_setup_penguin_graph(NervaEngine *e, uint32_t *bird, uint32_t *penguin,
                                         uint32_t *fly) {
    *bird = nerva_get_or_create_node(e, "bird");
    *penguin = nerva_get_or_create_node(e, "penguin");
    *fly = nerva_get_or_create_node(e, "fly");

    nerva_graph_create_edge(e, *bird, *fly, NERVA_REL_USUALLY_HAS_PROPERTY);
    nerva_graph_create_edge(e, *penguin, *bird, NERVA_REL_KIND_OF);
    nerva_graph_rebuild_adjacency(e);
}

static void nerva_test_apply_penguin_blocker(NervaEngine *e, uint32_t penguin, uint32_t fly) {
    expect_true(nerva_queue_blocker_edge(e, penguin, fly, NERVA_REL_BLOCKS),
                "queue penguin->fly blocker");
    nerva_apply_mutations(e);
    expect_true(nerva_test_find_blocker_edge(e, penguin, fly), "blocker edge exists after apply");
    expect_eq_u64(e->debug.blockers_applied, 1u, "blocker applied via mutation");
    expect_eq_u32(e->mutation_log_count, 1u, "blocker mutation logged");
    expect_eq_u32(e->mutation_log[0].type, (uint32_t)NERVA_MUT_ADD_BLOCKER_EDGE,
                  "blocker mutation type");
    expect_eq_u32(e->mutation_log[0].debug_reason, (uint32_t)NERVA_REASON_EXCEPTION_BLOCKER,
                  "blocker mutation reason");
}

static void test_blocker_created_via_mutation_queue(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "blocker init");

    uint32_t bird, penguin, fly;
    nerva_test_setup_penguin_graph(&e, &bird, &penguin, &fly);
    (void)bird;

    nerva_test_apply_penguin_blocker(&e, penguin, fly);
    expect_eq_u32(e.mutation_count, 0u, "mutation queue drained");

    nerva_engine_free(&e);
}

static void test_bird_query_reaches_fly(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "bird query init");

    uint32_t bird, penguin, fly;
    nerva_test_setup_penguin_graph(&e, &bird, &penguin, &fly);
    nerva_test_apply_penguin_blocker(&e, penguin, fly);

    nerva_trace_clear(&e);
    nerva_debug_clear_fire_log(&e);
    expect_true(nerva_activate_node(&e, bird, NERVA_Q8_8_ONE), "activate bird");
    nerva_tick_n(&e, 4);

    expect_true(nerva_test_fire_log_contains(&e, bird), "bird fired");
    expect_true(nerva_test_fire_log_contains(&e, fly), "fly fired on bird query");
    expect_true(e.nodes[fly].v >= e.nodes[fly].theta_fire || nerva_test_fire_log_contains(&e, fly),
                "fly at or past threshold");

    nerva_engine_free(&e);
}

static void test_penguin_query_blocks_fly(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "penguin query init");

    uint32_t bird, penguin, fly;
    nerva_test_setup_penguin_graph(&e, &bird, &penguin, &fly);
    nerva_test_apply_penguin_blocker(&e, penguin, fly);
    (void)bird;

    nerva_trace_clear(&e);
    nerva_debug_clear_fire_log(&e);
    expect_true(nerva_activate_node(&e, penguin, NERVA_Q8_8_ONE), "activate penguin");
    nerva_tick_n(&e, 4);

    expect_true(nerva_test_fire_log_contains(&e, penguin), "penguin fired");
    expect_true(!nerva_test_fire_log_contains(&e, fly), "fly did not fire on penguin query");
    expect_true(e.nodes[fly].v < e.nodes[fly].theta_fire, "fly below threshold");
    expect_true(nerva_exception_count_blocker_traces(&e) >= 1u, "blocker trace recorded");
    expect_true(nerva_exception_trace_has_suppressed_path(&e, 0u),
                "bird->fly path tagged with exception suppression");

    nerva_engine_free(&e);
}

static void test_penguin_suppression_is_partial(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "partial suppress init");

    uint32_t bird, penguin, fly;
    nerva_test_setup_penguin_graph(&e, &bird, &penguin, &fly);
    nerva_test_apply_penguin_blocker(&e, penguin, fly);

    nerva_debug_clear_fire_log(&e);
    expect_true(nerva_activate_node(&e, penguin, NERVA_Q8_8_ONE), "activate for partial test");
    nerva_tick_n(&e, 4);

    expect_true(e.nodes[fly].v > e.cfg.v_rest_q8_8 || e.nodes[fly].v < 0,
                "fly activation not hard-zeroed");
    expect_true(e.nodes[fly].v < e.nodes[fly].theta_fire, "fly still below fire threshold");

    nerva_engine_free(&e);
}

static void test_penguin_experiment_artifacts(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "artifact init");

    uint32_t bird, penguin, fly;
    nerva_test_setup_penguin_graph(&e, &bird, &penguin, &fly);
    nerva_test_apply_penguin_blocker(&e, penguin, fly);

    nerva_trace_clear(&e);
    expect_true(nerva_activate_node(&e, penguin, NERVA_Q8_8_ONE), "artifact activate penguin");
    nerva_tick_n(&e, 4);

    expect_eq_u32(e.mutation_log_count, 1u, "artifact mutation log retains blocker apply");
    expect_true(nerva_exception_trace_has_suppressed_path(&e, 0u),
                "artifact trace labels suppressed bird->fly path");

    expect_true(nerva_trace_save_path(&e, "experiments/v04_exception_handling/trace.log", 8) == 0,
                "exception trace.log saved");
    expect_true(nerva_mutation_save_log(&e, "experiments/v04_exception_handling/mutation.log") == 0,
                "exception mutation.log saved");

    nerva_engine_free(&e);
}

int test_exception_run(void) {
    g_failures = 0;
    test_blocker_created_via_mutation_queue();
    test_bird_query_reaches_fly();
    test_penguin_query_blocks_fly();
    test_penguin_suppression_is_partial();
    test_penguin_experiment_artifacts();
    return g_failures;
}
