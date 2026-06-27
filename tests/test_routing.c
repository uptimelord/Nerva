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
#include "nerva_routing.h"
#include "nerva_test_fixtures.h"
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

static void expect_eq_u64(uint64_t actual, uint64_t expected, const char *message) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (expected %llu, got %llu)\n", message,
                (unsigned long long)expected, (unsigned long long)actual);
        g_failures++;
    }
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

static void nerva_test_setup_poodle_graph_no_rebuild(NervaEngine *e, uint32_t *poodle, uint32_t *dog,
                                                     uint32_t *animal) {
    *poodle = nerva_get_or_create_node(e, "poodle");
    *dog = nerva_get_or_create_node(e, "dog");
    *animal = nerva_get_or_create_node(e, "animal");

    nerva_graph_create_edge(e, *poodle, *dog, NERVA_REL_KIND_OF);
    nerva_graph_create_edge(e, *dog, *animal, NERVA_REL_KIND_OF);
}

static void nerva_test_push_activation(NervaEngine *e, uint32_t node_id, nerva_q8_8_t signal) {
    NervaEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.due_tick = e->tick;
    ev.source = NERVA_INVALID_ID;
    ev.target = node_id;
    ev.edge_id = NERVA_INVALID_ID;
    ev.signal = signal;
    ev.type_flags = NERVA_EVT_ACTIVATION;
    expect_true(nerva_event_push(e, ev), "push activation event");
}

static void nerva_test_append_routing_snapshot(NervaEngine *e, const char *path,
                                             const char *label, int truncate) {
    FILE *out = fopen(path, truncate ? "w" : "a");
    expect_true(out != NULL, "open routing.log");
    if (out) {
        nerva_routing_print_snapshot(e, out, label);
        fclose(out);
    }
}

static void nerva_test_append_trace_section(NervaEngine *e, const char *path, const char *label,
                                            const uint32_t *path_ids, uint32_t path_len,
                                            int truncate) {
    FILE *out = fopen(path, truncate ? "w" : "a");
    expect_true(out != NULL, "open trace.log");
    if (!out) {
        return;
    }

    fprintf(out, "--- case=%s ---\n", label ? label : "query");
    nerva_debug_print_fire_trace(e, out);
    if (path_ids && path_len > 0) {
        nerva_debug_print_path_line(e, out, path_ids, path_len);
    }
    nerva_trace_print_path(e, out, 8);
    fputs("routing: ", out);
    nerva_routing_print_snapshot(e, out, label);
    fclose(out);
}

static void nerva_test_run_query(NervaEngine *e, uint32_t source, uint32_t target,
                                 uint16_t relation, uint32_t ticks) {
    nerva_routing_begin_query(e, source, target, relation);
    expect_true(nerva_activate_node(e, source, NERVA_Q8_8_ONE), "activate query source");
    nerva_tick_n(e, ticks);
}

static void test_routing_routine_difficulty_low(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "routine difficulty init");

    uint32_t poodle, dog, animal;
    nerva_test_setup_poodle_graph(&e, &poodle, &dog, &animal);
    (void)dog;

    nerva_routing_begin_query(&e, poodle, animal, NERVA_REL_KIND_OF);
    expect_true(nerva_routing_compute_difficulty(&e) < e.cfg.fluid_threshold_base_q8_8,
                "routine reachable query difficulty below base threshold");

    nerva_engine_free(&e);
}

static void test_routing_routine_stays_crystallized(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "routine crystallized init");

    uint32_t poodle, dog, animal;
    nerva_test_setup_poodle_graph(&e, &poodle, &dog, &animal);
    (void)dog;

    const uint32_t rounds = 20u;
    for (uint32_t i = 0; i < rounds; ++i) {
        nerva_test_run_query(&e, poodle, animal, NERVA_REL_KIND_OF, 4);
        expect_true(nerva_routing_was_crystallized_last_query(&e),
                    "routine query crystallized");
    }

    expect_eq_u64(e.debug.crystallized_queries, rounds, "all routine queries crystallized");
    expect_true(e.debug.fluid_activations * 100u < rounds * 5u,
                "routine fluid activation rate below 5%");

    nerva_engine_free(&e);
}

static void test_routing_novel_triggers_fluid(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "novel fluid init");

    uint32_t a = nerva_get_or_create_node(&e, "novelA");
    uint32_t z = nerva_get_or_create_node(&e, "novelZ");

    nerva_test_run_query(&e, a, z, NERVA_REL_KIND_OF, 2);
    expect_true(nerva_routing_fluid_active(&e), "novel unreachable query enters fluid");
    expect_eq_u64(e.debug.fluid_activations, 1u, "novel query fluid activation counted");

    nerva_engine_free(&e);
}

static void test_routing_contradiction_triggers_fluid(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "contradiction fluid init");

    uint32_t bird, penguin, fly;
    nerva_test_setup_penguin_graph(&e, &bird, &penguin, &fly);
    (void)bird;

    expect_true(nerva_queue_blocker_edge(&e, penguin, fly, NERVA_REL_BLOCKS),
                "queue penguin blocker");
    nerva_apply_mutations(&e);

    nerva_test_run_query(&e, penguin, fly, NERVA_REL_USUALLY_HAS_PROPERTY, 4);
    expect_true(nerva_routing_fluid_active(&e), "contradiction query enters fluid");
    expect_true(e.debug.exceptions_suppressed >= 1u, "exception suppression observed");

    nerva_engine_free(&e);
}

static void test_routing_adaptive_threshold_fatigue(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "fatigue init");

    nerva_q8_8_t base = e.cfg.fluid_threshold_base_q8_8;
    uint32_t a = nerva_get_or_create_node(&e, "fatigueA");
    uint32_t z = nerva_get_or_create_node(&e, "fatigueZ");

    nerva_test_run_query(&e, a, z, NERVA_REL_KIND_OF, 2);
    expect_true(e.router.fluid_threshold_q8_8 > base, "fluid use raises adaptive threshold");

    nerva_routing_reset(&e);
    expect_eq_u32((uint32_t)e.router.fluid_threshold_q8_8, (uint32_t)base,
                  "reset restores base threshold");

    nerva_engine_free(&e);
}

static void test_routing_fluid_workspace_bounded(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "bounded fluid init");

    uint32_t poodle, dog, animal;
    nerva_test_setup_poodle_graph(&e, &poodle, &dog, &animal);
    (void)dog;

    nerva_test_run_query(&e, poodle, animal, NERVA_REL_KIND_OF, 4);
    expect_true(nerva_routing_was_crystallized_last_query(&e), "warm-up crystallized query");

    uint32_t a = nerva_get_or_create_node(&e, "boundA");
    uint32_t z = nerva_get_or_create_node(&e, "boundZ");
    nerva_test_run_query(&e, a, z, NERVA_REL_KIND_OF, 2);

    expect_true(nerva_routing_fluid_active(&e), "novel query entered fluid workspace");
    expect_true(e.router.fluid_count <= NERVA_FLUID_ACTIVE_MAX,
                "fluid workspace capped at four nodes");
    expect_true(e.debug.fluid_workspace_steps >= 1u, "fluid workspace step logged");

    nerva_engine_free(&e);
}

static void test_routing_stale_adjacency_routine_crystallized(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "stale adjacency init");

    uint32_t poodle, dog, animal;
    nerva_test_setup_poodle_graph_no_rebuild(&e, &poodle, &dog, &animal);
    (void)dog;
    expect_true(!e.adjacency_valid, "adjacency left stale after edge create");

    nerva_test_run_query(&e, poodle, animal, NERVA_REL_KIND_OF, 4);
    expect_true(e.router.novelty_count == 0, "reachable routine not classified as novel");
    expect_true(nerva_routing_was_crystallized_last_query(&e),
                "routine query crystallized with stale adjacency healed");

    nerva_engine_free(&e);
}

static void test_routing_lateral_inhibition_suppresses_rivals(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "inhibition init");

    uint32_t n1 = nerva_get_or_create_node(&e, "rivalA");
    uint32_t n2 = nerva_get_or_create_node(&e, "rivalB");
    uint32_t z = nerva_get_or_create_node(&e, "rivalZ");

    const nerva_q8_8_t compete_v = (nerva_q8_8_t)200;
    nerva_routing_begin_query(&e, n1, z, NERVA_REL_KIND_OF);
    nerva_test_push_activation(&e, n1, compete_v);
    nerva_test_push_activation(&e, n2, compete_v);
    nerva_tick(&e);

    expect_true(nerva_routing_fluid_active(&e), "competing active nodes enter fluid");
    expect_true(e.router.fluid_count >= 2u, "two rivals admitted to fluid workspace");
    expect_true(e.nodes[n1].v >= e.cfg.theta_compete_q8_8, "rival A above compete threshold");
    expect_true(e.nodes[n2].v >= e.cfg.theta_compete_q8_8, "rival B above compete threshold");

    nerva_q8_8_t hi = e.nodes[n1].v > e.nodes[n2].v ? e.nodes[n1].v : e.nodes[n2].v;
    nerva_q8_8_t lo = e.nodes[n1].v > e.nodes[n2].v ? e.nodes[n2].v : e.nodes[n1].v;
    expect_true(lo < hi, "lateral inhibition separates winner and loser");
    expect_true(hi >= e.cfg.theta_compete_q8_8, "winning rival stays above compete threshold");
    expect_true(lo < compete_v, "losing rival suppressed below pre-inhibition activation");

    nerva_engine_free(&e);
}

static void test_routing_experiment_artifacts(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "routing artifact init");

    const char *routing_path = "experiments/v07_fluid_routing/routing.log";
    const char *trace_path = "experiments/v07_fluid_routing/trace.log";

    uint32_t poodle, dog, animal;
    nerva_test_setup_poodle_graph(&e, &poodle, &dog, &animal);
    nerva_trace_clear(&e);
    nerva_debug_clear_fire_log(&e);
    nerva_test_run_query(&e, poodle, animal, NERVA_REL_KIND_OF, 4);
    uint32_t routine_path[] = {poodle, dog, animal};
    nerva_test_append_routing_snapshot(&e, routing_path, "routine", 1);
    nerva_test_append_trace_section(&e, trace_path, "routine", routine_path, 3, 1);

    uint32_t a = nerva_get_or_create_node(&e, "novelA");
    uint32_t z = nerva_get_or_create_node(&e, "novelZ");
    nerva_trace_clear(&e);
    nerva_debug_clear_fire_log(&e);
    nerva_test_run_query(&e, a, z, NERVA_REL_KIND_OF, 2);
    uint32_t novel_path[] = {a};
    nerva_test_append_routing_snapshot(&e, routing_path, "novel", 0);
    nerva_test_append_trace_section(&e, trace_path, "novel", novel_path, 1, 0);

    uint32_t bird, penguin, fly;
    nerva_test_setup_penguin_graph(&e, &bird, &penguin, &fly);
    (void)bird;
    expect_true(nerva_queue_blocker_edge(&e, penguin, fly, NERVA_REL_BLOCKS),
                "queue penguin blocker for artifact");
    nerva_apply_mutations(&e);
    nerva_trace_clear(&e);
    nerva_debug_clear_fire_log(&e);
    nerva_test_run_query(&e, penguin, fly, NERVA_REL_USUALLY_HAS_PROPERTY, 4);
    uint32_t contradict_path[] = {penguin, bird};
    nerva_test_append_routing_snapshot(&e, routing_path, "contradiction", 0);
    nerva_test_append_trace_section(&e, trace_path, "contradiction", contradict_path, 2, 0);

    nerva_engine_free(&e);
}

int test_routing_run(void) {
    g_failures = 0;
    test_routing_routine_difficulty_low();
    test_routing_routine_stays_crystallized();
    test_routing_novel_triggers_fluid();
    test_routing_contradiction_triggers_fluid();
    test_routing_adaptive_threshold_fatigue();
    test_routing_fluid_workspace_bounded();
    test_routing_stale_adjacency_routine_crystallized();
    test_routing_lateral_inhibition_suppresses_rivals();
    test_routing_experiment_artifacts();
    return g_failures;
}
