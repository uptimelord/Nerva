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
    expect_true(result.metrics.train_total == 40u, "chatworld stage1 training ran");
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
        fputs("my name is Grace\tACK\tname\tgrace\t1\n", f);
        fputs("what is my name\tMEMORY_VALUE\tname\tgrace\t0\n", f);
        fputs("my name is Turing\tACK\tname\tturing\t1\n", f);
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
    ChatWorldTurn turn;
    memset(&ds, 0, sizeof(ds));
    memset(&turn, 0, sizeof(turn));
    strcpy(ds.turns[0].utterance, "what is my favorite color");
    ds.turns[0].expected = CHAT_EXPECT_UNKNOWN;
    ds.turns[0].learn = false;
    ds.count = 1u;
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
    test_chatworld_v14_learns_policy_and_memory();
    test_chatworld_unknown_query_does_not_read_arbitrary_memory();
    test_chatworld_frozen_eval_does_not_grow_graph();
    test_chatworld_trace_artifact_records_v14_paths();
    test_chatworld_ablation_drops_eval();
    test_chatworld_ambiguous_multiple_outputs_report_contradiction();
    return g_failures;
}
