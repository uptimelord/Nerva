// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Paul Odantabao II

#include "chatworld.h"

#include "nerva_debug.h"
#include "nerva_engine.h"
#include "nerva_event.h"
#include "nerva_graph.h"
#include "nerva_learning.h"
#include "nerva_math.h"
#include "nerva_mutation.h"
#include "nerva_trace.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHAT_REL_SURFACE_TO_BINDING ((uint16_t)(NERVA_REL_CUSTOM_BASE + 40u))
#define CHAT_REL_BINDING_TO_ACTION ((uint16_t)(NERVA_REL_CUSTOM_BASE + 41u))
#define CHAT_REL_ACTION_TO_OUTPUT ((uint16_t)(NERVA_REL_CUSTOM_BASE + 42u))
#define CHAT_REL_SURFACE_TO_OUTPUT ((uint16_t)(NERVA_REL_CUSTOM_BASE + 43u))
#define CHAT_REL_SURFACE_TO_ACTION ((uint16_t)(NERVA_REL_CUSTOM_BASE + 44u))
#define CHAT_REL_SURFACE_TO_KEY_TOKEN ((uint16_t)(NERVA_REL_CUSTOM_BASE + 45u))
#define CHAT_REL_KEY_TOKEN_TO_BINDING ((uint16_t)(NERVA_REL_CUSTOM_BASE + 46u))
#define CHAT_REL_SURFACE_TO_ACTION_GATE ((uint16_t)(NERVA_REL_CUSTOM_BASE + 47u))
#define CHAT_REL_BINDING_TO_ACTION_GATE ((uint16_t)(NERVA_REL_CUSTOM_BASE + 48u))
#define CHAT_REL_ACTION_GATE_TO_ACTION ((uint16_t)(NERVA_REL_CUSTOM_BASE + 49u))
#define CHAT_REL_KEY_TO_VALUE_BINDING ((uint16_t)(NERVA_REL_CUSTOM_BASE + 50u))

#define CHATWORLD_DEFAULT_TRAIN_PATH "worlds/chatworld/datasets/train.tsv"
#define CHATWORLD_DEFAULT_DEV_PATH "worlds/chatworld/datasets/dev.tsv"
#define CHATWORLD_DEFAULT_FROZEN_PATH "worlds/chatworld/datasets/frozen.tsv"
#define CHATWORLD_SUPPORT_WEIGHT_Q8_8 512
#define CHATWORLD_ACTION_THETA_Q8_8 1024
#define CHATWORLD_ACTION_GATE_THETA_Q8_8 1536
#define CHATWORLD_ACTION_GATE_OUTPUT_WEIGHT_Q8_8 1024

typedef struct ChatWorldSurface {
    char tokens[CHATWORLD_MAX_TOKENS][CHATWORLD_MAX_TOKEN_LEN + 1u];
    uint32_t token_count;
    char punct[CHATWORLD_MAX_TOKENS];
    uint32_t punct_count;
} ChatWorldSurface;

typedef enum ChatWorldPathKind {
    CHAT_PATH_RESPONSE = 0,
    CHAT_PATH_MEM_WRITE,
    CHAT_PATH_MEM_READ
} ChatWorldPathKind;

typedef struct ChatWorldPath {
    ChatWorldPathKind kind;
    ChatWorldAction action;
    uint32_t key_pos;
    uint32_t value_pos;
    uint32_t value_token_count;
    uint32_t action_node;
    uint32_t key_token_node;
    uint32_t key_candidate_node;
    uint32_t value_candidate_node;
    uint32_t output_node;
    uint32_t action_gate_node;
    uint32_t output_nodes[CHATWORLD_MAX_TOKENS];
    uint32_t output_node_count;
    uint32_t selected_edges[CHATWORLD_MAX_SELECTED_EDGES];
    uint32_t selected_edge_count;
} ChatWorldPath;

static int chatworld_surface_parse(const char *utterance, ChatWorldSurface *surface);
static uint32_t chatworld_value_tokenize(const char *value,
                                         char tokens[CHATWORLD_MAX_TOKENS]
                                                    [CHATWORLD_MAX_TOKEN_LEN + 1u]);

static const char *const chatworld_forbidden_names[] = {
    "INTENT",
    "SLOT",
    "CORRECT",
    "ANSWER_LABEL",
    "FACT_QUERY",
    "QUERY_TYPE",
    "USER_WANTS",
    "SHOULD_RESPOND",
    "EXPECTED_REPLY",
    "GROUND_TRUTH",
    "CANONICAL_ANSWER",
    "TASK_LABEL",
};

static void chatworld_copy_token(char *dst, const char *src) {
    uint32_t i = 0;
    while (src && src[i] != '\0' && i < CHATWORLD_MAX_TOKEN_LEN) {
        dst[i] = (char)tolower((unsigned char)src[i]);
        i++;
    }
    dst[i] = '\0';
}

static void chatworld_copy_value(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0u) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }

    size_t i = 0;
    size_t out = 0;
    int pending_space = 0;
    while (src[i] != '\0' && src[i] != '\r' && src[i] != '\n') {
        unsigned char ch = (unsigned char)src[i++];
        if (isspace(ch)) {
            pending_space = out > 0u;
            continue;
        }
        if (pending_space && out + 1u < dst_size) {
            dst[out++] = ' ';
        }
        pending_space = 0;
        if (out + 1u >= dst_size) {
            break;
        }
        dst[out++] = (char)tolower(ch);
    }
    dst[out] = '\0';
}

static void chatworld_copy_field(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0u) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t i = 0;
    while (src[i] != '\0' && src[i] != '\r' && src[i] != '\n' && i + 1u < dst_size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int chatworld_contains_case_insensitive(const char *s, const char *needle) {
    if (!s || !needle || needle[0] == '\0') {
        return 0;
    }
    size_t n = strlen(needle);
    for (size_t i = 0; s[i] != '\0'; ++i) {
        size_t j = 0;
        while (j < n && s[i + j] != '\0' &&
               tolower((unsigned char)s[i + j]) == tolower((unsigned char)needle[j])) {
            j++;
        }
        if (j == n) {
            return 1;
        }
    }
    return 0;
}

static int chatworld_has_forbidden_name(const char *name) {
    if (!name) {
        return 0;
    }
    for (uint32_t i = 0; i < sizeof(chatworld_forbidden_names) / sizeof(chatworld_forbidden_names[0]);
         ++i) {
        if (chatworld_contains_case_insensitive(name, chatworld_forbidden_names[i])) {
            return 1;
        }
    }
    return 0;
}

static uint32_t chatworld_get_or_create(NervaEngine *e, const char *name) {
    if (chatworld_has_forbidden_name(name)) {
        return UINT32_MAX;
    }
    return nerva_get_or_create_node(e, name);
}

static uint32_t chatworld_find_edge_by_relation(const NervaEngine *e, uint32_t source,
                                                uint32_t target, uint16_t relation) {
    if (!e || source >= e->node_count || target >= e->node_count) {
        return UINT32_MAX;
    }
    for (uint32_t i = 0; i < e->edge_count; ++i) {
        const NervaEdge *ed = &e->edges[i];
        if (!(ed->flags & NERVA_EDGE_DELETED) && ed->source == source &&
            ed->target == target && ed->relation == relation) {
            return i;
        }
    }
    return UINT32_MAX;
}

static uint32_t chatworld_create_edge_weight(NervaEngine *e, uint32_t source, uint32_t target,
                                             uint16_t relation, nerva_q8_8_t weight) {
    uint32_t edge_id = chatworld_find_edge_by_relation(e, source, target, relation);
    if (edge_id == UINT32_MAX) {
        edge_id = nerva_graph_create_edge(e, source, target, relation);
    }
    if (edge_id != UINT32_MAX) {
        e->edges[edge_id].weight = weight;
    }
    return edge_id;
}

static void chatworld_path_add_edge(ChatWorldPath *path, uint32_t edge_id) {
    if (!path || edge_id == UINT32_MAX ||
        path->selected_edge_count >= CHATWORLD_MAX_SELECTED_EDGES) {
        return;
    }
    for (uint32_t i = 0; i < path->selected_edge_count; ++i) {
        if (path->selected_edges[i] == edge_id) {
            return;
        }
    }
    path->selected_edges[path->selected_edge_count++] = edge_id;
}

static int chatworld_is_fixed_support_edge(const NervaEngine *e, uint32_t edge_id) {
    if (!e || edge_id == UINT32_MAX || edge_id >= e->edge_count) {
        return 0;
    }
    const NervaEdge *ed = &e->edges[edge_id];
    if (ed->relation == CHAT_REL_SURFACE_TO_BINDING &&
        ed->weight == CHATWORLD_SUPPORT_WEIGHT_Q8_8) {
        return 1;
    }
    if (ed->relation == CHAT_REL_BINDING_TO_ACTION_GATE &&
        ed->weight == CHATWORLD_SUPPORT_WEIGHT_Q8_8) {
        return 1;
    }
    if (ed->relation == CHAT_REL_ACTION_GATE_TO_ACTION &&
        ed->weight == CHATWORLD_ACTION_GATE_OUTPUT_WEIGHT_Q8_8) {
        return 1;
    }
    return 0;
}

static void chatworld_path_add_if_exists(const NervaEngine *e, ChatWorldPath *path,
                                         uint32_t source, uint32_t target, uint16_t relation) {
    uint32_t edge_id = chatworld_find_edge_by_relation(e, source, target, relation);
    chatworld_path_add_edge(path, edge_id);
}

void chatworld_config_defaults(ChatWorldConfig *cfg) {
    if (!cfg) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->seed = 1u;
    cfg->train_epochs = 20u;
    cfg->train = true;
    cfg->eval = true;
    cfg->train_path = CHATWORLD_DEFAULT_TRAIN_PATH;
    cfg->dev_path = CHATWORLD_DEFAULT_DEV_PATH;
    cfg->frozen_path = CHATWORLD_DEFAULT_FROZEN_PATH;
}

static ChatWorldExpected chatworld_expected_from_string(const char *s) {
    if (!s) {
        return CHAT_EXPECT_NONE;
    }
    if (strcmp(s, "GREET") == 0) {
        return CHAT_EXPECT_GREET;
    }
    if (strcmp(s, "ACK") == 0) {
        return CHAT_EXPECT_ACK;
    }
    if (strcmp(s, "UNKNOWN") == 0) {
        return CHAT_EXPECT_UNKNOWN;
    }
    if (strcmp(s, "MEMORY_VALUE") == 0) {
        return CHAT_EXPECT_MEMORY_VALUE;
    }
    if (strcmp(s, "NO_SUPPORTED_RESPONSE") == 0 || strcmp(s, "NO_SUPPORTED") == 0) {
        return CHAT_EXPECT_NO_SUPPORTED_RESPONSE;
    }
    return CHAT_EXPECT_NONE;
}

static const char *chatworld_expected_name(ChatWorldExpected expected) {
    switch (expected) {
    case CHAT_EXPECT_GREET:
        return "GREET";
    case CHAT_EXPECT_ACK:
        return "ACK";
    case CHAT_EXPECT_UNKNOWN:
        return "UNKNOWN";
    case CHAT_EXPECT_MEMORY_VALUE:
        return "MEMORY_VALUE";
    case CHAT_EXPECT_NO_SUPPORTED_RESPONSE:
        return "NO_SUPPORTED_RESPONSE";
    default:
        return "";
    }
}

static int chatworld_dataset_push(ChatWorldDataset *ds, const ChatWorldTurn *turn) {
    if (!ds || !turn) {
        return -1;
    }
    if (ds->count >= ds->cap) {
        uint32_t new_cap = ds->cap == 0u ? 64u : ds->cap * 2u;
        ChatWorldTurn *new_turns =
            (ChatWorldTurn *)realloc(ds->turns, (size_t)new_cap * sizeof(ChatWorldTurn));
        if (!new_turns) {
            return -1;
        }
        ds->turns = new_turns;
        ds->cap = new_cap;
    }
    ds->turns[ds->count++] = *turn;
    return 0;
}

int chatworld_load_dataset(const char *path, ChatWorldDataset *out) {
    if (!path || !out) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    FILE *f = fopen(path, "r");
    if (!f) {
        return -1;
    }

    char line[384];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '\0' || line[0] == '#' || line[0] == '\r' || line[0] == '\n') {
            continue;
        }
        char *fields[5] = {0};
        char *cursor = line;
        for (uint32_t i = 0; i < 5u; ++i) {
            fields[i] = cursor;
            char *tab = strchr(cursor, '\t');
            if (tab) {
                *tab = '\0';
                cursor = tab + 1;
            } else if (i < 4u) {
                fclose(f);
                return -1;
            }
        }

        ChatWorldTurn t;
        memset(&t, 0, sizeof(t));
        chatworld_copy_field(t.utterance, sizeof(t.utterance), fields[0]);
        t.expected = chatworld_expected_from_string(fields[1]);
        chatworld_copy_token(t.expected_key, fields[2]);
        chatworld_copy_value(t.expected_value, sizeof(t.expected_value), fields[3]);
        t.learn = fields[4] && strtoul(fields[4], NULL, 10) != 0u;
        if (t.utterance[0] == '\0' || t.expected == CHAT_EXPECT_NONE ||
            chatworld_dataset_push(out, &t) != 0) {
            fclose(f);
            chatworld_free_dataset(out);
            return -1;
        }
    }

    fclose(f);
    return out->count > 0u ? 0 : -1;
}

void chatworld_free_dataset(ChatWorldDataset *ds) {
    if (!ds) {
        return;
    }
    free(ds->turns);
    memset(ds, 0, sizeof(*ds));
}

static int chatworld_parse_stage_code(const char *stage) {
    if (!stage || stage[0] == '\0') {
        return 0;
    }
    char normalized[32];
    uint32_t out = 0;
    for (uint32_t i = 0; stage[i] != '\0' && out + 1u < sizeof(normalized); ++i) {
        unsigned char ch = (unsigned char)stage[i];
        if (isalnum(ch)) {
            normalized[out++] = (char)tolower(ch);
        }
    }
    normalized[out] = '\0';

    if (strcmp(normalized, "5") == 0 || strcmp(normalized, "50") == 0 ||
        strcmp(normalized, "stage5") == 0 || strcmp(normalized, "stage50") == 0) {
        return 50;
    }
    if (strcmp(normalized, "51") == 0 || strcmp(normalized, "stage51") == 0) {
        return 51;
    }
    if (strcmp(normalized, "52") == 0 || strcmp(normalized, "stage52") == 0) {
        return 52;
    }
    if (strcmp(normalized, "53") == 0 || strcmp(normalized, "stage53") == 0) {
        return 53;
    }
    if (strcmp(normalized, "54") == 0 || strcmp(normalized, "stage54") == 0) {
        return 54;
    }
    if (strcmp(normalized, "6") == 0 || strcmp(normalized, "60") == 0 ||
        strcmp(normalized, "stage6") == 0 || strcmp(normalized, "stage60") == 0) {
        return 60;
    }
    if (strcmp(normalized, "61") == 0 || strcmp(normalized, "stage61") == 0) {
        return 61;
    }
    if (strcmp(normalized, "phase3") == 0) {
        return 70;
    }
    if (strcmp(normalized, "phase4") == 0) {
        return 80;
    }
    if (strcmp(normalized, "phase5") == 0) {
        return 90;
    }
    if (strcmp(normalized, "phase6") == 0) {
        return 100;
    }
    if (strcmp(normalized, "phase7") == 0 || strcmp(normalized, "phase8") == 0 ||
        strcmp(normalized, "v14") == 0 || strcmp(normalized, "promotion") == 0) {
        return 110;
    }
    return 0;
}

static int chatworld_field_has_forbidden_label(const char *field) {
    if (!field) {
        return 0;
    }
    for (uint32_t i = 0; i < sizeof(chatworld_forbidden_names) / sizeof(chatworld_forbidden_names[0]);
         ++i) {
        if (chatworld_contains_case_insensitive(field, chatworld_forbidden_names[i])) {
            return 1;
        }
    }
    return 0;
}

static int chatworld_count_value_tokens(const char *value) {
    char tokens[CHATWORLD_MAX_TOKENS][CHATWORLD_MAX_TOKEN_LEN + 1u];
    return (int)chatworld_value_tokenize(value, tokens);
}

static int chatworld_key_allowed_for_stage(const char *key, int stage_code) {
    if (!key || key[0] == '\0') {
        return 1;
    }
    if (strcmp(key, "name") == 0) {
        return 1;
    }
    if (stage_code >= 80 &&
        (strcmp(key, "city") == 0 || strcmp(key, "color") == 0 ||
         strcmp(key, "pet") == 0 || strcmp(key, "kind") == 0)) {
        return 1;
    }
    if (stage_code >= 70) {
        return 1;
    }
    return 0;
}

static int chatworld_phrase_allowed_for_stage(const ChatWorldSurface *surface,
                                              ChatWorldExpected expected,
                                              const char *key, int stage_code) {
    if (!surface) {
        return 0;
    }
    if (expected == CHAT_EXPECT_GREET || expected == CHAT_EXPECT_UNKNOWN ||
        expected == CHAT_EXPECT_NO_SUPPORTED_RESPONSE) {
        return 1;
    }
    if (expected == CHAT_EXPECT_ACK && (!key || key[0] == '\0')) {
        return 1;
    }
    if (surface->token_count == 0u) {
        return 0;
    }

    if (expected == CHAT_EXPECT_ACK) {
        if (surface->token_count >= 4u && strcmp(surface->tokens[0], "my") == 0 &&
            strcmp(surface->tokens[1], "name") == 0 && strcmp(surface->tokens[2], "is") == 0) {
            return 1;
        }
        if (stage_code >= 52 && surface->token_count >= 4u &&
            strcmp(surface->tokens[0], "i") == 0 && strcmp(surface->tokens[1], "am") == 0 &&
            strcmp(surface->tokens[2], "called") == 0) {
            return 1;
        }
        if (stage_code >= 52 && surface->token_count >= 3u &&
            strcmp(surface->tokens[0], "call") == 0 && strcmp(surface->tokens[1], "me") == 0) {
            return 1;
        }
        if (stage_code >= 53 && surface->token_count >= 5u &&
            strcmp(surface->tokens[0], "no") == 0 && strcmp(surface->tokens[1], "my") == 0 &&
            strcmp(surface->tokens[2], "name") == 0 && strcmp(surface->tokens[3], "is") == 0) {
            return 1;
        }
        if (stage_code >= 70 && surface->token_count >= 3u &&
            strcmp(surface->tokens[surface->token_count - 2u], "is") == 0) {
            return 1;
        }
        if (stage_code >= 90 && surface->token_count >= 4u &&
            strcmp(surface->tokens[0], "no") == 0 &&
            strcmp(surface->tokens[surface->token_count - 2u], "is") == 0) {
            return 1;
        }
        if (stage_code >= 80 && surface->token_count >= 4u &&
            strcmp(surface->tokens[0], "i") == 0 && strcmp(surface->tokens[1], "live") == 0 &&
            strcmp(surface->tokens[2], "in") == 0) {
            return 1;
        }
        if (stage_code >= 80 && surface->token_count >= 4u &&
            strcmp(surface->tokens[0], "my") == 0 &&
            (strcmp(surface->tokens[1], "color") == 0 ||
             strcmp(surface->tokens[1], "pet") == 0 ||
             strcmp(surface->tokens[1], "kind") == 0) &&
            strcmp(surface->tokens[2], "is") == 0) {
            return 1;
        }
        return 0;
    }

    if (expected == CHAT_EXPECT_MEMORY_VALUE) {
        if (surface->token_count >= 4u && strcmp(surface->tokens[0], "what") == 0 &&
            strcmp(surface->tokens[1], "is") == 0 && strcmp(surface->tokens[2], "my") == 0 &&
            strcmp(surface->tokens[3], "name") == 0) {
            return 1;
        }
        if (stage_code >= 61 && surface->token_count >= 3u &&
            strcmp(surface->tokens[0], "who") == 0 && strcmp(surface->tokens[1], "am") == 0 &&
            strcmp(surface->tokens[2], "i") == 0) {
            return 1;
        }
        if (stage_code >= 61 && surface->token_count >= 4u &&
            strcmp(surface->tokens[0], "what") == 0 && strcmp(surface->tokens[1], "am") == 0 &&
            strcmp(surface->tokens[2], "i") == 0 && strcmp(surface->tokens[3], "called") == 0) {
            return 1;
        }
        if (stage_code >= 70 && surface->token_count >= 3u &&
            strcmp(surface->tokens[0], "what") == 0 && strcmp(surface->tokens[1], "is") == 0) {
            return 1;
        }
        if (stage_code >= 80 && surface->token_count >= 4u &&
            strcmp(surface->tokens[0], "where") == 0 && strcmp(surface->tokens[1], "do") == 0 &&
            strcmp(surface->tokens[2], "i") == 0 && strcmp(surface->tokens[3], "live") == 0) {
            return 1;
        }
        return 0;
    }

    return 0;
}

static int chatworld_surface_matches_supported_form(const ChatWorldSurface *surface,
                                                    int stage_code) {
    if (!surface || surface->token_count == 0u) {
        return 0;
    }
    if (surface->token_count == 1u &&
        (strcmp(surface->tokens[0], "hello") == 0 || strcmp(surface->tokens[0], "hi") == 0 ||
         strcmp(surface->tokens[0], "thanks") == 0)) {
        return 1;
    }
    if (surface->token_count == 2u && strcmp(surface->tokens[0], "thank") == 0 &&
        strcmp(surface->tokens[1], "you") == 0) {
        return 1;
    }
    if (surface->token_count >= 4u && strcmp(surface->tokens[0], "what") == 0 &&
        strcmp(surface->tokens[1], "is") == 0 && strcmp(surface->tokens[2], "my") == 0 &&
        strcmp(surface->tokens[3], "favorite") == 0) {
        return 1;
    }
    if (surface->token_count >= 4u && strcmp(surface->tokens[0], "my") == 0 &&
        strcmp(surface->tokens[1], "name") == 0 && strcmp(surface->tokens[2], "is") == 0) {
        return 1;
    }
    if (stage_code >= 52 && surface->token_count >= 4u &&
        strcmp(surface->tokens[0], "i") == 0 && strcmp(surface->tokens[1], "am") == 0 &&
        strcmp(surface->tokens[2], "called") == 0) {
        return 1;
    }
    if (stage_code >= 52 && surface->token_count >= 3u &&
        strcmp(surface->tokens[0], "call") == 0 && strcmp(surface->tokens[1], "me") == 0) {
        return 1;
    }
    if (stage_code >= 53 && surface->token_count >= 5u &&
        strcmp(surface->tokens[0], "no") == 0 && strcmp(surface->tokens[1], "my") == 0 &&
        strcmp(surface->tokens[2], "name") == 0 && strcmp(surface->tokens[3], "is") == 0) {
        return 1;
    }
    if (surface->token_count >= 4u && strcmp(surface->tokens[0], "what") == 0 &&
        strcmp(surface->tokens[1], "is") == 0 && strcmp(surface->tokens[2], "my") == 0 &&
        strcmp(surface->tokens[3], "name") == 0) {
        return 1;
    }
    if (stage_code >= 61 && surface->token_count >= 3u &&
        strcmp(surface->tokens[0], "who") == 0 && strcmp(surface->tokens[1], "am") == 0 &&
        strcmp(surface->tokens[2], "i") == 0) {
        return 1;
    }
    if (stage_code >= 61 && surface->token_count >= 4u &&
        strcmp(surface->tokens[0], "what") == 0 && strcmp(surface->tokens[1], "am") == 0 &&
        strcmp(surface->tokens[2], "i") == 0 && strcmp(surface->tokens[3], "called") == 0) {
        return 1;
    }
    if (stage_code >= 70 && surface->token_count >= 3u &&
        strcmp(surface->tokens[0], "what") == 0 && strcmp(surface->tokens[1], "is") == 0) {
        return 1;
    }
    if (stage_code >= 70 && surface->token_count >= 3u &&
        strcmp(surface->tokens[surface->token_count - 2u], "is") == 0) {
        return 1;
    }
    if (stage_code >= 90 && surface->token_count >= 4u &&
        strcmp(surface->tokens[0], "no") == 0 &&
        strcmp(surface->tokens[surface->token_count - 2u], "is") == 0) {
        return 1;
    }
    if (stage_code >= 80 && surface->token_count >= 4u &&
        strcmp(surface->tokens[0], "i") == 0 && strcmp(surface->tokens[1], "live") == 0 &&
        strcmp(surface->tokens[2], "in") == 0) {
        return 1;
    }
    if (stage_code >= 80 && surface->token_count >= 4u &&
        strcmp(surface->tokens[0], "where") == 0 && strcmp(surface->tokens[1], "do") == 0 &&
        strcmp(surface->tokens[2], "i") == 0 && strcmp(surface->tokens[3], "live") == 0) {
        return 1;
    }
    if (stage_code >= 80 && surface->token_count >= 4u &&
        strcmp(surface->tokens[0], "my") == 0 &&
        (strcmp(surface->tokens[1], "color") == 0 ||
         strcmp(surface->tokens[1], "pet") == 0 ||
         strcmp(surface->tokens[1], "kind") == 0) &&
        strcmp(surface->tokens[2], "is") == 0) {
        return 1;
    }
    return 0;
}

static void chatworld_set_validation_error(ChatWorldValidationResult *out, uint32_t line_no,
                                           const char *message) {
    if (!out) {
        return;
    }
    out->error_count++;
    if (out->first_error_line == 0u) {
        out->first_error_line = line_no;
        chatworld_copy_field(out->first_error, sizeof(out->first_error), message);
    }
}

static void chatworld_report_validation_error(FILE *err, const char *path, uint32_t line_no,
                                              const char *message) {
    if (err) {
        fprintf(err, "%s:%u: %s\n", path ? path : "<input>", line_no, message);
    }
}

int chatworld_validate_rows_file(const char *stage, const char *path, bool frozen_rows,
                                 FILE *err, ChatWorldValidationResult *out) {
    if (out) {
        memset(out, 0, sizeof(*out));
    }
    if (!stage || !path) {
        return -1;
    }
    int stage_code = chatworld_parse_stage_code(stage);
    if (stage_code == 0) {
        chatworld_set_validation_error(out, 0u, "unknown stage");
        chatworld_report_validation_error(err, path, 0u, "unknown stage");
        return -1;
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        chatworld_set_validation_error(out, 0u, "could not open input");
        chatworld_report_validation_error(err, path, 0u, "could not open input");
        return -1;
    }

    char line[384];
    uint32_t line_no = 0;
    while (fgets(line, sizeof(line), f)) {
        line_no++;
        if (line[0] == '\0' || line[0] == '#' || line[0] == '\r' || line[0] == '\n') {
            continue;
        }
        if (!strchr(line, '\n') && !feof(f)) {
            chatworld_set_validation_error(out, line_no, "line too long");
            chatworld_report_validation_error(err, path, line_no, "line too long");
            int ch;
            while ((ch = fgetc(f)) != '\n' && ch != EOF) {
            }
            continue;
        }

        char *fields[5] = {0};
        char *cursor = line;
        int columns = 1;
        for (uint32_t i = 0; i < 5u; ++i) {
            fields[i] = cursor;
            char *tab = strchr(cursor, '\t');
            if (tab) {
                *tab = '\0';
                cursor = tab + 1;
                columns++;
            } else {
                if (i < 4u) {
                    columns = (int)i + 1;
                }
                break;
            }
        }
        if (columns != 5 || (fields[4] && strchr(fields[4], '\t'))) {
            chatworld_set_validation_error(out, line_no, "bad column count");
            chatworld_report_validation_error(err, path, line_no, "bad column count");
            continue;
        }

        ChatWorldTurn t;
        memset(&t, 0, sizeof(t));
        chatworld_copy_field(t.utterance, sizeof(t.utterance), fields[0]);
        t.expected = chatworld_expected_from_string(fields[1]);
        chatworld_copy_token(t.expected_key, fields[2]);
        chatworld_copy_value(t.expected_value, sizeof(t.expected_value), fields[3]);
        t.learn = fields[4] && strtoul(fields[4], NULL, 10) != 0u;
        if (out) {
            out->row_count++;
        }

        if (t.utterance[0] == '\0') {
            chatworld_set_validation_error(out, line_no, "empty utterance");
            chatworld_report_validation_error(err, path, line_no, "empty utterance");
            continue;
        }
        if (t.expected == CHAT_EXPECT_NONE) {
            chatworld_set_validation_error(out, line_no, "unknown expected type");
            chatworld_report_validation_error(err, path, line_no, "unknown expected type");
            continue;
        }
        if (chatworld_field_has_forbidden_label(fields[0]) ||
            chatworld_field_has_forbidden_label(fields[1]) ||
            chatworld_field_has_forbidden_label(fields[2]) ||
            chatworld_field_has_forbidden_label(fields[3])) {
            chatworld_set_validation_error(out, line_no, "forbidden label word");
            chatworld_report_validation_error(err, path, line_no, "forbidden label word");
            continue;
        }
        if (frozen_rows && t.learn) {
            chatworld_set_validation_error(out, line_no, "frozen row has learn=1");
            chatworld_report_validation_error(err, path, line_no, "frozen row has learn=1");
            continue;
        }
        if (t.expected == CHAT_EXPECT_ACK &&
            ((t.expected_key[0] == '\0') != (t.expected_value[0] == '\0'))) {
            chatworld_set_validation_error(out, line_no, "ack row has partial key/value");
            chatworld_report_validation_error(err, path, line_no,
                                              "ack row has partial key/value");
            continue;
        }
        if (t.expected == CHAT_EXPECT_MEMORY_VALUE &&
            (t.expected_key[0] == '\0' || t.expected_value[0] == '\0')) {
            chatworld_set_validation_error(out, line_no, "missing key or value");
            chatworld_report_validation_error(err, path, line_no, "missing key or value");
            continue;
        }
        if ((t.expected == CHAT_EXPECT_GREET || t.expected == CHAT_EXPECT_UNKNOWN ||
             t.expected == CHAT_EXPECT_NO_SUPPORTED_RESPONSE) &&
            (t.expected_key[0] != '\0' || t.expected_value[0] != '\0')) {
            chatworld_set_validation_error(out, line_no, "response row smuggles answer fields");
            chatworld_report_validation_error(err, path, line_no,
                                              "response row smuggles answer fields");
            continue;
        }
        if (stage_code < 100 && chatworld_count_value_tokens(t.expected_value) > 1) {
            chatworld_set_validation_error(out, line_no, "multi-token value before phase6");
            chatworld_report_validation_error(err, path, line_no,
                                              "multi-token value before phase6");
            continue;
        }
        if (!chatworld_key_allowed_for_stage(t.expected_key, stage_code)) {
            chatworld_set_validation_error(out, line_no, "key unsupported for stage");
            chatworld_report_validation_error(err, path, line_no, "key unsupported for stage");
            continue;
        }
        if (t.expected == CHAT_EXPECT_NO_SUPPORTED_RESPONSE) {
            ChatWorldSurface unsupported_surface;
            if (chatworld_surface_parse(t.utterance, &unsupported_surface) == 0 &&
                chatworld_surface_matches_supported_form(&unsupported_surface, stage_code)) {
                chatworld_set_validation_error(out, line_no,
                                               "supported phrase marked unsupported");
                chatworld_report_validation_error(err, path, line_no,
                                                  "supported phrase marked unsupported");
                continue;
            }
        } else {
            ChatWorldSurface surface;
            if (chatworld_surface_parse(t.utterance, &surface) != 0 ||
                !chatworld_phrase_allowed_for_stage(&surface, t.expected, t.expected_key,
                                                    stage_code)) {
                char msg[128];
                snprintf(msg, sizeof(msg), "unsupported phrase form for %s",
                         chatworld_expected_name(t.expected));
                chatworld_set_validation_error(out, line_no, msg);
                chatworld_report_validation_error(err, path, line_no, msg);
                continue;
            }
        }
    }

    fclose(f);
    if (out && out->row_count == 0u) {
        chatworld_set_validation_error(out, 0u, "no rows");
        chatworld_report_validation_error(err, path, 0u, "no rows");
    }
    return (!out || out->error_count == 0u) ? 0 : -1;
}

void chatworld_reset(ChatWorld *w) {
    if (!w) {
        return;
    }
    memset(w, 0, sizeof(*w));
    w->memory = w->inline_memory;
    w->memory_cap = CHATWORLD_MEMORY_INLINE_CAP;
}

void chatworld_free(ChatWorld *w) {
    if (!w) {
        return;
    }
    if (w->memory && w->memory != w->inline_memory) {
        free(w->memory);
    }
    memset(w, 0, sizeof(*w));
}

const char *chatworld_action_name(ChatWorldAction action) {
    switch (action) {
    case CHAT_ACTION_MEM_WRITE:
        return "ACTION:MEM_WRITE";
    case CHAT_ACTION_MEM_READ:
        return "ACTION:MEM_READ";
    case CHAT_ACTION_RESP_UNKNOWN:
        return "ACTION:RESP_UNKNOWN";
    default:
        return "NO_ACTION";
    }
}

const char *chatworld_frame_name(ChatWorldFrame frame) {
    switch (frame) {
    case CHAT_FRAME_NO_SUPPORTED_RESPONSE:
        return "NO_SUPPORTED_RESPONSE";
    case CHAT_FRAME_OUTPUT:
        return "OUTPUT";
    case CHAT_FRAME_ACK:
        return "ACK";
    case CHAT_FRAME_UNKNOWN:
        return "RESP_UNKNOWN";
    case CHAT_FRAME_ANSWER_MEMORY:
        return "ANSWER_MEMORY";
    case CHAT_FRAME_CONTRADICTION_OR_AMBIGUOUS:
        return "CONTRADICTION_OR_AMBIGUOUS_RESPONSE";
    default:
        return "NO_SUPPORTED_RESPONSE";
    }
}

int chatworld_nerva_init(NervaEngine *e, ChatWorldNerva *cw) {
    if (!e || !cw) {
        return -1;
    }
    memset(cw, 0, sizeof(*cw));

    cw->turn_start = chatworld_get_or_create(e, "TURN_BOUNDARY:START");
    cw->turn_end = chatworld_get_or_create(e, "TURN_BOUNDARY:END");
    cw->speaker_user = chatworld_get_or_create(e, "SPEAKER:user");
    cw->speaker_assistant = chatworld_get_or_create(e, "SPEAKER:assistant");
    cw->bind_key = chatworld_get_or_create(e, "BIND_KEY");
    cw->bind_value = chatworld_get_or_create(e, "BIND_VALUE");
    if (cw->turn_start == UINT32_MAX || cw->turn_end == UINT32_MAX ||
        cw->speaker_user == UINT32_MAX || cw->speaker_assistant == UINT32_MAX ||
        cw->bind_key == UINT32_MAX || cw->bind_value == UINT32_MAX) {
        return -1;
    }

    char name[64];
    for (uint32_t i = 0; i < CHATWORLD_MAX_TOKENS; ++i) {
        snprintf(name, sizeof(name), "POSITION:%u", i);
        cw->position[i] = chatworld_get_or_create(e, name);
        snprintf(name, sizeof(name), "KEY_CANDIDATE:pos%u", i);
        cw->key_candidate[i] = chatworld_get_or_create(e, name);
        snprintf(name, sizeof(name), "VALUE_CANDIDATE:pos%u", i);
        cw->value_candidate[i] = chatworld_get_or_create(e, name);
        if (cw->position[i] == UINT32_MAX || cw->key_candidate[i] == UINT32_MAX ||
            cw->value_candidate[i] == UINT32_MAX) {
            return -1;
        }
    }

    for (uint32_t a = 0; a < CHAT_ACTION_COUNT; ++a) {
        cw->action_node[a] = chatworld_get_or_create(e, chatworld_action_name((ChatWorldAction)a));
        if (cw->action_node[a] == UINT32_MAX) {
            return -1;
        }
        e->nodes[cw->action_node[a]].theta_fire = CHATWORLD_ACTION_THETA_Q8_8;

        char gate_name[96];
        snprintf(gate_name, sizeof(gate_name), "ACTION_GATE:%s",
                 chatworld_action_name((ChatWorldAction)a));
        cw->action_gate_node[a] = chatworld_get_or_create(e, gate_name);
        if (cw->action_gate_node[a] == UINT32_MAX) {
            return -1;
        }
        e->nodes[cw->action_gate_node[a]].theta_fire = CHATWORLD_ACTION_GATE_THETA_Q8_8;
    }

    nerva_graph_rebuild_adjacency(e);
    return 0;
}

static int chatworld_surface_parse(const char *utterance, ChatWorldSurface *surface) {
    if (!utterance || !surface) {
        return -1;
    }
    memset(surface, 0, sizeof(*surface));

    char buf[CHATWORLD_MAX_TOKEN_LEN + 1u];
    uint32_t len = 0;
    for (uint32_t i = 0;; ++i) {
        unsigned char ch = (unsigned char)utterance[i];
        if (isalnum(ch) || ch == '\'') {
            if (len < CHATWORLD_MAX_TOKEN_LEN) {
                buf[len++] = (char)tolower(ch);
            }
        } else {
            if (len > 0) {
                buf[len] = '\0';
                if (surface->token_count < CHATWORLD_MAX_TOKENS) {
                    chatworld_copy_token(surface->tokens[surface->token_count], buf);
                    surface->token_count++;
                }
                len = 0;
            }
            if (ispunct(ch) && ch != '\'') {
                if (surface->punct_count < CHATWORLD_MAX_TOKENS) {
                    surface->punct[surface->punct_count++] = (char)ch;
                }
            }
            if (ch == '\0') {
                break;
            }
        }
    }
    return 0;
}

static uint32_t chatworld_value_tokenize(const char *value,
                                         char tokens[CHATWORLD_MAX_TOKENS]
                                                    [CHATWORLD_MAX_TOKEN_LEN + 1u]) {
    ChatWorldSurface surface;
    if (!value || chatworld_surface_parse(value, &surface) != 0) {
        return 0u;
    }
    for (uint32_t i = 0; i < surface.token_count; ++i) {
        chatworld_copy_token(tokens[i], surface.tokens[i]);
    }
    return surface.token_count;
}

static int chatworld_find_value_start(const ChatWorldSurface *surface, const char *value,
                                      uint32_t *token_count) {
    if (token_count) {
        *token_count = 0u;
    }
    if (!surface || !value || value[0] == '\0') {
        return -1;
    }

    char value_tokens[CHATWORLD_MAX_TOKENS][CHATWORLD_MAX_TOKEN_LEN + 1u];
    uint32_t count = chatworld_value_tokenize(value, value_tokens);
    if (count == 0u || count > surface->token_count) {
        return -1;
    }

    for (uint32_t i = 0; i + count <= surface->token_count; ++i) {
        int matched = 1;
        for (uint32_t j = 0; j < count; ++j) {
            if (strcmp(surface->tokens[i + j], value_tokens[j]) != 0) {
                matched = 0;
                break;
            }
        }
        if (matched) {
            if (token_count) {
                *token_count = count;
            }
            return (int)i;
        }
    }
    return -1;
}

static void chatworld_join_surface_tokens(const ChatWorldSurface *surface, uint32_t start,
                                          char *out, size_t out_size) {
    if (!out || out_size == 0u) {
        return;
    }
    out[0] = '\0';
    if (!surface || start >= surface->token_count) {
        return;
    }
    size_t used = 0u;
    for (uint32_t i = start; i < surface->token_count; ++i) {
        const char *tok = surface->tokens[i];
        size_t len = strlen(tok);
        size_t need = len + (used > 0u ? 1u : 0u);
        if (used + need + 1u > out_size) {
            break;
        }
        if (used > 0u) {
            out[used++] = ' ';
        }
        memcpy(out + used, tok, len);
        used += len;
        out[used] = '\0';
    }
}

int chatworld_tokenize(const char *utterance,
                       char tokens[CHATWORLD_MAX_TOKENS][CHATWORLD_MAX_TOKEN_LEN + 1u],
                       uint32_t *count) {
    ChatWorldSurface surface;
    if (!tokens || !count || chatworld_surface_parse(utterance, &surface) != 0) {
        return -1;
    }
    *count = surface.token_count;
    for (uint32_t i = 0; i < surface.token_count; ++i) {
        chatworld_copy_token(tokens[i], surface.tokens[i]);
    }
    return 0;
}

static int chatworld_output_token_index(const ChatWorldNerva *cw, const char *token) {
    if (!cw || !token || token[0] == '\0') {
        return -1;
    }
    char normalized[CHATWORLD_MAX_TOKEN_LEN + 1u];
    chatworld_copy_token(normalized, token);
    for (uint32_t i = 0; i < cw->output_count; ++i) {
        if (strcmp(cw->output_token[i], normalized) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static uint32_t chatworld_output_node_for_token(const ChatWorldNerva *cw, const char *token) {
    int idx = chatworld_output_token_index(cw, token);
    return idx >= 0 ? cw->output_node[(uint32_t)idx] : UINT32_MAX;
}

static int chatworld_key_token_index(const ChatWorldNerva *cw, const char *token) {
    if (!cw || !token || token[0] == '\0') {
        return -1;
    }
    char normalized[CHATWORLD_MAX_TOKEN_LEN + 1u];
    chatworld_copy_token(normalized, token);
    for (uint32_t i = 0; i < cw->key_count; ++i) {
        if (strcmp(cw->key_token[i], normalized) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static uint32_t chatworld_key_node_for_token(const ChatWorldNerva *cw, const char *token) {
    int idx = chatworld_key_token_index(cw, token);
    return idx >= 0 ? cw->key_node[(uint32_t)idx] : UINT32_MAX;
}

static uint32_t chatworld_preload_output_token(NervaEngine *e, ChatWorldNerva *cw,
                                               const char *token) {
    if (!e || !cw || !token || token[0] == '\0') {
        return UINT32_MAX;
    }
    char normalized[CHATWORLD_MAX_TOKEN_LEN + 1u];
    chatworld_copy_token(normalized, token);
    int existing = chatworld_output_token_index(cw, normalized);
    if (existing >= 0) {
        return cw->output_node[(uint32_t)existing];
    }
    if (cw->output_count >= CHATWORLD_MAX_VOCAB) {
        return UINT32_MAX;
    }

    char name[96];
    snprintf(name, sizeof(name), "OUTPUT_TOKEN:%.*s", (int)CHATWORLD_MAX_TOKEN_LEN, normalized);
    uint32_t node = chatworld_get_or_create(e, name);
    if (node == UINT32_MAX) {
        return UINT32_MAX;
    }

    uint32_t idx = cw->output_count++;
    cw->output_node[idx] = node;
    chatworld_copy_token(cw->output_token[idx], normalized);
    return node;
}

static uint32_t chatworld_preload_key_token(NervaEngine *e, ChatWorldNerva *cw,
                                            const char *token) {
    if (!e || !cw || !token || token[0] == '\0') {
        return UINT32_MAX;
    }
    char normalized[CHATWORLD_MAX_TOKEN_LEN + 1u];
    chatworld_copy_token(normalized, token);
    int existing = chatworld_key_token_index(cw, normalized);
    if (existing >= 0) {
        return cw->key_node[(uint32_t)existing];
    }
    if (cw->key_count >= CHATWORLD_MAX_VOCAB) {
        return UINT32_MAX;
    }

    char name[96];
    snprintf(name, sizeof(name), "KEY_TOKEN:%.*s", (int)CHATWORLD_MAX_TOKEN_LEN, normalized);
    uint32_t node = chatworld_get_or_create(e, name);
    if (node == UINT32_MAX) {
        return UINT32_MAX;
    }

    uint32_t idx = cw->key_count++;
    cw->key_node[idx] = node;
    chatworld_copy_token(cw->key_token[idx], normalized);
    return node;
}

static void chatworld_preload_output_value(NervaEngine *e, ChatWorldNerva *cw,
                                           const char *value) {
    if (!e || !cw || !value || value[0] == '\0') {
        return;
    }
    char tokens[CHATWORLD_MAX_TOKENS][CHATWORLD_MAX_TOKEN_LEN + 1u];
    uint32_t count = chatworld_value_tokenize(value, tokens);
    for (uint32_t i = 0; i < count; ++i) {
        chatworld_preload_output_token(e, cw, tokens[i]);
    }
}

static int chatworld_preload_surface(NervaEngine *e, const ChatWorldSurface *surface) {
    if (!e || !surface) {
        return -1;
    }
    char name[128];
    for (uint32_t i = 0; i < surface->token_count; ++i) {
        snprintf(name, sizeof(name), "TOKEN:%.*s", (int)CHATWORLD_MAX_TOKEN_LEN,
                 surface->tokens[i]);
        if (chatworld_get_or_create(e, name) == UINT32_MAX) {
            return -1;
        }
        snprintf(name, sizeof(name), "TOKEN_AT:%u:%.*s", i, (int)CHATWORLD_MAX_TOKEN_LEN,
                 surface->tokens[i]);
        if (chatworld_get_or_create(e, name) == UINT32_MAX) {
            return -1;
        }
    }
    for (uint32_t i = 0; i + 1u < surface->token_count; ++i) {
        snprintf(name, sizeof(name), "PAIR:%.*s:%.*s", (int)CHATWORLD_MAX_TOKEN_LEN,
                 surface->tokens[i], (int)CHATWORLD_MAX_TOKEN_LEN, surface->tokens[i + 1u]);
        if (chatworld_get_or_create(e, name) == UINT32_MAX) {
            return -1;
        }
    }
    for (uint32_t i = 0; i < surface->punct_count; ++i) {
        snprintf(name, sizeof(name), "PUNCT:%c", surface->punct[i]);
        if (chatworld_get_or_create(e, name) == UINT32_MAX) {
            return -1;
        }
    }
    return 0;
}

static void chatworld_edge_from_feature_to_all_positions(NervaEngine *e, const ChatWorldNerva *cw,
                                                         uint32_t feature_node) {
    if (!e || !cw || feature_node == UINT32_MAX) {
        return;
    }
    for (uint32_t pos = 0; pos < CHATWORLD_MAX_TOKENS; ++pos) {
        chatworld_create_edge_weight(e, feature_node, cw->key_candidate[pos],
                                     CHAT_REL_SURFACE_TO_BINDING, 0);
        chatworld_create_edge_weight(e, feature_node, cw->value_candidate[pos],
                                     CHAT_REL_SURFACE_TO_BINDING, 0);
    }
}

static int chatworld_preload_edges_for_surface(NervaEngine *e, const ChatWorldNerva *cw,
                                               const ChatWorldSurface *surface) {
    if (!e || !cw || !surface) {
        return -1;
    }
    char name[128];
    for (uint32_t i = 0; i < surface->token_count; ++i) {
        uint32_t feature = cw->position[i];
        chatworld_edge_from_feature_to_all_positions(e, cw, feature);
        snprintf(name, sizeof(name), "TOKEN:%.*s", (int)CHATWORLD_MAX_TOKEN_LEN,
                 surface->tokens[i]);
        feature = nerva_find_node_by_name(e, name);
        chatworld_edge_from_feature_to_all_positions(e, cw, feature);
        for (uint32_t a = 0; a < CHAT_ACTION_COUNT; ++a) {
            chatworld_create_edge_weight(e, feature, cw->action_node[a],
                                         CHAT_REL_SURFACE_TO_ACTION, 0);
            chatworld_create_edge_weight(e, feature, cw->action_gate_node[a],
                                         CHAT_REL_SURFACE_TO_ACTION_GATE, 0);
        }
        snprintf(name, sizeof(name), "TOKEN_AT:%u:%.*s", i, (int)CHATWORLD_MAX_TOKEN_LEN,
                 surface->tokens[i]);
        feature = nerva_find_node_by_name(e, name);
        chatworld_edge_from_feature_to_all_positions(e, cw, feature);
        for (uint32_t a = 0; a < CHAT_ACTION_COUNT; ++a) {
            chatworld_create_edge_weight(e, feature, cw->action_node[a],
                                         CHAT_REL_SURFACE_TO_ACTION, 0);
            chatworld_create_edge_weight(e, feature, cw->action_gate_node[a],
                                         CHAT_REL_SURFACE_TO_ACTION_GATE, 0);
        }
    }
    for (uint32_t i = 0; i + 1u < surface->token_count; ++i) {
        snprintf(name, sizeof(name), "PAIR:%.*s:%.*s", (int)CHATWORLD_MAX_TOKEN_LEN,
                 surface->tokens[i], (int)CHATWORLD_MAX_TOKEN_LEN, surface->tokens[i + 1u]);
        uint32_t feature = nerva_find_node_by_name(e, name);
        chatworld_edge_from_feature_to_all_positions(e, cw, feature);
        for (uint32_t a = 0; a < CHAT_ACTION_COUNT; ++a) {
            chatworld_create_edge_weight(e, feature, cw->action_node[a],
                                         CHAT_REL_SURFACE_TO_ACTION, 0);
            chatworld_create_edge_weight(e, feature, cw->action_gate_node[a],
                                         CHAT_REL_SURFACE_TO_ACTION_GATE, 0);
        }
    }
    for (uint32_t i = 0; i < surface->punct_count; ++i) {
        snprintf(name, sizeof(name), "PUNCT:%c", surface->punct[i]);
        uint32_t feature = nerva_find_node_by_name(e, name);
        chatworld_edge_from_feature_to_all_positions(e, cw, feature);
        for (uint32_t a = 0; a < CHAT_ACTION_COUNT; ++a) {
            chatworld_create_edge_weight(e, feature, cw->action_node[a],
                                         CHAT_REL_SURFACE_TO_ACTION, 0);
            chatworld_create_edge_weight(e, feature, cw->action_gate_node[a],
                                         CHAT_REL_SURFACE_TO_ACTION_GATE, 0);
        }
    }
    return 0;
}

static void chatworld_preload_binding_action_edges(NervaEngine *e, const ChatWorldNerva *cw) {
    if (!e || !cw) {
        return;
    }
    for (uint32_t pos = 0; pos < CHATWORLD_MAX_TOKENS; ++pos) {
        chatworld_create_edge_weight(e, cw->key_candidate[pos], cw->bind_key,
                                     CHAT_REL_SURFACE_TO_BINDING, CHATWORLD_SUPPORT_WEIGHT_Q8_8);
        chatworld_create_edge_weight(e, cw->value_candidate[pos], cw->bind_value,
                                     CHAT_REL_SURFACE_TO_BINDING, CHATWORLD_SUPPORT_WEIGHT_Q8_8);
        for (uint32_t value_pos = 0; value_pos < CHATWORLD_MAX_TOKENS; ++value_pos) {
            chatworld_create_edge_weight(e, cw->key_candidate[pos], cw->value_candidate[value_pos],
                                         CHAT_REL_KEY_TO_VALUE_BINDING, 0);
        }
        chatworld_create_edge_weight(e, cw->bind_key, cw->action_gate_node[CHAT_ACTION_MEM_READ],
                                      CHAT_REL_BINDING_TO_ACTION_GATE,
                                      CHATWORLD_SUPPORT_WEIGHT_Q8_8);
        chatworld_create_edge_weight(e, cw->bind_key, cw->action_gate_node[CHAT_ACTION_MEM_WRITE],
                                      CHAT_REL_BINDING_TO_ACTION_GATE,
                                      CHATWORLD_SUPPORT_WEIGHT_Q8_8);
        chatworld_create_edge_weight(e, cw->bind_value, cw->action_gate_node[CHAT_ACTION_MEM_WRITE],
                                      CHAT_REL_BINDING_TO_ACTION_GATE,
                                      CHATWORLD_SUPPORT_WEIGHT_Q8_8);
        chatworld_create_edge_weight(e, cw->key_candidate[pos],
                                     cw->action_node[CHAT_ACTION_RESP_UNKNOWN],
                                     CHAT_REL_BINDING_TO_ACTION, 0);
    }
    for (uint32_t a = 0; a < CHAT_ACTION_COUNT; ++a) {
        chatworld_create_edge_weight(e, cw->action_gate_node[a], cw->action_node[a],
                                     CHAT_REL_ACTION_GATE_TO_ACTION,
                                     CHATWORLD_ACTION_GATE_OUTPUT_WEIGHT_Q8_8);
    }
}

static void chatworld_preload_action_output_edges(NervaEngine *e, const ChatWorldNerva *cw) {
    if (!e || !cw) {
        return;
    }
    for (uint32_t i = 0; i < cw->output_count; ++i) {
        for (uint32_t a = 0; a < CHAT_ACTION_COUNT; ++a) {
            chatworld_create_edge_weight(e, cw->action_node[a], cw->output_node[i],
                                         CHAT_REL_ACTION_TO_OUTPUT, 0);
        }
    }
}

static void chatworld_preload_surface_output_edges(NervaEngine *e, const ChatWorldNerva *cw) {
    if (!e || !cw) {
        return;
    }
    for (uint32_t source = 0; source < e->node_count; ++source) {
        const char *name = nerva_name_by_id(e, e->nodes[source].name_id);
        if (!name) {
            continue;
        }
        if (strncmp(name, "TOKEN:", 6) != 0 && strncmp(name, "TOKEN_AT:", 9) != 0 &&
            strncmp(name, "PAIR:", 5) != 0 && strncmp(name, "PUNCT:", 6) != 0) {
            continue;
        }
        for (uint32_t out = 0; out < cw->output_count; ++out) {
            chatworld_create_edge_weight(e, source, cw->output_node[out],
                                         CHAT_REL_SURFACE_TO_OUTPUT, 0);
        }
    }
}

static void chatworld_preload_key_edges(NervaEngine *e, const ChatWorldNerva *cw) {
    if (!e || !cw) {
        return;
    }
    for (uint32_t source = 0; source < e->node_count; ++source) {
        const char *name = nerva_name_by_id(e, e->nodes[source].name_id);
        if (!name) {
            continue;
        }
        if (strncmp(name, "TOKEN:", 6) != 0 && strncmp(name, "TOKEN_AT:", 9) != 0 &&
            strncmp(name, "PAIR:", 5) != 0 && strncmp(name, "PUNCT:", 6) != 0) {
            continue;
        }
        for (uint32_t key = 0; key < cw->key_count; ++key) {
            chatworld_create_edge_weight(e, source, cw->key_node[key],
                                         CHAT_REL_SURFACE_TO_KEY_TOKEN, 0);
        }
    }
    for (uint32_t key = 0; key < cw->key_count; ++key) {
        for (uint32_t pos = 0; pos < CHATWORLD_MAX_TOKENS; ++pos) {
            chatworld_create_edge_weight(e, cw->key_node[key], cw->key_candidate[pos],
                                         CHAT_REL_KEY_TOKEN_TO_BINDING, 0);
        }
    }
}

int chatworld_preload_dataset(NervaEngine *e, ChatWorldNerva *cw, const ChatWorldDataset *ds) {
    if (!e || !cw || !ds) {
        return -1;
    }

    chatworld_preload_output_token(e, cw, "hello");
    chatworld_preload_output_token(e, cw, "ok");
    chatworld_preload_output_token(e, cw, "unknown");

    for (uint32_t i = 0; i < ds->count; ++i) {
        ChatWorldSurface surface;
        if (chatworld_surface_parse(ds->turns[i].utterance, &surface) != 0) {
            return -1;
        }
        if (chatworld_preload_surface(e, &surface) != 0) {
            return -1;
        }
        for (uint32_t j = 0; j < surface.token_count; ++j) {
            chatworld_preload_output_token(e, cw, surface.tokens[j]);
        }
        chatworld_preload_output_token(e, cw, ds->turns[i].expected_key);
        chatworld_preload_output_value(e, cw, ds->turns[i].expected_value);
        chatworld_preload_key_token(e, cw, ds->turns[i].expected_key);
    }

    chatworld_preload_binding_action_edges(e, cw);
    for (uint32_t i = 0; i < ds->count; ++i) {
        ChatWorldSurface surface;
        if (chatworld_surface_parse(ds->turns[i].utterance, &surface) != 0) {
            return -1;
        }
        if (chatworld_preload_edges_for_surface(e, cw, &surface) != 0) {
            return -1;
        }
    }
    chatworld_preload_action_output_edges(e, cw);
    chatworld_preload_surface_output_edges(e, cw);
    chatworld_preload_key_edges(e, cw);
    nerva_graph_rebuild_adjacency(e);
    return 0;
}

static void chatworld_quiesce_engine(NervaEngine *e) {
    if (!e) {
        return;
    }
    e->event_count = 0;
    e->active_count = 0;
    e->tick = 0;
    e->last_query_start_tick = 0;
    e->active_query_tag = 1u;
    e->trace_count = 0;
    for (uint32_t i = 0; i < e->node_count; ++i) {
        e->nodes[i].v = e->nodes[i].v_rest;
        e->nodes[i].last_fired_tick = 0;
        e->nodes[i].activation_count = 0;
    }
    nerva_debug_clear_fire_log(e);
}

static void chatworld_emit_node(NervaEngine *e, uint32_t node_id) {
    if (!e || node_id == UINT32_MAX) {
        return;
    }
    nerva_activate_node(e, node_id, e->nodes[node_id].theta_fire);
}

static void chatworld_tick_quiet(NervaEngine *e, uint32_t budget) {
    for (uint32_t i = 0; i < budget; ++i) {
        if (e->event_count == 0 && e->active_count == 0) {
            break;
        }
        nerva_tick(e);
    }
}

static int chatworld_emit_surface_internal(NervaEngine *e, ChatWorldNerva *cw,
                                           const char *utterance, int create_graph) {
    if (!e || !cw || !utterance) {
        return -1;
    }

    ChatWorldSurface surface;
    if (chatworld_surface_parse(utterance, &surface) != 0) {
        return -1;
    }

    if (create_graph && chatworld_preload_surface(e, &surface) != 0) {
        return -1;
    }

    chatworld_quiesce_engine(e);
    cw->token_count = surface.token_count;
    cw->pair_count = surface.token_count > 0u ? surface.token_count - 1u : 0u;
    cw->punct_count = surface.punct_count;

    chatworld_emit_node(e, cw->turn_start);
    chatworld_emit_node(e, cw->speaker_user);

    char name[128];
    for (uint32_t i = 0; i < surface.token_count; ++i) {
        snprintf(name, sizeof(name), "TOKEN:%.*s", (int)CHATWORLD_MAX_TOKEN_LEN,
                 surface.tokens[i]);
        cw->token_node[i] =
            create_graph ? chatworld_get_or_create(e, name) : nerva_find_node_by_name(e, name);
        chatworld_emit_node(e, cw->token_node[i]);
        chatworld_emit_node(e, cw->position[i]);

        snprintf(name, sizeof(name), "TOKEN_AT:%u:%.*s", i, (int)CHATWORLD_MAX_TOKEN_LEN,
                 surface.tokens[i]);
        cw->token_at_node[i] =
            create_graph ? chatworld_get_or_create(e, name) : nerva_find_node_by_name(e, name);
        chatworld_emit_node(e, cw->token_at_node[i]);
    }
    for (uint32_t i = surface.token_count; i < CHATWORLD_MAX_TOKENS; ++i) {
        cw->token_node[i] = UINT32_MAX;
        cw->token_at_node[i] = UINT32_MAX;
    }

    for (uint32_t i = 0; i < cw->pair_count; ++i) {
        snprintf(name, sizeof(name), "PAIR:%.*s:%.*s", (int)CHATWORLD_MAX_TOKEN_LEN,
                 surface.tokens[i], (int)CHATWORLD_MAX_TOKEN_LEN, surface.tokens[i + 1u]);
        cw->pair_node[i] =
            create_graph ? chatworld_get_or_create(e, name) : nerva_find_node_by_name(e, name);
        chatworld_emit_node(e, cw->pair_node[i]);
    }
    for (uint32_t i = cw->pair_count; i < CHATWORLD_MAX_TOKENS; ++i) {
        cw->pair_node[i] = UINT32_MAX;
    }

    for (uint32_t i = 0; i < surface.punct_count; ++i) {
        snprintf(name, sizeof(name), "PUNCT:%c", surface.punct[i]);
        cw->punct_node[i] =
            create_graph ? chatworld_get_or_create(e, name) : nerva_find_node_by_name(e, name);
        chatworld_emit_node(e, cw->punct_node[i]);
    }
    for (uint32_t i = surface.punct_count; i < CHATWORLD_MAX_TOKENS; ++i) {
        cw->punct_node[i] = UINT32_MAX;
    }

    chatworld_emit_node(e, cw->turn_end);
    nerva_graph_rebuild_adjacency(e);
    chatworld_tick_quiet(e, 12u);
    return 0;
}

int chatworld_emit_surface(NervaEngine *e, ChatWorldNerva *cw, const char *utterance) {
    return chatworld_emit_surface_internal(e, cw, utterance, 1);
}

static int chatworld_fire_log_contains(const NervaEngine *e, uint32_t node_id) {
    if (!e || node_id == UINT32_MAX) {
        return 0;
    }
    return node_id < e->node_count && e->nodes[node_id].activation_count > 0u;
}

static uint32_t chatworld_first_fired_position(const NervaEngine *e, const uint32_t *nodes,
                                               uint32_t count) {
    if (!e || !nodes) {
        return UINT32_MAX;
    }
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t node_id = nodes[i];
        if (node_id < e->node_count && e->nodes[node_id].activation_count > 0u) {
            return i;
        }
    }
    return UINT32_MAX;
}

static uint32_t chatworld_count_fired_actions(const NervaEngine *e, const ChatWorldNerva *cw,
                                              uint32_t *last_action) {
    uint32_t count = 0;
    if (last_action) {
        *last_action = CHAT_ACTION_COUNT;
    }
    for (uint32_t a = 0; a < CHAT_ACTION_COUNT; ++a) {
        if (chatworld_fire_log_contains(e, cw->action_node[a])) {
            count++;
            if (last_action) {
                *last_action = a;
            }
        }
    }
    return count;
}

static uint32_t chatworld_count_fired_outputs(const NervaEngine *e, const ChatWorldNerva *cw,
                                              uint32_t *last_output) {
    uint32_t count = 0;
    if (last_output) {
        *last_output = UINT32_MAX;
    }
    for (uint32_t i = 0; i < cw->output_count; ++i) {
        if (chatworld_fire_log_contains(e, cw->output_node[i])) {
            count++;
            if (last_output) {
                *last_output = i;
            }
        }
    }
    return count;
}

static uint32_t chatworld_find_named_feature(const NervaEngine *e, const char *prefix,
                                             const char *a, const char *b, uint32_t pos) {
    if (!e || !prefix || !a) {
        return UINT32_MAX;
    }
    char name[128];
    if (strcmp(prefix, "TOKEN") == 0) {
        snprintf(name, sizeof(name), "TOKEN:%.*s", (int)CHATWORLD_MAX_TOKEN_LEN, a);
    } else if (strcmp(prefix, "TOKEN_AT") == 0) {
        snprintf(name, sizeof(name), "TOKEN_AT:%u:%.*s", pos, (int)CHATWORLD_MAX_TOKEN_LEN, a);
    } else if (strcmp(prefix, "PAIR") == 0 && b) {
        snprintf(name, sizeof(name), "PAIR:%.*s:%.*s", (int)CHATWORLD_MAX_TOKEN_LEN, a,
                 (int)CHATWORLD_MAX_TOKEN_LEN, b);
    } else {
        return UINT32_MAX;
    }
    return nerva_find_node_by_name(e, name);
}

static void chatworld_add_binding_feature(const NervaEngine *e, ChatWorldPath *path,
                                          uint32_t feature_node, uint32_t target_node) {
    if (feature_node == UINT32_MAX || target_node == UINT32_MAX) {
        return;
    }
    chatworld_path_add_if_exists(e, path, feature_node, target_node, CHAT_REL_SURFACE_TO_BINDING);
}

static void chatworld_add_key_value_binding(const NervaEngine *e, ChatWorldPath *path) {
    if (!path || path->key_candidate_node == UINT32_MAX ||
        path->value_candidate_node == UINT32_MAX) {
        return;
    }
    chatworld_path_add_if_exists(e, path, path->key_candidate_node, path->value_candidate_node,
                                 CHAT_REL_KEY_TO_VALUE_BINDING);
}

static void chatworld_add_output_feature(const NervaEngine *e, ChatWorldPath *path,
                                         uint32_t feature_node) {
    if (feature_node == UINT32_MAX || path->output_node == UINT32_MAX) {
        return;
    }
    chatworld_path_add_if_exists(e, path, feature_node, path->output_node,
                                 CHAT_REL_SURFACE_TO_OUTPUT);
}

static void chatworld_add_action_feature(const NervaEngine *e, ChatWorldPath *path,
                                         uint32_t feature_node) {
    if (feature_node == UINT32_MAX || path->action_node == UINT32_MAX) {
        return;
    }
    if (path->kind == CHAT_PATH_MEM_WRITE || path->kind == CHAT_PATH_MEM_READ) {
        if (path->action_gate_node == UINT32_MAX) {
            return;
        }
        chatworld_path_add_if_exists(e, path, feature_node, path->action_gate_node,
                                     CHAT_REL_SURFACE_TO_ACTION_GATE);
        return;
    }
    chatworld_path_add_if_exists(e, path, feature_node, path->action_node,
                                 CHAT_REL_SURFACE_TO_ACTION);
}

static void chatworld_add_key_token_feature(const NervaEngine *e, const ChatWorldNerva *cw,
                                            ChatWorldPath *path, uint32_t feature_node,
                                            const char *key_token) {
    if (!cw || feature_node == UINT32_MAX || !key_token || key_token[0] == '\0') {
        return;
    }
    uint32_t key_node = chatworld_key_node_for_token(cw, key_token);
    if (key_node == UINT32_MAX || path->key_candidate_node == UINT32_MAX) {
        return;
    }
    chatworld_path_add_if_exists(e, path, feature_node, key_node,
                                 CHAT_REL_SURFACE_TO_KEY_TOKEN);
}

static void chatworld_select_response_features(const NervaEngine *e,
                                               const ChatWorldSurface *surface,
                                               ChatWorldPath *path) {
    if (!surface || !path || path->output_node == UINT32_MAX) {
        return;
    }
    if (path->action == CHAT_ACTION_RESP_UNKNOWN) {
        for (uint32_t i = 0; i < surface->token_count; ++i) {
            if (strcmp(surface->tokens[i], "favorite") == 0) {
                chatworld_add_output_feature(e, path,
                                             chatworld_find_named_feature(e, "TOKEN_AT",
                                                                          surface->tokens[i], NULL,
                                                                          i));
                if (i > 0u) {
                    chatworld_add_output_feature(
                        e, path,
                        chatworld_find_named_feature(e, "PAIR", surface->tokens[i - 1u],
                                                     surface->tokens[i], 0u));
                }
                if (i + 1u < surface->token_count) {
                    chatworld_add_output_feature(
                        e, path,
                        chatworld_find_named_feature(e, "PAIR", surface->tokens[i],
                                                     surface->tokens[i + 1u], 0u));
                }
            }
        }
        return;
    }

    for (uint32_t i = 0; i < surface->token_count; ++i) {
        chatworld_add_output_feature(
            e, path, chatworld_find_named_feature(e, "TOKEN_AT", surface->tokens[i], NULL, i));
        chatworld_add_output_feature(e, path,
                                     chatworld_find_named_feature(e, "TOKEN", surface->tokens[i],
                                                                  NULL, 0u));
    }
    for (uint32_t i = 0; i + 1u < surface->token_count; ++i) {
        chatworld_add_output_feature(
            e, path,
            chatworld_find_named_feature(e, "PAIR", surface->tokens[i], surface->tokens[i + 1u],
                                         0u));
    }
}

static void chatworld_select_unknown_action_features(const NervaEngine *e,
                                                     const ChatWorldSurface *surface,
                                                     ChatWorldPath *path) {
    if (!surface || !path || path->action_node == UINT32_MAX) {
        return;
    }
    for (uint32_t i = 0; i < surface->token_count; ++i) {
        if (strcmp(surface->tokens[i], "favorite") != 0) {
            continue;
        }
        uint32_t feature =
            chatworld_find_named_feature(e, "TOKEN_AT", surface->tokens[i], NULL, i);
        chatworld_add_action_feature(e, path, feature);
        feature = chatworld_find_named_feature(e, "TOKEN", surface->tokens[i], NULL, 0u);
        chatworld_add_action_feature(e, path, feature);
        if (i > 0u) {
            feature = chatworld_find_named_feature(e, "PAIR", surface->tokens[i - 1u],
                                                   surface->tokens[i], 0u);
            chatworld_add_action_feature(e, path, feature);
        }
        if (i + 1u < surface->token_count) {
            feature = chatworld_find_named_feature(e, "PAIR", surface->tokens[i],
                                                   surface->tokens[i + 1u], 0u);
            chatworld_add_action_feature(e, path, feature);
        }
    }
}

static void chatworld_add_feature_to_memory_path(const NervaEngine *e, ChatWorldPath *path,
                                                 uint32_t feature_node) {
    if (!path || feature_node == UINT32_MAX) {
        return;
    }
    chatworld_add_action_feature(e, path, feature_node);
}

static void chatworld_add_feature_context(const NervaEngine *e, const ChatWorldSurface *surface,
                                          ChatWorldPath *path, uint32_t pos,
                                          int include_token_node) {
    if (!surface || !path || pos >= surface->token_count) {
        return;
    }
    uint32_t feature =
        chatworld_find_named_feature(e, "TOKEN_AT", surface->tokens[pos], NULL, pos);
    chatworld_add_feature_to_memory_path(e, path, feature);
    if (include_token_node) {
        feature = chatworld_find_named_feature(e, "TOKEN", surface->tokens[pos], NULL, 0u);
        chatworld_add_feature_to_memory_path(e, path, feature);
    }
    if (pos > 0u) {
        feature = chatworld_find_named_feature(e, "PAIR", surface->tokens[pos - 1u],
                                               surface->tokens[pos], 0u);
        chatworld_add_feature_to_memory_path(e, path, feature);
    }
    if (pos + 1u < surface->token_count) {
        feature = chatworld_find_named_feature(e, "PAIR", surface->tokens[pos],
                                               surface->tokens[pos + 1u], 0u);
        chatworld_add_feature_to_memory_path(e, path, feature);
    }
}

static void chatworld_add_binding_from_feature(const NervaEngine *e, const ChatWorldNerva *cw,
                                               ChatWorldPath *path, uint32_t feature_node,
                                               const char *expected_key) {
    if (!path || feature_node == UINT32_MAX) {
        return;
    }
    if (path->key_candidate_node != UINT32_MAX && expected_key && expected_key[0] != '\0') {
        chatworld_add_key_token_feature(e, cw, path, feature_node, expected_key);
    }
    if (path->key_pos < CHATWORLD_MAX_TOKENS && path->key_candidate_node != UINT32_MAX) {
        chatworld_add_binding_feature(e, path, feature_node, path->key_candidate_node);
    }
}

static void chatworld_select_write_features(const NervaEngine *e, const ChatWorldNerva *cw,
                                            const ChatWorldSurface *surface,
                                            const char *expected_key, ChatWorldPath *path) {
    if (!surface || !path) {
        return;
    }

    uint32_t anchor = path->value_pos;
    if (anchor < surface->token_count && anchor > 0u) {
        anchor--;
    }
    if (anchor < surface->token_count) {
        uint32_t feature = UINT32_MAX;
        int copula_anchor = strcmp(surface->tokens[anchor], "is") == 0 ||
                            strcmp(surface->tokens[anchor], "are") == 0 ||
                            strcmp(surface->tokens[anchor], "am") == 0;
        if (copula_anchor && anchor > 0u) {
            chatworld_add_key_value_binding(e, path);
        } else {
            feature = chatworld_find_named_feature(e, "TOKEN_AT", surface->tokens[anchor], NULL,
                                                   anchor);
            chatworld_add_binding_feature(e, path, feature, path->value_candidate_node);
        }
        if (!copula_anchor) {
            chatworld_add_feature_context(e, surface, path, anchor, 0);
            chatworld_add_binding_from_feature(e, cw, path, feature, expected_key);
        }
    }

    for (uint32_t i = 0; i < surface->token_count; ++i) {
        if (i + 1u < surface->token_count &&
            (strcmp(surface->tokens[i + 1u], "is") == 0 ||
             strcmp(surface->tokens[i + 1u], "are") == 0 ||
             strcmp(surface->tokens[i + 1u], "am") == 0)) {
            uint32_t feature = chatworld_find_named_feature(e, "PAIR", surface->tokens[i],
                                                           surface->tokens[i + 1u], 0u);
            chatworld_add_action_feature(e, path, feature);
        }
        if (strcmp(surface->tokens[i], "called") == 0 || strcmp(surface->tokens[i], "call") == 0) {
            chatworld_add_feature_context(e, surface, path, i, 0);
            uint32_t feature =
                chatworld_find_named_feature(e, "TOKEN_AT", surface->tokens[i], NULL, i);
            if (expected_key && expected_key[0] != '\0' &&
                strcmp(surface->tokens[i], expected_key) == 0) {
                chatworld_add_binding_feature(e, path, feature, path->key_candidate_node);
            } else {
                chatworld_add_binding_from_feature(e, cw, path, feature, expected_key);
            }
        }
    }

    if (path->key_pos < surface->token_count) {
        uint32_t feature = chatworld_find_named_feature(e, "TOKEN_AT",
                                                       surface->tokens[path->key_pos], NULL,
                                                       path->key_pos);
        chatworld_add_binding_feature(e, path, feature, path->key_candidate_node);
        if (expected_key && expected_key[0] != '\0' &&
            strcmp(surface->tokens[path->key_pos], expected_key) != 0) {
            chatworld_add_key_token_feature(e, cw, path, feature, expected_key);
        }
    }
}

static void chatworld_select_read_features(const NervaEngine *e, const ChatWorldNerva *cw,
                                           const ChatWorldSurface *surface,
                                           const char *expected_key, ChatWorldPath *path) {
    if (!surface || !path) {
        return;
    }

    for (uint32_t i = 0; i < surface->token_count; ++i) {
        const char *tok = surface->tokens[i];
        if (strcmp(tok, "what") == 0) {
            uint32_t feature = chatworld_find_named_feature(e, "TOKEN_AT", tok, NULL, i);
            chatworld_add_action_feature(e, path, feature);
        }
        if (strcmp(tok, "who") == 0 || strcmp(tok, "where") == 0) {
            uint32_t feature = chatworld_find_named_feature(e, "TOKEN_AT", tok, NULL, i);
            chatworld_add_action_feature(e, path, feature);
            feature = chatworld_find_named_feature(e, "TOKEN", tok, NULL, 0u);
            chatworld_add_action_feature(e, path, feature);
            chatworld_add_binding_from_feature(e, cw, path, feature, expected_key);
            if (i + 1u < surface->token_count) {
                feature = chatworld_find_named_feature(e, "PAIR", tok, surface->tokens[i + 1u],
                                                       0u);
                chatworld_add_action_feature(e, path, feature);
            }
        }
        if (expected_key && expected_key[0] != '\0' && strcmp(tok, expected_key) == 0) {
            uint32_t feature = chatworld_find_named_feature(e, "TOKEN_AT", tok, NULL, i);
            chatworld_add_binding_feature(e, path, feature, path->key_candidate_node);
            if (i >= 2u) {
                chatworld_add_action_feature(e, path, feature);
            }
            chatworld_add_key_token_feature(e, cw, path, feature, expected_key);
        }
        if (strcmp(tok, "called") == 0 || strcmp(tok, "live") == 0 ||
            strcmp(tok, "color") == 0 || strcmp(tok, "pet") == 0 ||
            strcmp(tok, "where") == 0 || strcmp(tok, "who") == 0) {
            uint32_t feature = chatworld_find_named_feature(e, "TOKEN_AT", tok, NULL, i);
            if (i >= 2u || strcmp(tok, "where") == 0 || strcmp(tok, "who") == 0) {
                chatworld_add_action_feature(e, path, feature);
            }
            chatworld_add_binding_from_feature(e, cw, path, feature, expected_key);
        }
    }
}

static void chatworld_prepare_path(NervaEngine *e, const ChatWorldNerva *cw,
                                   const ChatWorldSurface *surface, const char *expected_key,
                                   ChatWorldPath *path) {
    if (!e || !cw || !surface || !path) {
        return;
    }

    if (path->key_pos < CHATWORLD_MAX_TOKENS) {
        path->key_candidate_node = cw->key_candidate[path->key_pos];
    }
    path->key_token_node =
        expected_key && expected_key[0] != '\0' ? chatworld_key_node_for_token(cw, expected_key)
                                                : UINT32_MAX;
    if (path->value_pos < CHATWORLD_MAX_TOKENS) {
        path->value_candidate_node = cw->value_candidate[path->value_pos];
    }
    path->action_node =
        path->action < CHAT_ACTION_COUNT ? cw->action_node[path->action] : UINT32_MAX;
    path->action_gate_node =
        path->action < CHAT_ACTION_COUNT ? cw->action_gate_node[path->action] : UINT32_MAX;

    if (path->kind == CHAT_PATH_RESPONSE) {
        chatworld_select_response_features(e, surface, path);
        if (path->action == CHAT_ACTION_RESP_UNKNOWN) {
            chatworld_select_unknown_action_features(e, surface, path);
        }
    } else if (path->kind == CHAT_PATH_MEM_WRITE) {
        chatworld_select_write_features(e, cw, surface, expected_key, path);
    } else if (path->kind == CHAT_PATH_MEM_READ) {
        chatworld_select_read_features(e, cw, surface, expected_key, path);
    }

    if (path->key_candidate_node != UINT32_MAX) {
        chatworld_path_add_if_exists(e, path, path->key_candidate_node, cw->bind_key,
                                     CHAT_REL_SURFACE_TO_BINDING);
        if (path->action == CHAT_ACTION_RESP_UNKNOWN) {
            chatworld_path_add_if_exists(e, path, path->key_candidate_node, path->action_node,
                                         CHAT_REL_BINDING_TO_ACTION);
        }
    }
    if (path->kind == CHAT_PATH_MEM_WRITE && path->value_candidate_node != UINT32_MAX) {
        chatworld_path_add_if_exists(e, path, path->value_candidate_node, cw->bind_value,
                                     CHAT_REL_SURFACE_TO_BINDING);
    }
    if (path->kind == CHAT_PATH_MEM_WRITE) {
        chatworld_path_add_if_exists(e, path, cw->bind_key, path->action_gate_node,
                                     CHAT_REL_BINDING_TO_ACTION_GATE);
        chatworld_path_add_if_exists(e, path, cw->bind_value, path->action_gate_node,
                                     CHAT_REL_BINDING_TO_ACTION_GATE);
    } else if (path->kind == CHAT_PATH_MEM_READ) {
        chatworld_path_add_if_exists(e, path, cw->bind_key, path->action_gate_node,
                                     CHAT_REL_BINDING_TO_ACTION_GATE);
    }
    if ((path->kind == CHAT_PATH_MEM_WRITE || path->kind == CHAT_PATH_MEM_READ) &&
        path->action_gate_node != UINT32_MAX && path->action_node != UINT32_MAX) {
        chatworld_path_add_if_exists(e, path, path->action_gate_node, path->action_node,
                                     CHAT_REL_ACTION_GATE_TO_ACTION);
    }
    if (path->kind == CHAT_PATH_MEM_WRITE && path->output_node != UINT32_MAX) {
        chatworld_path_add_if_exists(e, path, path->action_node, path->output_node,
                                     CHAT_REL_ACTION_TO_OUTPUT);
    }
    if (path->output_node_count == 0u && path->output_node != UINT32_MAX) {
        path->output_nodes[path->output_node_count++] = path->output_node;
    }
}

static int chatworld_find_first_token(const ChatWorldSurface *surface, const char *token) {
    if (!surface || !token || token[0] == '\0') {
        return -1;
    }
    for (uint32_t i = 0; i < surface->token_count; ++i) {
        if (strcmp(surface->tokens[i], token) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static uint32_t chatworld_default_key_pos_for_write(const ChatWorldSurface *surface,
                                                    const char *expected_key,
                                                    uint32_t value_pos) {
    int pos = chatworld_find_first_token(surface, expected_key);
    if (pos >= 0) {
        return (uint32_t)pos;
    }
    if (!surface || !expected_key || expected_key[0] == '\0') {
        return UINT32_MAX;
    }
    if (strcmp(expected_key, "name") == 0) {
        return CHATWORLD_MAX_TOKENS - 1u;
    }
    if (strcmp(expected_key, "city") == 0 || strcmp(expected_key, "live") == 0) {
        return CHATWORLD_MAX_TOKENS - 1u;
    }
    if (value_pos > 0u) {
        return value_pos - 1u;
    }
    return UINT32_MAX;
}

static uint32_t chatworld_default_key_pos_for_read(const ChatWorldSurface *surface,
                                                   const char *expected_key) {
    int pos = chatworld_find_first_token(surface, expected_key);
    if (pos >= 0) {
        return (uint32_t)pos;
    }
    if (!surface || !expected_key || expected_key[0] == '\0') {
        return UINT32_MAX;
    }
    if (strcmp(expected_key, "name") == 0 && surface->token_count >= 2u) {
        return surface->token_count - 1u;
    }
    if ((strcmp(expected_key, "city") == 0 || strcmp(expected_key, "live") == 0) &&
        surface->token_count > 0u) {
        return CHATWORLD_MAX_TOKENS - 1u;
    }
    if (surface->token_count >= 3u && strcmp(surface->tokens[0], "what") == 0 &&
        strcmp(surface->tokens[1], "is") == 0) {
        return 2u;
    }
    return surface->token_count > 0u ? surface->token_count - 1u : UINT32_MAX;
}

static void chatworld_fill_output_nodes_for_value(const ChatWorldNerva *cw, ChatWorldPath *path,
                                                  const char *value) {
    if (!cw || !path || !value || value[0] == '\0') {
        return;
    }
    char tokens[CHATWORLD_MAX_TOKENS][CHATWORLD_MAX_TOKEN_LEN + 1u];
    uint32_t count = chatworld_value_tokenize(value, tokens);
    for (uint32_t i = 0; i < count && i < CHATWORLD_MAX_TOKENS; ++i) {
        uint32_t node = chatworld_output_node_for_token(cw, tokens[i]);
        if (node != UINT32_MAX) {
            path->output_nodes[path->output_node_count++] = node;
            if (path->output_node == UINT32_MAX) {
                path->output_node = node;
            }
        }
    }
}

static void chatworld_make_expected_path(NervaEngine *e, const ChatWorldNerva *cw,
                                         const ChatWorldTurn *turn,
                                         const ChatWorldSurface *surface,
                                         ChatWorldPath *path) {
    memset(path, 0, sizeof(*path));
    path->key_pos = UINT32_MAX;
    path->value_pos = UINT32_MAX;
    path->key_token_node = UINT32_MAX;
    path->key_candidate_node = UINT32_MAX;
    path->value_candidate_node = UINT32_MAX;
    path->output_node = UINT32_MAX;
    path->output_node_count = 0u;
    path->value_token_count = 0u;

    switch (turn->expected) {
    case CHAT_EXPECT_GREET:
        path->kind = CHAT_PATH_RESPONSE;
        path->action = CHAT_ACTION_COUNT;
        path->key_pos = 0u;
        path->output_node = chatworld_output_node_for_token(cw, "hello");
        break;
    case CHAT_EXPECT_ACK:
        if (turn->expected_key[0] != '\0' && turn->expected_value[0] != '\0') {
            path->kind = CHAT_PATH_MEM_WRITE;
            path->action = CHAT_ACTION_MEM_WRITE;
            int value_start =
                chatworld_find_value_start(surface, turn->expected_value, &path->value_token_count);
            if (value_start >= 0) {
                path->value_pos = (uint32_t)value_start;
            }
            path->key_pos =
                chatworld_default_key_pos_for_write(surface, turn->expected_key, path->value_pos);
            path->output_node = chatworld_output_node_for_token(cw, "ok");
        } else {
            path->kind = CHAT_PATH_RESPONSE;
            path->action = CHAT_ACTION_COUNT;
            path->key_pos = 0u;
            path->output_node = chatworld_output_node_for_token(cw, "ok");
        }
        break;
    case CHAT_EXPECT_UNKNOWN:
        path->kind = CHAT_PATH_RESPONSE;
        path->action = CHAT_ACTION_RESP_UNKNOWN;
        path->key_pos = 0u;
        path->output_node = chatworld_output_node_for_token(cw, "unknown");
        break;
    case CHAT_EXPECT_MEMORY_VALUE:
        path->kind = CHAT_PATH_MEM_READ;
        path->action = CHAT_ACTION_MEM_READ;
        path->key_pos = chatworld_default_key_pos_for_read(surface, turn->expected_key);
        chatworld_fill_output_nodes_for_value(cw, path, turn->expected_value);
        break;
    case CHAT_EXPECT_NO_SUPPORTED_RESPONSE:
        path->kind = CHAT_PATH_RESPONSE;
        path->action = CHAT_ACTION_COUNT;
        break;
    default:
        break;
    }

    if (path->key_pos == UINT32_MAX && surface->token_count > 0u) {
        if (path->kind == CHAT_PATH_MEM_WRITE && turn->expected_key[0] != '\0') {
            path->key_pos = CHATWORLD_MAX_TOKENS - 1u;
        } else {
            path->key_pos = surface->token_count - 1u;
        }
    }
    if (path->kind != CHAT_PATH_MEM_READ && path->value_pos == UINT32_MAX &&
        surface->token_count > 0u) {
        path->value_pos = surface->token_count - 1u;
    }
    chatworld_prepare_path(e, cw, surface, turn->expected_key, path);
}

static void chatworld_force_path_fire(NervaEngine *e, ChatWorldNerva *cw, const ChatWorldPath *path) {
    if (!e || !cw || !path) {
        return;
    }
    if (path->kind != CHAT_PATH_RESPONSE && path->key_candidate_node != UINT32_MAX) {
        chatworld_emit_node(e, path->key_candidate_node);
        chatworld_tick_quiet(e, 2u);
    }
    if (path->kind == CHAT_PATH_MEM_WRITE && path->value_candidate_node != UINT32_MAX) {
        chatworld_emit_node(e, path->value_candidate_node);
        chatworld_tick_quiet(e, 2u);
    }
    if ((path->kind == CHAT_PATH_MEM_WRITE || path->kind == CHAT_PATH_MEM_READ) &&
        path->action_gate_node != UINT32_MAX) {
        chatworld_emit_node(e, path->action_gate_node);
        chatworld_tick_quiet(e, 2u);
    }
    if (path->action_node != UINT32_MAX) {
        chatworld_emit_node(e, path->action_node);
        chatworld_tick_quiet(e, 2u);
    }
    if (path->kind != CHAT_PATH_MEM_READ && path->output_node != UINT32_MAX) {
        chatworld_emit_node(e, path->output_node);
        chatworld_tick_quiet(e, 2u);
    }
    if (path->kind == CHAT_PATH_MEM_READ) {
        for (uint32_t i = 0; i < path->output_node_count; ++i) {
            chatworld_emit_node(e, path->output_nodes[i]);
            chatworld_tick_quiet(e, 2u);
        }
    }
}

static void chatworld_queue_path_feedback(NervaEngine *e, const ChatWorldPath *path,
                                          int positive) {
    if (!e || !path) {
        return;
    }
    for (uint32_t i = 0; i < path->selected_edge_count; ++i) {
        uint32_t edge_id = path->selected_edges[i];
        if (edge_id == UINT32_MAX || edge_id >= e->edge_count) {
            continue;
        }
        if (chatworld_is_fixed_support_edge(e, edge_id)) {
            continue;
        }
        if (positive) {
            nerva_queue_weight_delta(e, edge_id, e->cfg.ltp_delta_q8_8,
                                     NERVA_REASON_FEEDBACK_CORRECT,
                                     e->edges[edge_id].trace_tag);
        } else {
            nerva_queue_weight_delta(e, edge_id, e->cfg.ltd_delta_q8_8,
                                     NERVA_REASON_FEEDBACK_WRONG,
                                     e->edges[edge_id].trace_tag);
        }
    }
}

static int chatworld_memory_find(const ChatWorld *w, const char *key) {
    if (!w || !key || key[0] == '\0') {
        return -1;
    }
    for (uint32_t i = 0; i < w->memory_count; ++i) {
        if (strcmp(w->memory[i].key, key) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int chatworld_memory_reserve(ChatWorld *w, uint32_t needed) {
    if (!w) {
        return -1;
    }
    if (!w->memory) {
        chatworld_reset(w);
    }
    if (needed <= w->memory_cap) {
        return 0;
    }
    uint32_t new_cap = w->memory_cap == 0u ? CHATWORLD_MEMORY_INLINE_CAP : w->memory_cap * 2u;
    while (new_cap < needed) {
        new_cap *= 2u;
    }
    ChatWorldMemoryPair *new_memory =
        (ChatWorldMemoryPair *)calloc(new_cap, sizeof(ChatWorldMemoryPair));
    if (!new_memory) {
        return -1;
    }
    if (w->memory && w->memory_count > 0u) {
        memcpy(new_memory, w->memory, (size_t)w->memory_count * sizeof(ChatWorldMemoryPair));
    }
    if (w->memory && w->memory != w->inline_memory) {
        free(w->memory);
    }
    w->memory = new_memory;
    w->memory_cap = new_cap;
    return 0;
}

static void chatworld_memory_write(ChatWorld *w, const char *key, const char *value) {
    if (!w || !key || !value || key[0] == '\0' || value[0] == '\0') {
        return;
    }
    int idx = chatworld_memory_find(w, key);
    if (idx < 0) {
        if (chatworld_memory_reserve(w, w->memory_count + 1u) != 0) {
            return;
        }
        idx = (int)w->memory_count++;
    }
    chatworld_copy_token(w->memory[idx].key, key);
    chatworld_copy_value(w->memory[idx].value, sizeof(w->memory[idx].value), value);
    w->memory[idx].strength++;
}

static int chatworld_memory_read(const ChatWorld *w, const char *key, char *value) {
    int idx = chatworld_memory_find(w, key);
    if (idx < 0) {
        return 0;
    }
    chatworld_copy_value(value, CHATWORLD_MAX_VALUE_LEN + 1u, w->memory[idx].value);
    return 1;
}

static void chatworld_collect_used_edges_for_fired_path(const NervaEngine *e, ChatWorldDecision *d) {
    if (!e || !d) {
        return;
    }
    d->selected_edge_count = 0;
    uint32_t limit = e->trace_count;
    if (limit > e->cfg.trace_decay_scan_limit) {
        limit = e->cfg.trace_decay_scan_limit;
    }
    for (uint32_t age = limit; age > 0; --age) {
        NervaTrace *t = nerva_trace_recent((NervaEngine *)e, age - 1u);
        if (!t || !(t->flags & NERVA_TRACE_USED_PATH) || t->edge_id == NERVA_INVALID_ID ||
            t->edge_id >= e->edge_count) {
            continue;
        }
        uint16_t rel = e->edges[t->edge_id].relation;
        if (rel != CHAT_REL_SURFACE_TO_BINDING && rel != CHAT_REL_BINDING_TO_ACTION &&
            rel != CHAT_REL_ACTION_TO_OUTPUT && rel != CHAT_REL_SURFACE_TO_OUTPUT &&
            rel != CHAT_REL_SURFACE_TO_ACTION && rel != CHAT_REL_SURFACE_TO_KEY_TOKEN &&
            rel != CHAT_REL_KEY_TOKEN_TO_BINDING &&
            rel != CHAT_REL_SURFACE_TO_ACTION_GATE &&
            rel != CHAT_REL_BINDING_TO_ACTION_GATE &&
            rel != CHAT_REL_ACTION_GATE_TO_ACTION &&
            rel != CHAT_REL_KEY_TO_VALUE_BINDING) {
            continue;
        }
        if (d->selected_edge_count < CHATWORLD_MAX_SELECTED_EDGES) {
            int seen = 0;
            for (uint32_t i = 0; i < d->selected_edge_count; ++i) {
                if (d->selected_edges[i] == t->edge_id) {
                    seen = 1;
                    break;
                }
            }
            if (!seen) {
                d->selected_edges[d->selected_edge_count++] = t->edge_id;
            }
        }
    }
}

static void chatworld_emit_memory_outputs(NervaEngine *e, ChatWorldNerva *cw,
                                          const char *remembered,
                                          ChatWorldDecision *d) {
    if (!e || !cw || !remembered || !d) {
        return;
    }
    char tokens[CHATWORLD_MAX_TOKENS][CHATWORLD_MAX_TOKEN_LEN + 1u];
    uint32_t count = chatworld_value_tokenize(remembered, tokens);
    if (count == 0u) {
        d->no_supported_response = true;
        d->frame = CHAT_FRAME_NO_SUPPORTED_RESPONSE;
        return;
    }

    char rendered[CHATWORLD_MAX_RENDERED];
    rendered[0] = '\0';
    size_t used = 0u;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t output_node = chatworld_output_node_for_token(cw, tokens[i]);
        if (output_node == UINT32_MAX) {
            d->no_supported_response = true;
            d->frame = CHAT_FRAME_NO_SUPPORTED_RESPONSE;
            return;
        }
        chatworld_emit_node(e, output_node);
        chatworld_tick_quiet(e, 2u);
        size_t len = strlen(tokens[i]);
        size_t need = len + (used > 0u ? 1u : 0u);
        if (used + need + 1u >= sizeof(rendered)) {
            d->no_supported_response = true;
            d->frame = CHAT_FRAME_NO_SUPPORTED_RESPONSE;
            return;
        }
        if (used > 0u) {
            rendered[used++] = ' ';
        }
        memcpy(rendered + used, tokens[i], len);
        used += len;
        rendered[used] = '\0';
    }

    chatworld_copy_value(d->value, sizeof(d->value), remembered);
    chatworld_copy_field(d->rendered, sizeof(d->rendered), rendered);
    d->output_fired = true;
    d->fired_output_count = count;
    d->value_token_count = count;
    d->frame = CHAT_FRAME_ANSWER_MEMORY;
}

static int chatworld_exact_fired_binding(const NervaEngine *e, const ChatWorldNerva *cw,
                                         uint32_t *key_pos, uint32_t *value_pos,
                                         uint32_t *key_node) {
    uint32_t found_key_pos = UINT32_MAX;
    uint32_t found_value_pos = UINT32_MAX;
    uint32_t found_key_node = UINT32_MAX;
    uint32_t key_count = 0u;
    uint32_t value_count = 0u;
    uint32_t key_node_count = 0u;

    if (!e || !cw) {
        return -1;
    }

    uint32_t limit = e->trace_count;
    if (limit > e->cfg.trace_decay_scan_limit) {
        limit = e->cfg.trace_decay_scan_limit;
    }
    for (uint32_t age = limit; age > 0u; --age) {
        NervaTrace *t = nerva_trace_recent((NervaEngine *)e, age - 1u);
        if (!t || !(t->flags & NERVA_TRACE_USED_PATH) || t->edge_id == NERVA_INVALID_ID ||
            t->edge_id >= e->edge_count) {
            continue;
        }
        const NervaEdge *ed = &e->edges[t->edge_id];
        if (ed->relation == CHAT_REL_SURFACE_TO_BINDING && ed->target == cw->bind_key) {
            for (uint32_t pos = 0; pos < CHATWORLD_MAX_TOKENS; ++pos) {
                if (ed->source == cw->key_candidate[pos]) {
                    if (found_key_pos != pos) {
                        found_key_pos = pos;
                        key_count++;
                    }
                    break;
                }
            }
        }
        if (value_pos && ed->relation == CHAT_REL_SURFACE_TO_BINDING &&
            ed->target == cw->bind_value) {
            for (uint32_t pos = 0; pos < CHATWORLD_MAX_TOKENS; ++pos) {
                if (ed->source == cw->value_candidate[pos]) {
                    if (found_value_pos != pos) {
                        found_value_pos = pos;
                        value_count++;
                    }
                    break;
                }
            }
        }
        if (ed->relation == CHAT_REL_SURFACE_TO_KEY_TOKEN) {
            for (uint32_t idx = 0; idx < cw->key_count; ++idx) {
                if (ed->target == cw->key_node[idx]) {
                    if (found_key_node != idx) {
                        found_key_node = idx;
                        key_node_count++;
                    }
                    break;
                }
            }
        }
    }

    if (key_count > 1u || key_node_count > 1u || value_count > 1u) {
        return 1;
    }
    if (key_count == 0u && key_node_count == 0u) {
        return -1;
    }
    if (value_pos && value_count == 0u) {
        return -1;
    }
    if (key_pos) {
        *key_pos = found_key_pos;
    }
    if (value_pos) {
        *value_pos = found_value_pos;
    }
    if (key_node) {
        *key_node = found_key_node;
    }
    return 0;
}

static void chatworld_mark_ambiguous(ChatWorldDecision *d) {
    if (!d) {
        return;
    }
    d->ambiguous_response = true;
    d->no_supported_response = false;
    d->frame = CHAT_FRAME_CONTRADICTION_OR_AMBIGUOUS;
}

static void chatworld_fill_decision_from_fires(NervaEngine *e, ChatWorldNerva *cw, ChatWorld *w,
                                               const ChatWorldSurface *surface,
                                               ChatWorldDecision *d) {
    uint32_t last_action = CHAT_ACTION_COUNT;
    uint32_t last_output = UINT32_MAX;
    d->fired_action_count = chatworld_count_fired_actions(e, cw, &last_action);
    d->fired_output_count = chatworld_count_fired_outputs(e, cw, &last_output);
    d->mem_write_fired = chatworld_fire_log_contains(e, cw->action_node[CHAT_ACTION_MEM_WRITE]);
    d->mem_read_fired = chatworld_fire_log_contains(e, cw->action_node[CHAT_ACTION_MEM_READ]);
    d->resp_unknown_fired =
        chatworld_fire_log_contains(e, cw->action_node[CHAT_ACTION_RESP_UNKNOWN]);
    d->output_fired = d->fired_output_count > 0u;
    d->key_pos = chatworld_first_fired_position(e, cw->key_candidate, CHATWORLD_MAX_TOKENS);
    d->value_pos = chatworld_first_fired_position(e, cw->value_candidate, CHATWORLD_MAX_TOKENS);

    if (d->fired_action_count > 1u || (d->fired_output_count > 1u && d->fired_action_count == 0u) ||
        (d->mem_write_fired && d->mem_read_fired)) {
        d->ambiguous_response = true;
        d->frame = CHAT_FRAME_CONTRADICTION_OR_AMBIGUOUS;
        return;
    }
    if (d->fired_action_count == 0u && d->fired_output_count == 0u) {
        d->no_supported_response = true;
        d->frame = CHAT_FRAME_NO_SUPPORTED_RESPONSE;
        return;
    }

    d->action = (ChatWorldAction)last_action;
    if (last_output != UINT32_MAX && !d->mem_write_fired && !d->mem_read_fired) {
        chatworld_copy_field(d->rendered, sizeof(d->rendered), cw->output_token[last_output]);
        chatworld_copy_token(d->value, cw->output_token[last_output]);
    }

    if (d->mem_write_fired) {
        uint32_t fired_key_node = UINT32_MAX;
        int binding_rc = chatworld_exact_fired_binding(e, cw, &d->key_pos, &d->value_pos,
                                                       &fired_key_node);
        if (binding_rc > 0) {
            chatworld_mark_ambiguous(d);
            return;
        }
        if (binding_rc < 0) {
            d->no_supported_response = true;
            d->frame = CHAT_FRAME_NO_SUPPORTED_RESPONSE;
            return;
        }
        if ((d->key_pos < surface->token_count || fired_key_node != UINT32_MAX) &&
            d->value_pos < surface->token_count) {
            if (fired_key_node != UINT32_MAX) {
                chatworld_copy_token(d->key, cw->key_token[fired_key_node]);
            } else if (d->key_pos < surface->token_count &&
                       chatworld_key_node_for_token(cw, surface->tokens[d->key_pos]) !=
                           UINT32_MAX) {
                chatworld_copy_token(d->key, surface->tokens[d->key_pos]);
            } else {
                d->no_supported_response = true;
                d->frame = CHAT_FRAME_NO_SUPPORTED_RESPONSE;
                return;
            }
            chatworld_join_surface_tokens(surface, d->value_pos, d->value, sizeof(d->value));
            chatworld_memory_write(w, d->key, d->value);
            d->frame = CHAT_FRAME_ACK;
        } else {
            d->no_supported_response = true;
            d->frame = CHAT_FRAME_NO_SUPPORTED_RESPONSE;
        }
    } else if (d->mem_read_fired) {
        uint32_t fired_key_node = UINT32_MAX;
        int binding_rc = chatworld_exact_fired_binding(e, cw, &d->key_pos, NULL,
                                                       &fired_key_node);
        if (binding_rc > 0) {
            chatworld_mark_ambiguous(d);
            return;
        }
        if (binding_rc < 0) {
            d->no_supported_response = true;
            d->frame = CHAT_FRAME_NO_SUPPORTED_RESPONSE;
            return;
        }
        if (d->key_pos < surface->token_count || fired_key_node != UINT32_MAX) {
            if (fired_key_node != UINT32_MAX) {
                chatworld_copy_token(d->key, cw->key_token[fired_key_node]);
            } else if (d->key_pos < surface->token_count &&
                       chatworld_key_node_for_token(cw, surface->tokens[d->key_pos]) !=
                           UINT32_MAX) {
                chatworld_copy_token(d->key, surface->tokens[d->key_pos]);
            } else {
                d->no_supported_response = true;
                d->frame = CHAT_FRAME_NO_SUPPORTED_RESPONSE;
                return;
            }
            char remembered[CHATWORLD_MAX_VALUE_LEN + 1u];
            if (chatworld_memory_read(w, d->key, remembered)) {
                chatworld_emit_memory_outputs(e, cw, remembered, d);
            } else {
                d->no_supported_response = true;
                d->frame = CHAT_FRAME_NO_SUPPORTED_RESPONSE;
            }
        } else {
            d->no_supported_response = true;
            d->frame = CHAT_FRAME_NO_SUPPORTED_RESPONSE;
        }
    } else if (d->resp_unknown_fired) {
        d->frame = CHAT_FRAME_UNKNOWN;
    } else if (d->output_fired) {
        d->frame = CHAT_FRAME_OUTPUT;
    } else {
        d->no_supported_response = true;
        d->frame = CHAT_FRAME_NO_SUPPORTED_RESPONSE;
    }

    chatworld_collect_used_edges_for_fired_path(e, d);
}

static ChatWorldDecision chatworld_step_internal(NervaEngine *e, ChatWorldNerva *cw, ChatWorld *w,
                                                 const ChatWorldTurn *turn, int allow_explore);

static int chatworld_response_is_correct(const ChatWorldDecision *d, const ChatWorldTurn *turn) {
    if (!d || !turn) {
        return 0;
    }
    if (turn->expected == CHAT_EXPECT_NO_SUPPORTED_RESPONSE) {
        return d->no_supported_response && !d->ambiguous_response;
    }
    if (d->no_supported_response || d->ambiguous_response) {
        return 0;
    }
    switch (turn->expected) {
    case CHAT_EXPECT_GREET:
        return d->output_fired && strcmp(d->rendered, "hello") == 0;
    case CHAT_EXPECT_ACK:
        if (turn->expected_key[0] != '\0' && turn->expected_value[0] != '\0') {
            return d->mem_write_fired && strcmp(d->key, turn->expected_key) == 0 &&
                   strcmp(d->value, turn->expected_value) == 0;
        }
        return d->output_fired && strcmp(d->rendered, "ok") == 0;
    case CHAT_EXPECT_UNKNOWN:
        return d->resp_unknown_fired && d->output_fired && strcmp(d->rendered, "unknown") == 0;
    case CHAT_EXPECT_MEMORY_VALUE:
        return d->mem_read_fired && d->output_fired &&
               turn->expected_key[0] != '\0' && strcmp(d->key, turn->expected_key) == 0 &&
               turn->expected_value[0] != '\0' &&
               strcmp(d->value, turn->expected_value) == 0;
    case CHAT_EXPECT_NO_SUPPORTED_RESPONSE:
        return d->no_supported_response && !d->ambiguous_response;
    default:
        return 0;
    }
}

static void chatworld_trace_decision(const NervaEngine *e, FILE *trace_out, const char *phase,
                                     const ChatWorldTurn *turn, const ChatWorldDecision *d,
                                     int correct, uint32_t mutation_delta) {
    if (!e || !trace_out || !phase || !turn || !d) {
        return;
    }
    fprintf(trace_out,
            "phase=%s utterance=\"%s\" expected=%u fired_action=%s frame=%s "
            "no_supported=%u ambiguous=%u correct=%d mutation_delta=%u key=\"%s\" value=\"%s\" "
            "rendered=\"%s\" fired_actions=%u fired_outputs=%u mem_write=%u mem_read=%u "
            "resp_unknown=%u key_pos=%u value_pos=%u trace_edges=",
            phase, turn->utterance, (unsigned)turn->expected,
            d->action < CHAT_ACTION_COUNT ? chatworld_action_name(d->action) : "NO_ACTION",
            chatworld_frame_name(d->frame), d->no_supported_response ? 1u : 0u,
            d->ambiguous_response ? 1u : 0u, correct, mutation_delta, d->key, d->value,
            d->rendered, d->fired_action_count, d->fired_output_count,
            d->mem_write_fired ? 1u : 0u, d->mem_read_fired ? 1u : 0u,
            d->resp_unknown_fired ? 1u : 0u, d->key_pos, d->value_pos);
    for (uint32_t i = 0; i < d->selected_edge_count; ++i) {
        uint32_t edge_id = d->selected_edges[i];
        int32_t weight = 0;
        if (edge_id < e->edge_count) {
            weight = e->edges[edge_id].weight;
        }
        fprintf(trace_out, "%s%u:%d", i == 0u ? "" : ",", edge_id, weight);
    }
    fputc('\n', trace_out);
}

ChatWorldDecision chatworld_step(NervaEngine *e, ChatWorldNerva *cw, ChatWorld *w,
                                 const ChatWorldTurn *turn) {
    return chatworld_step_internal(e, cw, w, turn, 0);
}

static ChatWorldDecision chatworld_step_internal(NervaEngine *e, ChatWorldNerva *cw, ChatWorld *w,
                                                 const ChatWorldTurn *turn, int allow_explore) {
    ChatWorldDecision d;
    memset(&d, 0, sizeof(d));
    d.action = CHAT_ACTION_COUNT;
    d.frame = CHAT_FRAME_NO_SUPPORTED_RESPONSE;
    d.key_pos = UINT32_MAX;
    d.value_pos = UINT32_MAX;

    ChatWorldSurface surface;
    if (!e || !cw || !w || !turn || chatworld_surface_parse(turn->utterance, &surface) != 0 ||
        chatworld_emit_surface_internal(e, cw, turn->utterance, allow_explore) != 0) {
        d.no_supported_response = true;
        return d;
    }

    if (allow_explore && turn->learn) {
        ChatWorldPath expected_path;
        chatworld_make_expected_path(e, cw, turn, &surface, &expected_path);
        chatworld_force_path_fire(e, cw, &expected_path);
        chatworld_queue_path_feedback(e, &expected_path, 1);
        chatworld_fill_decision_from_fires(e, cw, w, &surface, &d);
        d.selected_edge_count = 0;
        for (uint32_t i = 0; i < expected_path.selected_edge_count; ++i) {
            d.selected_edges[d.selected_edge_count++] = expected_path.selected_edges[i];
        }
    } else {
        chatworld_fill_decision_from_fires(e, cw, w, &surface, &d);
    }
    return d;
}

static void chatworld_train_turn(NervaEngine *e, ChatWorldNerva *cw, ChatWorld *w,
                                 const ChatWorldTurn *turn, ChatWorldMetrics *m,
                                 FILE *trace_out) {
    uint64_t before = e->debug.mutations_applied;
    ChatWorldDecision d = chatworld_step_internal(e, cw, w, turn, 1);
    if (turn->learn) {
        nerva_apply_mutations(e);
    }
    uint32_t mutation_delta = (uint32_t)(e->debug.mutations_applied - before);
    int correct = chatworld_response_is_correct(&d, turn);
    chatworld_trace_decision(e, trace_out, "train", turn, &d, correct, mutation_delta);
    if (trace_out) {
        m->decision_trace_count++;
    }

    m->train_total++;
    if (correct) {
        m->train_correct++;
    }
    if (d.mem_write_fired) {
        m->memory_write_count++;
        m->binding_candidate_count++;
    }
    if (d.mem_read_fired) {
        m->memory_read_count++;
        m->binding_candidate_count++;
    }
}

static void chatworld_eval_turn(NervaEngine *e, ChatWorldNerva *cw, ChatWorld *w,
                                const ChatWorldTurn *turn, ChatWorldMetrics *m,
                                FILE *trace_out) {
    uint64_t before = e->debug.mutations_applied;
    ChatWorldDecision d = chatworld_step(e, cw, w, turn);
    uint64_t after = e->debug.mutations_applied;
    int correct = chatworld_response_is_correct(&d, turn);
    uint32_t mutation_delta = (uint32_t)(after - before);
    chatworld_trace_decision(e, trace_out, "frozen", turn, &d, correct, mutation_delta);
    if (trace_out) {
        m->decision_trace_count++;
    }

    m->eval_total++;
    if (correct) {
        m->eval_correct++;
    }
    if (turn->expected == CHAT_EXPECT_UNKNOWN) {
        m->eval_baseline_correct++;
    }
    if (d.no_supported_response) {
        m->no_supported_response_count++;
    }
    if (d.ambiguous_response) {
        m->fallback_count++;
    }
    if (d.mem_write_fired) {
        m->memory_write_count++;
        m->binding_candidate_count++;
    }
    if (d.mem_read_fired) {
        m->memory_read_count++;
        m->binding_candidate_count++;
    }
    m->eval_mutations += mutation_delta;
}

static void chatworld_zero_learned_edges(NervaEngine *e) {
    if (!e) {
        return;
    }
    for (uint32_t i = 0; i < e->edge_count; ++i) {
        NervaEdge *ed = &e->edges[i];
        if (ed->relation == CHAT_REL_SURFACE_TO_BINDING ||
            ed->relation == CHAT_REL_BINDING_TO_ACTION ||
            ed->relation == CHAT_REL_ACTION_TO_OUTPUT ||
            ed->relation == CHAT_REL_SURFACE_TO_OUTPUT ||
            ed->relation == CHAT_REL_SURFACE_TO_ACTION ||
            ed->relation == CHAT_REL_SURFACE_TO_KEY_TOKEN ||
            ed->relation == CHAT_REL_KEY_TOKEN_TO_BINDING ||
            ed->relation == CHAT_REL_SURFACE_TO_ACTION_GATE ||
            ed->relation == CHAT_REL_BINDING_TO_ACTION_GATE ||
            ed->relation == CHAT_REL_ACTION_GATE_TO_ACTION ||
            ed->relation == CHAT_REL_KEY_TO_VALUE_BINDING) {
            if (ed->weight > 0 && !chatworld_is_fixed_support_edge(e, i)) {
                ed->weight = 0;
            }
        }
    }
}

int chatworld_run(NervaEngine *e, const ChatWorldConfig *cfg, ChatWorldResult *out) {
    if (!e || !cfg || !out) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    int rc = -1;

    ChatWorldNerva cw;
    ChatWorld w;
    ChatWorldDataset train_ds;
    ChatWorldDataset frozen_ds;
    memset(&train_ds, 0, sizeof(train_ds));
    memset(&frozen_ds, 0, sizeof(frozen_ds));
    chatworld_reset(&w);

    if (cfg->train && chatworld_load_dataset(cfg->train_path, &train_ds) != 0) {
        goto cleanup;
    }
    if (cfg->eval && chatworld_load_dataset(cfg->frozen_path, &frozen_ds) != 0) {
        goto cleanup;
    }

    if (chatworld_nerva_init(e, &cw) != 0) {
        goto cleanup;
    }
    if (cfg->train && chatworld_preload_dataset(e, &cw, &train_ds) != 0) {
        goto cleanup;
    }
    if (cfg->eval && chatworld_preload_dataset(e, &cw, &frozen_ds) != 0) {
        goto cleanup;
    }

    FILE *trace_out = NULL;
    if (cfg->trace_path) {
        trace_out = fopen(cfg->trace_path, "w");
        if (!trace_out) {
            goto cleanup;
        }
        fprintf(trace_out, "chatworld_trace_v1_4=1\n");
        fprintf(trace_out, "train_path=%s\n", cfg->train_path ? cfg->train_path : "");
        fprintf(trace_out, "frozen_path=%s\n", cfg->frozen_path ? cfg->frozen_path : "");
    }

    if (cfg->train) {
        for (uint32_t epoch = 0; epoch < cfg->train_epochs; ++epoch) {
            chatworld_reset(&w);
            for (uint32_t i = 0; i < train_ds.count; ++i) {
                chatworld_train_turn(e, &cw, &w, &train_ds.turns[i], &out->metrics, trace_out);
            }
        }
    }

    if (cfg->ablate_response_edges) {
        chatworld_zero_learned_edges(e);
    }

    if (cfg->eval) {
        if (!cfg->train) {
            chatworld_reset(&w);
        }
        uint32_t eval_count = frozen_ds.count;
        if (cfg->eval_episodes > 0u && cfg->eval_episodes < eval_count) {
            eval_count = cfg->eval_episodes;
        }
        for (uint32_t i = 0; i < eval_count; ++i) {
            chatworld_eval_turn(e, &cw, &w, &frozen_ds.turns[i], &out->metrics, trace_out);
        }
    }

    if (cfg->ablate_response_edges) {
        out->metrics.response_ablation_correct = out->metrics.eval_correct;
        out->metrics.response_ablation_total = out->metrics.eval_total;
    }

    out->metrics.trace_count = e->trace_count;
    if (trace_out) {
        fclose(trace_out);
    }
    rc = 0;

cleanup:
    chatworld_free_dataset(&train_ds);
    chatworld_free_dataset(&frozen_ds);
    chatworld_free(&w);
    return rc;
}

void chatworld_print_metrics(const ChatWorldMetrics *m, FILE *out) {
    if (!m || !out) {
        return;
    }
    double train_accuracy =
        m->train_total > 0u ? (double)m->train_correct / (double)m->train_total : 0.0;
    double eval_accuracy =
        m->eval_total > 0u ? (double)m->eval_correct / (double)m->eval_total : 0.0;
    double eval_baseline =
        m->eval_total > 0u ? (double)m->eval_baseline_correct / (double)m->eval_total : 0.0;
    double ablation_accuracy = m->response_ablation_total > 0u
                                   ? (double)m->response_ablation_correct /
                                         (double)m->response_ablation_total
                                   : 0.0;

    fprintf(out, "train_total=%u\n", m->train_total);
    fprintf(out, "train_correct=%u\n", m->train_correct);
    fprintf(out, "train_accuracy=%.3f\n", train_accuracy);
    fprintf(out, "eval_total=%u\n", m->eval_total);
    fprintf(out, "eval_correct=%u\n", m->eval_correct);
    fprintf(out, "eval_accuracy=%.3f\n", eval_accuracy);
    fprintf(out, "eval_baseline=%.3f\n", eval_baseline);
    fprintf(out, "eval_mutations=%u\n", m->eval_mutations);
    fprintf(out, "fallback_count=%u\n", m->fallback_count);
    fprintf(out, "no_supported_response_count=%u\n", m->no_supported_response_count);
    fprintf(out, "oracle_label_count=%u\n", m->oracle_label_count);
    fprintf(out, "response_ablation_accuracy=%.3f\n", ablation_accuracy);
    fprintf(out, "memory_write_count=%u\n", m->memory_write_count);
    fprintf(out, "memory_read_count=%u\n", m->memory_read_count);
    fprintf(out, "trace_count=%u\n", m->trace_count);
    fprintf(out, "decision_trace_count=%u\n", m->decision_trace_count);
    fprintf(out, "binding_candidate_count=%u\n", m->binding_candidate_count);
}
