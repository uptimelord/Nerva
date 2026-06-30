// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Paul Odantabao II

#include "chatworld.h"
#include "nerva_config.h"
#include "nerva_engine.h"
#include "nerva_graph.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;

static void expect_true(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        g_failures++;
    }
}

static int node_exists(const NervaEngine *e, const char *name) {
    return nerva_find_node_by_name(e, name) != UINT32_MAX;
}

static int trace_line_has_edges(const char *line) {
    const char *edges = strstr(line, "trace_edges=");
    if (!edges) {
        return 0;
    }
    edges += strlen("trace_edges=");
    return edges[0] != '\0' && edges[0] != '\r' && edges[0] != '\n';
}

static void set_one_turn_dataset(ChatWorldDataset *ds, ChatWorldTurn *turns,
                                 const char *utterance, ChatWorldExpected expected,
                                 const char *key, const char *value, bool learn) {
    memset(ds, 0, sizeof(*ds));
    memset(turns, 0, sizeof(*turns));
    strcpy(turns[0].utterance, utterance);
    turns[0].expected = expected;
    if (key) {
        strcpy(turns[0].expected_key, key);
    }
    if (value) {
        strcpy(turns[0].expected_value, value);
    }
    turns[0].learn = learn;
    ds->turns = turns;
    ds->count = 1u;
    ds->cap = 1u;
}

static void test_chatworld_surface_adapter_purist_nodes(void) {
    NervaEngine e;
    ChatWorldNerva cw;
    ChatWorldDataset ds;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "chatworld surface init");
    expect_true(chatworld_nerva_init(&e, &cw) == 0, "chatworld nerva init");
    expect_true(chatworld_load_dataset("worlds/chatworld/datasets/train.tsv", &ds) == 0,
                "chatworld dataset load");
    expect_true(chatworld_preload_dataset(&e, &cw, &ds) == 0, "chatworld preload train");
    expect_true(chatworld_emit_surface(&e, &cw, "my name is Ada") == 0,
                "chatworld emit surface");

    expect_true(node_exists(&e, "TOKEN:my"), "TOKEN:my emitted");
    expect_true(node_exists(&e, "TOKEN:name"), "TOKEN:name emitted");
    expect_true(node_exists(&e, "TOKEN:ada"), "TOKEN:ada emitted");
    expect_true(node_exists(&e, "TOKEN_AT:2:is"), "TOKEN_AT emitted");
    expect_true(node_exists(&e, "PAIR:name:is"), "adjacent pair emitted");
    expect_true(node_exists(&e, "SPEAKER:user"), "speaker emitted");
    expect_true(node_exists(&e, "BIND_KEY"), "BIND_KEY node exists");
    expect_true(node_exists(&e, "BIND_VALUE"), "BIND_VALUE node exists");
    expect_true(node_exists(&e, "KEY_CANDIDATE:pos1"), "key candidate node exists");
    expect_true(node_exists(&e, "VALUE_CANDIDATE:pos3"), "value candidate node exists");
    expect_true(node_exists(&e, "ACTION:MEM_WRITE"), "MEM_WRITE action node exists");
    expect_true(node_exists(&e, "ACTION:MEM_READ"), "MEM_READ action node exists");
    expect_true(node_exists(&e, "ACTION:RESP_UNKNOWN"), "RESP_UNKNOWN action node exists");
    expect_true(node_exists(&e, "OUTPUT_TOKEN:ada"), "OUTPUT_TOKEN:ada exists");

    for (uint32_t i = 0; i < e.name_count; ++i) {
        const char *name = e.names[i];
        expect_true(strstr(name, "INTENT") == NULL, "no INTENT labels");
        expect_true(strstr(name, "SLOT") == NULL, "no SLOT labels");
        expect_true(strstr(name, "CORRECT") == NULL, "no CORRECT labels");
        expect_true(strstr(name, "ANSWER_LABEL") == NULL, "no ANSWER_LABEL labels");
        expect_true(strstr(name, "FACT_QUERY") == NULL, "no FACT_QUERY labels");
        expect_true(strstr(name, "QUERY_TYPE") == NULL, "no QUERY_TYPE labels");
        expect_true(strstr(name, "USER_WANTS") == NULL, "no USER_WANTS labels");
        expect_true(strstr(name, "SHOULD_RESPOND") == NULL, "no SHOULD_RESPOND labels");
        expect_true(strstr(name, "EXPECTED_REPLY") == NULL, "no EXPECTED_REPLY labels");
        expect_true(strstr(name, "GROUND_TRUTH") == NULL, "no GROUND_TRUTH labels");
        expect_true(strstr(name, "CANONICAL_ANSWER") == NULL, "no CANONICAL_ANSWER labels");
        expect_true(strstr(name, "TASK_LABEL") == NULL, "no TASK_LABEL labels");
    }
    nerva_engine_free(&e);
}

static void test_chatworld_dataset_files_load(void) {
    ChatWorldDataset train;
    ChatWorldDataset frozen;
    expect_true(chatworld_load_dataset("worlds/chatworld/datasets/train.tsv", &train) == 0,
                "chatworld train dataset loads");
    expect_true(chatworld_load_dataset("worlds/chatworld/datasets/frozen.tsv", &frozen) == 0,
                "chatworld frozen dataset loads");
    expect_true(train.count >= 8u, "chatworld train dataset has turns");
    expect_true(frozen.count >= 6u, "chatworld frozen dataset has turns");
    expect_true(strcmp(train.turns[0].utterance, "hello") == 0, "train utterance parsed");
    expect_true(frozen.turns[0].learn == false, "frozen first turn is eval-only");
}

static void test_chatworld_zero_score_eval_has_no_fallback(void) {
    NervaEngine e;
    ChatWorldNerva cw;
    ChatWorld w;
    ChatWorldTurn turn;
    memset(&turn, 0, sizeof(turn));
    strcpy(turn.utterance, "what is my favorite food");
    turn.expected = CHAT_EXPECT_UNKNOWN;
    chatworld_reset(&w);
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "chatworld zero init");
    expect_true(chatworld_nerva_init(&e, &cw) == 0, "chatworld zero nerva init");
    ChatWorldDecision d = chatworld_step(&e, &cw, &w, &turn);
    expect_true(d.no_supported_response, "zero-score eval reports no supported response");
    expect_true(d.action == CHAT_ACTION_COUNT, "zero-score eval selects no fallback action");
    expect_true(strcmp(d.rendered, "") == 0, "zero-score eval renders no token");
    nerva_engine_free(&e);
}

static void test_chatworld_stage1_memory_heartbeat(void) {
    const char *train_path = "build/chatworld_stage1_train.tsv";
    const char *frozen_path = "build/chatworld_stage1_frozen.tsv";
    const char *trace_path = "build/chatworld_stage1_trace.log";
    FILE *f = fopen(train_path, "w");
    expect_true(f != NULL, "chatworld stage1 train file opens");
    if (f) {
        fputs("# utterance\texpected\tkey\tvalue\tlearn\n", f);
        fputs("my name is Ada\tACK\tname\tada\t1\n", f);
        fputs("what is my name\tMEMORY_VALUE\tname\tada\t1\n", f);
        fclose(f);
    }
    f = fopen(frozen_path, "w");
    expect_true(f != NULL, "chatworld stage1 frozen file opens");
    if (f) {
        fputs("# utterance\texpected\tkey\tvalue\tlearn\n", f);
        fputs("what is my name\tMEMORY_VALUE\tname\tada\t0\n", f);
        fclose(f);
    }

    NervaEngine e;
    ChatWorldConfig cfg;
    ChatWorldResult result;
    chatworld_config_defaults(&cfg);
    cfg.train_path = train_path;
    cfg.frozen_path = frozen_path;
    cfg.trace_path = trace_path;
    cfg.train_epochs = 40u;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "chatworld stage1 init");
    expect_true(chatworld_run(&e, &cfg, &result) == 0, "chatworld stage1 run");
    expect_true(result.metrics.train_total == 80u, "chatworld stage1 training ran");
    expect_true(result.metrics.eval_total == 1u, "chatworld stage1 eval ran");
    expect_true(result.metrics.eval_correct == 1u, "chatworld stage1 read returns Ada");
    expect_true(result.metrics.eval_mutations == 0u, "chatworld stage1 frozen has zero mutations");
    expect_true(result.metrics.fallback_count == 0u, "chatworld stage1 has no ambiguity");
    expect_true(result.metrics.no_supported_response_count == 0u,
                "chatworld stage1 has no unsupported response");
    nerva_engine_free(&e);

    f = fopen(trace_path, "r");
    expect_true(f != NULL, "chatworld stage1 trace opens");
    if (f) {
        char line[768];
        int saw_write = 0;
        int saw_read = 0;
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, "phase=train utterance=\"my name is Ada\"") &&
                strstr(line, "fired_action=ACTION:MEM_WRITE") &&
                strstr(line, "key=\"name\"") && strstr(line, "value=\"ada\"")) {
                saw_write = 1;
            }
            if (strstr(line, "phase=frozen utterance=\"what is my name\"") &&
                strstr(line, "fired_action=ACTION:MEM_READ") &&
                strstr(line, "rendered=\"ada\"") && strstr(line, "mutation_delta=0")) {
                saw_read = 1;
            }
        }
        fclose(f);
        expect_true(saw_write, "chatworld stage1 trace shows MEM_WRITE");
        expect_true(saw_read, "chatworld stage1 trace shows frozen MEM_READ output");
    }
}

static void test_chatworld_stage2_held_out_same_template_entities(void) {
    const char *train_path = "build/chatworld_stage2_train.tsv";
    const char *frozen_path = "build/chatworld_stage2_frozen.tsv";
    const char *trace_path = "build/chatworld_stage2_trace.log";
    FILE *f = fopen(train_path, "w");
    expect_true(f != NULL, "chatworld stage2 train file opens");
    if (f) {
        fputs("# utterance\texpected\tkey\tvalue\tlearn\n", f);
        fputs("my name is Ada\tACK\tname\tada\t1\n", f);
        fputs("what is my name\tMEMORY_VALUE\tname\tada\t1\n", f);
        fputs("my name is Bob\tACK\tname\tbob\t1\n", f);
        fputs("what is my name\tMEMORY_VALUE\tname\tbob\t1\n", f);
        fclose(f);
    }
    f = fopen(frozen_path, "w");
    expect_true(f != NULL, "chatworld stage2 frozen file opens");
    if (f) {
        fputs("# utterance\texpected\tkey\tvalue\tlearn\n", f);
        fputs("my name is Grace\tACK\tname\tgrace\t0\n", f);
        fputs("what is my name\tMEMORY_VALUE\tname\tgrace\t0\n", f);
        fputs("my name is Turing\tACK\tname\tturing\t0\n", f);
        fputs("what is my name\tMEMORY_VALUE\tname\tturing\t0\n", f);
        fclose(f);
    }

    NervaEngine e;
    ChatWorldNerva preload_cw;
    ChatWorldDataset train_ds;
    ChatWorldDataset frozen_ds;
    ChatWorldConfig cfg;
    ChatWorldResult result;
    chatworld_config_defaults(&cfg);
    cfg.train_path = train_path;
    cfg.frozen_path = frozen_path;
    cfg.trace_path = trace_path;
    cfg.train_epochs = 40u;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "chatworld stage2 init");
    expect_true(chatworld_nerva_init(&e, &preload_cw) == 0, "chatworld stage2 preload cw init");
    expect_true(chatworld_load_dataset(train_path, &train_ds) == 0,
                "chatworld stage2 preload train load");
    expect_true(chatworld_load_dataset(frozen_path, &frozen_ds) == 0,
                "chatworld stage2 preload frozen load");
    expect_true(chatworld_preload_dataset(&e, &preload_cw, &train_ds) == 0,
                "chatworld stage2 preload train vocabulary");
    expect_true(chatworld_preload_dataset(&e, &preload_cw, &frozen_ds) == 0,
                "chatworld stage2 preload frozen vocabulary");

    uint32_t node_count = e.node_count;
    uint32_t edge_count = e.edge_count;
    uint32_t name_count = e.name_count;

    expect_true(chatworld_run(&e, &cfg, &result) == 0, "chatworld stage2 run");
    expect_true(result.metrics.train_total == 160u, "chatworld stage2 training ran");
    expect_true(result.metrics.eval_total == 4u, "chatworld stage2 eval ran");
    expect_true(result.metrics.eval_correct == 4u, "chatworld stage2 held-out entities pass");
    expect_true(result.metrics.eval_mutations == 0u, "chatworld stage2 frozen has zero mutations");
    expect_true(result.metrics.fallback_count == 0u, "chatworld stage2 has no ambiguity");
    expect_true(result.metrics.no_supported_response_count == 0u,
                "chatworld stage2 has no unsupported response");
    expect_true(e.node_count == node_count, "chatworld stage2 train/eval does not add nodes");
    expect_true(e.edge_count == edge_count, "chatworld stage2 train/eval does not add edges");
    expect_true(e.name_count == name_count, "chatworld stage2 train/eval does not add names");
    nerva_engine_free(&e);

    f = fopen(trace_path, "r");
    expect_true(f != NULL, "chatworld stage2 trace opens");
    if (f) {
        char line[768];
        int saw_grace_write = 0;
        int saw_grace_read = 0;
        int saw_turing_write = 0;
        int saw_turing_read = 0;
        int saw_stale_read = 0;
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, "phase=frozen utterance=\"my name is Grace\"") &&
                strstr(line, "fired_action=ACTION:MEM_WRITE") &&
                strstr(line, "key=\"name\"") && strstr(line, "value=\"grace\"") &&
                strstr(line, "mutation_delta=0")) {
                saw_grace_write = 1;
            }
            if (strstr(line, "phase=frozen utterance=\"what is my name\"") &&
                strstr(line, "fired_action=ACTION:MEM_READ") &&
                strstr(line, "rendered=\"grace\"") && strstr(line, "mutation_delta=0")) {
                saw_grace_read = 1;
                if (strstr(line, "rendered=\"ada\"") || strstr(line, "rendered=\"bob\"") ||
                    strstr(line, "rendered=\"turing\"")) {
                    saw_stale_read = 1;
                }
            }
            if (strstr(line, "phase=frozen utterance=\"my name is Turing\"") &&
                strstr(line, "fired_action=ACTION:MEM_WRITE") &&
                strstr(line, "key=\"name\"") && strstr(line, "value=\"turing\"") &&
                strstr(line, "mutation_delta=0")) {
                saw_turing_write = 1;
            }
            if (strstr(line, "phase=frozen utterance=\"what is my name\"") &&
                strstr(line, "fired_action=ACTION:MEM_READ") &&
                strstr(line, "rendered=\"turing\"") && strstr(line, "mutation_delta=0")) {
                saw_turing_read = 1;
                if (strstr(line, "rendered=\"ada\"") || strstr(line, "rendered=\"bob\"") ||
                    strstr(line, "rendered=\"grace\"")) {
                    saw_stale_read = 1;
                }
            }
        }
        fclose(f);
        expect_true(saw_grace_write, "chatworld stage2 trace shows Grace MEM_WRITE");
        expect_true(saw_grace_read, "chatworld stage2 trace shows Grace MEM_READ output");
        expect_true(saw_turing_write, "chatworld stage2 trace shows Turing MEM_WRITE");
        expect_true(saw_turing_read, "chatworld stage2 trace shows Turing MEM_READ output");
        expect_true(!saw_stale_read, "chatworld stage2 does not leak stale held-out value");
    }
}

static void test_chatworld_stage3_response_behaviors_are_learned_paths(void) {
    const char *train_path = "build/chatworld_stage3_train.tsv";
    const char *frozen_path = "build/chatworld_stage3_frozen.tsv";
    const char *trace_path = "build/chatworld_stage3_trace.log";
    FILE *f = fopen(train_path, "w");
    expect_true(f != NULL, "chatworld stage3 train file opens");
    if (f) {
        fputs("# utterance\texpected\tkey\tvalue\tlearn\n", f);
        fputs("hello\tGREET\t\t\t1\n", f);
        fputs("thanks\tACK\t\t\t1\n", f);
        fputs("what is my favorite food\tUNKNOWN\t\t\t1\n", f);
        fclose(f);
    }
    f = fopen(frozen_path, "w");
    expect_true(f != NULL, "chatworld stage3 frozen file opens");
    if (f) {
        fputs("# utterance\texpected\tkey\tvalue\tlearn\n", f);
        fputs("hello\tGREET\t\t\t0\n", f);
        fputs("thanks\tACK\t\t\t0\n", f);
        fputs("what is my favorite color\tUNKNOWN\t\t\t0\n", f);
        fclose(f);
    }

    NervaEngine e;
    ChatWorldNerva preload_cw;
    ChatWorldDataset train_ds;
    ChatWorldDataset frozen_ds;
    ChatWorldConfig cfg;
    ChatWorldResult result;
    chatworld_config_defaults(&cfg);
    cfg.train_path = train_path;
    cfg.frozen_path = frozen_path;
    cfg.trace_path = trace_path;
    cfg.train_epochs = 40u;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "chatworld stage3 init");
    expect_true(chatworld_nerva_init(&e, &preload_cw) == 0, "chatworld stage3 preload cw init");
    expect_true(chatworld_load_dataset(train_path, &train_ds) == 0,
                "chatworld stage3 preload train load");
    expect_true(chatworld_load_dataset(frozen_path, &frozen_ds) == 0,
                "chatworld stage3 preload frozen load");
    expect_true(chatworld_preload_dataset(&e, &preload_cw, &train_ds) == 0,
                "chatworld stage3 preload train vocabulary");
    expect_true(chatworld_preload_dataset(&e, &preload_cw, &frozen_ds) == 0,
                "chatworld stage3 preload frozen vocabulary");

    uint32_t node_count = e.node_count;
    uint32_t edge_count = e.edge_count;
    uint32_t name_count = e.name_count;

    expect_true(chatworld_run(&e, &cfg, &result) == 0, "chatworld stage3 run");
    expect_true(result.metrics.train_total == 120u, "chatworld stage3 training ran");
    expect_true(result.metrics.eval_total == 3u, "chatworld stage3 eval ran");
    expect_true(result.metrics.eval_correct == 3u, "chatworld stage3 response eval passes");
    expect_true(result.metrics.eval_mutations == 0u, "chatworld stage3 frozen has zero mutations");
    expect_true(result.metrics.fallback_count == 0u, "chatworld stage3 has no ambiguity");
    expect_true(result.metrics.no_supported_response_count == 0u,
                "chatworld stage3 has no unsupported response");
    expect_true(result.metrics.memory_write_count == 0u, "chatworld stage3 does not write memory");
    expect_true(result.metrics.memory_read_count == 0u, "chatworld stage3 does not read memory");
    expect_true(e.node_count == node_count, "chatworld stage3 train/eval does not add nodes");
    expect_true(e.edge_count == edge_count, "chatworld stage3 train/eval does not add edges");
    expect_true(e.name_count == name_count, "chatworld stage3 train/eval does not add names");
    nerva_engine_free(&e);

    NervaEngine ablate_e;
    ChatWorldConfig ablate_cfg = cfg;
    ChatWorldResult ablated;
    ablate_cfg.trace_path = NULL;
    ablate_cfg.ablate_response_edges = true;
    expect_true(nerva_engine_init(&ablate_e, nerva_config_test()) == 0,
                "chatworld stage3 ablate init");
    expect_true(chatworld_run(&ablate_e, &ablate_cfg, &ablated) == 0,
                "chatworld stage3 ablate run");
    expect_true(ablated.metrics.eval_total == 3u, "chatworld stage3 ablate eval ran");
    expect_true(ablated.metrics.eval_correct == 0u,
                "chatworld stage3 response-edge ablation collapses eval");
    expect_true(ablated.metrics.no_supported_response_count == ablated.metrics.eval_total,
                "chatworld stage3 ablation produces only no-supported responses");
    nerva_engine_free(&ablate_e);

    f = fopen(trace_path, "r");
    expect_true(f != NULL, "chatworld stage3 trace opens");
    if (f) {
        char line[768];
        int saw_hello = 0;
        int saw_ack = 0;
        int saw_unknown = 0;
        int saw_bad_response = 0;
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, "phase=frozen utterance=\"hello\"")) {
                if (strstr(line, "fired_action=NO_ACTION") &&
                    strstr(line, "frame=OUTPUT") && strstr(line, "rendered=\"hello\"") &&
                    strstr(line, "fired_outputs=1") && strstr(line, "mutation_delta=0") &&
                    trace_line_has_edges(line)) {
                    saw_hello = 1;
                } else {
                    saw_bad_response = 1;
                }
            }
            if (strstr(line, "phase=frozen utterance=\"thanks\"")) {
                if (strstr(line, "fired_action=NO_ACTION") &&
                    strstr(line, "frame=OUTPUT") && strstr(line, "rendered=\"ok\"") &&
                    strstr(line, "fired_outputs=1") && strstr(line, "mutation_delta=0") &&
                    trace_line_has_edges(line)) {
                    saw_ack = 1;
                } else {
                    saw_bad_response = 1;
                }
            }
            if (strstr(line, "phase=frozen utterance=\"what is my favorite color\"")) {
                if (strstr(line, "fired_action=ACTION:RESP_UNKNOWN") &&
                    strstr(line, "frame=RESP_UNKNOWN") &&
                    strstr(line, "rendered=\"unknown\"") && strstr(line, "fired_actions=1") &&
                    strstr(line, "fired_outputs=1") && strstr(line, "resp_unknown=1") &&
                    strstr(line, "mem_write=0") && strstr(line, "mem_read=0") &&
                    strstr(line, "mutation_delta=0") && trace_line_has_edges(line)) {
                    saw_unknown = 1;
                } else {
                    saw_bad_response = 1;
                }
            }
        }
        fclose(f);
        expect_true(saw_hello, "chatworld stage3 trace shows learned greeting output");
        expect_true(saw_ack, "chatworld stage3 trace shows learned acknowledgement output");
        expect_true(saw_unknown, "chatworld stage3 trace shows learned RESP_UNKNOWN output");
        expect_true(!saw_bad_response, "chatworld stage3 trace has no response handholding leak");
    }
}

static void test_chatworld_stage3_unknown_requires_learned_support(void) {
    NervaEngine e;
    ChatWorldNerva cw;
    ChatWorld w;
    ChatWorldDataset ds;
    ChatWorldTurn turns[1];
    ChatWorldTurn turn;
    memset(&turn, 0, sizeof(turn));
    set_one_turn_dataset(&ds, turns, "what is my favorite color", CHAT_EXPECT_UNKNOWN, "", "",
                         false);
    strcpy(turn.utterance, "what is my favorite color");
    turn.expected = CHAT_EXPECT_UNKNOWN;

    chatworld_reset(&w);
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0,
                "chatworld stage3 unsupported init");
    expect_true(chatworld_nerva_init(&e, &cw) == 0,
                "chatworld stage3 unsupported cw init");
    expect_true(chatworld_preload_dataset(&e, &cw, &ds) == 0,
                "chatworld stage3 unsupported preload");
    ChatWorldDecision d = chatworld_step(&e, &cw, &w, &turn);
    expect_true(d.no_supported_response,
                "chatworld stage3 unsupported unknown has no learned support");
    expect_true(!d.resp_unknown_fired, "chatworld stage3 unsupported does not fire RESP_UNKNOWN");
    expect_true(!d.output_fired, "chatworld stage3 unsupported renders no output token");
    expect_true(strcmp(d.rendered, "") == 0, "chatworld stage3 unsupported renders empty");
    nerva_engine_free(&e);
}

static void test_chatworld_stage4_correction_overrides_prior_value(void) {
    const char *train_path = "build/chatworld_stage4_train.tsv";
    const char *frozen_path = "build/chatworld_stage4_frozen.tsv";
    const char *trace_path = "build/chatworld_stage4_trace.log";
    FILE *f = fopen(train_path, "w");
    expect_true(f != NULL, "chatworld stage4 train file opens");
    if (f) {
        fputs("# utterance\texpected\tkey\tvalue\tlearn\n", f);
        fputs("my name is Ada\tACK\tname\tada\t1\n", f);
        fputs("what is my name\tMEMORY_VALUE\tname\tada\t1\n", f);
        fputs("no my name is Grace\tACK\tname\tgrace\t1\n", f);
        fclose(f);
    }
    f = fopen(frozen_path, "w");
    expect_true(f != NULL, "chatworld stage4 frozen file opens");
    if (f) {
        fputs("# utterance\texpected\tkey\tvalue\tlearn\n", f);
        fputs("what is my name\tMEMORY_VALUE\tname\tgrace\t0\n", f);
        fclose(f);
    }

    NervaEngine e;
    ChatWorldNerva preload_cw;
    ChatWorldDataset train_ds;
    ChatWorldDataset frozen_ds;
    ChatWorldConfig cfg;
    ChatWorldResult result;
    chatworld_config_defaults(&cfg);
    cfg.train_path = train_path;
    cfg.frozen_path = frozen_path;
    cfg.trace_path = trace_path;
    cfg.train_epochs = 40u;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "chatworld stage4 init");
    expect_true(chatworld_nerva_init(&e, &preload_cw) == 0, "chatworld stage4 preload cw init");
    expect_true(chatworld_load_dataset(train_path, &train_ds) == 0,
                "chatworld stage4 preload train load");
    expect_true(chatworld_load_dataset(frozen_path, &frozen_ds) == 0,
                "chatworld stage4 preload frozen load");
    expect_true(chatworld_preload_dataset(&e, &preload_cw, &train_ds) == 0,
                "chatworld stage4 preload train vocabulary");
    expect_true(chatworld_preload_dataset(&e, &preload_cw, &frozen_ds) == 0,
                "chatworld stage4 preload frozen vocabulary");

    uint32_t node_count = e.node_count;
    uint32_t edge_count = e.edge_count;
    uint32_t name_count = e.name_count;

    expect_true(chatworld_run(&e, &cfg, &result) == 0, "chatworld stage4 run");
    expect_true(result.metrics.train_total == 120u, "chatworld stage4 training ran");
    expect_true(result.metrics.eval_total == 1u, "chatworld stage4 eval ran");
    expect_true(result.metrics.eval_correct == 1u, "chatworld stage4 correction wins");
    expect_true(result.metrics.eval_mutations == 0u, "chatworld stage4 frozen has zero mutations");
    expect_true(result.metrics.fallback_count == 0u, "chatworld stage4 has no ambiguity");
    expect_true(result.metrics.no_supported_response_count == 0u,
                "chatworld stage4 has no unsupported response");
    expect_true(e.node_count == node_count, "chatworld stage4 train/eval does not add nodes");
    expect_true(e.edge_count == edge_count, "chatworld stage4 train/eval does not add edges");
    expect_true(e.name_count == name_count, "chatworld stage4 train/eval does not add names");
    nerva_engine_free(&e);

    NervaEngine ablate_e;
    ChatWorldConfig ablate_cfg = cfg;
    ChatWorldResult ablated;
    ablate_cfg.trace_path = NULL;
    ablate_cfg.ablate_response_edges = true;
    expect_true(nerva_engine_init(&ablate_e, nerva_config_test()) == 0,
                "chatworld stage4 ablate init");
    expect_true(chatworld_run(&ablate_e, &ablate_cfg, &ablated) == 0,
                "chatworld stage4 ablate run");
    expect_true(ablated.metrics.eval_total == 1u, "chatworld stage4 ablate eval ran");
    expect_true(ablated.metrics.eval_correct == 0u,
                "chatworld stage4 learned-edge ablation collapses correction read");
    expect_true(ablated.metrics.no_supported_response_count == 1u,
                "chatworld stage4 ablation produces no-supported correction read");
    nerva_engine_free(&ablate_e);

    f = fopen(trace_path, "r");
    expect_true(f != NULL, "chatworld stage4 trace opens");
    if (f) {
        char line[768];
        int saw_ada_write = 0;
        int saw_ada_read = 0;
        int saw_grace_write = 0;
        int saw_grace_read = 0;
        int saw_stale_frozen_value = 0;
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, "phase=train utterance=\"my name is Ada\"") &&
                strstr(line, "fired_action=ACTION:MEM_WRITE") &&
                strstr(line, "key=\"name\"") && strstr(line, "value=\"ada\"") &&
                trace_line_has_edges(line)) {
                saw_ada_write = 1;
            }
            if (strstr(line, "phase=train utterance=\"what is my name\"") &&
                strstr(line, "fired_action=ACTION:MEM_READ") &&
                strstr(line, "rendered=\"ada\"") && trace_line_has_edges(line)) {
                saw_ada_read = 1;
            }
            if (strstr(line, "phase=train utterance=\"no my name is Grace\"") &&
                strstr(line, "fired_action=ACTION:MEM_WRITE") &&
                strstr(line, "key=\"name\"") && strstr(line, "value=\"grace\"") &&
                trace_line_has_edges(line)) {
                saw_grace_write = 1;
            }
            if (strstr(line, "phase=frozen utterance=\"what is my name\"")) {
                if (strstr(line, "fired_action=ACTION:MEM_READ") &&
                    strstr(line, "frame=ANSWER_MEMORY") &&
                    strstr(line, "rendered=\"grace\"") && strstr(line, "value=\"grace\"") &&
                    strstr(line, "mutation_delta=0") && strstr(line, "fired_outputs=1") &&
                    strstr(line, "mem_read=1") && trace_line_has_edges(line)) {
                    saw_grace_read = 1;
                }
                if (strstr(line, "rendered=\"ada\"") || strstr(line, "value=\"ada\"")) {
                    saw_stale_frozen_value = 1;
                }
            }
        }
        fclose(f);
        expect_true(saw_ada_write, "chatworld stage4 trace shows initial Ada MEM_WRITE");
        expect_true(saw_ada_read, "chatworld stage4 trace shows Ada was readable before correction");
        expect_true(saw_grace_write, "chatworld stage4 trace shows Grace correction MEM_WRITE");
        expect_true(saw_grace_read, "chatworld stage4 trace shows corrected Grace MEM_READ");
        expect_true(!saw_stale_frozen_value, "chatworld stage4 frozen read does not leak Ada");
    }
}

static void test_chatworld_stage5_paraphrase_write_chunk_paths(void) {
    const char *train_path = "build/chatworld_stage5_train.tsv";
    const char *frozen_path = "build/chatworld_stage5_frozen.tsv";
    const char *trace_path = "build/chatworld_stage5_trace.log";
    const char *ablate_trace_path = "build/chatworld_stage5_ablate_trace.log";
    FILE *f = fopen(train_path, "w");
    expect_true(f != NULL, "chatworld stage5 train file opens");
    if (f) {
        fputs("# utterance\texpected\tkey\tvalue\tlearn\n", f);
        fputs("my name is Ada\tACK\tname\tada\t1\n", f);
        fputs("I am called Bob\tACK\tname\tbob\t1\n", f);
        fputs("call me Grace\tACK\tname\tgrace\t1\n", f);
        fputs("what is my name\tMEMORY_VALUE\tname\tgrace\t1\n", f);
        fclose(f);
    }
    f = fopen(frozen_path, "w");
    expect_true(f != NULL, "chatworld stage5 frozen file opens");
    if (f) {
        fputs("# utterance\texpected\tkey\tvalue\tlearn\n", f);
        fputs("what is my name\tMEMORY_VALUE\tname\tgrace\t0\n", f);
        fclose(f);
    }

    NervaEngine e;
    ChatWorldNerva preload_cw;
    ChatWorldDataset train_ds;
    ChatWorldDataset frozen_ds;
    ChatWorldConfig cfg;
    ChatWorldResult result;
    chatworld_config_defaults(&cfg);
    cfg.train_path = train_path;
    cfg.frozen_path = frozen_path;
    cfg.trace_path = trace_path;
    cfg.train_epochs = 40u;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "chatworld stage5 init");
    expect_true(chatworld_nerva_init(&e, &preload_cw) == 0, "chatworld stage5 preload cw init");
    expect_true(chatworld_load_dataset(train_path, &train_ds) == 0,
                "chatworld stage5 preload train load");
    expect_true(chatworld_load_dataset(frozen_path, &frozen_ds) == 0,
                "chatworld stage5 preload frozen load");
    expect_true(chatworld_preload_dataset(&e, &preload_cw, &train_ds) == 0,
                "chatworld stage5 preload train vocabulary");
    expect_true(chatworld_preload_dataset(&e, &preload_cw, &frozen_ds) == 0,
                "chatworld stage5 preload frozen vocabulary");

    uint32_t node_count = e.node_count;
    uint32_t edge_count = e.edge_count;
    uint32_t name_count = e.name_count;

    expect_true(chatworld_run(&e, &cfg, &result) == 0, "chatworld stage5 run");
    expect_true(result.metrics.train_total == 160u, "chatworld stage5 training ran");
    expect_true(result.metrics.train_correct > 0u, "chatworld stage5 train writes learn");
    expect_true(result.metrics.eval_total == 1u, "chatworld stage5 eval ran");
    expect_true(result.metrics.eval_correct == 1u, "chatworld stage5 paraphrase write wins");
    expect_true(result.metrics.eval_mutations == 0u, "chatworld stage5 frozen has zero mutations");
    expect_true(result.metrics.fallback_count == 0u, "chatworld stage5 has no ambiguity");
    expect_true(e.node_count == node_count, "chatworld stage5 train/eval does not add nodes");
    expect_true(e.edge_count == edge_count, "chatworld stage5 train/eval does not add edges");
    expect_true(e.name_count == name_count, "chatworld stage5 train/eval does not add names");
    nerva_engine_free(&e);

    NervaEngine ablate_e;
    ChatWorldConfig ablate_cfg = cfg;
    ChatWorldResult ablated;
    ablate_cfg.trace_path = ablate_trace_path;
    ablate_cfg.ablate_response_edges = true;
    expect_true(nerva_engine_init(&ablate_e, nerva_config_test()) == 0,
                "chatworld stage5 ablate init");
    expect_true(chatworld_run(&ablate_e, &ablate_cfg, &ablated) == 0,
                "chatworld stage5 ablate run");
    expect_true(ablated.metrics.eval_total == 1u, "chatworld stage5 ablate eval ran");
    expect_true(ablated.metrics.eval_correct == 0u,
                "chatworld stage5 learned-edge ablation collapses paraphrase read");
    nerva_engine_free(&ablate_e);

    f = fopen(trace_path, "r");
    expect_true(f != NULL, "chatworld stage5 trace opens");
    if (f) {
        char line[768];
        int saw_ada_write = 0;
        int saw_bob_write = 0;
        int saw_grace_write = 0;
        int saw_grace_read = 0;
        int saw_stale_frozen_value = 0;
        int saw_bad_frozen_support = 0;
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, "phase=train utterance=\"my name is Ada\"") &&
                strstr(line, "fired_action=ACTION:MEM_WRITE") &&
                strstr(line, "key=\"name\"") && strstr(line, "value=\"ada\"") &&
                trace_line_has_edges(line)) {
                saw_ada_write = 1;
            }
            if (strstr(line, "phase=train utterance=\"I am called Bob\"") &&
                strstr(line, "fired_action=ACTION:MEM_WRITE") &&
                strstr(line, "key=\"name\"") && strstr(line, "value=\"bob\"") &&
                trace_line_has_edges(line)) {
                saw_bob_write = 1;
            }
            if (strstr(line, "phase=train utterance=\"call me Grace\"") &&
                strstr(line, "fired_action=ACTION:MEM_WRITE") &&
                strstr(line, "key=\"name\"") && strstr(line, "value=\"grace\"") &&
                trace_line_has_edges(line)) {
                saw_grace_write = 1;
            }
            if (strstr(line, "phase=frozen utterance=\"what is my name\"")) {
                if (strstr(line, "fired_action=ACTION:MEM_READ") &&
                    strstr(line, "rendered=\"grace\"") && strstr(line, "value=\"grace\"") &&
                    strstr(line, "no_supported=0") &&
                    strstr(line, "mutation_delta=0") && trace_line_has_edges(line)) {
                    saw_grace_read = 1;
                }
                if (strstr(line, "no_supported=1") ||
                    strstr(line, "frame=NO_SUPPORTED_RESPONSE")) {
                    saw_bad_frozen_support = 1;
                }
                if (strstr(line, "rendered=\"ada\"") || strstr(line, "value=\"ada\"") ||
                    strstr(line, "rendered=\"bob\"") || strstr(line, "value=\"bob\"")) {
                    saw_stale_frozen_value = 1;
                }
            }
        }
        fclose(f);
        expect_true(saw_ada_write, "chatworld stage5 trace shows canonical write");
        expect_true(saw_bob_write, "chatworld stage5 trace shows called write");
        expect_true(saw_grace_write, "chatworld stage5 trace shows call-me write");
        expect_true(saw_grace_read, "chatworld stage5 trace shows final paraphrase value read");
        expect_true(!saw_bad_frozen_support,
                    "chatworld stage5 frozen read has learned support");
        expect_true(!saw_stale_frozen_value,
                    "chatworld stage5 frozen read does not leak older values");
    }

    f = fopen(ablate_trace_path, "r");
    expect_true(f != NULL, "chatworld stage5 ablate trace opens");
    if (f) {
        char line[768];
        int saw_ablate_no_supported = 0;
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, "phase=frozen utterance=\"what is my name\"") &&
                strstr(line, "no_supported=1") &&
                strstr(line, "frame=NO_SUPPORTED_RESPONSE")) {
                saw_ablate_no_supported = 1;
            }
        }
        fclose(f);
        expect_true(saw_ablate_no_supported,
                    "chatworld stage5 ablation produces no-supported paraphrase read");
    }
}

static void test_chatworld_stage5_1_held_out_paraphrase_values(void) {
    const char *train_path = "build/chatworld_stage5_1_train.tsv";
    const char *frozen_path = "build/chatworld_stage5_1_frozen.tsv";
    const char *trace_path = "build/chatworld_stage5_1_trace.log";
    const char *ablate_trace_path = "build/chatworld_stage5_1_ablate_trace.log";
    FILE *f = fopen(train_path, "w");
    expect_true(f != NULL, "chatworld stage5.1 train file opens");
    if (f) {
        fputs("# utterance\texpected\tkey\tvalue\tlearn\n", f);
        fputs("I am called Ada\tACK\tname\tada\t1\n", f);
        fputs("call me Bob\tACK\tname\tbob\t1\n", f);
        fputs("what is my name\tMEMORY_VALUE\tname\tbob\t1\n", f);
        fclose(f);
    }
    f = fopen(frozen_path, "w");
    expect_true(f != NULL, "chatworld stage5.1 frozen file opens");
    if (f) {
        fputs("# utterance\texpected\tkey\tvalue\tlearn\n", f);
        fputs("I am called Grace\tACK\tname\tgrace\t0\n", f);
        fputs("what is my name\tMEMORY_VALUE\tname\tgrace\t0\n", f);
        fputs("call me Turing\tACK\tname\tturing\t0\n", f);
        fputs("what is my name\tMEMORY_VALUE\tname\tturing\t0\n", f);
        fclose(f);
    }

    NervaEngine e;
    ChatWorldNerva preload_cw;
    ChatWorldDataset train_ds;
    ChatWorldDataset frozen_ds;
    ChatWorldConfig cfg;
    ChatWorldResult result;
    chatworld_config_defaults(&cfg);
    cfg.train_path = train_path;
    cfg.frozen_path = frozen_path;
    cfg.trace_path = trace_path;
    cfg.train_epochs = 40u;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "chatworld stage5.1 init");
    expect_true(chatworld_nerva_init(&e, &preload_cw) == 0, "chatworld stage5.1 preload cw init");
    expect_true(chatworld_load_dataset(train_path, &train_ds) == 0,
                "chatworld stage5.1 preload train load");
    expect_true(chatworld_load_dataset(frozen_path, &frozen_ds) == 0,
                "chatworld stage5.1 preload frozen load");
    expect_true(chatworld_preload_dataset(&e, &preload_cw, &train_ds) == 0,
                "chatworld stage5.1 preload train vocabulary");
    expect_true(chatworld_preload_dataset(&e, &preload_cw, &frozen_ds) == 0,
                "chatworld stage5.1 preload frozen vocabulary");

    uint32_t node_count = e.node_count;
    uint32_t edge_count = e.edge_count;
    uint32_t name_count = e.name_count;

    expect_true(chatworld_run(&e, &cfg, &result) == 0, "chatworld stage5.1 run");
    expect_true(result.metrics.train_total == 120u, "chatworld stage5.1 training ran");
    expect_true(result.metrics.train_correct > 0u, "chatworld stage5.1 train writes learn");
    expect_true(result.metrics.eval_total == 4u, "chatworld stage5.1 eval ran");
    expect_true(result.metrics.eval_correct == 4u, "chatworld stage5.1 held-out values pass");
    expect_true(result.metrics.eval_mutations == 0u, "chatworld stage5.1 frozen has zero mutations");
    expect_true(result.metrics.fallback_count == 0u, "chatworld stage5.1 has no ambiguity");
    expect_true(e.node_count == node_count, "chatworld stage5.1 train/eval does not add nodes");
    expect_true(e.edge_count == edge_count, "chatworld stage5.1 train/eval does not add edges");
    expect_true(e.name_count == name_count, "chatworld stage5.1 train/eval does not add names");
    nerva_engine_free(&e);

    NervaEngine ablate_e;
    ChatWorldConfig ablate_cfg = cfg;
    ChatWorldResult ablated;
    ablate_cfg.trace_path = ablate_trace_path;
    ablate_cfg.ablate_response_edges = true;
    expect_true(nerva_engine_init(&ablate_e, nerva_config_test()) == 0,
                "chatworld stage5.1 ablate init");
    expect_true(chatworld_run(&ablate_e, &ablate_cfg, &ablated) == 0,
                "chatworld stage5.1 ablate run");
    expect_true(ablated.metrics.eval_total == 4u, "chatworld stage5.1 ablate eval ran");
    expect_true(ablated.metrics.eval_correct == 0u,
                "chatworld stage5.1 learned-edge ablation collapses held-out values");
    nerva_engine_free(&ablate_e);

    f = fopen(trace_path, "r");
    expect_true(f != NULL, "chatworld stage5.1 trace opens");
    if (f) {
        char line[768];
        int saw_grace_write = 0;
        int saw_grace_read = 0;
        int saw_turing_write = 0;
        int saw_turing_read = 0;
        int saw_bad_frozen_support = 0;
        int after_turing_write = 0;
        int saw_stale_after_turing = 0;
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, "phase=frozen utterance=\"I am called Grace\"")) {
                if (strstr(line, "fired_action=ACTION:MEM_WRITE") &&
                    strstr(line, "frame=ACK") && strstr(line, "key=\"name\"") &&
                    strstr(line, "value=\"grace\"") && strstr(line, "no_supported=0") &&
                    strstr(line, "mutation_delta=0") && trace_line_has_edges(line)) {
                    saw_grace_write = 1;
                }
            }
            if (strstr(line, "phase=frozen utterance=\"call me Turing\"")) {
                if (strstr(line, "fired_action=ACTION:MEM_WRITE") &&
                    strstr(line, "frame=ACK") && strstr(line, "key=\"name\"") &&
                    strstr(line, "value=\"turing\"") && strstr(line, "no_supported=0") &&
                    strstr(line, "mutation_delta=0") && trace_line_has_edges(line)) {
                    saw_turing_write = 1;
                    after_turing_write = 1;
                }
            }
            if (strstr(line, "phase=frozen utterance=\"what is my name\"")) {
                if (strstr(line, "fired_action=ACTION:MEM_READ") &&
                    strstr(line, "rendered=\"grace\"") && strstr(line, "value=\"grace\"") &&
                    strstr(line, "no_supported=0") && strstr(line, "mutation_delta=0") &&
                    trace_line_has_edges(line)) {
                    saw_grace_read = 1;
                }
                if (strstr(line, "fired_action=ACTION:MEM_READ") &&
                    strstr(line, "rendered=\"turing\"") && strstr(line, "value=\"turing\"") &&
                    strstr(line, "no_supported=0") && strstr(line, "mutation_delta=0") &&
                    trace_line_has_edges(line)) {
                    saw_turing_read = 1;
                }
                if (after_turing_write &&
                    (strstr(line, "rendered=\"ada\"") || strstr(line, "value=\"ada\"") ||
                     strstr(line, "rendered=\"bob\"") || strstr(line, "value=\"bob\"") ||
                     strstr(line, "rendered=\"grace\"") || strstr(line, "value=\"grace\""))) {
                    saw_stale_after_turing = 1;
                }
            }
            if (strstr(line, "phase=frozen") &&
                (strstr(line, "no_supported=1") ||
                 strstr(line, "frame=NO_SUPPORTED_RESPONSE"))) {
                saw_bad_frozen_support = 1;
            }
        }
        fclose(f);
        expect_true(saw_grace_write, "chatworld stage5.1 trace shows held-out called write");
        expect_true(saw_grace_read, "chatworld stage5.1 trace shows held-out called read");
        expect_true(saw_turing_write, "chatworld stage5.1 trace shows held-out call-me write");
        expect_true(saw_turing_read, "chatworld stage5.1 trace shows held-out call-me read");
        expect_true(!saw_bad_frozen_support,
                    "chatworld stage5.1 frozen turns have learned support");
        expect_true(!saw_stale_after_turing,
                    "chatworld stage5.1 final read does not leak older values");
    }

    f = fopen(ablate_trace_path, "r");
    expect_true(f != NULL, "chatworld stage5.1 ablate trace opens");
    if (f) {
        char line[768];
        uint32_t no_supported_frozen = 0;
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, "phase=frozen") && strstr(line, "no_supported=1") &&
                strstr(line, "frame=NO_SUPPORTED_RESPONSE")) {
                no_supported_frozen++;
            }
        }
        fclose(f);
        expect_true(no_supported_frozen == 4u,
                    "chatworld stage5.1 ablation gives no-supported frozen turns");
    }
}

static void test_chatworld_stage5_2_mixed_write_shapes(void) {
    const char *train_path = "build/chatworld_stage5_2_train.tsv";
    const char *frozen_path = "build/chatworld_stage5_2_frozen.tsv";
    FILE *f = fopen(train_path, "w");
    expect_true(f != NULL, "chatworld stage5.2 train file opens");
    if (f) {
        fputs("# utterance\texpected\tkey\tvalue\tlearn\n", f);
        fputs("I am called Ada\tACK\tname\tada\t1\n", f);
        fputs("call me Bob\tACK\tname\tbob\t1\n", f);
        fputs("my name is Ada\tACK\tname\tada\t1\n", f);
        fputs("what is my name\tMEMORY_VALUE\tname\tada\t1\n", f);
        fclose(f);
    }
    f = fopen(frozen_path, "w");
    expect_true(f != NULL, "chatworld stage5.2 frozen file opens");
    if (f) {
        fputs("# utterance\texpected\tkey\tvalue\tlearn\n", f);
        fputs("my name is Grace\tACK\tname\tgrace\t0\n", f);
        fputs("what is my name\tMEMORY_VALUE\tname\tgrace\t0\n", f);
        fclose(f);
    }

    NervaEngine e;
    ChatWorldConfig cfg;
    ChatWorldResult result;
    chatworld_config_defaults(&cfg);
    cfg.train_path = train_path;
    cfg.frozen_path = frozen_path;
    cfg.train_epochs = 40u;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "chatworld stage5.2 init");
    expect_true(chatworld_run(&e, &cfg, &result) == 0, "chatworld stage5.2 run");
    expect_true(result.metrics.eval_total == 2u, "chatworld stage5.2 eval ran");
    expect_true(result.metrics.eval_correct == 2u, "chatworld stage5.2 mixed write passes");
    expect_true(result.metrics.eval_mutations == 0u, "chatworld stage5.2 frozen mutations zero");
    nerva_engine_free(&e);
}

static void test_chatworld_stage5_3_paraphrase_correction(void) {
    const char *train_path = "build/chatworld_stage5_3_train.tsv";
    const char *frozen_path = "build/chatworld_stage5_3_frozen.tsv";
    const char *trace_path = "build/chatworld_stage5_3_trace.log";
    FILE *f = fopen(train_path, "w");
    expect_true(f != NULL, "chatworld stage5.3 train file opens");
    if (f) {
        fputs("# utterance\texpected\tkey\tvalue\tlearn\n", f);
        fputs("I am called Ada\tACK\tname\tada\t1\n", f);
        fputs("call me Grace\tACK\tname\tgrace\t1\n", f);
        fputs("what is my name\tMEMORY_VALUE\tname\tgrace\t1\n", f);
        fclose(f);
    }
    f = fopen(frozen_path, "w");
    expect_true(f != NULL, "chatworld stage5.3 frozen file opens");
    if (f) {
        fputs("# utterance\texpected\tkey\tvalue\tlearn\n", f);
        fputs("what is my name\tMEMORY_VALUE\tname\tgrace\t0\n", f);
        fclose(f);
    }

    NervaEngine e;
    ChatWorldConfig cfg;
    ChatWorldResult result;
    chatworld_config_defaults(&cfg);
    cfg.train_path = train_path;
    cfg.frozen_path = frozen_path;
    cfg.trace_path = trace_path;
    cfg.train_epochs = 40u;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "chatworld stage5.3 init");
    expect_true(chatworld_run(&e, &cfg, &result) == 0, "chatworld stage5.3 run");
    expect_true(result.metrics.eval_correct == 1u, "chatworld stage5.3 correction wins");
    nerva_engine_free(&e);

    f = fopen(trace_path, "r");
    expect_true(f != NULL, "chatworld stage5.3 trace opens");
    if (f) {
        char line[768];
        int saw_grace = 0;
        int leaked_ada = 0;
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, "phase=frozen utterance=\"what is my name\"")) {
                if (strstr(line, "rendered=\"grace\"") && strstr(line, "value=\"grace\"")) {
                    saw_grace = 1;
                }
                if (strstr(line, "rendered=\"ada\"") || strstr(line, "value=\"ada\"")) {
                    leaked_ada = 1;
                }
            }
        }
        fclose(f);
        expect_true(saw_grace, "chatworld stage5.3 trace renders corrected value");
        expect_true(!leaked_ada, "chatworld stage5.3 trace does not leak old value");
    }
}

static void test_chatworld_stage5_4_unsupported_phrase_isolation(void) {
    const char *train_path = "build/chatworld_stage5_4_train.tsv";
    const char *frozen_path = "build/chatworld_stage5_4_frozen.tsv";
    FILE *f = fopen(train_path, "w");
    expect_true(f != NULL, "chatworld stage5.4 train file opens");
    if (f) {
        fputs("# utterance\texpected\tkey\tvalue\tlearn\n", f);
        fputs("call me Ada\tACK\tname\tada\t1\n", f);
        fputs("what is my name\tMEMORY_VALUE\tname\tada\t1\n", f);
        fclose(f);
    }
    f = fopen(frozen_path, "w");
    expect_true(f != NULL, "chatworld stage5.4 frozen file opens");
    if (f) {
        fputs("# utterance\texpected\tkey\tvalue\tlearn\n", f);
        fputs("call the office\tNO_SUPPORTED_RESPONSE\t\t\t0\n", f);
        fputs("called the number\tNO_SUPPORTED_RESPONSE\t\t\t0\n", f);
        fputs("name the file\tNO_SUPPORTED_RESPONSE\t\t\t0\n", f);
        fputs("what is my name\tMEMORY_VALUE\tname\tada\t0\n", f);
        fclose(f);
    }

    NervaEngine e;
    ChatWorldConfig cfg;
    ChatWorldResult result;
    chatworld_config_defaults(&cfg);
    cfg.train_path = train_path;
    cfg.frozen_path = frozen_path;
    cfg.train_epochs = 40u;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "chatworld stage5.4 init");
    expect_true(chatworld_run(&e, &cfg, &result) == 0, "chatworld stage5.4 run");
    expect_true(result.metrics.eval_total == 4u, "chatworld stage5.4 eval ran");
    expect_true(result.metrics.eval_correct == 4u, "chatworld stage5.4 isolation passes");
    expect_true(result.metrics.no_supported_response_count == 3u,
                "chatworld stage5.4 unsupported rows stay unsupported");
    nerva_engine_free(&e);
}

static void test_chatworld_stage6_1_read_paraphrases(void) {
    const char *train_path = "build/chatworld_stage6_1_train.tsv";
    const char *frozen_path = "build/chatworld_stage6_1_frozen.tsv";
    FILE *f = fopen(train_path, "w");
    expect_true(f != NULL, "chatworld stage6.1 train file opens");
    if (f) {
        fputs("# utterance\texpected\tkey\tvalue\tlearn\n", f);
        fputs("my name is Ada\tACK\tname\tada\t1\n", f);
        fputs("what is my name\tMEMORY_VALUE\tname\tada\t1\n", f);
        fputs("I am called Bob\tACK\tname\tbob\t1\n", f);
        fputs("who am I\tMEMORY_VALUE\tname\tbob\t1\n", f);
        fputs("call me Grace\tACK\tname\tgrace\t1\n", f);
        fputs("what am I called\tMEMORY_VALUE\tname\tgrace\t1\n", f);
        fclose(f);
    }
    f = fopen(frozen_path, "w");
    expect_true(f != NULL, "chatworld stage6.1 frozen file opens");
    if (f) {
        fputs("# utterance\texpected\tkey\tvalue\tlearn\n", f);
        fputs("call me Turing\tACK\tname\tturing\t0\n", f);
        fputs("who am I\tMEMORY_VALUE\tname\tturing\t0\n", f);
        fputs("what am I called\tMEMORY_VALUE\tname\tturing\t0\n", f);
        fclose(f);
    }

    NervaEngine e;
    ChatWorldConfig cfg;
    ChatWorldResult result;
    chatworld_config_defaults(&cfg);
    cfg.train_path = train_path;
    cfg.frozen_path = frozen_path;
    cfg.train_epochs = 40u;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "chatworld stage6.1 init");
    expect_true(chatworld_run(&e, &cfg, &result) == 0, "chatworld stage6.1 run");
    expect_true(result.metrics.eval_total == 3u, "chatworld stage6.1 eval ran");
    expect_true(result.metrics.eval_correct == 3u, "chatworld stage6.1 read paraphrases pass");
    expect_true(result.metrics.eval_mutations == 0u, "chatworld stage6.1 frozen mutations zero");
    nerva_engine_free(&e);
}

static void test_chatworld_phase3_training_memory_facts(void) {
    const char *train_path = "build/chatworld_phase3_train.tsv";
    const char *frozen_path = "build/chatworld_phase3_frozen.tsv";
    const char *trace_path = "build/chatworld_phase3_trace.log";
    FILE *f = fopen(train_path, "w");
    expect_true(f != NULL, "chatworld phase3 train file opens");
    if (f) {
        fputs("# utterance\texpected\tkey\tvalue\tlearn\n", f);
        fputs("my name is Ada\tACK\tname\tada\t1\n", f);
        fputs("what is my name\tMEMORY_VALUE\tname\tada\t1\n", f);
        fputs("poodle is dog\tACK\tpoodle\tdog\t1\n", f);
        fputs("what is poodle\tMEMORY_VALUE\tpoodle\tdog\t1\n", f);
        fputs("Paris is city\tACK\tparis\tcity\t1\n", f);
        fputs("what is Paris\tMEMORY_VALUE\tparis\tcity\t1\n", f);
        fclose(f);
    }
    f = fopen(frozen_path, "w");
    expect_true(f != NULL, "chatworld phase3 frozen file opens");
    if (f) {
        fputs("# utterance\texpected\tkey\tvalue\tlearn\n", f);
        fputs("what is my name\tMEMORY_VALUE\tname\tada\t0\n", f);
        fputs("what is poodle\tMEMORY_VALUE\tpoodle\tdog\t0\n", f);
        fputs("what is Paris\tMEMORY_VALUE\tparis\tcity\t0\n", f);
        fclose(f);
    }

    NervaEngine e;
    ChatWorldConfig cfg;
    ChatWorldResult result;
    chatworld_config_defaults(&cfg);
    cfg.train_path = train_path;
    cfg.frozen_path = frozen_path;
    cfg.trace_path = trace_path;
    cfg.train_epochs = 40u;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "chatworld phase3 init");
    expect_true(chatworld_run(&e, &cfg, &result) == 0, "chatworld phase3 run");
    expect_true(result.metrics.eval_total == 3u, "chatworld phase3 eval ran");
    expect_true(result.metrics.eval_correct == 3u, "chatworld phase3 training memory passes");
    expect_true(result.metrics.eval_mutations == 0u, "chatworld phase3 frozen mutations zero");
    expect_true(result.metrics.fallback_count == 0u, "chatworld phase3 frozen fallback zero");
    nerva_engine_free(&e);

    NervaEngine ablate_e;
    ChatWorldConfig ablate_cfg = cfg;
    ChatWorldResult ablated;
    ablate_cfg.trace_path = NULL;
    ablate_cfg.ablate_response_edges = true;
    expect_true(nerva_engine_init(&ablate_e, nerva_config_test()) == 0,
                "chatworld phase3 ablate init");
    expect_true(chatworld_run(&ablate_e, &ablate_cfg, &ablated) == 0,
                "chatworld phase3 ablate run");
    expect_true(ablated.metrics.eval_total == 3u, "chatworld phase3 ablate eval ran");
    expect_true(ablated.metrics.eval_correct == 0u,
                "chatworld phase3 learned-edge ablation collapses facts");
    nerva_engine_free(&ablate_e);

    f = fopen(trace_path, "r");
    expect_true(f != NULL, "chatworld phase3 trace opens");
    if (f) {
        char line[768];
        int saw_poodle = 0;
        int saw_paris = 0;
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, "phase=frozen utterance=\"what is poodle\"") &&
                strstr(line, "fired_action=ACTION:MEM_READ") &&
                strstr(line, "rendered=\"dog\"") && trace_line_has_edges(line)) {
                saw_poodle = 1;
            }
            if (strstr(line, "phase=frozen utterance=\"what is Paris\"") &&
                strstr(line, "fired_action=ACTION:MEM_READ") &&
                strstr(line, "rendered=\"city\"") && trace_line_has_edges(line)) {
                saw_paris = 1;
            }
        }
        fclose(f);
        expect_true(saw_poodle, "chatworld phase3 trace reads trained poodle fact");
        expect_true(saw_paris, "chatworld phase3 trace reads trained Paris fact");
    }
}

static void test_chatworld_phase4_multiple_keys_do_not_leak(void) {
    const char *train_path = "build/chatworld_phase4_train.tsv";
    const char *frozen_path = "build/chatworld_phase4_frozen.tsv";
    FILE *f = fopen(train_path, "w");
    expect_true(f != NULL, "chatworld phase4 train file opens");
    if (f) {
        fputs("# utterance\texpected\tkey\tvalue\tlearn\n", f);
        fputs("call me Ada\tACK\tname\tada\t1\n", f);
        fputs("what is my name\tMEMORY_VALUE\tname\tada\t1\n", f);
        fputs("I live in Rome\tACK\tcity\trome\t1\n", f);
        fputs("where do I live\tMEMORY_VALUE\tcity\trome\t1\n", f);
        fputs("my color is blue\tACK\tcolor\tblue\t1\n", f);
        fputs("what is my color\tMEMORY_VALUE\tcolor\tblue\t1\n", f);
        fputs("my pet is luna\tACK\tpet\tluna\t1\n", f);
        fputs("what is my pet\tMEMORY_VALUE\tpet\tluna\t1\n", f);
        fclose(f);
    }
    f = fopen(frozen_path, "w");
    expect_true(f != NULL, "chatworld phase4 frozen file opens");
    if (f) {
        fputs("# utterance\texpected\tkey\tvalue\tlearn\n", f);
        fputs("call me Turing\tACK\tname\tturing\t0\n", f);
        fputs("I live in Tokyo\tACK\tcity\ttokyo\t0\n", f);
        fputs("my color is green\tACK\tcolor\tgreen\t0\n", f);
        fputs("my pet is nova\tACK\tpet\tnova\t0\n", f);
        fputs("what is my name\tMEMORY_VALUE\tname\tturing\t0\n", f);
        fputs("where do I live\tMEMORY_VALUE\tcity\ttokyo\t0\n", f);
        fputs("what is my color\tMEMORY_VALUE\tcolor\tgreen\t0\n", f);
        fputs("what is my pet\tMEMORY_VALUE\tpet\tnova\t0\n", f);
        fclose(f);
    }

    NervaEngine e;
    ChatWorldConfig cfg;
    ChatWorldResult result;
    chatworld_config_defaults(&cfg);
    cfg.train_path = train_path;
    cfg.frozen_path = frozen_path;
    cfg.train_epochs = 40u;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "chatworld phase4 init");
    expect_true(chatworld_run(&e, &cfg, &result) == 0, "chatworld phase4 run");
    expect_true(result.metrics.eval_total == 8u, "chatworld phase4 eval ran");
    expect_true(result.metrics.eval_correct == 8u, "chatworld phase4 keys stay separated");
    expect_true(result.metrics.eval_mutations == 0u, "chatworld phase4 frozen mutations zero");
    expect_true(result.metrics.fallback_count == 0u, "chatworld phase4 frozen fallback zero");
    nerva_engine_free(&e);
}

static void test_chatworld_phase5_simple_facts_and_correction(void) {
    const char *train_path = "build/chatworld_phase5_train.tsv";
    const char *frozen_path = "build/chatworld_phase5_frozen.tsv";
    const char *trace_path = "build/chatworld_phase5_trace.log";
    FILE *f = fopen(train_path, "w");
    expect_true(f != NULL, "chatworld phase5 train file opens");
    if (f) {
        fputs("# utterance\texpected\tkey\tvalue\tlearn\n", f);
        fputs("poodle is dog\tACK\tpoodle\tdog\t1\n", f);
        fputs("what is poodle\tMEMORY_VALUE\tpoodle\tdog\t1\n", f);
        fputs("dog is animal\tACK\tdog\tanimal\t1\n", f);
        fputs("what is dog\tMEMORY_VALUE\tdog\tanimal\t1\n", f);
        fputs("Paris is city\tACK\tparis\tcity\t1\n", f);
        fputs("what is Paris\tMEMORY_VALUE\tparis\tcity\t1\n", f);
        fputs("no poodle is breed\tACK\tpoodle\tbreed\t1\n", f);
        fputs("what is poodle\tMEMORY_VALUE\tpoodle\tbreed\t1\n", f);
        fclose(f);
    }
    f = fopen(frozen_path, "w");
    expect_true(f != NULL, "chatworld phase5 frozen file opens");
    if (f) {
        fputs("# utterance\texpected\tkey\tvalue\tlearn\n", f);
        fputs("what is poodle\tMEMORY_VALUE\tpoodle\tbreed\t0\n", f);
        fputs("what is dog\tMEMORY_VALUE\tdog\tanimal\t0\n", f);
        fputs("what is Paris\tMEMORY_VALUE\tparis\tcity\t0\n", f);
        fclose(f);
    }

    NervaEngine e;
    ChatWorldConfig cfg;
    ChatWorldResult result;
    chatworld_config_defaults(&cfg);
    cfg.train_path = train_path;
    cfg.frozen_path = frozen_path;
    cfg.trace_path = trace_path;
    cfg.train_epochs = 40u;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "chatworld phase5 init");
    expect_true(chatworld_run(&e, &cfg, &result) == 0, "chatworld phase5 run");
    expect_true(result.metrics.eval_total == 3u, "chatworld phase5 eval ran");
    expect_true(result.metrics.eval_correct == 3u, "chatworld phase5 facts pass");
    expect_true(result.metrics.eval_mutations == 0u, "chatworld phase5 frozen mutations zero");
    nerva_engine_free(&e);

    f = fopen(trace_path, "r");
    expect_true(f != NULL, "chatworld phase5 trace opens");
    if (f) {
        char line[768];
        int saw_breed = 0;
        int leaked_dog = 0;
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, "phase=frozen utterance=\"what is poodle\"")) {
                if (strstr(line, "rendered=\"breed\"") && strstr(line, "value=\"breed\"")) {
                    saw_breed = 1;
                }
                if (strstr(line, "rendered=\"dog\"") || strstr(line, "value=\"dog\"")) {
                    leaked_dog = 1;
                }
            }
        }
        fclose(f);
        expect_true(saw_breed, "chatworld phase5 correction reads breed");
        expect_true(!leaked_dog, "chatworld phase5 old poodle value does not leak");
    }
}

static void test_chatworld_phase6_multi_token_values(void) {
    const char *train_path = "build/chatworld_phase6_train.tsv";
    const char *frozen_path = "build/chatworld_phase6_frozen.tsv";
    FILE *f = fopen(train_path, "w");
    expect_true(f != NULL, "chatworld phase6 train file opens");
    if (f) {
        fputs("# utterance\texpected\tkey\tvalue\tlearn\n", f);
        fputs("my name is Ada Lovelace\tACK\tname\tada lovelace\t1\n", f);
        fputs("what is my name\tMEMORY_VALUE\tname\tada lovelace\t1\n", f);
        fputs("I live in New York\tACK\tcity\tnew york\t1\n", f);
        fputs("where do I live\tMEMORY_VALUE\tcity\tnew york\t1\n", f);
        fclose(f);
    }
    f = fopen(frozen_path, "w");
    expect_true(f != NULL, "chatworld phase6 frozen file opens");
    if (f) {
        fputs("# utterance\texpected\tkey\tvalue\tlearn\n", f);
        fputs("what is my name\tMEMORY_VALUE\tname\tada lovelace\t0\n", f);
        fputs("where do I live\tMEMORY_VALUE\tcity\tnew york\t0\n", f);
        fclose(f);
    }

    NervaEngine e;
    ChatWorldConfig cfg;
    ChatWorldResult result;
    chatworld_config_defaults(&cfg);
    cfg.train_path = train_path;
    cfg.frozen_path = frozen_path;
    cfg.train_epochs = 40u;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "chatworld phase6 init");
    expect_true(chatworld_run(&e, &cfg, &result) == 0, "chatworld phase6 run");
    expect_true(result.metrics.eval_total == 2u, "chatworld phase6 eval ran");
    expect_true(result.metrics.eval_correct == 2u, "chatworld phase6 multi-token reads pass");
    expect_true(result.metrics.eval_mutations == 0u, "chatworld phase6 frozen mutations zero");
    nerva_engine_free(&e);
}

static void test_chatworld_promotion_mixed_recall_gate(void) {
    const char *train_path = "build/chatworld_promotion_train.tsv";
    const char *frozen_path = "build/chatworld_promotion_frozen.tsv";
    FILE *f = fopen(train_path, "w");
    expect_true(f != NULL, "chatworld promotion train file opens");
    if (f) {
        fputs("# utterance\texpected\tkey\tvalue\tlearn\n", f);
        fputs("my name is Ada\tACK\tname\tada\t1\n", f);
        fputs("what is my name\tMEMORY_VALUE\tname\tada\t1\n", f);
        fputs("poodle is dog\tACK\tpoodle\tdog\t1\n", f);
        fputs("what is poodle\tMEMORY_VALUE\tpoodle\tdog\t1\n", f);
        fputs("Paris is city\tACK\tparis\tcity\t1\n", f);
        fputs("what is Paris\tMEMORY_VALUE\tparis\tcity\t1\n", f);
        fputs("I live in Rome\tACK\tcity\trome\t1\n", f);
        fputs("where do I live\tMEMORY_VALUE\tcity\trome\t1\n", f);
        fclose(f);
    }
    f = fopen(frozen_path, "w");
    expect_true(f != NULL, "chatworld promotion frozen file opens");
    if (f) {
        fputs("# utterance\texpected\tkey\tvalue\tlearn\n", f);
        fputs("what is my name\tMEMORY_VALUE\tname\tada\t0\n", f);
        fputs("what is poodle\tMEMORY_VALUE\tpoodle\tdog\t0\n", f);
        fputs("what is Paris\tMEMORY_VALUE\tparis\tcity\t0\n", f);
        fputs("where do I live\tMEMORY_VALUE\tcity\trome\t0\n", f);
        fputs("what is my pet\tNO_SUPPORTED_RESPONSE\t\t\t0\n", f);
        fclose(f);
    }

    NervaEngine e;
    ChatWorldConfig cfg;
    ChatWorldResult result;
    chatworld_config_defaults(&cfg);
    cfg.train_path = train_path;
    cfg.frozen_path = frozen_path;
    cfg.train_epochs = 40u;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "chatworld promotion init");
    expect_true(chatworld_run(&e, &cfg, &result) == 0, "chatworld promotion run");
    expect_true(result.metrics.eval_total == 5u, "chatworld promotion eval ran");
    expect_true(result.metrics.eval_correct == 5u, "chatworld promotion gate passes");
    expect_true(result.metrics.no_supported_response_count == 1u,
                "chatworld promotion missing pet is unsupported");
    expect_true(result.metrics.eval_mutations == 0u, "chatworld promotion frozen mutations zero");
    expect_true(result.metrics.fallback_count == 0u, "chatworld promotion frozen fallback zero");
    nerva_engine_free(&e);
}

static void test_chatworld_v14_learns_policy_and_memory(void) {
    NervaEngine e;
    ChatWorldConfig cfg;
    ChatWorldResult result;
    chatworld_config_defaults(&cfg);
    cfg.train_epochs = 40u;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "chatworld run init");
    expect_true(chatworld_run(&e, &cfg, &result) == 0, "chatworld run");
    expect_true(result.metrics.eval_total > 0u, "chatworld eval ran");
    expect_true(result.metrics.eval_correct == result.metrics.eval_total,
                "chatworld v1.4 frozen eval all cases pass");
    expect_true(result.metrics.eval_mutations == 0u, "chatworld frozen eval has zero mutations");
    expect_true(result.metrics.fallback_count == 0u, "chatworld has zero fallback count");
    expect_true(result.metrics.oracle_label_count == 0u, "chatworld has zero oracle labels");
    expect_true(result.metrics.memory_write_count > 0u, "chatworld writes memory");
    expect_true(result.metrics.memory_read_count > 0u, "chatworld reads memory");
    expect_true(result.metrics.trace_count > 0u, "chatworld records traces");
    expect_true(result.metrics.binding_candidate_count > 0u,
                "chatworld fires learned binding candidates");
    nerva_engine_free(&e);
}

static void test_chatworld_unknown_query_does_not_read_arbitrary_memory(void) {
    NervaEngine e;
    ChatWorldConfig cfg;
    ChatWorldResult result;
    const char *trace_path = "build/chatworld_unknown_trace_test.log";
    chatworld_config_defaults(&cfg);
    cfg.train_epochs = 40u;
    cfg.trace_path = trace_path;

    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "chatworld unknown init");
    expect_true(chatworld_run(&e, &cfg, &result) == 0, "chatworld unknown run");
    expect_true(result.metrics.eval_correct == result.metrics.eval_total,
                "chatworld unknown run passes frozen gate");
    nerva_engine_free(&e);

    FILE *f = fopen(trace_path, "r");
    expect_true(f != NULL, "chatworld unknown trace opens");
    if (f) {
        char line[768];
        int saw_unknown = 0;
        int saw_bad_memory_value = 0;
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, "phase=frozen utterance=\"what is my favorite color\"")) {
                saw_unknown = 1;
                if (!strstr(line, "fired_action=ACTION:RESP_UNKNOWN") ||
                    !strstr(line, "rendered=\"unknown\"") || strstr(line, "value=\"grace\"") ||
                    strstr(line, "value=\"mira\"") || strstr(line, "value=\"ada\"")) {
                    saw_bad_memory_value = 1;
                }
            }
        }
        fclose(f);
        expect_true(saw_unknown, "trace includes unknown frozen query");
        expect_true(!saw_bad_memory_value, "unknown query does not answer arbitrary memory");
    }
}

static void test_chatworld_frozen_eval_does_not_grow_graph(void) {
    NervaEngine e;
    ChatWorldConfig cfg;
    ChatWorldResult train_result;
    ChatWorldResult eval_result;
    chatworld_config_defaults(&cfg);
    cfg.train_epochs = 40u;
    cfg.eval = false;

    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "chatworld frozen growth init");
    expect_true(chatworld_run(&e, &cfg, &train_result) == 0, "chatworld train-only run");

    ChatWorldNerva cw;
    ChatWorldDataset frozen;
    expect_true(chatworld_nerva_init(&e, &cw) == 0, "chatworld growth cw init");
    expect_true(chatworld_load_dataset("worlds/chatworld/datasets/frozen.tsv", &frozen) == 0,
                "chatworld growth frozen load");
    expect_true(chatworld_preload_dataset(&e, &cw, &frozen) == 0,
                "chatworld growth frozen vocabulary preload");

    uint32_t node_count = e.node_count;
    uint32_t edge_count = e.edge_count;
    uint32_t name_count = e.name_count;

    cfg.train = false;
    cfg.eval = true;
    expect_true(chatworld_run(&e, &cfg, &eval_result) == 0, "chatworld eval-only run");
    expect_true(eval_result.metrics.eval_total > 0u, "chatworld eval-only ran");
    expect_true(eval_result.metrics.eval_mutations == 0u, "chatworld eval-only has zero mutations");
    expect_true(e.node_count == node_count, "chatworld frozen eval does not add nodes");
    expect_true(e.edge_count == edge_count, "chatworld frozen eval does not add edges");
    expect_true(e.name_count == name_count, "chatworld frozen eval does not add names");

    nerva_engine_free(&e);
}

static void test_chatworld_trace_artifact_records_v14_paths(void) {
    NervaEngine e;
    ChatWorldConfig cfg;
    ChatWorldResult result;
    const char *trace_path = "build/chatworld_trace_test.log";
    chatworld_config_defaults(&cfg);
    cfg.train_epochs = 4u;
    cfg.trace_path = trace_path;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "chatworld trace init");
    expect_true(chatworld_run(&e, &cfg, &result) == 0, "chatworld trace run");
    expect_true(result.metrics.decision_trace_count > 0u, "chatworld decision trace count");
    nerva_engine_free(&e);

    FILE *f = fopen(trace_path, "r");
    expect_true(f != NULL, "chatworld trace artifact opens");
    if (f) {
        char line[768];
        int saw_header = 0;
        int saw_train = 0;
        int saw_frozen = 0;
        int saw_action = 0;
        int saw_mutation = 0;
        int saw_trace_edges = 0;
        int saw_binding = 0;
        int saw_score_smuggle = 0;
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, "chatworld_trace_v1_4=1")) {
                saw_header = 1;
            }
            if (strstr(line, "phase=train")) {
                saw_train = 1;
            }
            if (strstr(line, "phase=frozen")) {
                saw_frozen = 1;
            }
            if (strstr(line, "fired_action=")) {
                saw_action = 1;
            }
            if (strstr(line, "mutation_delta=")) {
                saw_mutation = 1;
            }
            if (strstr(line, "trace_edges=")) {
                saw_trace_edges = 1;
            }
            if (strstr(line, "fired_action=ACTION:MEM_WRITE") ||
                strstr(line, "fired_action=ACTION:MEM_READ")) {
                saw_binding = 1;
            }
            if (strstr(line, "candidate=") || strstr(line, "score=")) {
                saw_score_smuggle = 1;
            }
        }
        fclose(f);
        expect_true(saw_header, "trace has v1.4 header");
        expect_true(saw_train, "trace has train decisions");
        expect_true(saw_frozen, "trace has frozen decisions");
        expect_true(saw_action, "trace has fired action");
        expect_true(saw_mutation, "trace has mutation delta");
        expect_true(saw_trace_edges, "trace has trace-backed edges");
        expect_true(saw_binding, "trace has memory binding action");
        expect_true(!saw_score_smuggle, "trace does not emit candidate scorer fields");
    }
}

static void test_chatworld_ablation_drops_eval(void) {
    NervaEngine full_e;
    NervaEngine ablate_e;
    ChatWorldConfig cfg;
    ChatWorldResult full;
    ChatWorldResult ablated;
    chatworld_config_defaults(&cfg);
    cfg.train_epochs = 40u;

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

static void test_chatworld_row_validator_rejects_bad_rows(void) {
    const char *valid_path = "build/chatworld_validator_valid.tsv";
    const char *bad_columns_path = "build/chatworld_validator_bad_columns.tsv";
    const char *bad_expected_path = "build/chatworld_validator_bad_expected.tsv";
    const char *bad_frozen_path = "build/chatworld_validator_bad_frozen.tsv";
    const char *bad_phrase_path = "build/chatworld_validator_bad_phrase.tsv";
    const char *bad_multi_path = "build/chatworld_validator_bad_multi.tsv";
    const char *bad_label_path = "build/chatworld_validator_bad_label.tsv";
    FILE *f = fopen(valid_path, "w");
    expect_true(f != NULL, "chatworld validator valid file opens");
    if (f) {
        fputs("# utterance\texpected\tkey\tvalue\tlearn\n", f);
        fputs("my name is Ada\tACK\tname\tada\t1\n", f);
        fputs("what is my name\tMEMORY_VALUE\tname\tada\t0\n", f);
        fputs("call the office\tNO_SUPPORTED_RESPONSE\t\t\t0\n", f);
        fclose(f);
    }
    f = fopen(bad_columns_path, "w");
    expect_true(f != NULL, "chatworld validator bad columns file opens");
    if (f) {
        fputs("my name is Ada\tACK\tname\n", f);
        fclose(f);
    }
    f = fopen(bad_expected_path, "w");
    expect_true(f != NULL, "chatworld validator bad expected file opens");
    if (f) {
        fputs("my name is Ada\tANSWER\tname\tada\t1\n", f);
        fclose(f);
    }
    f = fopen(bad_frozen_path, "w");
    expect_true(f != NULL, "chatworld validator bad frozen file opens");
    if (f) {
        fputs("my name is Ada\tACK\tname\tada\t1\n", f);
        fclose(f);
    }
    f = fopen(bad_phrase_path, "w");
    expect_true(f != NULL, "chatworld validator bad phrase file opens");
    if (f) {
        fputs("name the file\tACK\tname\tfile\t1\n", f);
        fclose(f);
    }
    f = fopen(bad_multi_path, "w");
    expect_true(f != NULL, "chatworld validator bad multi file opens");
    if (f) {
        fputs("my name is Ada Lovelace\tACK\tname\tada lovelace\t1\n", f);
        fclose(f);
    }
    f = fopen(bad_label_path, "w");
    expect_true(f != NULL, "chatworld validator bad label file opens");
    if (f) {
        fputs("my name is Ada\tANSWER_LABEL\tname\tada\t1\n", f);
        fclose(f);
    }

    ChatWorldValidationResult result;
    expect_true(chatworld_validate_rows_file("stage5.4", valid_path, false, NULL, &result) == 0,
                "chatworld validator accepts valid stage5.4 rows");
    expect_true(result.row_count == 3u && result.error_count == 0u,
                "chatworld validator valid result counts");
    expect_true(chatworld_validate_rows_file("stage5.4", bad_columns_path, false, NULL,
                                             &result) != 0,
                "chatworld validator rejects bad column count");
    expect_true(chatworld_validate_rows_file("stage5.4", bad_expected_path, false, NULL,
                                             &result) != 0,
                "chatworld validator rejects unknown expected type");
    expect_true(chatworld_validate_rows_file("stage5.4", bad_frozen_path, true, NULL,
                                             &result) != 0,
                "chatworld validator rejects frozen learn row");
    expect_true(chatworld_validate_rows_file("stage5.4", bad_phrase_path, false, NULL,
                                             &result) != 0,
                "chatworld validator rejects unsupported phrase for stage");
    expect_true(chatworld_validate_rows_file("phase5", bad_multi_path, false, NULL,
                                             &result) != 0,
                "chatworld validator rejects multi-token before phase6");
    expect_true(chatworld_validate_rows_file("phase6", bad_multi_path, false, NULL,
                                             &result) == 0,
                "chatworld validator accepts multi-token at phase6");
    expect_true(chatworld_validate_rows_file("stage5.4", bad_label_path, false, NULL,
                                             &result) != 0,
                "chatworld validator rejects smuggled label");
}

static void test_chatworld_phase7_offline_rows_gate(void) {
    const char *train_path = "worlds/chatworld/datasets/phase7_train.tsv";
    const char *frozen_path = "worlds/chatworld/datasets/phase7_frozen.tsv";
    const char *trace_path = "build/chatworld_phase7_trace.log";
    ChatWorldValidationResult validation;
    expect_true(chatworld_validate_rows_file("phase7", train_path, false, NULL, &validation) == 0,
                "chatworld phase7 train rows validate");
    expect_true(validation.row_count > 0u, "chatworld phase7 train rows present");
    expect_true(chatworld_validate_rows_file("phase7", frozen_path, true, NULL, &validation) == 0,
                "chatworld phase7 frozen rows validate");
    expect_true(validation.row_count > 0u, "chatworld phase7 frozen rows present");

    NervaEngine e;
    ChatWorldConfig cfg;
    ChatWorldResult result;
    chatworld_config_defaults(&cfg);
    cfg.train_path = train_path;
    cfg.frozen_path = frozen_path;
    cfg.trace_path = trace_path;
    cfg.train_epochs = 40u;
    expect_true(nerva_engine_init(&e, nerva_config_default()) == 0, "chatworld phase7 init");
    expect_true(chatworld_run(&e, &cfg, &result) == 0, "chatworld phase7 run");
    expect_true(result.metrics.eval_total == 14u, "chatworld phase7 eval ran");
    expect_true(result.metrics.eval_correct == result.metrics.eval_total,
                "chatworld phase7 offline rows pass");
    expect_true(result.metrics.eval_mutations == 0u, "chatworld phase7 frozen mutations zero");
    expect_true(result.metrics.fallback_count == 0u, "chatworld phase7 ambiguity zero");
    expect_true(result.metrics.no_supported_response_count == 1u,
                "chatworld phase7 unsupported row stays unsupported");
    nerva_engine_free(&e);

    FILE *f = fopen(trace_path, "r");
    expect_true(f != NULL, "chatworld phase7 trace opens");
    if (f) {
        char line[768];
        int saw_mira = 0;
        int saw_tokyo = 0;
        int saw_unsupported = 0;
        int saw_bad_trace = 0;
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, "phase=frozen") && !trace_line_has_edges(line) &&
                !strstr(line, "frame=NO_SUPPORTED_RESPONSE")) {
                saw_bad_trace = 1;
            }
            if (strstr(line, "phase=frozen utterance=\"who am I\"") &&
                strstr(line, "rendered=\"mira\"") && trace_line_has_edges(line)) {
                saw_mira = 1;
            }
            if (strstr(line, "phase=frozen utterance=\"where do I live\"") &&
                strstr(line, "rendered=\"tokyo\"") && trace_line_has_edges(line)) {
                saw_tokyo = 1;
            }
            if (strstr(line, "phase=frozen utterance=\"open the pod bay door\"") &&
                strstr(line, "frame=NO_SUPPORTED_RESPONSE") &&
                strstr(line, "no_supported=1")) {
                saw_unsupported = 1;
            }
        }
        fclose(f);
        expect_true(saw_mira, "chatworld phase7 trace reads held-out name");
        expect_true(saw_tokyo, "chatworld phase7 trace reads held-out city");
        expect_true(saw_unsupported, "chatworld phase7 trace has no-supported row");
        expect_true(!saw_bad_trace, "chatworld phase7 supported frozen rows have traces");
    }
}

static void write_chatworld_scale_block(FILE *f, uint32_t idx, int frozen) {
    (void)idx;
    const char *name = frozen ? "turing" : "ada";
    const char *city = frozen ? "tokyo" : "rome";
    const char *color = frozen ? "green" : "blue";
    const char *pet = frozen ? "nova" : "luna";
    const char *fact = frozen ? "breed" : "dog";
    const char *learn_write = frozen ? "0" : "1";
    const char *learn_read = frozen ? "0" : "1";

    fprintf(f, "call me %s\tACK\tname\t%s\t%s\n", name, name, learn_write);
    fprintf(f, "what is my name\tMEMORY_VALUE\tname\t%s\t%s\n", name, learn_read);
    fprintf(f, "I live in %s\tACK\tcity\t%s\t%s\n", city, city, learn_write);
    fprintf(f, "where do I live\tMEMORY_VALUE\tcity\t%s\t%s\n", city, learn_read);
    fprintf(f, "my color is %s\tACK\tcolor\t%s\t%s\n", color, color, learn_write);
    fprintf(f, "what is my color\tMEMORY_VALUE\tcolor\t%s\t%s\n", color, learn_read);
    fprintf(f, "my pet is %s\tACK\tpet\t%s\t%s\n", pet, pet, learn_write);
    fprintf(f, "what is my pet\tMEMORY_VALUE\tpet\t%s\t%s\n", pet, learn_read);
    fprintf(f, "poodle is %s\tACK\tpoodle\t%s\t%s\n", fact, fact, learn_write);
    fprintf(f, "what is poodle\tMEMORY_VALUE\tpoodle\t%s\t%s\n", fact, learn_read);
    fprintf(f, "call the office\tNO_SUPPORTED_RESPONSE\t\t\t0\n");
    fprintf(f, "called the number\tNO_SUPPORTED_RESPONSE\t\t\t0\n");
    fprintf(f, "name the file\tNO_SUPPORTED_RESPONSE\t\t\t0\n");
    fprintf(f, "who am I\tMEMORY_VALUE\tname\t%s\t%s\n", name, learn_read);
    fprintf(f, "what am I called\tMEMORY_VALUE\tname\t%s\t%s\n", name, learn_read);
    fprintf(f, "my kind is person\tACK\tkind\tperson\t%s\n", learn_write);
    fprintf(f, "what is my kind\tMEMORY_VALUE\tkind\tperson\t%s\n", learn_read);
    fprintf(f, "dog is animal\tACK\tdog\tanimal\t%s\n", learn_write);
    fprintf(f, "what is dog\tMEMORY_VALUE\tdog\tanimal\t%s\n", learn_read);
    fprintf(f, "Paris is city\tACK\tparis\tcity\t%s\n", learn_write);
    fprintf(f, "what is Paris\tMEMORY_VALUE\tparis\tcity\t%s\n", learn_read);
    fprintf(f, "I am called %s\tACK\tname\t%s\t%s\n", name, name, learn_write);
    fprintf(f, "my name is %s\tACK\tname\t%s\t%s\n", name, name, learn_write);
    fprintf(f, "no poodle is %s\tACK\tpoodle\t%s\t%s\n", fact, fact, learn_write);
    fprintf(f, "what is poodle\tMEMORY_VALUE\tpoodle\t%s\t%s\n", fact, learn_read);
}

static void write_chatworld_scale_dataset(const char *path, uint32_t rows, int frozen) {
    FILE *f = fopen(path, "w");
    expect_true(f != NULL, "chatworld scale dataset file opens");
    if (!f) {
        return;
    }
    fputs("# utterance\texpected\tkey\tvalue\tlearn\n", f);
    uint32_t blocks = rows / 25u;
    for (uint32_t i = 0; i < blocks; ++i) {
        write_chatworld_scale_block(f, i, frozen);
    }
    fclose(f);
}

static void run_chatworld_scale_gate(uint32_t rows, uint32_t epochs, uint32_t min_correct) {
    char train_path[96];
    char frozen_path[96];
    snprintf(train_path, sizeof(train_path), "build/chatworld_scale_%u_train.tsv", rows);
    snprintf(frozen_path, sizeof(frozen_path), "build/chatworld_scale_%u_frozen.tsv", rows);
    write_chatworld_scale_dataset(train_path, rows, 0);
    write_chatworld_scale_dataset(frozen_path, rows, 1);

    ChatWorldValidationResult validation;
    expect_true(chatworld_validate_rows_file("phase8", train_path, false, NULL, &validation) == 0,
                "chatworld scale train rows validate");
    expect_true(chatworld_validate_rows_file("phase8", frozen_path, true, NULL, &validation) == 0,
                "chatworld scale frozen rows validate");

    NervaConfig ncfg = nerva_config_default();
    NervaEngine e;
    ChatWorldConfig cfg;
    ChatWorldResult result;
    chatworld_config_defaults(&cfg);
    cfg.train_path = train_path;
    cfg.frozen_path = frozen_path;
    cfg.train_epochs = epochs;
    expect_true(nerva_engine_init(&e, ncfg) == 0, "chatworld scale init");
    expect_true(chatworld_run(&e, &cfg, &result) == 0, "chatworld scale run");
    expect_true(result.metrics.eval_total == rows, "chatworld scale eval count");
    expect_true(result.metrics.eval_correct >= min_correct, "chatworld scale accuracy target");
    expect_true(result.metrics.eval_mutations == 0u, "chatworld scale frozen mutations zero");
    expect_true(result.metrics.fallback_count == 0u, "chatworld scale fallback zero");
    expect_true(result.metrics.trace_count > 0u, "chatworld scale internal trace count");
    nerva_engine_free(&e);

    NervaEngine ablate_e;
    ChatWorldConfig ablate_cfg = cfg;
    ChatWorldResult ablated;
    ablate_cfg.trace_path = NULL;
    ablate_cfg.ablate_response_edges = true;
    expect_true(nerva_engine_init(&ablate_e, ncfg) == 0, "chatworld scale ablate init");
    expect_true(chatworld_run(&ablate_e, &ablate_cfg, &ablated) == 0,
                "chatworld scale ablate run");
    expect_true(ablated.metrics.eval_correct < result.metrics.eval_correct / 2u,
                "chatworld scale ablation drops accuracy hard");
    nerva_engine_free(&ablate_e);
}

static void test_chatworld_scale_gates(void) {
    run_chatworld_scale_gate(100u, 12u, 100u);
    const char *full = getenv("CHATWORLD_SCALE_FULL");
    if (full && strcmp(full, "1") == 0) {
        run_chatworld_scale_gate(1000u, 8u, 1000u);
        run_chatworld_scale_gate(10000u, 4u, 9900u);
    }
}

static void test_chatworld_ambiguous_multiple_outputs_report_contradiction(void) {
    NervaEngine e;
    ChatWorldNerva cw;
    ChatWorld w;
    ChatWorldDataset ds;
    ChatWorldTurn turn;
    memset(&turn, 0, sizeof(turn));
    strcpy(turn.utterance, "hello");
    turn.expected = CHAT_EXPECT_GREET;

    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "chatworld ambiguity init");
    expect_true(chatworld_nerva_init(&e, &cw) == 0, "chatworld ambiguity cw init");
    expect_true(chatworld_load_dataset("worlds/chatworld/datasets/train.tsv", &ds) == 0,
                "chatworld ambiguity dataset");
    expect_true(chatworld_preload_dataset(&e, &cw, &ds) == 0, "chatworld ambiguity preload");

    uint32_t hello = nerva_find_node_by_name(&e, "TOKEN:hello");
    uint32_t out_hello = nerva_find_node_by_name(&e, "OUTPUT_TOKEN:hello");
    uint32_t out_ok = nerva_find_node_by_name(&e, "OUTPUT_TOKEN:ok");
    expect_true(hello != UINT32_MAX && out_hello != UINT32_MAX && out_ok != UINT32_MAX,
                "chatworld ambiguity nodes exist");
    uint32_t edge_hello = nerva_graph_create_edge(&e, hello, out_hello,
                                                  (uint16_t)(NERVA_REL_CUSTOM_BASE + 43u));
    uint32_t edge_ok = nerva_graph_create_edge(&e, hello, out_ok,
                                               (uint16_t)(NERVA_REL_CUSTOM_BASE + 43u));
    e.edges[edge_hello].weight = 256;
    e.edges[edge_ok].weight = 256;
    nerva_graph_rebuild_adjacency(&e);

    chatworld_reset(&w);
    ChatWorldDecision d = chatworld_step(&e, &cw, &w, &turn);
    expect_true(d.ambiguous_response, "multiple fired outputs report ambiguity");
    expect_true(d.frame == CHAT_FRAME_CONTRADICTION_OR_AMBIGUOUS,
                "ambiguity frame reported");
    nerva_engine_free(&e);
}

int test_chatworld_run(void) {
    g_failures = 0;
    test_chatworld_surface_adapter_purist_nodes();
    test_chatworld_dataset_files_load();
    test_chatworld_zero_score_eval_has_no_fallback();
    test_chatworld_stage1_memory_heartbeat();
    test_chatworld_stage2_held_out_same_template_entities();
    test_chatworld_stage3_response_behaviors_are_learned_paths();
    test_chatworld_stage3_unknown_requires_learned_support();
    test_chatworld_stage4_correction_overrides_prior_value();
    test_chatworld_stage5_paraphrase_write_chunk_paths();
    test_chatworld_stage5_1_held_out_paraphrase_values();
    test_chatworld_stage5_2_mixed_write_shapes();
    test_chatworld_stage5_3_paraphrase_correction();
    test_chatworld_stage5_4_unsupported_phrase_isolation();
    test_chatworld_stage6_1_read_paraphrases();
    test_chatworld_phase3_training_memory_facts();
    test_chatworld_phase4_multiple_keys_do_not_leak();
    test_chatworld_phase5_simple_facts_and_correction();
    test_chatworld_phase6_multi_token_values();
    test_chatworld_promotion_mixed_recall_gate();
    test_chatworld_v14_learns_policy_and_memory();
    test_chatworld_unknown_query_does_not_read_arbitrary_memory();
    test_chatworld_frozen_eval_does_not_grow_graph();
    test_chatworld_trace_artifact_records_v14_paths();
    test_chatworld_ablation_drops_eval();
    test_chatworld_row_validator_rejects_bad_rows();
    test_chatworld_phase7_offline_rows_gate();
    test_chatworld_scale_gates();
    test_chatworld_ambiguous_multiple_outputs_report_contradiction();
    return g_failures;
}
