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
#include "nerva_prediction.h"
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

static void expect_eq_i32(int32_t actual, int32_t expected, const char *message) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (expected %d, got %d)\n", message, (int)expected, (int)actual);
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

static int nerva_test_trace_has_flags(const NervaEngine *e, uint16_t flags) {
    uint32_t limit = e->trace_count;
    if (limit > e->cfg.trace_decay_scan_limit) {
        limit = e->cfg.trace_decay_scan_limit;
    }
    for (uint32_t age = 0; age < limit; ++age) {
        NervaTrace *t = nerva_trace_recent((NervaEngine *)e, age);
        if (t && (t->flags & flags) == flags) {
            return 1;
        }
    }
    return 0;
}

static int nerva_test_fire_log_contains(const NervaEngine *e, uint32_t node_id) {
    for (uint32_t i = 0; i < e->fire_log_count; ++i) {
        if (e->fire_log[i].node_id == node_id) {
            return 1;
        }
    }
    return 0;
}

static void nerva_test_train_ab(NervaEngine *e, uint32_t a, uint32_t rounds) {
    nerva_set_prediction_mode(e, 0);
    for (uint32_t i = 0; i < rounds; ++i) {
        nerva_debug_clear_fire_log(e);
        expect_true(nerva_activate_node(e, a, NERVA_Q8_8_ONE), "train activate A");
        nerva_tick_n(e, 4);
    }
}

static void test_prediction_normal_propagation_when_mode_off(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "propagation off init");

    uint32_t a = nerva_get_or_create_node(&e, "A");
    uint32_t b = nerva_get_or_create_node(&e, "B");
    nerva_graph_create_edge(&e, a, b, NERVA_REL_KIND_OF);
    nerva_graph_rebuild_adjacency(&e);

    nerva_set_prediction_mode(&e, 0);
    nerva_debug_clear_fire_log(&e);
    expect_true(nerva_activate_node(&e, a, NERVA_Q8_8_ONE), "activate with prediction off");
    nerva_tick_n(&e, 4);

    expect_true(nerva_test_fire_log_contains(&e, a), "A fired under normal propagation");
    expect_true(nerva_test_fire_log_contains(&e, b), "B fired from propagation when mode off");
    expect_eq_u32(nerva_prediction_count_pending(&e), 0u, "no expectations when mode off");

    nerva_engine_free(&e);
}

static void test_prediction_emits_expected_not_fire(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "prediction init");

    uint32_t a = nerva_get_or_create_node(&e, "A");
    uint32_t b = nerva_get_or_create_node(&e, "B");
    uint32_t edge_ab = nerva_graph_create_edge(&e, a, b, NERVA_REL_KIND_OF);
    nerva_graph_rebuild_adjacency(&e);
    expect_true(edge_ab == 0u, "A->B edge id");

    nerva_test_train_ab(&e, a, 3);

    nerva_set_prediction_mode(&e, 1);
    nerva_trace_clear(&e);
    nerva_debug_clear_fire_log(&e);
    expect_true(nerva_activate_node(&e, a, NERVA_Q8_8_ONE), "test activate A");
    nerva_tick_n(&e, 2);

    expect_true(nerva_test_fire_log_contains(&e, a), "A fired");
    expect_true(!nerva_test_fire_log_contains(&e, b), "B did not fire from prediction");
    expect_true(e.nodes[b].v > 0, "B pre-charged");
    expect_true(e.nodes[b].v < e.nodes[b].theta_fire, "B below fire threshold");
    expect_eq_u32(nerva_prediction_count_pending(&e), 1u, "one pending expectation");
    expect_true(nerva_prediction_pending_for_query(&e, e.active_query_tag) != NULL,
                "pending expectation for query");
    expect_eq_u64(e.debug.predictions_emitted, 1u, "prediction emitted");

    NervaTrace *expected = nerva_trace_recent(&e, 0);
    expect_true(expected && (expected->flags & NERVA_TRACE_EXPECTED), "expected trace recorded");

    nerva_engine_free(&e);
}

static void test_prediction_confirm_strengthens_edge(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "confirm init");

    uint32_t a = nerva_get_or_create_node(&e, "A");
    uint32_t b = nerva_get_or_create_node(&e, "B");
    uint32_t edge_ab = nerva_graph_create_edge(&e, a, b, NERVA_REL_KIND_OF);
    nerva_graph_rebuild_adjacency(&e);
    (void)edge_ab;

    nerva_test_train_ab(&e, a, 3);
    nerva_q8_8_t weight_before = e.edges[0].weight;

    nerva_set_prediction_mode(&e, 1);
    nerva_mutation_clear_log(&e);
    expect_true(nerva_activate_node(&e, a, NERVA_Q8_8_ONE), "activate for confirm test");
    nerva_tick(&e);

    expect_true(nerva_inject_edge_event(&e, 0, NERVA_Q8_8_ONE), "inject actual B event");
    nerva_tick(&e);
    nerva_apply_mutations(&e);

    expect_eq_u64(e.debug.predictions_confirmed, 1u, "prediction confirmed");
    expect_eq_i32((int32_t)e.edges[0].weight, (int32_t)weight_before + e.cfg.ltp_delta_q8_8,
                  "predicting edge strengthened");
    expect_eq_u32(nerva_prediction_count_pending(&e), 0u, "expectation cleared");

    nerva_engine_free(&e);
}

static void test_prediction_miss_weakens_edge(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "miss init");

    uint32_t a = nerva_get_or_create_node(&e, "A");
    uint32_t b = nerva_get_or_create_node(&e, "B");
    uint32_t edge_ab = nerva_graph_create_edge(&e, a, b, NERVA_REL_KIND_OF);
    nerva_graph_rebuild_adjacency(&e);
    expect_true(edge_ab == 0u, "A->B edge id");

    nerva_test_train_ab(&e, a, 3);

    uint32_t c = nerva_get_or_create_node(&e, "C");
    uint32_t edge_ac = nerva_graph_create_edge(&e, a, c, NERVA_REL_KIND_OF);
    nerva_graph_rebuild_adjacency(&e);
    expect_true(edge_ac != UINT32_MAX, "A->C decoy edge");

    nerva_q8_8_t weight_before = e.edges[0].weight;

    nerva_set_prediction_mode(&e, 1);
    nerva_mutation_clear_log(&e);
    expect_true(nerva_activate_node(&e, a, NERVA_Q8_8_ONE), "activate for miss test");
    nerva_tick(&e);

    expect_true(nerva_inject_edge_event(&e, edge_ac, NERVA_Q8_8_ONE), "inject actual C event");
    nerva_tick(&e);
    nerva_apply_mutations(&e);

    expect_eq_u64(e.debug.predictions_missed, 1u, "prediction missed");
    expect_eq_i32((int32_t)e.edges[0].weight, (int32_t)weight_before + e.cfg.ltd_delta_q8_8,
                  "predicting edge weakened");
    expect_true(nerva_test_trace_has_flags(&e, (uint16_t)(NERVA_TRACE_PRED_MISSED | NERVA_TRACE_SURPRISE)),
                "surprise trace recorded");

    nerva_engine_free(&e);
}

static void test_prediction_window_expires_pending(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "window init");

    uint32_t a = nerva_get_or_create_node(&e, "A");
    uint32_t b = nerva_get_or_create_node(&e, "B");
    nerva_graph_create_edge(&e, a, b, NERVA_REL_KIND_OF);
    nerva_graph_rebuild_adjacency(&e);

    nerva_test_train_ab(&e, a, 3);
    nerva_set_prediction_mode(&e, 1);
    expect_true(nerva_activate_node(&e, a, NERVA_Q8_8_ONE), "activate for window test");
    nerva_tick(&e);
    expect_eq_u32(nerva_prediction_count_pending(&e), 1u, "expectation pending before window");

    nerva_tick_n(&e, e.cfg.prediction_window_ticks + 1u);
    expect_eq_u32(nerva_prediction_count_pending(&e), 0u, "expectation expired after window");
    expect_eq_u64(e.debug.predictions_missed, 1u, "expired expectation counts as miss");

    nerva_engine_free(&e);
}

static void test_prediction_ab_experiment_artifacts(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "artifact init");

    uint32_t a = nerva_get_or_create_node(&e, "A");
    uint32_t b = nerva_get_or_create_node(&e, "B");
    nerva_graph_create_edge(&e, a, b, NERVA_REL_KIND_OF);
    nerva_graph_rebuild_adjacency(&e);

    nerva_test_train_ab(&e, a, 3);
    nerva_set_prediction_mode(&e, 1);
    expect_true(nerva_activate_node(&e, a, NERVA_Q8_8_ONE), "artifact activate");
    nerva_tick(&e);

    expect_true(nerva_inject_edge_event(&e, 0, NERVA_Q8_8_ONE), "artifact inject confirm");
    nerva_tick(&e);

    expect_true(nerva_prediction_save_trace(&e, "experiments/v035_next_event_prediction/prediction.log",
                                           8) == 0,
                "prediction.log saved");

    nerva_engine_free(&e);
}

int test_prediction_run(void) {
    g_failures = 0;
    test_prediction_normal_propagation_when_mode_off();
    test_prediction_emits_expected_not_fire();
    test_prediction_confirm_strengthens_edge();
    test_prediction_miss_weakens_edge();
    test_prediction_window_expires_pending();
    test_prediction_ab_experiment_artifacts();
    return g_failures;
}
