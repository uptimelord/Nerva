// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Paul Odantabao II

#include "nerva_config.h"
#include "nerva_debug.h"
#include "nerva_engine.h"
#include "nerva_event.h"
#include "nerva_graph.h"
#include "nerva_learning.h"
#include "nerva_math.h"
#include "nerva_memory.h"
#include "nerva_mutation.h"
#include "nerva_persist.h"
#include "nerva_schema.h"
#include "nerva_test_fixtures.h"

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

static void expect_eq_i32(int32_t actual, int32_t expected, const char *message) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (expected %d, got %d)\n", message, (int)expected, (int)actual);
        g_failures++;
    }
}

static const char *g_snapshot_path = "experiments/v09_persistence/roundtrip.nerva";

static void nerva_test_build_poodle(NervaEngine *e, uint32_t *poodle, uint32_t *dog, uint32_t *animal) {
    nerva_test_setup_poodle_graph(e, poodle, dog, animal);
}

static void nerva_test_run_poodle_propagation(NervaEngine *e, uint32_t poodle) {
    nerva_debug_clear_fire_log(e);
    expect_true(nerva_activate_node(e, poodle, NERVA_Q8_8_ONE), "activate poodle");
    nerva_tick_n(e, 4);
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

static void nerva_test_patch_header_crc(NervaFileHeader *h) {
    h->header_crc32 = 0;
    h->header_crc32 = nerva_persist_crc32((const uint8_t *)h, sizeof(*h));
}

static void test_persist_roundtrip_graph(void) {
    NervaEngine a;
    NervaEngine b;
    expect_true(nerva_engine_init(&a, nerva_config_test()) == 0, "roundtrip init a");
    expect_true(nerva_engine_init(&b, nerva_config_test()) == 0, "roundtrip init b");

    uint32_t poodle, dog, animal;
    nerva_test_build_poodle(&a, &poodle, &dog, &animal);

    nerva_test_run_poodle_propagation(&a, poodle);
    nerva_q8_8_t weight_before = a.edges[0].weight;

    expect_true(nerva_persist_save(&a, g_snapshot_path) == 0, "save roundtrip snapshot");
    expect_true(nerva_persist_load(&b, g_snapshot_path) == 0, "load roundtrip snapshot");

    expect_eq_u32(b.node_count, 3u, "loaded node count");
    expect_eq_u32(b.edge_count, 2u, "loaded edge count");
    expect_true(b.adjacency_valid, "loaded adjacency rebuilt");
    expect_true(nerva_graph_reachable(&b, poodle, animal, NERVA_REL_KIND_OF),
                "loaded reachability poodle->animal");
    expect_eq_i32((int32_t)b.edges[0].weight, (int32_t)weight_before, "loaded edge weight preserved");

    nerva_test_run_poodle_propagation(&b, poodle);
    expect_true(nerva_debug_fire_sequence_matches(&b, (uint32_t[]){poodle, dog, animal}, 3),
                "loaded engine fire sequence matches");

    nerva_engine_free(&a);
    nerva_engine_free(&b);
}

static void test_persist_rejects_bad_crc(void) {
    NervaEngine a;
    NervaEngine b;
    expect_true(nerva_engine_init(&a, nerva_config_test()) == 0, "crc init a");
    expect_true(nerva_engine_init(&b, nerva_config_test()) == 0, "crc init b");

    uint32_t poodle, dog, animal;
    nerva_test_build_poodle(&a, &poodle, &dog, &animal);
    (void)dog;
    nerva_test_build_poodle(&b, &poodle, &dog, &animal);
    (void)dog;

    const uint32_t node_count_before = b.node_count;
    const uint32_t edge_count_before = b.edge_count;
    const nerva_q8_8_t weight_before = b.edges[0].weight;

    expect_true(nerva_persist_save(&a, g_snapshot_path) == 0, "save for crc test");

    FILE *f = fopen(g_snapshot_path, "r+b");
    expect_true(f != NULL, "open snapshot for crc corrupt");
    if (f) {
        fseek(f, (long)sizeof(NervaFileHeader) + 4, SEEK_SET);
        int c = fgetc(f);
        if (c != EOF) {
            fseek(f, -1, SEEK_CUR);
            fputc(c ^ 0x5A, f);
        }
        fclose(f);
    }

    expect_true(nerva_persist_load(&b, g_snapshot_path) != 0, "reject corrupted crc");
    expect_eq_u32(b.node_count, node_count_before, "failed load leaves node count unchanged");
    expect_eq_u32(b.edge_count, edge_count_before, "failed load leaves edge count unchanged");
    expect_eq_i32((int32_t)b.edges[0].weight, (int32_t)weight_before,
                    "failed load leaves edge weight unchanged");

    nerva_engine_free(&a);
    nerva_engine_free(&b);
}

static void test_persist_rejects_truncated_header(void) {
    const char *path = "experiments/v09_persistence/truncated.nerva";
    FILE *f = fopen(path, "wb");
    expect_true(f != NULL, "write truncated file");
    if (f) {
        uint8_t byte = 0;
        fwrite(&byte, 1, 1, f);
        fclose(f);
    }

    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "truncated init");
    expect_true(nerva_persist_load(&e, path) != 0, "reject truncated snapshot");
    nerva_engine_free(&e);
}

static void test_persist_rejects_bad_header_layout(void) {
    NervaEngine a;
    NervaEngine b;
    expect_true(nerva_engine_init(&a, nerva_config_test()) == 0, "header layout init a");
    expect_true(nerva_engine_init(&b, nerva_config_test()) == 0, "header layout init b");

    uint32_t poodle, dog, animal;
    nerva_test_build_poodle(&a, &poodle, &dog, &animal);
    nerva_test_build_poodle(&b, &poodle, &dog, &animal);
    const uint32_t node_count_before = b.node_count;

    expect_true(nerva_persist_save(&a, g_snapshot_path) == 0, "save for header layout test");

    FILE *f = fopen(g_snapshot_path, "r+b");
    expect_true(f != NULL, "open snapshot for header patch");
    if (f) {
        NervaFileHeader hdr;
        expect_true(fread(&hdr, sizeof(hdr), 1, f) == 1, "read header for patch");
        hdr.node_count = hdr.node_count + 100u;
        nerva_test_patch_header_crc(&hdr);
        fseek(f, 0, SEEK_SET);
        expect_true(fwrite(&hdr, sizeof(hdr), 1, f) == 1, "write patched header");
        fclose(f);
    }

    expect_true(nerva_persist_load(&b, g_snapshot_path) != 0, "reject inconsistent header layout");
    expect_eq_u32(b.node_count, node_count_before, "header reject leaves engine unchanged");

    nerva_engine_free(&a);
    nerva_engine_free(&b);
}

static void test_persist_memory_schema_roundtrip(void) {
    NervaEngine a;
    NervaEngine b;
    expect_true(nerva_engine_init(&a, nerva_config_test()) == 0, "mem/schema init a");
    expect_true(nerva_engine_init(&b, nerva_config_test()) == 0, "mem/schema init b");

    nerva_test_train_kind_of_schema(&a, a.cfg.schema_support_threshold);
    expect_true(nerva_schema_count(&a) >= 1u, "schema candidate exists");
    expect_true((a.schemas[0].flags & NERVA_SCHEMA_PROMOTED) != 0, "schema promoted before save");

    uint32_t poodle, dog, animal;
    nerva_test_build_poodle(&a, &poodle, &dog, &animal);
    (void)dog;
    (void)animal;
    uint32_t mem_id = nerva_memory_begin_episode(&a, 0x9009u);
    nerva_test_run_poodle_propagation(&a, poodle);
    nerva_memory_end_episode(&a, mem_id);
    nerva_memory_charge_update(&a, mem_id, 6.0f, 0.0f, 1.0f, 0.0f);
    expect_true(nerva_memory_get(&a, mem_id) != NULL, "memory block exists before save");

    const float charge_before = nerva_memory_get(&a, mem_id)->charge;
    const uint16_t schema_flags_before = a.schemas[0].flags;

    expect_true(nerva_persist_save(&a, g_snapshot_path) == 0, "save mem/schema snapshot");
    expect_true(nerva_persist_load(&b, g_snapshot_path) == 0, "load mem/schema snapshot");

    expect_eq_u32(b.memory_count, 1u, "loaded memory count");
    expect_eq_u32(b.schema_count, 1u, "loaded schema count");
    expect_true(nerva_memory_get(&b, 0) != NULL, "loaded memory block readable");
    expect_true(nerva_schema_count(&b) >= 1u, "loaded schema readable");
    expect_eq_u32(b.schemas[0].flags, (uint32_t)schema_flags_before, "schema flags preserved");
    expect_true(nerva_memory_get(&b, 0)->charge == charge_before, "memory charge preserved");

    nerva_engine_free(&a);
    nerva_engine_free(&b);
}

static void test_persist_feedback_weights_roundtrip(void) {
    NervaEngine a;
    NervaEngine b;
    expect_true(nerva_engine_init(&a, nerva_config_test()) == 0, "feedback init a");
    expect_true(nerva_engine_init(&b, nerva_config_test()) == 0, "feedback init b");

    uint32_t poodle, dog, animal;
    nerva_test_build_poodle(&a, &poodle, &dog, &animal);
    (void)animal;

    nerva_test_run_poodle_propagation(&a, poodle);
    expect_true(nerva_feedback_correct(&a), "feedback correct before save");
    nerva_apply_mutations(&a);

    nerva_q8_8_t w0 = a.edges[0].weight;
    expect_true(w0 > a.cfg.default_weight_q8_8, "edge strengthened before save");

    expect_true(nerva_persist_save(&a, g_snapshot_path) == 0, "save feedback snapshot");
    expect_true(nerva_persist_load(&b, g_snapshot_path) == 0, "load feedback snapshot");
    expect_eq_i32((int32_t)b.edges[0].weight, (int32_t)w0, "feedback weight survives load");

    nerva_engine_free(&a);
    nerva_engine_free(&b);
}

static void test_persist_experiment_artifacts(void) {
    NervaEngine a;
    NervaEngine b;
    expect_true(nerva_engine_init(&a, nerva_config_test()) == 0, "artifact init a");
    expect_true(nerva_engine_init(&b, nerva_config_test()) == 0, "artifact init b");

    uint32_t poodle, dog, animal;
    nerva_test_build_poodle(&a, &poodle, &dog, &animal);

    nerva_test_train_kind_of_schema(&a, a.cfg.schema_support_threshold);
    uint32_t mem_id = nerva_memory_begin_episode(&a, 0x9009u);
    nerva_test_run_poodle_propagation(&a, poodle);
    nerva_memory_end_episode(&a, mem_id);
    nerva_memory_charge_update(&a, mem_id, 6.0f, 0.0f, 1.0f, 0.0f);

    nerva_test_run_poodle_propagation(&a, poodle);
    expect_true(nerva_debug_save_fire_trace_with_path(
                    &a, "experiments/v09_persistence/trace.log",
                    (uint32_t[]){poodle, dog, animal}, 3) == 0,
                "pre-save trace.log");

    expect_true(nerva_persist_save(&a, g_snapshot_path) == 0, "artifact save");
    expect_true(nerva_persist_load(&b, g_snapshot_path) == 0, "artifact load");

    nerva_debug_clear_fire_log(&b);
    nerva_test_run_poodle_propagation(&b, poodle);
    {
        FILE *trace_out = fopen("experiments/v09_persistence/trace.log", "a");
        expect_true(trace_out != NULL, "append post-load trace");
        if (trace_out) {
            fputs("post-load:\n", trace_out);
            nerva_debug_print_fire_trace(&b, trace_out);
            nerva_debug_print_path_line(&b, trace_out, (uint32_t[]){poodle, dog, animal}, 3);
            fclose(trace_out);
        }
    }

    FILE *persist_out = fopen("experiments/v09_persistence/persist.log", "w");
    expect_true(persist_out != NULL, "open persist.log");
    if (persist_out) {
        fprintf(persist_out, "snapshot=%s\n", g_snapshot_path);
        fprintf(persist_out, "crc=validated header+payload\n");
        fprintf(persist_out, "load=ok\n");
        nerva_persist_print_summary(&a, persist_out, "pre-save");
        nerva_persist_print_summary(&b, persist_out, "post-load");
        fclose(persist_out);
    }

    nerva_engine_free(&a);
    nerva_engine_free(&b);
}

int test_persist_run(void) {
    g_failures = 0;
    test_persist_roundtrip_graph();
    test_persist_rejects_bad_crc();
    test_persist_rejects_truncated_header();
    test_persist_rejects_bad_header_layout();
    test_persist_feedback_weights_roundtrip();
    test_persist_memory_schema_roundtrip();
    test_persist_experiment_artifacts();
    return g_failures;
}
