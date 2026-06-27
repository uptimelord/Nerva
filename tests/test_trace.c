// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Paul Odantabao II

#include "nerva_config.h"
#include "nerva_engine.h"
#include "nerva_event.h"
#include "nerva_graph.h"
#include "nerva_math.h"
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

static int within_percent_u16(uint16_t actual, uint32_t expected, uint32_t percent) {
    uint32_t low = (expected * (100u - percent)) / 100u;
    uint32_t high = (expected * (100u + percent)) / 100u;
    return actual >= low && actual <= high;
}

static void test_trace_records_poodle_hops(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "trace poodle init");

    uint32_t poodle, dog, animal;
    nerva_test_setup_poodle_graph(&e, &poodle, &dog, &animal);
    nerva_trace_clear(&e);

    expect_true(nerva_activate_node(&e, poodle, NERVA_Q8_8_ONE), "activate poodle");
    nerva_tick_n(&e, 4);

    expect_true(e.trace_count >= 2, "at least two edge traces recorded");
    expect_true(nerva_trace_find_edge_in_recent(&e, 0, NERVA_TRACE_USED_PATH, 0), "poodle->dog trace");
    expect_true(nerva_trace_find_edge_in_recent(&e, 1, NERVA_TRACE_USED_PATH, 0), "dog->animal trace");

    NervaTrace *t0 = nerva_trace_recent(&e, 1);
    NervaTrace *t1 = nerva_trace_recent(&e, 0);
    expect_true(t0 && t1 && t0->tick <= t1->tick, "trace ticks increase");
    expect_true(t0->trace_tag != 0 && t1->trace_tag != 0, "path tags assigned");

    expect_true(nerva_trace_save_path(&e, "experiments/v02_trace_buffer/trace.log", 8) == 0,
                "trace.log saved in path order");

    nerva_engine_free(&e);
}

static void test_trace_decay_within_tolerance(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "trace decay init");

    NervaEvent ev = {0};
    ev.source = 0;
    ev.target = 0;
    ev.edge_id = 0;
    ev.signal = NERVA_Q8_8_ONE;
    ev.relation = NERVA_REL_KIND_OF;
    ev.trace_tag = 1;

    nerva_trace_record(&e, &ev, NERVA_TRACE_USED_PATH, 0, NERVA_Q8_8_ONE, 0);
    NervaTrace *t = nerva_trace_recent(&e, 0);
    expect_true(t != NULL, "trace exists");
    t->pre = NERVA_UQ0_16_ONE;
    t->post = NERVA_UQ0_16_ONE;

    nerva_trace_decay(&e);

    uint32_t expected_pre = ((uint32_t)NERVA_UQ0_16_ONE * NERVA_TRACE_PRE_DECAY_Q0_16) >> 16;
    uint32_t expected_post = ((uint32_t)NERVA_UQ0_16_ONE * NERVA_TRACE_POST_DECAY_Q0_16) >> 16;
    expect_true(within_percent_u16(t->pre, expected_pre, 5), "pre decay within 5 percent");
    expect_true(within_percent_u16(t->post, expected_post, 5), "post decay within 5 percent");

    nerva_engine_free(&e);
}

static void test_trace_ring_bounded(void) {
    NervaConfig cfg = nerva_config_test();
    cfg.max_traces = 4;
    NervaEngine e;
    expect_true(nerva_engine_init(&e, cfg) == 0, "bounded trace init");

    NervaEvent ev = {0};
    ev.edge_id = 0;
    ev.relation = NERVA_REL_KIND_OF;

    for (uint32_t i = 0; i < 8; ++i) {
        ev.target = i;
        nerva_trace_record(&e, &ev, NERVA_TRACE_USED_PATH, 0, 0, 0);
    }

    expect_eq_u32(e.trace_count, cfg.max_traces, "trace count capped at max_traces");
    expect_eq_u32(e.trace_head, 0u, "trace head wraps");

    nerva_engine_free(&e);
}

static void test_trace_find_used_path_for_feedback(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "trace find init");

    uint32_t poodle, dog, animal;
    nerva_test_setup_poodle_graph(&e, &poodle, &dog, &animal);
    nerva_trace_clear(&e);

    expect_true(nerva_activate_node(&e, poodle, NERVA_Q8_8_ONE), "activate");
    nerva_tick_n(&e, 4);

    expect_eq_u32(nerva_trace_count_used_path(&e), 2u, "two used-path traces searchable");
    expect_true(nerva_trace_find_edge_in_recent(&e, 0, NERVA_TRACE_USED_PATH, 0), "first hop findable");
    expect_true(nerva_trace_find_edge_in_recent(&e, 1, NERVA_TRACE_USED_PATH, 0), "second hop findable");

    NervaTrace *hop0 = nerva_trace_recent(&e, 1);
    NervaTrace *hop1 = nerva_trace_recent(&e, 0);
    expect_true(hop0 && hop1, "path hops present");
    expect_true(nerva_trace_find_edge_in_recent(&e, 0, NERVA_TRACE_USED_PATH, hop0->trace_tag),
                "edge findable with trace_tag filter");
    expect_true(!nerva_trace_find_edge_in_recent(&e, 0, NERVA_TRACE_USED_PATH, hop1->trace_tag),
                "wrong trace_tag does not match first hop");

    nerva_engine_free(&e);
}

int test_trace_run(void) {
    g_failures = 0;
    test_trace_records_poodle_hops();
    test_trace_decay_within_tolerance();
    test_trace_ring_bounded();
    test_trace_find_used_path_for_feedback();
    return g_failures;
}
