// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Paul Odantabao II

#include "nerva_config.h"
#include "nerva_engine.h"
#include "nerva_graph.h"

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

static void test_create_nodes_edges(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "engine init");

    uint32_t poodle_a = nerva_get_or_create_node(&e, "poodle");
    uint32_t poodle_b = nerva_get_or_create_node(&e, "poodle");
    uint32_t dog = nerva_get_or_create_node(&e, "dog");

    expect_eq_u32(poodle_a, poodle_b, "duplicate names resolve to same node id");
    expect_eq_u32(e.node_count, 2u, "node count after duplicate create");
    expect_true(poodle_a != dog, "distinct names get distinct ids");
    expect_eq_u32(e.nodes[poodle_a].id, poodle_a, "node id equals array index");
    expect_eq_u32(e.nodes[dog].name_id, nerva_intern_name(&e, "dog"), "node stores name id");
    expect_eq_u32(nerva_intern_name(NULL, "x"), 0u, "intern_name rejects null engine");

    nerva_engine_free(&e);
}

static void test_static_reachability_poodle(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "engine init for poodle graph");

    uint32_t poodle = nerva_get_or_create_node(&e, "poodle");
    uint32_t dog = nerva_get_or_create_node(&e, "dog");
    uint32_t animal = nerva_get_or_create_node(&e, "animal");

    expect_eq_u32(e.node_count, 3u, "poodle graph node count");

    expect_true(nerva_graph_create_edge(&e, poodle, dog, NERVA_REL_KIND_OF) != UINT32_MAX,
                "create poodle -> dog edge");
    expect_true(nerva_graph_create_edge(&e, dog, animal, NERVA_REL_KIND_OF) != UINT32_MAX,
                "create dog -> animal edge");
    expect_eq_u32(e.edge_count, 2u, "poodle graph edge count");

    nerva_graph_rebuild_adjacency(&e);
    expect_true(e.adjacency_valid, "adjacency rebuilt");

    expect_true(nerva_graph_reachable(&e, poodle, animal, NERVA_REL_KIND_OF),
                "poodle reaches animal through dog");
    expect_true(nerva_graph_reachable_named(&e, "poodle", "animal", "kind_of"),
                "named reachability query");

    expect_true(!nerva_graph_reachable(&e, animal, poodle, NERVA_REL_KIND_OF),
                "reverse path is not reachable");

    nerva_engine_free(&e);
}

static void test_adjacency_outgoing_lists(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "engine init for adjacency");

    uint32_t a = nerva_get_or_create_node(&e, "a");
    uint32_t b = nerva_get_or_create_node(&e, "b");
    uint32_t c = nerva_get_or_create_node(&e, "c");

    nerva_graph_create_edge(&e, a, b, NERVA_REL_KIND_OF);
    nerva_graph_create_edge(&e, a, c, NERVA_REL_KIND_OF);
    nerva_graph_rebuild_adjacency(&e);

    expect_eq_u32(e.nodes[a].out_count, 2u, "node a has two outgoing edge slots");
    expect_eq_u32(e.edges[e.sorted_edges[e.nodes[a].first_out]].target, b,
                  "first outgoing edge target");
    expect_eq_u32(e.edges[e.sorted_edges[e.nodes[a].first_out + 1]].target, c,
                  "second outgoing edge target");

    nerva_engine_free(&e);
}

static void test_edge_ids_stable_after_rebuild(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "engine init for stable edge ids");

    uint32_t a = nerva_get_or_create_node(&e, "a");
    uint32_t b = nerva_get_or_create_node(&e, "b");
    uint32_t c = nerva_get_or_create_node(&e, "c");

    uint32_t edge_ab = nerva_graph_create_edge(&e, a, b, NERVA_REL_KIND_OF);
    uint32_t edge_bc = nerva_graph_create_edge(&e, b, c, NERVA_REL_KIND_OF);
    (void)edge_bc;

    nerva_graph_rebuild_adjacency(&e);

    expect_eq_u32(e.edges[edge_ab].source, a, "edge id still refers to same source after rebuild");
    expect_eq_u32(e.edges[edge_ab].target, b, "edge id still refers to same target after rebuild");

    nerva_engine_free(&e);
}

static void test_reachable_requires_adjacency(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "engine init for adjacency gate");

    uint32_t a = nerva_get_or_create_node(&e, "a");
    uint32_t b = nerva_get_or_create_node(&e, "b");
    nerva_graph_create_edge(&e, a, b, NERVA_REL_KIND_OF);

    expect_true(!nerva_graph_reachable(&e, a, b, NERVA_REL_KIND_OF),
                "reachable returns false before adjacency rebuild");

    nerva_engine_free(&e);
}

static void test_edge_dedup_and_wrong_relation(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "engine init for dedup");

    uint32_t a = nerva_get_or_create_node(&e, "a");
    uint32_t b = nerva_get_or_create_node(&e, "b");

    uint32_t first = nerva_graph_create_edge(&e, a, b, NERVA_REL_KIND_OF);
    uint32_t second = nerva_graph_create_edge(&e, a, b, NERVA_REL_KIND_OF);
    expect_eq_u32(first, second, "duplicate edge returns same id");
    expect_eq_u32(e.edge_count, 1u, "duplicate edge does not grow edge_count");

    nerva_graph_create_edge(&e, a, b, NERVA_REL_INSIDE);
    nerva_graph_rebuild_adjacency(&e);

    expect_true(nerva_graph_reachable(&e, a, b, NERVA_REL_KIND_OF), "kind_of path exists");
    expect_true(!nerva_graph_reachable(&e, a, b, NERVA_REL_BLOCKS), "wrong relation unreachable");

    nerva_engine_free(&e);
}

static void test_reachability_self_and_invalid(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "engine init for invalid reachability");

    uint32_t a = nerva_get_or_create_node(&e, "a");
    nerva_graph_rebuild_adjacency(&e);

    expect_true(nerva_graph_reachable(&e, a, a, NERVA_REL_KIND_OF), "source equals target");
    expect_true(!nerva_graph_reachable(&e, a, 999u, NERVA_REL_KIND_OF), "invalid target rejected");
    expect_true(!nerva_graph_reachable_named(&e, "missing", "a", "kind_of"), "missing source rejected");

    nerva_engine_free(&e);
}

static void test_deleted_edge_span(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "engine init for deleted edge span");

    uint32_t a = nerva_get_or_create_node(&e, "a");
    uint32_t b = nerva_get_or_create_node(&e, "b");
    uint32_t mid = nerva_get_or_create_node(&e, "mid");
    uint32_t c = nerva_get_or_create_node(&e, "c");

    nerva_graph_create_edge(&e, a, b, NERVA_REL_KIND_OF);
    uint32_t dead = nerva_graph_create_edge(&e, a, mid, NERVA_REL_KIND_OF);
    nerva_graph_create_edge(&e, a, c, NERVA_REL_KIND_OF);
    nerva_graph_create_edge(&e, c, mid, NERVA_REL_KIND_OF);

    e.edges[dead].flags |= NERVA_EDGE_DELETED;
    nerva_graph_rebuild_adjacency(&e);

    expect_true(nerva_graph_reachable(&e, a, mid, NERVA_REL_KIND_OF),
                "reachability skips deleted middle edge via trailing live edge");

    nerva_engine_free(&e);
}

static void test_capacity_overflow(void) {
    NervaConfig cfg = nerva_config_test();
    cfg.max_nodes = 2;
    cfg.max_edges = 1;
    cfg.max_names = 4;
    NervaEngine e;
    expect_true(nerva_engine_init(&e, cfg) == 0, "tiny engine init");

    expect_true(nerva_get_or_create_node(&e, "a") != UINT32_MAX, "first node ok");
    expect_true(nerva_get_or_create_node(&e, "b") != UINT32_MAX, "second node ok");
    expect_eq_u32(nerva_get_or_create_node(&e, "c"), UINT32_MAX, "node cap overflow");

    uint32_t a = 0;
    uint32_t b = 1;
    expect_true(nerva_graph_create_edge(&e, a, b, NERVA_REL_KIND_OF) != UINT32_MAX, "first edge ok");
    expect_eq_u32(nerva_graph_create_edge(&e, a, b, NERVA_REL_INSIDE), UINT32_MAX, "edge cap overflow");

    nerva_engine_free(&e);
}

int test_graph_run(void) {
    g_failures = 0;
    test_create_nodes_edges();
    test_static_reachability_poodle();
    test_adjacency_outgoing_lists();
    test_edge_ids_stable_after_rebuild();
    test_reachable_requires_adjacency();
    test_edge_dedup_and_wrong_relation();
    test_reachability_self_and_invalid();
    test_deleted_edge_span();
    test_capacity_overflow();
    return g_failures;
}
