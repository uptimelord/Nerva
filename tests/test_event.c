// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Paul Odantabao II

#include "nerva_config.h"
#include "nerva_debug.h"
#include "nerva_engine.h"
#include "nerva_event.h"
#include "nerva_graph.h"
#include "nerva_math.h"
#include "nerva_test_fixtures.h"

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

static void test_event_heap_orders_due_ticks(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "engine init for heap order");

    uint32_t n0 = nerva_graph_create_node(&e, 1);
    (void)n0;

    NervaEvent later = {0};
    later.due_tick = 2;
    later.target = n0;
    later.signal = NERVA_Q8_8_ONE;
    NervaEvent earlier = later;
    earlier.due_tick = 1;

    expect_true(nerva_event_push(&e, later), "push later event");
    expect_true(nerva_event_push(&e, earlier), "push earlier event");

    e.tick = 2;
    NervaEvent out;
    expect_true(nerva_event_pop(&e, &out), "pop first due event");
    expect_eq_u32((uint32_t)out.due_tick, 1u, "earlier due tick pops first");

    later.due_tick = 0;
    e.tick = 100;
    expect_true(!nerva_event_push(&e, later), "stale event rejected");
    expect_true(e.debug.events_stale_dropped > 0, "stale drop counted");

    nerva_engine_free(&e);
}

static void test_event_overflow_not_silent(void) {
    NervaConfig cfg = nerva_config_test();
    cfg.max_events = 2;
    NervaEngine e;
    expect_true(nerva_engine_init(&e, cfg) == 0, "tiny event cap init");

    uint32_t n = nerva_graph_create_node(&e, 1);
    NervaEvent ev = {0};
    ev.due_tick = 0;
    ev.target = n;
    ev.signal = NERVA_Q8_8_ONE;

    expect_true(nerva_event_push(&e, ev), "push first");
    expect_true(nerva_event_push(&e, ev), "push second fills heap");
    expect_true(!nerva_event_push(&e, ev), "equal-strength overflow rejected");
    expect_true(e.debug.events_overflow_dropped > 0, "overflow drop counted");

    nerva_engine_free(&e);
}

static void test_activation_fires_once_with_refractory(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "engine init for refractory");

    uint32_t n = nerva_graph_create_node(&e, 1);
    nerva_graph_rebuild_adjacency(&e);
    nerva_debug_clear_fire_log(&e);

    expect_true(nerva_activate_node(&e, n, NERVA_Q8_8_ONE), "activate node");
    nerva_tick(&e);

    expect_true(e.nodes[n].last_fired_tick == 0, "node fired on tick 0");
    expect_true(e.debug.total_fired >= 1, "fire counter incremented");
    expect_true(nerva_node_refractory_remaining(&e, &e.nodes[n]) > 0, "refractory active after fire");

    uint64_t fires_after_first = e.debug.total_fired;
    expect_true(nerva_activate_node(&e, n, NERVA_Q8_8_ONE), "second activation during refractory");
    nerva_tick(&e);
    expect_eq_u64(e.debug.total_fired, fires_after_first, "no second fire during refractory");

    nerva_engine_free(&e);
}

static void test_event_overflow_admits_stronger_signal(void) {
    NervaConfig cfg = nerva_config_test();
    cfg.max_events = 2;
    NervaEngine e;
    expect_true(nerva_engine_init(&e, cfg) == 0, "overflow admit init");

    uint32_t n = nerva_graph_create_node(&e, 1);
    NervaEvent weak = {0};
    weak.due_tick = 0;
    weak.target = n;
    weak.signal = 16;
    NervaEvent strong = weak;
    strong.signal = NERVA_Q8_8_ONE;

    expect_true(nerva_event_push(&e, weak), "push weak first");
    expect_true(nerva_event_push(&e, weak), "fill heap");
    expect_true(nerva_event_push(&e, strong), "stronger event admitted on overflow");
    expect_eq_u64(e.debug.events_overflow_admitted, 1u, "overflow admit counted");
    expect_eq_u64(e.debug.events_overflow_dropped, 0u, "no silent overflow on admit path");

    nerva_engine_free(&e);
}

static void test_fire_without_adjacency_still_records(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "fire without adjacency init");

    uint32_t n = nerva_graph_create_node(&e, 1);
    nerva_debug_clear_fire_log(&e);
    e.nodes[n].v = NERVA_Q8_8_ONE;
    e.adjacency_valid = 0;

    nerva_fire_node(&e, n);

    expect_eq_u32((uint32_t)e.nodes[n].last_fired_tick, 0u, "fire recorded without adjacency");
    expect_eq_u32(e.fire_log_count, 1u, "fire log written without adjacency");
    expect_eq_u32(e.event_count, 0u, "no propagation without adjacency");

    nerva_engine_free(&e);
}

static void test_refractory_decays_on_idle_ticks(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "refractory idle init");

    uint32_t n = nerva_graph_create_node(&e, 1);
    nerva_graph_rebuild_adjacency(&e);

    expect_true(nerva_activate_node(&e, n, NERVA_Q8_8_ONE), "activate");
    nerva_tick(&e);
    expect_true(nerva_node_refractory_remaining(&e, &e.nodes[n]) > 0, "refractory active");

    uint32_t left = nerva_node_refractory_remaining(&e, &e.nodes[n]);
    nerva_tick(&e);
    expect_true(nerva_node_refractory_remaining(&e, &e.nodes[n]) < left,
                "refractory decays on wall-clock ticks without new input");

    nerva_engine_free(&e);
}

static void test_event_propagates_poodle_dog_animal(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "engine init for poodle propagation");

    uint32_t poodle, dog, animal;
    nerva_test_setup_poodle_graph(&e, &poodle, &dog, &animal);
    nerva_debug_clear_fire_log(&e);

    expect_true(nerva_activate_node(&e, poodle, NERVA_Q8_8_ONE), "activate poodle");
    nerva_tick_n(&e, 4);

    expect_eq_u32((uint32_t)e.nodes[poodle].last_fired_tick, 0u, "poodle fired tick 0");
    expect_eq_u32((uint32_t)e.nodes[dog].last_fired_tick, 1u, "dog fired tick 1");
    expect_eq_u32((uint32_t)e.nodes[animal].last_fired_tick, 2u, "animal fired tick 2");
    expect_eq_u32(e.event_count, 0u, "event queue drained");

    uint32_t path[] = {poodle, dog, animal};
    expect_true(nerva_debug_fire_sequence_matches(&e, path, 3), "fire trace matches path");
    expect_true(nerva_debug_save_fire_trace_with_path(
                    &e, "experiments/v01_event_propagation/trace.log", path, 3) == 0,
                "trace.log saved");

    nerva_engine_free(&e);
}

static void test_propagation_not_name_hardcoded(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "engine init for generic graph");

    uint32_t x = nerva_get_or_create_node(&e, "alpha");
    uint32_t y = nerva_get_or_create_node(&e, "beta");
    uint32_t z = nerva_get_or_create_node(&e, "gamma");
    nerva_graph_create_edge(&e, x, y, NERVA_REL_KIND_OF);
    nerva_graph_create_edge(&e, y, z, NERVA_REL_KIND_OF);
    nerva_graph_rebuild_adjacency(&e);
    nerva_debug_clear_fire_log(&e);

    expect_true(nerva_activate_node(&e, x, NERVA_Q8_8_ONE), "activate alpha");
    nerva_tick_n(&e, 4);

    expect_eq_u32((uint32_t)e.nodes[z].last_fired_tick, 2u, "gamma fires on tick 2");

    nerva_engine_free(&e);
}

int test_event_run(void) {
    g_failures = 0;
    test_event_heap_orders_due_ticks();
    test_event_overflow_not_silent();
    test_event_overflow_admits_stronger_signal();
    test_fire_without_adjacency_still_records();
    test_refractory_decays_on_idle_ticks();
    test_activation_fires_once_with_refractory();
    test_event_propagates_poodle_dog_animal();
    test_propagation_not_name_hardcoded();
    return g_failures;
}
