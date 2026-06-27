// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Paul Odantabao II

#include "nerva_config.h"
#include "nerva_engine.h"
#include "nerva_event.h"
#include "nerva_graph.h"
#include "nerva_learning.h"
#include "nerva_math.h"
#include "nerva_mutation.h"
#include "nerva_schema.h"
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

static void nerva_test_train_kind_of_schema(NervaEngine *e, uint32_t rounds) {
    static const char *chains[][3] = {
        {"A1", "B1", "C1"},
        {"A2", "B2", "C2"},
        {"A3", "B3", "C3"},
        {"A4", "B4", "C4"},
    };

    for (uint32_t i = 0; i < rounds; ++i) {
        uint32_t a = nerva_get_or_create_node(e, chains[i][0]);
        uint32_t b = nerva_get_or_create_node(e, chains[i][1]);
        uint32_t c = nerva_get_or_create_node(e, chains[i][2]);
        nerva_schema_observe_triple(e, a, NERVA_REL_KIND_OF, b, NERVA_REL_KIND_OF, c);
    }
}

static void test_schema_observe_builds_candidate(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "schema observe init");

    uint32_t a = nerva_get_or_create_node(&e, "A");
    uint32_t b = nerva_get_or_create_node(&e, "B");
    uint32_t c = nerva_get_or_create_node(&e, "C");

    nerva_schema_observe_triple(&e, a, NERVA_REL_KIND_OF, b, NERVA_REL_KIND_OF, c);
    expect_eq_u32(nerva_schema_count(&e), 1u, "one schema candidate");
    expect_eq_u32(e.schemas[0].distinct_count, 1u, "one distinct example");
    expect_true(nerva_schema_find_promoted(&e, NERVA_REL_KIND_OF, NERVA_REL_KIND_OF) == NULL,
                "not promoted after one observe");

    nerva_engine_free(&e);
}

static void test_schema_duplicate_observe_does_not_promote(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "duplicate observe init");

    uint32_t a = nerva_get_or_create_node(&e, "A");
    uint32_t b = nerva_get_or_create_node(&e, "B");
    uint32_t c = nerva_get_or_create_node(&e, "C");

    for (uint32_t i = 0; i < e.cfg.schema_support_threshold; ++i) {
        nerva_schema_observe_triple(&e, a, NERVA_REL_KIND_OF, b, NERVA_REL_KIND_OF, c);
    }

    expect_eq_u32(e.schemas[0].distinct_count, 1u, "duplicate observes stay one distinct");
    expect_eq_u32(e.schemas[0].support_count, 1u, "support not inflated by duplicates");
    expect_true(nerva_schema_find_promoted(&e, NERVA_REL_KIND_OF, NERVA_REL_KIND_OF) == NULL,
                "no promotion from repeated same triple");

    nerva_engine_free(&e);
}

static void test_schema_promotes_at_threshold(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "schema promote init");

    nerva_test_train_kind_of_schema(&e, e.cfg.schema_support_threshold);
    const NervaSchema *s =
        nerva_schema_find_promoted(&e, NERVA_REL_KIND_OF, NERVA_REL_KIND_OF);
    expect_true(s != NULL, "kind_of schema promoted");
    expect_eq_u32(s->distinct_count, (uint32_t)e.cfg.schema_support_threshold,
                  "distinct examples at threshold");
    expect_eq_u32(s->support_count, (uint32_t)e.cfg.schema_support_threshold,
                  "support at threshold");
    expect_true(s->raw_hop_cost > s->schema_edge_cost, "compression benefit present");
    expect_eq_u64(e.debug.schemas_promoted, 1u, "promotion counted");

    nerva_engine_free(&e);
}

static void test_schema_apply_before_promote_fails(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "apply gate init");

    uint32_t x = nerva_get_or_create_node(&e, "X");
    uint32_t y = nerva_get_or_create_node(&e, "Y");
    uint32_t z = nerva_get_or_create_node(&e, "Z");
    nerva_graph_create_edge(&e, x, y, NERVA_REL_KIND_OF);
    nerva_graph_create_edge(&e, y, z, NERVA_REL_KIND_OF);
    nerva_graph_rebuild_adjacency(&e);

    nerva_schema_observe_triple(&e, x, NERVA_REL_KIND_OF, y, NERVA_REL_KIND_OF, z);
    expect_true(nerva_schema_apply(&e, x, NERVA_REL_KIND_OF, y, NERVA_REL_KIND_OF, z) == 0,
                "apply blocked before promotion");

    nerva_engine_free(&e);
}

static void test_schema_apply_requires_premise_edges(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "premise apply init");

    nerva_test_train_kind_of_schema(&e, e.cfg.schema_support_threshold);
    expect_true(nerva_schema_find_promoted(&e, NERVA_REL_KIND_OF, NERVA_REL_KIND_OF) != NULL,
                "schema promoted for premise test");

    uint32_t x = nerva_get_or_create_node(&e, "X");
    uint32_t y = nerva_get_or_create_node(&e, "Y");
    uint32_t z = nerva_get_or_create_node(&e, "Z");
    expect_true(nerva_schema_apply(&e, x, NERVA_REL_KIND_OF, y, NERVA_REL_KIND_OF, z) == 0,
                "apply fails without premise edges");

    nerva_graph_create_edge(&e, x, y, NERVA_REL_KIND_OF);
    nerva_graph_create_edge(&e, y, z, NERVA_REL_KIND_OF);
    nerva_graph_rebuild_adjacency(&e);
    expect_true(nerva_schema_apply(&e, x, NERVA_REL_KIND_OF, y, NERVA_REL_KIND_OF, z) == 1,
                "apply succeeds once premises exist");

    nerva_engine_free(&e);
}

static void test_schema_transitive_kind_of_reachable(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "transitive init");

    nerva_test_train_kind_of_schema(&e, e.cfg.schema_support_threshold);

    uint32_t x = nerva_get_or_create_node(&e, "X");
    uint32_t y = nerva_get_or_create_node(&e, "Y");
    uint32_t z = nerva_get_or_create_node(&e, "Z");
    nerva_graph_create_edge(&e, x, y, NERVA_REL_KIND_OF);
    nerva_graph_create_edge(&e, y, z, NERVA_REL_KIND_OF);
    nerva_graph_rebuild_adjacency(&e);

    expect_true(!nerva_graph_has_edge(&e, x, z, NERVA_REL_KIND_OF),
                "no direct shortcut before schema apply");
    expect_true(nerva_graph_reachable(&e, x, z, NERVA_REL_KIND_OF),
                "two-hop path exists before shortcut");
    expect_true(nerva_schema_apply(&e, x, NERVA_REL_KIND_OF, y, NERVA_REL_KIND_OF, z) == 1,
                "schema apply queues shortcut");
    expect_eq_u64(e.debug.schemas_applied, 1u, "schema apply counted");
    nerva_apply_mutations(&e);
    expect_true(nerva_graph_has_edge(&e, x, z, NERVA_REL_KIND_OF),
                "direct shortcut edge after apply");
    expect_true(nerva_graph_reachable(&e, x, z, NERVA_REL_KIND_OF),
                "held-out pair reachable after apply");

    nerva_engine_free(&e);
}

static void test_schema_inside_move_output_relation(void) {
    expect_eq_u32(nerva_schema_output_relation(NERVA_REL_INSIDE, NERVA_REL_MOVED_TO),
                  (uint32_t)NERVA_REL_LOCATED_AT, "inside+moved_to -> located_at");
    expect_eq_u32(nerva_schema_output_relation(NERVA_REL_KIND_OF, NERVA_REL_BLOCKS),
                  (uint32_t)NERVA_REL_NONE, "unknown pair has no output");
}

static void test_schema_experiment_artifacts(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "artifact init");

    nerva_test_train_kind_of_schema(&e, e.cfg.schema_support_threshold);

    uint32_t x = nerva_get_or_create_node(&e, "X");
    uint32_t y = nerva_get_or_create_node(&e, "Y");
    uint32_t z = nerva_get_or_create_node(&e, "Z");
    nerva_graph_create_edge(&e, x, y, NERVA_REL_KIND_OF);
    nerva_graph_create_edge(&e, y, z, NERVA_REL_KIND_OF);
    nerva_graph_rebuild_adjacency(&e);

    nerva_mutation_clear_log(&e);
    expect_true(nerva_schema_apply(&e, x, NERVA_REL_KIND_OF, y, NERVA_REL_KIND_OF, z) == 1,
                "artifact schema apply");
    nerva_apply_mutations(&e);

    expect_eq_u32(e.mutation_log_count, 1u, "schema mutation logged");
    expect_eq_u32(e.mutation_log[0].type, (uint32_t)NERVA_MUT_CREATE_EDGE, "create edge mutation");
    expect_eq_u32(e.mutation_log[0].debug_reason, (uint32_t)NERVA_REASON_SCHEMA_APPLY,
                  "schema apply reason");

    expect_true(nerva_mutation_save_log(&e, "experiments/v05_schema_induction/mutation.log") == 0,
                "schema mutation.log saved");

    nerva_trace_clear(&e);
    expect_true(nerva_activate_node(&e, x, NERVA_Q8_8_ONE), "artifact activate X after apply");
    nerva_tick_n(&e, 4);
    expect_true(nerva_graph_reachable(&e, x, z, NERVA_REL_KIND_OF),
                "artifact held-out reachability via inferred edge");
    expect_true(nerva_trace_save_path(&e, "experiments/v05_schema_induction/trace.log", 8) == 0,
                "schema trace.log saved");

    nerva_engine_free(&e);
}

int test_schema_run(void) {
    g_failures = 0;
    test_schema_observe_builds_candidate();
    test_schema_duplicate_observe_does_not_promote();
    test_schema_promotes_at_threshold();
    test_schema_apply_before_promote_fails();
    test_schema_apply_requires_premise_edges();
    test_schema_transitive_kind_of_reachable();
    test_schema_inside_move_output_relation();
    test_schema_experiment_artifacts();
    return g_failures;
}
