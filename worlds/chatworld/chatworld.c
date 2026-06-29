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

#define CHATWORLD_DEFAULT_TRAIN_PATH "worlds/chatworld/datasets/train.tsv"
#define CHATWORLD_DEFAULT_DEV_PATH "worlds/chatworld/datasets/dev.tsv"
#define CHATWORLD_DEFAULT_FROZEN_PATH "worlds/chatworld/datasets/frozen.tsv"
#define CHATWORLD_SUPPORT_WEIGHT_Q8_8 512
#define CHATWORLD_ACTION_THETA_Q8_8 1536

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
    uint32_t action_node;
    uint32_t key_candidate_node;
    uint32_t value_candidate_node;
    uint32_t output_node;
    uint32_t selected_edges[CHATWORLD_MAX_SELECTED_EDGES];
    uint32_t selected_edge_count;
} ChatWorldPath;

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

static int chatworld_has_forbidden_name(const char *name) {
    if (!name) {
        return 0;
    }
    for (uint32_t i = 0; i < sizeof(chatworld_forbidden_names) / sizeof(chatworld_forbidden_names[0]);
         ++i) {
        if (strstr(name, chatworld_forbidden_names[i])) {
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
    return CHAT_EXPECT_NONE;
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
        if (out->count >= CHATWORLD_MAX_TURNS) {
            fclose(f);
            return -1;
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

        ChatWorldTurn *t = &out->turns[out->count];
        chatworld_copy_field(t->utterance, sizeof(t->utterance), fields[0]);
        t->expected = chatworld_expected_from_string(fields[1]);
        chatworld_copy_token(t->expected_key, fields[2]);
        chatworld_copy_token(t->expected_value, fields[3]);
        t->learn = fields[4] && strtoul(fields[4], NULL, 10) != 0u;
        if (t->utterance[0] == '\0' || t->expected == CHAT_EXPECT_NONE) {
            fclose(f);
            return -1;
        }
        out->count++;
    }

    fclose(f);
    return out->count > 0u ? 0 : -1;
}

void chatworld_reset(ChatWorld *w) {
    if (!w) {
        return;
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
        snprintf(name, sizeof(name), "TOKEN:%.*s", (int)CHATWORLD_MAX_TOKEN_LEN,
                 surface->tokens[i]);
        uint32_t feature = nerva_find_node_by_name(e, name);
        chatworld_edge_from_feature_to_all_positions(e, cw, feature);
        for (uint32_t a = 0; a < CHAT_ACTION_COUNT; ++a) {
            chatworld_create_edge_weight(e, feature, cw->action_node[a],
                                         CHAT_REL_SURFACE_TO_ACTION, 0);
        }
        snprintf(name, sizeof(name), "TOKEN_AT:%u:%.*s", i, (int)CHATWORLD_MAX_TOKEN_LEN,
                 surface->tokens[i]);
        feature = nerva_find_node_by_name(e, name);
        chatworld_edge_from_feature_to_all_positions(e, cw, feature);
        for (uint32_t a = 0; a < CHAT_ACTION_COUNT; ++a) {
            chatworld_create_edge_weight(e, feature, cw->action_node[a],
                                         CHAT_REL_SURFACE_TO_ACTION, 0);
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
        }
    }
    for (uint32_t i = 0; i < surface->punct_count; ++i) {
        snprintf(name, sizeof(name), "PUNCT:%c", surface->punct[i]);
        uint32_t feature = nerva_find_node_by_name(e, name);
        chatworld_edge_from_feature_to_all_positions(e, cw, feature);
        for (uint32_t a = 0; a < CHAT_ACTION_COUNT; ++a) {
            chatworld_create_edge_weight(e, feature, cw->action_node[a],
                                         CHAT_REL_SURFACE_TO_ACTION, 0);
        }
    }
    return 0;
}

static int chatworld_is_canonical_name_read(const ChatWorldSurface *surface) {
    return surface && surface->token_count == 4u &&
           strcmp(surface->tokens[0], "what") == 0 &&
           strcmp(surface->tokens[1], "is") == 0 &&
           strcmp(surface->tokens[2], "my") == 0 &&
           strcmp(surface->tokens[3], "name") == 0;
}

static void chatworld_preload_read_affordance(NervaEngine *e, const ChatWorldNerva *cw,
                                              const ChatWorldSurface *surface) {
    if (!e || !cw || !chatworld_is_canonical_name_read(surface)) {
        return;
    }
    char name[128];
    snprintf(name, sizeof(name), "TOKEN_AT:3:name");
    uint32_t key_feature = nerva_find_node_by_name(e, name);
    snprintf(name, sizeof(name), "TOKEN_AT:0:what");
    uint32_t what_feature = nerva_find_node_by_name(e, name);
    snprintf(name, sizeof(name), "PAIR:what:is");
    uint32_t what_is_feature = nerva_find_node_by_name(e, name);
    chatworld_create_edge_weight(e, key_feature, cw->key_candidate[3u],
                                 CHAT_REL_SURFACE_TO_BINDING, CHATWORLD_SUPPORT_WEIGHT_Q8_8);
    chatworld_create_edge_weight(e, what_feature, cw->action_node[CHAT_ACTION_MEM_READ],
                                 CHAT_REL_SURFACE_TO_ACTION, CHATWORLD_SUPPORT_WEIGHT_Q8_8);
    chatworld_create_edge_weight(e, what_is_feature, cw->action_node[CHAT_ACTION_MEM_READ],
                                 CHAT_REL_SURFACE_TO_ACTION, CHATWORLD_SUPPORT_WEIGHT_Q8_8);
    chatworld_create_edge_weight(e, key_feature, cw->action_node[CHAT_ACTION_MEM_READ],
                                 CHAT_REL_SURFACE_TO_ACTION, CHATWORLD_SUPPORT_WEIGHT_Q8_8);
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
        chatworld_create_edge_weight(e, cw->bind_key, cw->action_node[CHAT_ACTION_MEM_READ],
                                     CHAT_REL_BINDING_TO_ACTION, 0);
        chatworld_create_edge_weight(e, cw->bind_key, cw->action_node[CHAT_ACTION_MEM_WRITE],
                                     CHAT_REL_BINDING_TO_ACTION, 0);
        chatworld_create_edge_weight(e, cw->bind_value, cw->action_node[CHAT_ACTION_MEM_WRITE],
                                     CHAT_REL_BINDING_TO_ACTION, 0);
        chatworld_create_edge_weight(e, cw->key_candidate[pos],
                                     cw->action_node[CHAT_ACTION_RESP_UNKNOWN],
                                     CHAT_REL_BINDING_TO_ACTION, 0);
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
        chatworld_preload_output_token(e, cw, ds->turns[i].expected_value);
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
        chatworld_preload_read_affordance(e, cw, &surface);
    }
    chatworld_preload_action_output_edges(e, cw);
    chatworld_preload_surface_output_edges(e, cw);
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

static int chatworld_path_target_is_fired(const NervaEngine *e, const ChatWorldNerva *cw,
                                          const ChatWorldPath *path) {
    if (!e || !cw || !path) {
        return 0;
    }
    if (path->kind == CHAT_PATH_MEM_WRITE) {
        return chatworld_fire_log_contains(e, cw->action_node[CHAT_ACTION_MEM_WRITE]) &&
               chatworld_fire_log_contains(e, cw->bind_key) &&
               chatworld_fire_log_contains(e, cw->bind_value) &&
               chatworld_fire_log_contains(e, path->key_candidate_node) &&
               chatworld_fire_log_contains(e, path->value_candidate_node);
    }
    if (path->kind == CHAT_PATH_MEM_READ) {
        return chatworld_fire_log_contains(e, cw->action_node[CHAT_ACTION_MEM_READ]) &&
               chatworld_fire_log_contains(e, cw->bind_key) &&
               chatworld_fire_log_contains(e, path->key_candidate_node);
    }
    if (path->action == CHAT_ACTION_RESP_UNKNOWN) {
        return chatworld_fire_log_contains(e, path->action_node) &&
               chatworld_fire_log_contains(e, path->output_node);
    }
    return chatworld_fire_log_contains(e, path->output_node);
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
    chatworld_path_add_if_exists(e, path, feature_node, path->action_node,
                                 CHAT_REL_SURFACE_TO_ACTION);
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

static void chatworld_prepare_path(NervaEngine *e, const ChatWorldNerva *cw,
                                   const ChatWorldSurface *surface, ChatWorldPath *path) {
    if (!e || !cw || !surface || !path) {
        return;
    }

    if (path->key_pos < CHATWORLD_MAX_TOKENS) {
        path->key_candidate_node = cw->key_candidate[path->key_pos];
    }
    if (path->value_pos < CHATWORLD_MAX_TOKENS) {
        path->value_candidate_node = cw->value_candidate[path->value_pos];
    }
    path->action_node =
        path->action < CHAT_ACTION_COUNT ? cw->action_node[path->action] : UINT32_MAX;

    if (path->kind == CHAT_PATH_RESPONSE) {
        chatworld_select_response_features(e, surface, path);
        if (path->action == CHAT_ACTION_RESP_UNKNOWN) {
            chatworld_select_unknown_action_features(e, surface, path);
        }
    } else if (path->kind == CHAT_PATH_MEM_WRITE) {
        for (uint32_t i = 0; i < surface->token_count; ++i) {
            if (i + 1u < surface->token_count && strcmp(surface->tokens[i], "name") == 0 &&
                strcmp(surface->tokens[i + 1u], "is") == 0) {
                chatworld_add_action_feature(
                    e, path,
                    chatworld_find_named_feature(e, "PAIR", surface->tokens[i],
                                                 surface->tokens[i + 1u], 0u));
            }
        }
        if (path->key_pos < surface->token_count) {
            chatworld_add_binding_feature(
                e, path,
                chatworld_find_named_feature(e, "TOKEN_AT", surface->tokens[path->key_pos], NULL,
                                             path->key_pos),
                path->key_candidate_node);
        }
        if (path->value_pos < surface->token_count && path->value_pos > 0u &&
            strcmp(surface->tokens[path->value_pos - 1u], "is") == 0) {
            chatworld_add_binding_feature(
                e, path,
                chatworld_find_named_feature(e, "TOKEN_AT",
                                             surface->tokens[path->value_pos - 1u], NULL,
                                             path->value_pos - 1u),
                path->value_candidate_node);
        }
    } else if (path->kind == CHAT_PATH_MEM_READ) {
        if (path->key_pos < surface->token_count) {
            chatworld_add_action_feature(
                e, path,
                chatworld_find_named_feature(e, "TOKEN_AT", surface->tokens[path->key_pos], NULL,
                                             path->key_pos));
        }
        if (path->key_pos < surface->token_count) {
            chatworld_add_binding_feature(
                e, path,
                chatworld_find_named_feature(e, "TOKEN_AT", surface->tokens[path->key_pos], NULL,
                                             path->key_pos),
                path->key_candidate_node);
        }
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
        chatworld_path_add_if_exists(e, path, cw->bind_value, path->action_node,
                                     CHAT_REL_BINDING_TO_ACTION);
    } else if (path->kind == CHAT_PATH_MEM_READ) {
        chatworld_path_add_if_exists(e, path, cw->bind_key, path->action_node,
                                     CHAT_REL_BINDING_TO_ACTION);
    }
    if (path->kind == CHAT_PATH_MEM_WRITE && path->output_node != UINT32_MAX) {
        chatworld_path_add_if_exists(e, path, path->action_node, path->output_node,
                                     CHAT_REL_ACTION_TO_OUTPUT);
    }
}

static void chatworld_make_expected_path(NervaEngine *e, const ChatWorldNerva *cw,
                                         const ChatWorldTurn *turn,
                                         const ChatWorldSurface *surface,
                                         ChatWorldPath *path) {
    memset(path, 0, sizeof(*path));
    path->key_pos = UINT32_MAX;
    path->value_pos = UINT32_MAX;
    path->key_candidate_node = UINT32_MAX;
    path->value_candidate_node = UINT32_MAX;
    path->output_node = UINT32_MAX;

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
            for (uint32_t i = 1; i < surface->token_count; ++i) {
                if (strcmp(surface->tokens[i], "is") == 0 &&
                    strcmp(surface->tokens[i - 1u], turn->expected_key) == 0) {
                    path->key_pos = i - 1u;
                    break;
                }
            }
            for (uint32_t i = 0; i < surface->token_count; ++i) {
                if (strcmp(surface->tokens[i], turn->expected_key) == 0 &&
                    path->key_pos == UINT32_MAX) {
                    path->key_pos = i;
                }
                if (strcmp(surface->tokens[i], turn->expected_value) == 0 &&
                    path->value_pos == UINT32_MAX) {
                    path->value_pos = i;
                }
            }
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
        for (uint32_t i = 0; i < surface->token_count; ++i) {
            if (strcmp(surface->tokens[i], turn->expected_key) == 0 && path->key_pos == UINT32_MAX) {
                path->key_pos = i;
            }
        }
        path->output_node = chatworld_output_node_for_token(cw, turn->expected_value);
        break;
    default:
        break;
    }

    if (path->key_pos == UINT32_MAX && surface->token_count > 0u) {
        path->key_pos = surface->token_count - 1u;
    }
    if (path->kind != CHAT_PATH_MEM_READ && path->value_pos == UINT32_MAX &&
        surface->token_count > 0u) {
        path->value_pos = surface->token_count - 1u;
    }
    chatworld_prepare_path(e, cw, surface, path);
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
    if (path->action_node != UINT32_MAX) {
        chatworld_emit_node(e, path->action_node);
        chatworld_tick_quiet(e, 2u);
    }
    if (path->kind != CHAT_PATH_MEM_READ && path->output_node != UINT32_MAX) {
        chatworld_emit_node(e, path->output_node);
        chatworld_tick_quiet(e, 2u);
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

static void chatworld_memory_write(ChatWorld *w, const char *key, const char *value) {
    if (!w || !key || !value || key[0] == '\0' || value[0] == '\0') {
        return;
    }
    int idx = chatworld_memory_find(w, key);
    if (idx < 0) {
        if (w->memory_count >= CHATWORLD_MEMORY_CAP) {
            return;
        }
        idx = (int)w->memory_count++;
    }
    chatworld_copy_token(w->memory[idx].key, key);
    chatworld_copy_token(w->memory[idx].value, value);
    w->memory[idx].strength++;
}

static int chatworld_memory_read(const ChatWorld *w, const char *key, char *value) {
    int idx = chatworld_memory_find(w, key);
    if (idx < 0) {
        return 0;
    }
    chatworld_copy_token(value, w->memory[idx].value);
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
            rel != CHAT_REL_SURFACE_TO_ACTION) {
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
        if (d->key_pos < surface->token_count && d->value_pos < surface->token_count) {
            chatworld_copy_token(d->key, surface->tokens[d->key_pos]);
            chatworld_copy_token(d->value, surface->tokens[d->value_pos]);
            chatworld_memory_write(w, d->key, d->value);
            d->frame = CHAT_FRAME_ACK;
        } else {
            d->no_supported_response = true;
            d->frame = CHAT_FRAME_NO_SUPPORTED_RESPONSE;
        }
    } else if (d->mem_read_fired) {
        if (d->key_pos < surface->token_count) {
            chatworld_copy_token(d->key, surface->tokens[d->key_pos]);
            char remembered[CHATWORLD_MAX_TOKEN_LEN + 1u];
            if (chatworld_memory_read(w, d->key, remembered)) {
                uint32_t output_node = chatworld_output_node_for_token(cw, remembered);
                if (output_node != UINT32_MAX) {
                    chatworld_emit_node(e, output_node);
                    chatworld_tick_quiet(e, 2u);
                    chatworld_copy_token(d->value, remembered);
                    chatworld_copy_field(d->rendered, sizeof(d->rendered), remembered);
                    d->output_fired = true;
                    d->fired_output_count = 1u;
                    d->frame = CHAT_FRAME_ANSWER_MEMORY;
                } else {
                    d->no_supported_response = true;
                    d->frame = CHAT_FRAME_NO_SUPPORTED_RESPONSE;
                }
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
    if (!d || !turn || d->no_supported_response || d->ambiguous_response) {
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

    ChatWorldPath expected_path;
    chatworld_make_expected_path(e, cw, turn, &surface, &expected_path);

    if (allow_explore && turn->learn) {
        if (!chatworld_path_target_is_fired(e, cw, &expected_path)) {
            chatworld_force_path_fire(e, cw, &expected_path);
        }
        chatworld_queue_path_feedback(e, &expected_path, 1);
    }

    chatworld_fill_decision_from_fires(e, cw, w, &surface, &d);

    if (allow_explore && turn->learn) {
        d.selected_edge_count = 0;
        for (uint32_t i = 0; i < expected_path.selected_edge_count; ++i) {
            d.selected_edges[d.selected_edge_count++] = expected_path.selected_edges[i];
        }
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
            ed->relation == CHAT_REL_SURFACE_TO_ACTION) {
            if (ed->weight > 0 && !(ed->relation == CHAT_REL_SURFACE_TO_BINDING &&
                                    ed->weight == CHATWORLD_SUPPORT_WEIGHT_Q8_8)) {
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

    ChatWorldNerva cw;
    ChatWorld w;
    ChatWorldDataset train_ds;
    ChatWorldDataset frozen_ds;
    memset(&train_ds, 0, sizeof(train_ds));
    memset(&frozen_ds, 0, sizeof(frozen_ds));
    chatworld_reset(&w);

    if (cfg->train && chatworld_load_dataset(cfg->train_path, &train_ds) != 0) {
        return -1;
    }
    if (cfg->eval && chatworld_load_dataset(cfg->frozen_path, &frozen_ds) != 0) {
        return -1;
    }

    if (chatworld_nerva_init(e, &cw) != 0) {
        return -1;
    }
    if (cfg->train && chatworld_preload_dataset(e, &cw, &train_ds) != 0) {
        return -1;
    }
    if (cfg->eval && chatworld_preload_dataset(e, &cw, &frozen_ds) != 0) {
        return -1;
    }

    FILE *trace_out = NULL;
    if (cfg->trace_path) {
        trace_out = fopen(cfg->trace_path, "w");
        if (!trace_out) {
            return -1;
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
    return 0;
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
