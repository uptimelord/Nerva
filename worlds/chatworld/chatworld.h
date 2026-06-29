// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Paul Odantabao II

#ifndef CHATWORLD_H
#define CHATWORLD_H

#include "nerva_types.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define CHATWORLD_MAX_TOKENS 12u
#define CHATWORLD_MAX_TOKEN_LEN 31u
#define CHATWORLD_MEMORY_CAP 32u
#define CHATWORLD_MAX_TURNS 64u
#define CHATWORLD_MAX_SELECTED_EDGES 48u
#define CHATWORLD_MAX_RENDERED 128u
#define CHATWORLD_MAX_VOCAB 128u

typedef enum ChatWorldFrame {
    CHAT_FRAME_NO_SUPPORTED_RESPONSE = 0,
    CHAT_FRAME_OUTPUT,
    CHAT_FRAME_ACK,
    CHAT_FRAME_UNKNOWN,
    CHAT_FRAME_ANSWER_MEMORY,
    CHAT_FRAME_CONTRADICTION_OR_AMBIGUOUS,
    CHAT_FRAME_COUNT
} ChatWorldFrame;

typedef enum ChatWorldAction {
    CHAT_ACTION_MEM_WRITE = 0,
    CHAT_ACTION_MEM_READ,
    CHAT_ACTION_RESP_UNKNOWN,
    CHAT_ACTION_COUNT
} ChatWorldAction;

typedef enum ChatWorldExpected {
    CHAT_EXPECT_NONE = 0,
    CHAT_EXPECT_GREET,
    CHAT_EXPECT_ACK,
    CHAT_EXPECT_UNKNOWN,
    CHAT_EXPECT_MEMORY_VALUE
} ChatWorldExpected;

typedef struct ChatWorldConfig {
    uint32_t seed;
    uint32_t train_epochs;
    uint32_t eval_episodes;
    bool train;
    bool eval;
    bool ablate_response_edges;
    bool trace;
    const char *train_path;
    const char *dev_path;
    const char *frozen_path;
    const char *trace_path;
} ChatWorldConfig;

typedef struct ChatWorldTurn {
    char utterance[128];
    ChatWorldExpected expected;
    char expected_key[CHATWORLD_MAX_TOKEN_LEN + 1u];
    char expected_value[CHATWORLD_MAX_TOKEN_LEN + 1u];
    bool learn;
} ChatWorldTurn;

typedef struct ChatWorldDataset {
    ChatWorldTurn turns[CHATWORLD_MAX_TURNS];
    uint32_t count;
} ChatWorldDataset;

typedef struct ChatWorldNerva {
    uint32_t turn_start;
    uint32_t turn_end;
    uint32_t speaker_user;
    uint32_t speaker_assistant;
    uint32_t position[CHATWORLD_MAX_TOKENS];
    uint32_t bind_key;
    uint32_t bind_value;
    uint32_t key_candidate[CHATWORLD_MAX_TOKENS];
    uint32_t value_candidate[CHATWORLD_MAX_TOKENS];
    uint32_t action_node[CHAT_ACTION_COUNT];
    uint32_t token_node[CHATWORLD_MAX_TOKENS];
    uint32_t token_at_node[CHATWORLD_MAX_TOKENS];
    uint32_t pair_node[CHATWORLD_MAX_TOKENS];
    uint32_t punct_node[CHATWORLD_MAX_TOKENS];
    uint32_t output_node[CHATWORLD_MAX_VOCAB];
    char output_token[CHATWORLD_MAX_VOCAB][CHATWORLD_MAX_TOKEN_LEN + 1u];
    uint32_t token_count;
    uint32_t pair_count;
    uint32_t punct_count;
    uint32_t output_count;
} ChatWorldNerva;

typedef struct ChatWorldMemoryPair {
    char key[CHATWORLD_MAX_TOKEN_LEN + 1u];
    char value[CHATWORLD_MAX_TOKEN_LEN + 1u];
    uint32_t strength;
} ChatWorldMemoryPair;

typedef struct ChatWorld {
    ChatWorldMemoryPair memory[CHATWORLD_MEMORY_CAP];
    uint32_t memory_count;
} ChatWorld;

typedef struct ChatWorldDecision {
    ChatWorldAction action;
    ChatWorldFrame frame;
    char key[CHATWORLD_MAX_TOKEN_LEN + 1u];
    char value[CHATWORLD_MAX_TOKEN_LEN + 1u];
    char rendered[CHATWORLD_MAX_RENDERED];
    uint32_t key_pos;
    uint32_t value_pos;
    uint32_t fired_output_count;
    uint32_t fired_action_count;
    uint32_t selected_edge_count;
    uint32_t selected_edges[CHATWORLD_MAX_SELECTED_EDGES];
    bool no_supported_response;
    bool ambiguous_response;
    bool mem_write_fired;
    bool mem_read_fired;
    bool resp_unknown_fired;
    bool output_fired;
} ChatWorldDecision;

typedef struct ChatWorldMetrics {
    uint32_t train_total;
    uint32_t train_correct;
    uint32_t eval_total;
    uint32_t eval_correct;
    uint32_t eval_baseline_correct;
    uint32_t eval_mutations;
    uint32_t fallback_count;
    uint32_t no_supported_response_count;
    uint32_t oracle_label_count;
    uint32_t memory_write_count;
    uint32_t memory_read_count;
    uint32_t trace_count;
    uint32_t decision_trace_count;
    uint32_t binding_candidate_count;
    uint32_t response_ablation_correct;
    uint32_t response_ablation_total;
} ChatWorldMetrics;

typedef struct ChatWorldResult {
    ChatWorldMetrics metrics;
} ChatWorldResult;

void chatworld_config_defaults(ChatWorldConfig *cfg);
int chatworld_load_dataset(const char *path, ChatWorldDataset *out);
int chatworld_nerva_init(NervaEngine *e, ChatWorldNerva *cw);
int chatworld_preload_dataset(NervaEngine *e, ChatWorldNerva *cw, const ChatWorldDataset *ds);
void chatworld_reset(ChatWorld *w);
int chatworld_tokenize(const char *utterance,
                       char tokens[CHATWORLD_MAX_TOKENS][CHATWORLD_MAX_TOKEN_LEN + 1u],
                       uint32_t *count);
int chatworld_emit_surface(NervaEngine *e, ChatWorldNerva *cw, const char *utterance);
ChatWorldDecision chatworld_step(NervaEngine *e, ChatWorldNerva *cw, ChatWorld *w,
                                 const ChatWorldTurn *turn);
int chatworld_run(NervaEngine *e, const ChatWorldConfig *cfg, ChatWorldResult *out);
void chatworld_print_metrics(const ChatWorldMetrics *m, FILE *out);
const char *chatworld_action_name(ChatWorldAction action);
const char *chatworld_frame_name(ChatWorldFrame frame);

#endif
