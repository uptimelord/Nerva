// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Paul Odantabao II

#include "chatworld.h"
#include "nerva_config.h"
#include "nerva_engine.h"
#include "nerva_graph.h"

#include <stdio.h>
#include <string.h>

static int g_failures;

static void expect_true(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        g_failures++;
    }
}

static void test_chatworld_surface_adapter_no_oracle_labels(void) {
    NervaEngine e;
    ChatWorldNerva cw;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "chatworld surface init");
    expect_true(chatworld_nerva_init(&e, &cw) == 0, "chatworld nerva init");
    expect_true(chatworld_emit_surface(&e, &cw, "my name is Ada") == 0,
                "chatworld emit surface");

    expect_true(nerva_find_node_by_name(&e, "TOKEN:my") != UINT32_MAX, "TOKEN:my emitted");
    expect_true(nerva_find_node_by_name(&e, "TOKEN:name") != UINT32_MAX, "TOKEN:name emitted");
    expect_true(nerva_find_node_by_name(&e, "TOKEN:ada") != UINT32_MAX, "TOKEN:ada emitted");
    expect_true(nerva_find_node_by_name(&e, "POSITION:0") != UINT32_MAX, "POSITION emitted");
    expect_true(nerva_find_node_by_name(&e, "SPEAKER:user") != UINT32_MAX, "speaker emitted");

    for (uint32_t i = 0; i < e.name_count; ++i) {
        const char *name = e.names[i];
        expect_true(strstr(name, "INTENT") == NULL, "no INTENT labels");
        expect_true(strstr(name, "SLOT") == NULL, "no SLOT labels");
        expect_true(strstr(name, "CORRECT") == NULL, "no CORRECT labels");
        expect_true(strstr(name, "FACT_QUERY") == NULL, "no FACT_QUERY labels");
    }

    for (uint32_t i = 0; i < cw.token_count; ++i) {
        for (uint32_t a = 0; a < CHAT_ACTION_COUNT; ++a) {
            uint32_t edge_id = cw.policy_edge[i][a];
            expect_true(edge_id != UINT32_MAX, "candidate edge exists");
            expect_true(e.edges[edge_id].weight == 0, "candidate edge starts zero");
        }
    }
    nerva_engine_free(&e);
}

static void test_chatworld_zero_score_eval_has_no_fallback(void) {
    NervaEngine e;
    ChatWorldNerva cw;
    ChatWorld w;
    ChatWorldTurn turn = {"what is my favorite food", CHAT_EXPECT_UNKNOWN, NULL, NULL, false};
    chatworld_reset(&w);
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "chatworld zero init");
    expect_true(chatworld_nerva_init(&e, &cw) == 0, "chatworld zero nerva init");
    ChatWorldDecision d = chatworld_step(&e, &cw, &w, &turn);
    expect_true(d.no_supported_response, "zero-score eval reports no supported response");
    expect_true(d.action == CHAT_ACTION_COUNT, "zero-score eval selects no fallback action");
    nerva_engine_free(&e);
}

static void test_chatworld_lite_learns_policy_and_memory(void) {
    NervaEngine e;
    ChatWorldConfig cfg;
    ChatWorldResult result;
    chatworld_config_defaults(&cfg);
    cfg.train_epochs = 24u;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "chatworld run init");
    expect_true(chatworld_run(&e, &cfg, &result) == 0, "chatworld run");
    expect_true(result.metrics.eval_total > 0u, "chatworld eval ran");
    expect_true(result.metrics.eval_correct * 100u >= result.metrics.eval_total * 70u,
                "chatworld eval accuracy >=70%");
    expect_true(result.metrics.eval_correct >= result.metrics.eval_baseline_correct + 2u,
                "chatworld beats unknown baseline");
    expect_true(result.metrics.eval_mutations == 0u, "chatworld frozen eval has zero mutations");
    expect_true(result.metrics.fallback_count == 0u, "chatworld has zero fallback count");
    expect_true(result.metrics.oracle_label_count == 0u, "chatworld has zero oracle labels");
    expect_true(result.metrics.memory_write_count > 0u, "chatworld writes memory");
    expect_true(result.metrics.memory_read_count > 0u, "chatworld reads memory");
    expect_true(result.metrics.trace_count > 0u, "chatworld records traces");
    nerva_engine_free(&e);
}

static void test_chatworld_ablation_drops_eval(void) {
    NervaEngine full_e;
    NervaEngine ablate_e;
    ChatWorldConfig cfg;
    ChatWorldResult full;
    ChatWorldResult ablated;
    chatworld_config_defaults(&cfg);
    cfg.train_epochs = 24u;

    expect_true(nerva_engine_init(&full_e, nerva_config_test()) == 0, "chatworld full init");
    expect_true(chatworld_run(&full_e, &cfg, &full) == 0, "chatworld full run");
    nerva_engine_free(&full_e);

    cfg.ablate_response_edges = true;
    expect_true(nerva_engine_init(&ablate_e, nerva_config_test()) == 0, "chatworld ablate init");
    expect_true(chatworld_run(&ablate_e, &cfg, &ablated) == 0, "chatworld ablate run");
    expect_true(ablated.metrics.eval_correct < full.metrics.eval_correct,
                "chatworld response-edge ablation drops eval");
    expect_true(ablated.metrics.no_supported_response_count > full.metrics.no_supported_response_count,
                "chatworld ablation produces no-supported responses");
    nerva_engine_free(&ablate_e);
}

int test_chatworld_run(void) {
    g_failures = 0;
    test_chatworld_surface_adapter_no_oracle_labels();
    test_chatworld_zero_score_eval_has_no_fallback();
    test_chatworld_lite_learns_policy_and_memory();
    test_chatworld_ablation_drops_eval();
    return g_failures;
}
