// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Paul Odantabao II

#include "chatworld.h"

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

#define CHAT_REL_SURFACE_TO_ACTION ((uint16_t)(NERVA_REL_CUSTOM_BASE + 40u))

#define CHATWORLD_DEFAULT_TRAIN_PATH "worlds/chatworld/datasets/train.tsv"
#define CHATWORLD_DEFAULT_DEV_PATH "worlds/chatworld/datasets/dev.tsv"
#define CHATWORLD_DEFAULT_FROZEN_PATH "worlds/chatworld/datasets/frozen.tsv"
#define CHATWORLD_MAX_BINDING_CANDIDATES 16u

typedef enum ChatWorldCandidateKind {
    CHAT_CAND_RESPONSE = 0,
    CHAT_CAND_MEM_WRITE,
    CHAT_CAND_MEM_READ
} ChatWorldCandidateKind;

typedef struct ChatWorldCandidate {
    ChatWorldCandidateKind kind;
    ChatWorldAction action;
    char label[CHATWORLD_MAX_CANDIDATE_LABEL + 1u];
    char key[CHATWORLD_MAX_TOKEN_LEN + 1u];
    char value[CHATWORLD_MAX_TOKEN_LEN + 1u];
    uint32_t action_node;
} ChatWorldCandidate;

typedef struct ChatWorldCandidateList {
    ChatWorldCandidate items[CHAT_ACTION_COUNT + CHATWORLD_MAX_BINDING_CANDIDATES];
    uint32_t count;
} ChatWorldCandidateList;

static void chatworld_copy_token(char *dst, const char *src) {
    uint32_t i = 0;
    while (src[i] != '\0' && i < CHATWORLD_MAX_TOKEN_LEN) {
        dst[i] = (char)tolower((unsigned char)src[i]);
        i++;
    }
    dst[i] = '\0';
}

static int chatworld_has_forbidden_name(const char *name) {
    static const char *bad[] = {
        "INTENT",
        "SLOT",
        "CORRECT",
        "ANSWER_LABEL",
        "FACT_QUERY",
    };
    if (!name) {
        return 0;
    }
    for (uint32_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        if (strstr(name, bad[i])) {
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

static uint32_t chatworld_find_surface_edge(const NervaEngine *e, uint32_t source,
                                            uint32_t target) {
    if (!e || source >= e->node_count || target >= e->node_count) {
        return UINT32_MAX;
    }
    for (uint32_t i = 0; i < e->edge_count; ++i) {
        const NervaEdge *ed = &e->edges[i];
        if (!(ed->flags & NERVA_EDGE_DELETED) && ed->source == source &&
            ed->target == target && ed->relation == CHAT_REL_SURFACE_TO_ACTION) {
            return i;
        }
    }
    return UINT32_MAX;
}

static uint32_t chatworld_find_candidate_node(const NervaEngine *e, const char *label) {
    char name[96];
    snprintf(name, sizeof(name), "CAND:%s", label);
    return nerva_find_node_by_name(e, name);
}

static uint32_t chatworld_get_candidate_node(NervaEngine *e, const char *label) {
    char name[96];
    snprintf(name, sizeof(name), "CAND:%s", label);
    return chatworld_get_or_create(e, name);
}

static uint32_t chatworld_create_edge(NervaEngine *e, uint32_t source, uint32_t target) {
    uint32_t existing = chatworld_find_surface_edge(e, source, target);
    if (existing != UINT32_MAX) {
        return existing;
    }

    uint32_t id = nerva_graph_create_edge(e, source, target, CHAT_REL_SURFACE_TO_ACTION);
    if (id != UINT32_MAX) {
        e->edges[id].weight = 0;
    }
    return id;
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
        chatworld_copy_field(t->expected_key, sizeof(t->expected_key), fields[2]);
        chatworld_copy_field(t->expected_value, sizeof(t->expected_value), fields[3]);
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
    case CHAT_ACTION_RESP_GREET:
        return "RESP_GREET";
    case CHAT_ACTION_RESP_ACK:
        return "RESP_ACK";
    case CHAT_ACTION_RESP_UNKNOWN:
        return "RESP_UNKNOWN";
    case CHAT_ACTION_RESP_ASK_CLARIFY:
        return "RESP_ASK_CLARIFY";
    case CHAT_ACTION_RESP_CONFIRM:
        return "RESP_CONFIRM";
    case CHAT_ACTION_RESP_ANSWER_MEMORY:
        return "RESP_ANSWER_MEMORY";
    case CHAT_ACTION_MEM_WRITE_PAIR:
        return "MEM_WRITE_PAIR";
    case CHAT_ACTION_MEM_READ_PAIR:
        return "MEM_READ_PAIR";
    default:
        return "UNKNOWN_ACTION";
    }
}

const char *chatworld_frame_name(ChatWorldFrame frame) {
    switch (frame) {
    case CHAT_FRAME_GREET:
        return "RESP_GREET";
    case CHAT_FRAME_ACK:
        return "RESP_ACK";
    case CHAT_FRAME_UNKNOWN:
        return "RESP_UNKNOWN";
    case CHAT_FRAME_ASK_CLARIFY:
        return "RESP_ASK_CLARIFY";
    case CHAT_FRAME_CONFIRM:
        return "RESP_CONFIRM";
    case CHAT_FRAME_ANSWER_MEMORY:
        return "RESP_ANSWER_MEMORY";
    default:
        return "UNKNOWN_FRAME";
    }
}

int chatworld_nerva_init(NervaEngine *e, ChatWorldNerva *cw) {
    if (!e || !cw) {
        return -1;
    }
    memset(cw, 0, sizeof(*cw));

    cw->turn_start = chatworld_get_or_create(e, "TURN_START");
    cw->turn_end = chatworld_get_or_create(e, "TURN_END");
    cw->speaker_user = chatworld_get_or_create(e, "SPEAKER:user");
    cw->speaker_assistant = chatworld_get_or_create(e, "SPEAKER:assistant");
    if (cw->turn_start == UINT32_MAX || cw->turn_end == UINT32_MAX ||
        cw->speaker_user == UINT32_MAX || cw->speaker_assistant == UINT32_MAX) {
        return -1;
    }

    char name[64];
    for (uint32_t i = 0; i < CHATWORLD_MAX_TOKENS; ++i) {
        snprintf(name, sizeof(name), "POSITION:%u", i);
        cw->position[i] = chatworld_get_or_create(e, name);
        if (cw->position[i] == UINT32_MAX) {
            return -1;
        }
    }

    for (uint32_t a = 0; a < CHAT_ACTION_COUNT; ++a) {
        cw->action_node[a] = chatworld_get_or_create(e, chatworld_action_name((ChatWorldAction)a));
        if (cw->action_node[a] == UINT32_MAX) {
            return -1;
        }
    }

    nerva_graph_rebuild_adjacency(e);
    return 0;
}

int chatworld_tokenize(const char *utterance,
                       char tokens[CHATWORLD_MAX_TOKENS][CHATWORLD_MAX_TOKEN_LEN + 1u],
                       uint32_t *count) {
    if (!utterance || !tokens || !count) {
        return -1;
    }

    *count = 0;
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
                if (*count < CHATWORLD_MAX_TOKENS) {
                    chatworld_copy_token(tokens[*count], buf);
                    (*count)++;
                }
                len = 0;
            }
            if (ch == '\0') {
                break;
            }
        }
    }
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
}

static void chatworld_emit_node(NervaEngine *e, uint32_t node_id) {
    if (!e || node_id == UINT32_MAX) {
        return;
    }
    nerva_activate_node(e, node_id, NERVA_Q8_8_ONE);
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

    char tokens[CHATWORLD_MAX_TOKENS][CHATWORLD_MAX_TOKEN_LEN + 1u];
    uint32_t count = 0;
    if (chatworld_tokenize(utterance, tokens, &count) != 0) {
        return -1;
    }

    chatworld_quiesce_engine(e);
    cw->token_count = count;
    chatworld_emit_node(e, cw->turn_start);
    chatworld_emit_node(e, cw->speaker_user);

    char name[128];
    for (uint32_t i = 0; i < count; ++i) {
        snprintf(name, sizeof(name), "TOKEN:%.*s", (int)CHATWORLD_MAX_TOKEN_LEN, tokens[i]);
        cw->token_node[i] =
            create_graph ? chatworld_get_or_create(e, name) : nerva_find_node_by_name(e, name);
        if (create_graph && cw->token_node[i] == UINT32_MAX) {
            return -1;
        }
        chatworld_emit_node(e, cw->token_node[i]);
        chatworld_emit_node(e, cw->position[i]);

        snprintf(name, sizeof(name), "TOKEN_AT:%u:%.*s", i, (int)CHATWORLD_MAX_TOKEN_LEN,
                 tokens[i]);
        cw->token_at_node[i] =
            create_graph ? chatworld_get_or_create(e, name) : nerva_find_node_by_name(e, name);
        if (create_graph && cw->token_at_node[i] == UINT32_MAX) {
            return -1;
        }
        chatworld_emit_node(e, cw->token_at_node[i]);
    }
    for (uint32_t i = count; i < CHATWORLD_MAX_TOKENS; ++i) {
        cw->token_node[i] = UINT32_MAX;
        cw->token_at_node[i] = UINT32_MAX;
    }
    cw->pair_count = count > 0u ? count - 1u : 0u;
    for (uint32_t i = 0; i < cw->pair_count; ++i) {
        snprintf(name, sizeof(name), "PAIR:%.*s:%.*s", (int)CHATWORLD_MAX_TOKEN_LEN, tokens[i],
                 (int)CHATWORLD_MAX_TOKEN_LEN, tokens[i + 1u]);
        cw->pair_node[i] =
            create_graph ? chatworld_get_or_create(e, name) : nerva_find_node_by_name(e, name);
        if (create_graph && cw->pair_node[i] == UINT32_MAX) {
            return -1;
        }
        chatworld_emit_node(e, cw->pair_node[i]);
    }
    for (uint32_t i = cw->pair_count; i < CHATWORLD_MAX_TOKENS; ++i) {
        cw->pair_node[i] = UINT32_MAX;
    }
    chatworld_emit_node(e, cw->turn_end);

    for (uint32_t i = 0; i < count; ++i) {
        for (uint32_t a = 0; a < CHAT_ACTION_COUNT; ++a) {
            cw->policy_edge[i][a] =
                create_graph ? chatworld_create_edge(e, cw->token_node[i], cw->action_node[a])
                             : chatworld_find_surface_edge(e, cw->token_node[i],
                                                           cw->action_node[a]);
        }
    }
    nerva_graph_rebuild_adjacency(e);
    chatworld_tick_quiet(e, 8u);
    return 0;
}

int chatworld_emit_surface(NervaEngine *e, ChatWorldNerva *cw, const char *utterance) {
    return chatworld_emit_surface_internal(e, cw, utterance, 1);
}

static void chatworld_candidate_add_response(ChatWorldCandidateList *list, ChatWorldAction action,
                                             uint32_t action_node) {
    if (!list || list->count >= sizeof(list->items) / sizeof(list->items[0])) {
        return;
    }
    ChatWorldCandidate *c = &list->items[list->count++];
    memset(c, 0, sizeof(*c));
    c->kind = CHAT_CAND_RESPONSE;
    c->action = action;
    snprintf(c->label, sizeof(c->label), "%s", chatworld_action_name(action));
    c->action_node = action_node;
}

static void chatworld_candidate_add_write(ChatWorldCandidateList *list, const char *key,
                                          const char *value, uint32_t key_pos,
                                          uint32_t value_pos) {
    if (!list || !key || !value || key[0] == '\0' || value[0] == '\0' ||
        list->count >= sizeof(list->items) / sizeof(list->items[0])) {
        return;
    }
    ChatWorldCandidate *c = &list->items[list->count++];
    memset(c, 0, sizeof(*c));
    c->kind = CHAT_CAND_MEM_WRITE;
    c->action = CHAT_ACTION_MEM_WRITE_PAIR;
    chatworld_copy_token(c->key, key);
    chatworld_copy_token(c->value, value);
    snprintf(c->label, sizeof(c->label), "MEM_WRITE_PAIR:k%u:v%u", key_pos, value_pos);
}

static void chatworld_candidate_add_read(ChatWorldCandidateList *list, const char *key,
                                         uint32_t key_pos) {
    if (!list || !key || key[0] == '\0' ||
        list->count >= sizeof(list->items) / sizeof(list->items[0])) {
        return;
    }
    ChatWorldCandidate *c = &list->items[list->count++];
    memset(c, 0, sizeof(*c));
    c->kind = CHAT_CAND_MEM_READ;
    c->action = CHAT_ACTION_MEM_READ_PAIR;
    chatworld_copy_token(c->key, key);
    snprintf(c->label, sizeof(c->label), "MEM_READ_PAIR:k%u", key_pos);
}

static void chatworld_build_candidates(
    NervaEngine *e, const ChatWorldNerva *cw,
    char tokens[CHATWORLD_MAX_TOKENS][CHATWORLD_MAX_TOKEN_LEN + 1u], uint32_t count,
    ChatWorldCandidateList *list, int create_candidate_nodes) {
    memset(list, 0, sizeof(*list));
    for (uint32_t a = 0; a < CHAT_ACTION_COUNT; ++a) {
        if (a == CHAT_ACTION_MEM_WRITE_PAIR || a == CHAT_ACTION_MEM_READ_PAIR) {
            continue;
        }
        chatworld_candidate_add_response(list, (ChatWorldAction)a, cw->action_node[a]);
    }

    if (count > 0 && (strcmp(tokens[0], "what") == 0 || strcmp(tokens[0], "who") == 0 ||
                      strcmp(tokens[0], "where") == 0 || strcmp(tokens[0], "when") == 0 ||
                      strcmp(tokens[0], "why") == 0 || strcmp(tokens[0], "how") == 0)) {
        if (count > 0) {
            chatworld_candidate_add_read(list, tokens[count - 1u], count - 1u);
        }
        for (uint32_t i = 0; i < count; ++i) {
            if (strcmp(tokens[i], "is") == 0 || strcmp(tokens[i], "what") == 0) {
                continue;
            }
            chatworld_candidate_add_read(list, tokens[i], i);
        }
    } else {
        for (uint32_t i = 0; i + 2u < count; ++i) {
            if (strcmp(tokens[i + 1u], "is") != 0) {
                continue;
            }
            chatworld_candidate_add_write(list, tokens[i], tokens[i + 2u], i, i + 2u);
            if (i > 0u) {
                chatworld_candidate_add_write(list, tokens[i - 1u], tokens[i + 2u], i - 1u,
                                              i + 2u);
            }
            if (i + 3u < count) {
                chatworld_candidate_add_write(list, tokens[i], tokens[i + 3u], i, i + 3u);
            }
        }
    }

    for (uint32_t i = 0; i < list->count; ++i) {
        if (list->items[i].kind != CHAT_CAND_RESPONSE) {
            list->items[i].action_node =
                create_candidate_nodes
                    ? chatworld_get_candidate_node(e, list->items[i].label)
                    : chatworld_find_candidate_node(e, list->items[i].label);
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

static void chatworld_record_selected_edges(NervaEngine *e, const ChatWorldDecision *d) {
    if (!e || !d) {
        return;
    }
    for (uint32_t i = 0; i < d->selected_edge_count; ++i) {
        uint32_t edge_id = d->selected_edges[i];
        if (edge_id >= e->edge_count) {
            continue;
        }
        const NervaEdge *ed = &e->edges[edge_id];
        NervaEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.source = ed->source;
        ev.target = ed->target;
        ev.edge_id = edge_id;
        ev.signal = NERVA_Q8_8_ONE;
        ev.relation = ed->relation;
        ev.trace_tag = nerva_make_path_tag(e, edge_id);
        nerva_trace_record(e, &ev, NERVA_TRACE_USED_PATH, 0, 0, 0);
    }
}

static ChatWorldFrame chatworld_frame_for_action(ChatWorldAction action) {
    switch (action) {
    case CHAT_ACTION_RESP_GREET:
        return CHAT_FRAME_GREET;
    case CHAT_ACTION_RESP_ACK:
    case CHAT_ACTION_MEM_WRITE_PAIR:
        return CHAT_FRAME_ACK;
    case CHAT_ACTION_RESP_UNKNOWN:
        return CHAT_FRAME_UNKNOWN;
    case CHAT_ACTION_RESP_ASK_CLARIFY:
        return CHAT_FRAME_ASK_CLARIFY;
    case CHAT_ACTION_RESP_CONFIRM:
        return CHAT_FRAME_CONFIRM;
    case CHAT_ACTION_RESP_ANSWER_MEMORY:
    case CHAT_ACTION_MEM_READ_PAIR:
        return CHAT_FRAME_ANSWER_MEMORY;
    default:
        return CHAT_FRAME_UNKNOWN;
    }
}

static int32_t chatworld_candidate_score(const NervaEngine *e, const ChatWorldNerva *cw,
                                         const ChatWorldCandidate *candidate, uint32_t *selected,
                                         uint32_t *selected_count) {
    int32_t score = 0;
    *selected_count = 0;
    uint32_t feature_nodes[CHATWORLD_MAX_TOKENS * 3u];
    uint32_t feature_count = 0;
    for (uint32_t i = 0; i < cw->token_count; ++i) {
        if (cw->token_node[i] != UINT32_MAX && feature_count < sizeof(feature_nodes) / sizeof(feature_nodes[0])) {
            feature_nodes[feature_count++] = cw->token_node[i];
        }
        if (cw->token_at_node[i] != UINT32_MAX && feature_count < sizeof(feature_nodes) / sizeof(feature_nodes[0])) {
            feature_nodes[feature_count++] = cw->token_at_node[i];
        }
    }
    for (uint32_t i = 0; i < cw->pair_count; ++i) {
        if (cw->pair_node[i] != UINT32_MAX && feature_count < sizeof(feature_nodes) / sizeof(feature_nodes[0])) {
            feature_nodes[feature_count++] = cw->pair_node[i];
        }
    }

    for (uint32_t i = 0; i < feature_count; ++i) {
        if (candidate->action_node == UINT32_MAX) {
            continue;
        }
        uint32_t edge_id = chatworld_find_surface_edge(e, feature_nodes[i], candidate->action_node);
        if (edge_id == UINT32_MAX || edge_id >= e->edge_count) {
            continue;
        }
        int32_t w = e->edges[edge_id].weight;
        score += w;
        if (*selected_count < CHATWORLD_MAX_SELECTED_EDGES) {
            selected[*selected_count] = edge_id;
            (*selected_count)++;
        }
    }
    return score;
}

static void chatworld_apply_selected_action(ChatWorld *w, ChatWorldDecision *d,
                                            const ChatWorldCandidate *candidate) {
    if (!w || !d) {
        return;
    }
    if (candidate) {
        chatworld_copy_field(d->candidate, sizeof(d->candidate), candidate->label);
    }
    if (candidate && candidate->kind == CHAT_CAND_MEM_WRITE) {
        chatworld_copy_token(d->key, candidate->key);
        chatworld_copy_token(d->value, candidate->value);
        chatworld_memory_write(w, d->key, d->value);
    } else if (candidate && candidate->kind == CHAT_CAND_MEM_READ) {
        chatworld_copy_token(d->key, candidate->key);
        if (!chatworld_memory_read(w, candidate->key, d->value)) {
            d->frame = CHAT_FRAME_UNKNOWN;
        }
    }
}

static ChatWorldDecision chatworld_step_internal(NervaEngine *e, ChatWorldNerva *cw, ChatWorld *w,
                                                 const ChatWorldTurn *turn, int allow_explore,
                                                 uint32_t explore_seed);

static int chatworld_response_is_correct(const ChatWorldDecision *d, const ChatWorldTurn *turn) {
    if (!d || !turn || d->no_supported_response) {
        return 0;
    }
    switch (turn->expected) {
    case CHAT_EXPECT_GREET:
        return d->action == CHAT_ACTION_RESP_GREET && d->frame == CHAT_FRAME_GREET;
    case CHAT_EXPECT_ACK:
        if (turn->expected_key[0] != '\0' && turn->expected_value[0] != '\0') {
            return d->action == CHAT_ACTION_MEM_WRITE_PAIR && d->frame == CHAT_FRAME_ACK &&
                   strcmp(d->key, turn->expected_key) == 0 &&
                   strcmp(d->value, turn->expected_value) == 0;
        }
        return d->action == CHAT_ACTION_RESP_ACK && d->frame == CHAT_FRAME_ACK;
    case CHAT_EXPECT_UNKNOWN:
        return d->frame == CHAT_FRAME_UNKNOWN;
    case CHAT_EXPECT_MEMORY_VALUE:
        return d->action == CHAT_ACTION_MEM_READ_PAIR && d->frame == CHAT_FRAME_ANSWER_MEMORY &&
               turn->expected_key[0] != '\0' && strcmp(d->key, turn->expected_key) == 0 &&
               turn->expected_value[0] != '\0' &&
               strcmp(d->value, turn->expected_value) == 0;
    default:
        return 0;
    }
}

static void chatworld_trace_decision(const NervaEngine *e, FILE *trace_out, const char *phase,
                                     const ChatWorldTurn *turn, const ChatWorldDecision *d, int correct,
                                     uint32_t mutation_delta) {
    if (!e || !trace_out || !phase || !turn || !d) {
        return;
    }
    fprintf(trace_out,
            "phase=%s utterance=\"%s\" expected=%u selected_action=%s selected_frame=%s "
            "candidate=\"%s\" score=%d no_supported=%u correct=%d mutation_delta=%u "
            "key=\"%s\" value=\"%s\" selected_edges=",
            phase, turn->utterance, (unsigned)turn->expected,
            d->action < CHAT_ACTION_COUNT ? chatworld_action_name(d->action) : "NO_ACTION",
            chatworld_frame_name(d->frame), d->candidate, d->score,
            d->no_supported_response ? 1u : 0u, correct, mutation_delta, d->key, d->value);
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
    return chatworld_step_internal(e, cw, w, turn, 0, 0u);
}

static ChatWorldDecision chatworld_step_internal(NervaEngine *e, ChatWorldNerva *cw, ChatWorld *w,
                                                 const ChatWorldTurn *turn, int allow_explore,
                                                 uint32_t explore_seed) {
    ChatWorldDecision d;
    memset(&d, 0, sizeof(d));
    d.action = CHAT_ACTION_COUNT;
    d.frame = CHAT_FRAME_UNKNOWN;

    char tokens[CHATWORLD_MAX_TOKENS][CHATWORLD_MAX_TOKEN_LEN + 1u];
    uint32_t count = 0;
    chatworld_tokenize(turn->utterance, tokens, &count);
    chatworld_emit_surface_internal(e, cw, turn->utterance, allow_explore);

    ChatWorldCandidateList candidates;
    chatworld_build_candidates(e, cw, tokens, count, &candidates, allow_explore);
    nerva_graph_rebuild_adjacency(e);

    uint32_t best_edges[CHATWORLD_MAX_SELECTED_EDGES];
    uint32_t best_edge_count = 0;
    int32_t best_score = 0;
    uint32_t best_index = UINT32_MAX;

    for (uint32_t ci = 0; ci < candidates.count; ++ci) {
        uint32_t edges[CHATWORLD_MAX_SELECTED_EDGES];
        uint32_t edge_count = 0;
        int32_t score = chatworld_candidate_score(e, cw, &candidates.items[ci], edges, &edge_count);
        if (score > best_score) {
            best_score = score;
            best_index = ci;
            best_edge_count = edge_count;
            for (uint32_t j = 0; j < edge_count; ++j) {
                best_edges[j] = edges[j];
            }
        }
    }

    if (allow_explore) {
        if (candidates.count == 0) {
            d.no_supported_response = true;
            return d;
        }
        uint32_t explored = explore_seed % candidates.count;
        if (best_score <= 0) {
            best_index = explored;
            best_score = 1;
            best_edge_count = 0;
            for (uint32_t i = 0; i < cw->token_count; ++i) {
                uint32_t edge_id = chatworld_create_edge(e, cw->token_node[i],
                                                         candidates.items[best_index].action_node);
                if (edge_id != UINT32_MAX && best_edge_count < CHATWORLD_MAX_SELECTED_EDGES) {
                    best_edges[best_edge_count++] = edge_id;
                }
                edge_id = chatworld_create_edge(e, cw->token_at_node[i],
                                                candidates.items[best_index].action_node);
                if (edge_id != UINT32_MAX && best_edge_count < CHATWORLD_MAX_SELECTED_EDGES) {
                    best_edges[best_edge_count++] = edge_id;
                }
            }
            for (uint32_t i = 0; i < cw->pair_count; ++i) {
                uint32_t edge_id = chatworld_create_edge(e, cw->pair_node[i],
                                                         candidates.items[best_index].action_node);
                if (edge_id != UINT32_MAX && best_edge_count < CHATWORLD_MAX_SELECTED_EDGES) {
                    best_edges[best_edge_count++] = edge_id;
                }
            }
        }
    }

    if (best_score <= 0 || best_index == UINT32_MAX) {
        if (!allow_explore) {
            d.no_supported_response = true;
            return d;
        }
        d.no_supported_response = true;
        return d;
    }

    ChatWorldCandidate *selected = &candidates.items[best_index];
    d.action = selected->action;
    d.frame = chatworld_frame_for_action(selected->action);
    d.score = best_score;
    d.selected_edge_count = best_edge_count;
    for (uint32_t i = 0; i < best_edge_count; ++i) {
        d.selected_edges[i] = best_edges[i];
    }

    chatworld_apply_selected_action(w, &d, selected);

    chatworld_record_selected_edges(e, &d);
    return d;
}

static void chatworld_train_turn(NervaEngine *e, ChatWorldNerva *cw, ChatWorld *w,
                                 const ChatWorldTurn *turn, ChatWorldMetrics *m,
                                 uint32_t explore_seed, FILE *trace_out) {
    ChatWorldDecision d = chatworld_step_internal(e, cw, w, turn, 1, explore_seed);
    int correct = chatworld_response_is_correct(&d, turn);
    uint64_t before = e->debug.mutations_applied;

    if (!d.no_supported_response) {
        if (correct) {
            nerva_feedback_correct(e);
        } else {
            nerva_feedback_wrong(e);
        }
        nerva_apply_mutations(e);
    }
    uint32_t mutation_delta = (uint32_t)(e->debug.mutations_applied - before);
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
    if (d.action == CHAT_ACTION_MEM_WRITE_PAIR) {
        m->memory_write_count++;
        m->binding_candidate_count++;
    }
    if (d.action == CHAT_ACTION_MEM_READ_PAIR) {
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
    if (d.action == CHAT_ACTION_MEM_WRITE_PAIR) {
        m->memory_write_count++;
        m->binding_candidate_count++;
    }
    if (d.action == CHAT_ACTION_MEM_READ_PAIR) {
        m->memory_read_count++;
        m->binding_candidate_count++;
    }
    m->eval_mutations += mutation_delta;
}

static void chatworld_zero_response_edges(NervaEngine *e) {
    if (!e) {
        return;
    }
    for (uint32_t i = 0; i < e->edge_count; ++i) {
        NervaEdge *ed = &e->edges[i];
        if (ed->relation == CHAT_REL_SURFACE_TO_ACTION) {
            ed->weight = 0;
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

    if (cfg->train && chatworld_load_dataset(cfg->train_path, &train_ds) != 0) {
        return -1;
    }
    if (cfg->eval && chatworld_load_dataset(cfg->frozen_path, &frozen_ds) != 0) {
        return -1;
    }

    chatworld_reset(&w);
    if (chatworld_nerva_init(e, &cw) != 0) {
        return -1;
    }

    FILE *trace_out = NULL;
    if (cfg->trace_path) {
        trace_out = fopen(cfg->trace_path, "w");
        if (!trace_out) {
            return -1;
        }
        fprintf(trace_out, "chatworld_trace_v1=1\n");
        fprintf(trace_out, "train_path=%s\n", cfg->train_path ? cfg->train_path : "");
        fprintf(trace_out, "frozen_path=%s\n", cfg->frozen_path ? cfg->frozen_path : "");
    }

    if (cfg->train) {
        for (uint32_t epoch = 0; epoch < cfg->train_epochs; ++epoch) {
            chatworld_reset(&w);
            for (uint32_t i = 0; i < train_ds.count; ++i) {
                uint32_t explore_seed = cfg->seed + epoch * train_ds.count + i;
                chatworld_train_turn(e, &cw, &w, &train_ds.turns[i], &out->metrics, explore_seed,
                                     trace_out);
            }
        }
    }

    if (cfg->ablate_response_edges) {
        chatworld_zero_response_edges(e);
    }

    if (cfg->eval) {
        chatworld_reset(&w);
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
