// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Paul Odantabao II

#include "nerva_config.h"
#include "nerva_engine.h"
#include "nerva_event.h"
#include "nerva_graph.h"
#include "nerva_learning.h"
#include "nerva_math.h"
#include "nerva_mutation.h"
#include "nerva_test_fixtures.h"
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

static void nerva_test_run_poodle_query(NervaEngine *e, uint32_t poodle) {
    nerva_mutation_clear_log(e);
    expect_true(nerva_activate_node(e, poodle, NERVA_Q8_8_ONE), "activate poodle");
    nerva_tick_n(e, 4);
}

static void nerva_test_run_poodle_query_fresh_traces(NervaEngine *e, uint32_t poodle) {
    nerva_trace_clear(e);
    nerva_test_run_poodle_query(e, poodle);
}

static void test_feedback_correct_strengthens_used_edges(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "learning init");

    uint32_t poodle, dog, animal;
    nerva_test_setup_poodle_graph(&e, &poodle, &dog, &animal);
    (void)dog;
    (void)animal;

    nerva_q8_8_t w0_before = e.edges[0].weight;
    nerva_q8_8_t w1_before = e.edges[1].weight;

    nerva_test_run_poodle_query_fresh_traces(&e, poodle);
    expect_eq_u32(nerva_trace_count_used_path(&e), 2u, "two used-path traces");

    expect_eq_u32(nerva_feedback_correct(&e), 2u, "correct feedback queues two deltas");
    expect_eq_i32((int32_t)e.edges[0].weight, (int32_t)w0_before, "weight unchanged before apply");
    nerva_apply_mutations(&e);
    expect_eq_i32((int32_t)e.edges[0].weight, (int32_t)w0_before + e.cfg.ltp_delta_q8_8,
                    "edge 0 strengthened");
    expect_eq_i32((int32_t)e.edges[1].weight, (int32_t)w1_before + e.cfg.ltp_delta_q8_8,
                    "edge 1 strengthened");
    expect_eq_u32(e.mutation_log_count, 2u, "mutations logged");

    nerva_engine_free(&e);
}

static void test_feedback_wrong_weakens_used_edges(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "wrong feedback init");

    uint32_t poodle, dog, animal;
    nerva_test_setup_poodle_graph(&e, &poodle, &dog, &animal);
    (void)dog;
    (void)animal;

    nerva_q8_8_t w0_before = e.edges[0].weight;
    nerva_test_run_poodle_query_fresh_traces(&e, poodle);

    expect_eq_u32(nerva_feedback_wrong(&e), 2u, "wrong feedback queues deltas");
    nerva_apply_mutations(&e);
    expect_eq_i32((int32_t)e.edges[0].weight, (int32_t)w0_before + e.cfg.ltd_delta_q8_8,
                    "edge 0 weakened");

    nerva_engine_free(&e);
}

static void test_feedback_skips_unused_edges(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "unused edge init");

    uint32_t poodle, dog, animal;
    nerva_test_setup_poodle_graph(&e, &poodle, &dog, &animal);
    uint32_t cat = nerva_get_or_create_node(&e, "cat");
    uint32_t decoy = nerva_graph_create_edge(&e, cat, animal, NERVA_REL_KIND_OF);
    nerva_graph_rebuild_adjacency(&e);
    expect_true(decoy != UINT32_MAX, "decoy edge created");
    (void)cat;

    nerva_q8_8_t decoy_before = e.edges[decoy].weight;
    nerva_test_run_poodle_query_fresh_traces(&e, poodle);

    nerva_feedback_correct(&e);
    nerva_apply_mutations(&e);
    expect_eq_i32((int32_t)e.edges[decoy].weight, (int32_t)decoy_before, "unused edge unchanged");

    nerva_engine_free(&e);
}

static void test_feedback_wrong_gates_after_repeat(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "gate close init");

    uint32_t poodle, dog, animal;
    nerva_test_setup_poodle_graph(&e, &poodle, &dog, &animal);
    (void)dog;
    (void)animal;

    nerva_uq0_16_t gate_before = e.edges[0].gate;

    nerva_test_run_poodle_query_fresh_traces(&e, poodle);
    nerva_feedback_wrong(&e);
    nerva_apply_mutations(&e);
    expect_eq_u32((uint32_t)e.edges[0].gate, (uint32_t)gate_before, "gate unchanged after one wrong");

    nerva_test_run_poodle_query(&e, poodle);
    nerva_feedback_wrong(&e);
    nerva_apply_mutations(&e);
    expect_true(e.edges[0].gate < gate_before, "gate closes after repeated wrong feedback");

    nerva_engine_free(&e);
}

static void test_weight_clipping_prevents_runaway(void) {
    NervaConfig cfg = nerva_config_test();
    cfg.weight_max_q8_8 = 280;
    NervaEngine e;
    expect_true(nerva_engine_init(&e, cfg) == 0, "clip init");

    uint32_t poodle, dog, animal;
    nerva_test_setup_poodle_graph(&e, &poodle, &dog, &animal);
    (void)dog;
    (void)animal;

    e.edges[0].weight = 272;

    for (uint32_t i = 0; i < 8; ++i) {
        nerva_test_run_poodle_query_fresh_traces(&e, poodle);
        nerva_feedback_correct(&e);
        nerva_apply_mutations(&e);
    }

    expect_eq_i32((int32_t)e.edges[0].weight, (int32_t)cfg.weight_max_q8_8,
                  "weight clipped at max after repeated feedback");

    nerva_engine_free(&e);
}

static void test_feedback_scoped_to_latest_query(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "query scope init");

    uint32_t poodle, dog, animal;
    nerva_test_setup_poodle_graph(&e, &poodle, &dog, &animal);
    (void)dog;
    (void)animal;

    uint32_t alpha = nerva_get_or_create_node(&e, "alpha");
    uint32_t beta = nerva_get_or_create_node(&e, "beta");
    uint32_t gamma = nerva_get_or_create_node(&e, "gamma");
    uint32_t edge_ab = nerva_graph_create_edge(&e, alpha, beta, NERVA_REL_KIND_OF);
    uint32_t edge_bg = nerva_graph_create_edge(&e, beta, gamma, NERVA_REL_KIND_OF);
    nerva_graph_rebuild_adjacency(&e);
    expect_true(edge_ab != UINT32_MAX && edge_bg != UINT32_MAX, "alpha chain edges");

    nerva_q8_8_t poodle_weight_before = e.edges[0].weight;
    nerva_q8_8_t alpha_weight_before = e.edges[edge_ab].weight;

    nerva_test_run_poodle_query_fresh_traces(&e, poodle);
    expect_true(nerva_activate_node(&e, alpha, NERVA_Q8_8_ONE), "activate alpha second query");
    nerva_tick_n(&e, 4);

    expect_eq_u32(nerva_feedback_correct(&e), 2u, "feedback targets latest query only");
    nerva_apply_mutations(&e);
    expect_eq_i32((int32_t)e.edges[0].weight, (int32_t)poodle_weight_before,
                  "prior query edges unchanged");
    expect_eq_i32((int32_t)e.edges[edge_ab].weight,
                  (int32_t)alpha_weight_before + e.cfg.ltp_delta_q8_8,
                  "latest query edge strengthened");

    nerva_engine_free(&e);
}

static void test_feedback_poodle_experiment_artifacts(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "artifact init");

    uint32_t poodle, dog, animal;
    nerva_test_setup_poodle_graph(&e, &poodle, &dog, &animal);
    (void)dog;
    (void)animal;

    nerva_test_run_poodle_query_fresh_traces(&e, poodle);
    nerva_feedback_correct(&e);
    nerva_apply_mutations(&e);

    expect_true(nerva_trace_save_path(&e, "experiments/v03_feedback_writeback/trace.log", 8) == 0,
                "trace.log saved");
    expect_true(nerva_mutation_save_log(&e, "experiments/v03_feedback_writeback/mutation.log") == 0,
                "mutation.log saved");

    nerva_engine_free(&e);
}

static void test_i32_saturating_add_clips(void) {
    expect_true(nerva_i32_saturating_add(NERVA_I32_SCORE_MAX, 1) == NERVA_I32_SCORE_MAX,
                "i32 sat add clips high");
    expect_true(nerva_i32_saturating_add(NERVA_I32_SCORE_MIN, -1) == NERVA_I32_SCORE_MIN,
                "i32 sat add clips low");
    expect_true(nerva_i32_saturating_add(100, 200) == 300, "i32 sat add normal");
}

int test_learning_run(void) {
    g_failures = 0;
    test_i32_saturating_add_clips();
    test_feedback_correct_strengthens_used_edges();
    test_feedback_wrong_weakens_used_edges();
    test_feedback_skips_unused_edges();
    test_feedback_wrong_gates_after_repeat();
    test_weight_clipping_prevents_runaway();
    test_feedback_scoped_to_latest_query();
    test_feedback_poodle_experiment_artifacts();
    return g_failures;
}
