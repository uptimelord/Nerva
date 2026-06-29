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
#include <string.h>

#define CHAT_REL_SURFACE_TO_ACTION ((uint16_t)(NERVA_REL_CUSTOM_BASE + 40u))

static const ChatWorldTurn CHAT_TRAIN_TURNS[] = {
    {"hello", CHAT_EXPECT_GREET, NULL, NULL, true},
    {"hi", CHAT_EXPECT_GREET, NULL, NULL, true},
    {"thanks", CHAT_EXPECT_ACK, NULL, NULL, true},
    {"thank you", CHAT_EXPECT_ACK, NULL, NULL, true},
    {"what is my favorite food", CHAT_EXPECT_UNKNOWN, NULL, NULL, true},
    {"my name is Ada", CHAT_EXPECT_ACK, "name", "ada", true},
    {"what is my name", CHAT_EXPECT_MEMORY_VALUE, "name", "ada", true},
    {"poodle is dog", CHAT_EXPECT_ACK, "poodle", "dog", true},
    {"what is poodle", CHAT_EXPECT_MEMORY_VALUE, "poodle", "dog", true},
    {"no my name is Grace", CHAT_EXPECT_ACK, "name", "grace", true},
    {"what is my name", CHAT_EXPECT_MEMORY_VALUE, "name", "grace", true},
};

static const ChatWorldTurn CHAT_EVAL_TURNS[] = {
    {"hey", CHAT_EXPECT_GREET, NULL, NULL, false},
    {"hello there", CHAT_EXPECT_GREET, NULL, NULL, false},
    {"thank you kindly", CHAT_EXPECT_ACK, NULL, NULL, false},
    {"what is my favorite color", CHAT_EXPECT_UNKNOWN, NULL, NULL, false},
    {"my name is Mira", CHAT_EXPECT_ACK, "name", "mira", true},
    {"what is my name", CHAT_EXPECT_MEMORY_VALUE, "name", "mira", false},
    {"sparrow is bird", CHAT_EXPECT_ACK, "sparrow", "bird", true},
    {"what is sparrow", CHAT_EXPECT_MEMORY_VALUE, "sparrow", "bird", false},
};

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

static uint32_t chatworld_create_edge(NervaEngine *e, uint32_t source, uint32_t target) {
    uint32_t existing = UINT32_MAX;
    if (e && source < e->node_count && target < e->node_count) {
        for (uint32_t i = 0; i < e->edge_count; ++i) {
            const NervaEdge *ed = &e->edges[i];
            if (!(ed->flags & NERVA_EDGE_DELETED) && ed->source == source &&
                ed->target == target && ed->relation == CHAT_REL_SURFACE_TO_ACTION) {
                existing = i;
                break;
            }
        }
    }

    uint32_t id = nerva_graph_create_edge(e, source, target, CHAT_REL_SURFACE_TO_ACTION);
    if (id != UINT32_MAX && existing == UINT32_MAX) {
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
    cfg->eval_episodes = (uint32_t)(sizeof(CHAT_EVAL_TURNS) / sizeof(CHAT_EVAL_TURNS[0]));
    cfg->train = true;
    cfg->eval = true;
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

int chatworld_emit_surface(NervaEngine *e, ChatWorldNerva *cw, const char *utterance) {
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

    char name[80];
    for (uint32_t i = 0; i < count; ++i) {
        snprintf(name, sizeof(name), "TOKEN:%s", tokens[i]);
        cw->token_node[i] = chatworld_get_or_create(e, name);
        if (cw->token_node[i] == UINT32_MAX) {
            return -1;
        }
        chatworld_emit_node(e, cw->token_node[i]);
        chatworld_emit_node(e, cw->position[i]);
    }
    for (uint32_t i = count; i < CHATWORLD_MAX_TOKENS; ++i) {
        cw->token_node[i] = UINT32_MAX;
    }
    chatworld_emit_node(e, cw->turn_end);

    for (uint32_t i = 0; i < count; ++i) {
        for (uint32_t a = 0; a < CHAT_ACTION_COUNT; ++a) {
            cw->policy_edge[i][a] = chatworld_create_edge(e, cw->token_node[i], cw->action_node[a]);
        }
    }
    nerva_graph_rebuild_adjacency(e);
    chatworld_tick_quiet(e, 8u);
    return 0;
}

static void chatworld_infer_pair(char tokens[CHATWORLD_MAX_TOKENS][CHATWORLD_MAX_TOKEN_LEN + 1u],
                                 uint32_t count, char *key, char *value) {
    key[0] = '\0';
    value[0] = '\0';
    if (count > 0 && (strcmp(tokens[0], "what") == 0 || strcmp(tokens[0], "who") == 0 ||
                      strcmp(tokens[0], "where") == 0 || strcmp(tokens[0], "when") == 0 ||
                      strcmp(tokens[0], "why") == 0 || strcmp(tokens[0], "how") == 0)) {
        return;
    }
    for (uint32_t i = 0; i + 2u < count; ++i) {
        if (strcmp(tokens[i + 1u], "is") != 0) {
            continue;
        }
        chatworld_copy_token(key, tokens[i]);
        chatworld_copy_token(value, tokens[i + 2u]);
        return;
    }
}

static void chatworld_infer_read_key(char tokens[CHATWORLD_MAX_TOKENS][CHATWORLD_MAX_TOKEN_LEN + 1u],
                                     uint32_t count, char *key) {
    key[0] = '\0';
    if (count == 0) {
        return;
    }
    chatworld_copy_token(key, tokens[count - 1u]);
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

static int chatworld_action_valid(ChatWorldAction action, const char *write_key,
                                  const char *read_key) {
    if (action == CHAT_ACTION_MEM_WRITE_PAIR) {
        return write_key && write_key[0] != '\0';
    }
    if (action == CHAT_ACTION_MEM_READ_PAIR) {
        return read_key && read_key[0] != '\0';
    }
    return 1;
}

static uint32_t chatworld_action_score(const NervaEngine *e, const ChatWorldNerva *cw,
                                       ChatWorldAction action, uint32_t *selected,
                                       uint32_t *selected_count) {
    uint32_t score = 0;
    *selected_count = 0;
    for (uint32_t i = 0; i < cw->token_count; ++i) {
        uint32_t edge_id = cw->policy_edge[i][action];
        if (edge_id == UINT32_MAX || edge_id >= e->edge_count) {
            continue;
        }
        int32_t w = e->edges[edge_id].weight;
        if (w <= 0) {
            continue;
        }
        score += (uint32_t)w;
        if (*selected_count < CHATWORLD_MAX_TOKENS) {
            selected[*selected_count] = edge_id;
            (*selected_count)++;
        }
    }
    return score;
}

static void chatworld_apply_selected_action(ChatWorld *w, ChatWorldDecision *d,
                                            const char *write_key, const char *write_value,
                                            const char *read_key) {
    if (!w || !d) {
        return;
    }
    if (d->action == CHAT_ACTION_MEM_WRITE_PAIR) {
        chatworld_copy_token(d->key, write_key);
        chatworld_copy_token(d->value, write_value);
        chatworld_memory_write(w, d->key, d->value);
    } else if (d->action == CHAT_ACTION_MEM_READ_PAIR) {
        chatworld_copy_token(d->key, read_key);
        if (!chatworld_memory_read(w, read_key, d->value)) {
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
        if (turn->expected_key && turn->expected_value) {
            return d->action == CHAT_ACTION_MEM_WRITE_PAIR && d->frame == CHAT_FRAME_ACK &&
                   strcmp(d->key, turn->expected_key) == 0 &&
                   strcmp(d->value, turn->expected_value) == 0;
        }
        return d->action == CHAT_ACTION_RESP_ACK && d->frame == CHAT_FRAME_ACK;
    case CHAT_EXPECT_UNKNOWN:
        return d->action == CHAT_ACTION_RESP_UNKNOWN && d->frame == CHAT_FRAME_UNKNOWN;
    case CHAT_EXPECT_MEMORY_VALUE:
        return d->action == CHAT_ACTION_MEM_READ_PAIR && d->frame == CHAT_FRAME_ANSWER_MEMORY &&
               turn->expected_value &&
               strcmp(d->value, turn->expected_value) == 0;
    default:
        return 0;
    }
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
    chatworld_emit_surface(e, cw, turn->utterance);

    char write_key[CHATWORLD_MAX_TOKEN_LEN + 1u];
    char write_value[CHATWORLD_MAX_TOKEN_LEN + 1u];
    char read_key[CHATWORLD_MAX_TOKEN_LEN + 1u];
    chatworld_infer_pair(tokens, count, write_key, write_value);
    chatworld_infer_read_key(tokens, count, read_key);

    uint32_t best_edges[CHATWORLD_MAX_TOKENS];
    uint32_t best_edge_count = 0;
    uint32_t best_score = 0;
    ChatWorldAction best_action = CHAT_ACTION_COUNT;

    for (uint32_t a = 0; a < CHAT_ACTION_COUNT; ++a) {
        if (!chatworld_action_valid((ChatWorldAction)a, write_key, read_key)) {
            continue;
        }
        uint32_t edges[CHATWORLD_MAX_TOKENS];
        uint32_t edge_count = 0;
        uint32_t score = chatworld_action_score(e, cw, (ChatWorldAction)a, edges, &edge_count);
        if (score > best_score) {
            best_score = score;
            best_action = (ChatWorldAction)a;
            best_edge_count = edge_count;
            for (uint32_t j = 0; j < edge_count; ++j) {
                best_edges[j] = edges[j];
            }
        }
    }

    if (allow_explore) {
        ChatWorldAction valid[CHAT_ACTION_COUNT];
        uint32_t valid_count = 0;
        for (uint32_t a = 0; a < CHAT_ACTION_COUNT; ++a) {
            if (chatworld_action_valid((ChatWorldAction)a, write_key, read_key)) {
                valid[valid_count++] = (ChatWorldAction)a;
            }
        }
        if (valid_count == 0) {
            d.no_supported_response = true;
            return d;
        }
        ChatWorldAction explored = valid[explore_seed % valid_count];
        if (best_score == 0 || (explore_seed % 3u) != 0u) {
            best_action = explored;
            best_score = 1u;
            best_edge_count = 0;
            for (uint32_t i = 0; i < cw->token_count; ++i) {
                uint32_t edge_id = cw->policy_edge[i][best_action];
                if (edge_id != UINT32_MAX && best_edge_count < CHATWORLD_MAX_TOKENS) {
                    best_edges[best_edge_count++] = edge_id;
                }
            }
        }
    }

    if (best_score == 0 || best_action == CHAT_ACTION_COUNT) {
        if (!allow_explore) {
            d.no_supported_response = true;
            return d;
        }
        d.no_supported_response = true;
        return d;
    }

    d.action = best_action;
    d.frame = chatworld_frame_for_action(best_action);
    d.score = best_score;
    d.selected_edge_count = best_edge_count;
    for (uint32_t i = 0; i < best_edge_count; ++i) {
        d.selected_edges[i] = best_edges[i];
    }

    chatworld_apply_selected_action(w, &d, write_key, write_value, read_key);

    chatworld_record_selected_edges(e, &d);
    return d;
}

static void chatworld_train_turn(NervaEngine *e, ChatWorldNerva *cw, ChatWorld *w,
                                 const ChatWorldTurn *turn, ChatWorldMetrics *m,
                                 uint32_t explore_seed) {
    ChatWorldDecision d = chatworld_step_internal(e, cw, w, turn, 1, explore_seed);
    int correct = chatworld_response_is_correct(&d, turn);

    if (!d.no_supported_response) {
        if (correct) {
            nerva_feedback_correct(e);
        } else {
            nerva_feedback_wrong(e);
        }
        nerva_apply_mutations(e);
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
    }
    if (d.action == CHAT_ACTION_MEM_READ_PAIR) {
        m->memory_read_count++;
    }
}

static void chatworld_eval_turn(NervaEngine *e, ChatWorldNerva *cw, ChatWorld *w,
                                const ChatWorldTurn *turn, ChatWorldMetrics *m) {
    uint64_t before = e->debug.mutations_applied;
    ChatWorldDecision d = chatworld_step(e, cw, w, turn);
    uint64_t after = e->debug.mutations_applied;
    int correct = chatworld_response_is_correct(&d, turn);

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
    }
    if (d.action == CHAT_ACTION_MEM_READ_PAIR) {
        m->memory_read_count++;
    }
    m->eval_mutations += (uint32_t)(after - before);
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
    chatworld_reset(&w);
    if (chatworld_nerva_init(e, &cw) != 0) {
        return -1;
    }

    if (cfg->train) {
        for (uint32_t epoch = 0; epoch < cfg->train_epochs; ++epoch) {
            for (uint32_t i = 0; i < sizeof(CHAT_TRAIN_TURNS) / sizeof(CHAT_TRAIN_TURNS[0]); ++i) {
                uint32_t explore_seed =
                    cfg->seed + epoch * (uint32_t)(sizeof(CHAT_TRAIN_TURNS) /
                                                   sizeof(CHAT_TRAIN_TURNS[0])) +
                    i;
                chatworld_train_turn(e, &cw, &w, &CHAT_TRAIN_TURNS[i], &out->metrics,
                                     explore_seed);
            }
        }
    }

    if (cfg->ablate_response_edges) {
        chatworld_zero_response_edges(e);
    }

    if (cfg->eval) {
        for (uint32_t i = 0; i < sizeof(CHAT_EVAL_TURNS) / sizeof(CHAT_EVAL_TURNS[0]); ++i) {
            chatworld_eval_turn(e, &cw, &w, &CHAT_EVAL_TURNS[i], &out->metrics);
        }
    }

    if (cfg->ablate_response_edges) {
        out->metrics.response_ablation_correct = out->metrics.eval_correct;
        out->metrics.response_ablation_total = out->metrics.eval_total;
    }

    out->metrics.trace_count = e->trace_count;
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
}
