// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Paul Odantabao II

#include "tagworld_viz.h"

#include "nerva_config.h"
#include "nerva_debug.h"
#include "nerva_engine.h"
#include "nerva_event.h"
#include "nerva_graph.h"
#include "nerva_learning.h"
#include "nerva_math.h"
#include "nerva_mutation.h"
#include "nerva_persist.h"
#include "nerva_prediction.h"
#include "nerva_trace.h"

#include <stdlib.h>
#include <string.h>

static uint32_t g_tagworld_rng = 1u;
static TagWorldOnlinePhase g_online_phase = TAGWORLD_ONLINE_NONE;

typedef struct TagWorldPolicySnap {
    NervaNode *nodes;
    NervaEdge *edges;
    uint32_t node_count;
    uint32_t edge_count;
    int active;
} TagWorldPolicySnap;

static TagWorldPolicySnap g_tool_policy_snap;
static TagWorldPolicySnap g_online_learned_snap;

TagWorldOnlinePhase tagworld_online_phase(void) {
    return g_online_phase;
}

static uint32_t tagworld_xorshift(void) {
    uint32_t x = g_tagworld_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_tagworld_rng = x ? x : 1u;
    return g_tagworld_rng;
}

static uint32_t tagworld_get_or_create(NervaEngine *e, const char *name) {
    return nerva_get_or_create_node(e, name);
}

static uint32_t tagworld_create_edge(NervaEngine *e, uint32_t src, uint32_t dst, uint16_t rel) {
    return nerva_graph_create_edge(e, src, dst, rel);
}

const char *tagworld_action_name(TagWorldAction action) {
    switch (action) {
    case TAG_ACTION_WAIT:
        return "ACTION_WAIT";
    case TAG_ACTION_RUN_TO_SAFE:
        return "ACTION_RUN_TO_SAFE";
    case TAG_ACTION_PUSH_BLOCK:
        return "ACTION_PUSH_BLOCK";
    case TAG_ACTION_PUSH_BLOCK_TO_DOORWAY:
        return "ACTION_PUSH_BLOCK_TO_DOORWAY";
    default:
        return "ACTION_UNKNOWN";
    }
}

const char *tagworld_outcome_name(TagWorldOutcome outcome) {
    switch (outcome) {
    case TAGWORLD_OUTCOME_CAUGHT:
        return "RUNNER_CAUGHT";
    case TAGWORLD_OUTCOME_ESCAPED:
        return "RUNNER_ESCAPED";
    case TAGWORLD_OUTCOME_TIMEOUT:
        return "TIMEOUT";
    default:
        return "NONE";
    }
}

const char *tagworld_mode_name(TagWorldMode mode) {
    switch (mode) {
    case TAGWORLD_MODE_OBSERVER:
        return "observer";
    case TAGWORLD_MODE_PREDICTION:
        return "prediction";
    case TAGWORLD_MODE_ACTION:
        return "action";
    default:
        return "unknown";
    }
}

const char *tagworld_map_name(TagWorldMapId map_id) {
    switch (map_id) {
    case TAGWORLD_MAP_TOOL_PRESSURE:
        return "tool_pressure";
    case TAGWORLD_MAP_CORRIDOR:
    default:
        return "corridor";
    }
}

void tagworld_config_defaults(TagWorldConfig *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->seed = 1u;
    cfg->episodes = 1u;
    cfg->max_ticks = 64u;
    cfg->trace_every = 1000u;
    cfg->grid = 7;
    cfg->mode = TAGWORLD_MODE_OBSERVER;
    cfg->map_id = TAGWORLD_MAP_CORRIDOR;
    cfg->online_learn_episodes = 200u;
    cfg->online_eval_episodes = 100u;
    cfg->online_explore_pct = 15u;
    cfg->online_anneal_episodes = 50u;
}

static void tagworld_init_map_corridor(TagWorld *w, int grid) {
    memset(w, 0, sizeof(*w));
    if (grid < 5) {
        grid = 5;
    }
    if (grid > TAGWORLD_MAX_DIM) {
        grid = TAGWORLD_MAX_DIM;
    }
    w->width = grid;
    w->height = grid;

    for (int y = 0; y < grid; ++y) {
        for (int x = 0; x < grid; ++x) {
            if (x == 0 || y == 0 || x == grid - 1 || y == grid - 1) {
                w->cells[y][x] = TAG_CELL_WALL;
            } else {
                w->cells[y][x] = TAG_CELL_EMPTY;
            }
        }
    }

    w->doorway.x = 3;
    w->doorway.y = 3;
    w->safe.x = 5;
    w->safe.y = 2;
    w->cells[w->doorway.y][w->doorway.x] = TAG_CELL_DOORWAY;
    w->cells[w->safe.y][w->safe.x] = TAG_CELL_SAFE;

    /* Narrow corridor: force doorway choke; leave side bypass lanes at x=1 and x=5. */
    for (int x = 2; x <= 4; ++x) {
        w->cells[2][x] = TAG_CELL_WALL;
        w->cells[4][x] = TAG_CELL_WALL;
    }

    w->runner.x = 1;
    w->runner.y = 3;
    w->seeker.x = 5;
    w->seeker.y = 3;
    w->block.x = 1;
    w->block.y = 5;
    w->map_id = TAGWORLD_MAP_CORRIDOR;
}

void tagworld_init_map(TagWorld *w, int grid) {
    tagworld_init_map_corridor(w, grid);
}

void tagworld_init_map_tool_pressure(TagWorld *w, int grid) {
    tagworld_init_map_corridor(w, grid);

    /* Block top-left bypass: safe only reachable via south detour after doorway block. */
    for (int x = 2; x <= 4; ++x) {
        w->cells[1][x] = TAG_CELL_WALL;
    }
    /* South detour: leave x=2 and x=4 open on row 4; keep x=3 blocked. */
    w->cells[4][2] = TAG_CELL_EMPTY;
    w->cells[4][4] = TAG_CELL_EMPTY;

    /* Safe on south lane. */
    w->cells[w->safe.y][w->safe.x] = TAG_CELL_EMPTY;
    w->safe.x = 5;
    w->safe.y = 4;
    w->cells[w->safe.y][w->safe.x] = TAG_CELL_SAFE;

    w->map_id = TAGWORLD_MAP_TOOL_PRESSURE;
}

static void tagworld_reset_tool_pressure(TagWorld *w) {
    w->runner.x = 1;
    w->runner.y = 3;
    w->seeker.x = 5;
    w->seeker.y = 3;
    w->block.x = 2;
    w->block.y = 3;
    w->episode_variant = 3u;
}

void tagworld_reset(TagWorld *w, uint32_t seed, uint32_t episode) {
    TagWorldConfig cfg;
    tagworld_config_defaults(&cfg);
    cfg.seed = seed;
    cfg.grid = w->width > 0 ? w->width : 7;
    cfg.map_id = w->map_id;
    tagworld_reset_for_config(w, &cfg, episode);
}

void tagworld_reset_for_config(TagWorld *w, const TagWorldConfig *cfg, uint32_t episode) {
    int grid = cfg->grid > 0 ? cfg->grid : 7;
    w->map_id = cfg->map_id;

    if (cfg->map_id == TAGWORLD_MAP_TOOL_PRESSURE) {
        tagworld_init_map_tool_pressure(w, grid);
    } else {
        tagworld_init_map_corridor(w, grid);
    }

    w->seed = cfg->seed;
    w->episode = episode;
    w->tick = 0;
    w->done = false;
    w->outcome = TAGWORLD_OUTCOME_NONE;

    if (cfg->map_id == TAGWORLD_MAP_TOOL_PRESSURE) {
        tagworld_reset_tool_pressure(w);
        return;
    }

    w->episode_variant = (cfg->seed ^ (episode * 2654435761u)) % 3u;

    w->runner.x = 1;
    w->runner.y = 3;
    w->seeker.x = 5;
    w->seeker.y = 3;

    if (w->episode_variant == 1u) {
        w->block.x = w->doorway.x;
        w->block.y = w->doorway.y;
    } else if (w->episode_variant == 2u) {
        w->block.x = 2;
        w->block.y = 3;
    } else {
        w->block.x = 1;
        w->block.y = 5;
    }
}

int tagworld_manhattan(TagWorldPos a, TagWorldPos b) {
    int dx = a.x - b.x;
    int dy = a.y - b.y;
    if (dx < 0) {
        dx = -dx;
    }
    if (dy < 0) {
        dy = -dy;
    }
    return dx + dy;
}

int tagworld_is_block_at_doorway(const TagWorld *w) {
    return w->block.x == w->doorway.x && w->block.y == w->doorway.y;
}

int tagworld_is_doorway_open(const TagWorld *w) {
    return !tagworld_is_block_at_doorway(w);
}

static int tagworld_cell_walkable_mut(TagWorld *w, int x, int y) {
    if (x < 0 || y < 0 || x >= w->width || y >= w->height) {
        return 0;
    }
    if (w->cells[y][x] == TAG_CELL_WALL) {
        return 0;
    }
    if (x == w->block.x && y == w->block.y) {
        return 0;
    }
    if (x == w->runner.x && y == w->runner.y) {
        return 0;
    }
    if (x == w->seeker.x && y == w->seeker.y) {
        return 0;
    }
    if (w->map_id == TAGWORLD_MAP_TOOL_PRESSURE && !(x == w->safe.x && y == w->safe.y)) {
        TagWorldPos cell = {x, y};
        if (tagworld_manhattan(cell, w->seeker) <= 1) {
            return 0;
        }
    }
    return 1;
}

int tagworld_seeker_can_reach_runner(const TagWorld *w) {
    TagWorld copy = *w;
    TagWorldPos start = copy.seeker;
    TagWorldPos goal = copy.runner;
    int visited[TAGWORLD_MAX_DIM][TAGWORLD_MAX_DIM];
    int queue_x[TAGWORLD_MAX_DIM * TAGWORLD_MAX_DIM];
    int queue_y[TAGWORLD_MAX_DIM * TAGWORLD_MAX_DIM];
    memset(visited, 0, sizeof(visited));

    int head = 0;
    int tail = 0;
    queue_x[tail] = start.x;
    queue_y[tail] = start.y;
    tail++;
    visited[start.y][start.x] = 1;

    static const int dx[4] = {1, -1, 0, 0};
    static const int dy[4] = {0, 0, 1, -1};

    while (head < tail) {
        int cx = queue_x[head];
        int cy = queue_y[head];
        head++;
        if (cx == goal.x && cy == goal.y) {
            return 1;
        }
        for (int i = 0; i < 4; ++i) {
            int nx = cx + dx[i];
            int ny = cy + dy[i];
            if (nx < 0 || ny < 0 || nx >= copy.width || ny >= copy.height || visited[ny][nx]) {
                continue;
            }
            if (!tagworld_cell_walkable_mut(&copy, nx, ny)) {
                continue;
            }
            visited[ny][nx] = 1;
            queue_x[tail] = nx;
            queue_y[tail] = ny;
            tail++;
        }
    }
    return 0;
}

static int tagworld_try_push_block(TagWorld *w, int dx, int dy) {
    int bx = w->block.x;
    int by = w->block.y;
    int rx = w->runner.x;
    int ry = w->runner.y;
    if (tagworld_manhattan(w->runner, w->block) != 1) {
        return 0;
    }
    int push_from_x = bx - dx;
    int push_from_y = by - dy;
    if (push_from_x != rx || push_from_y != ry) {
        return 0;
    }
    int nx = bx + dx;
    int ny = by + dy;
    if (nx < 0 || ny < 0 || nx >= w->width || ny >= w->height) {
        return 0;
    }
    if (w->cells[ny][nx] == TAG_CELL_WALL) {
        return 0;
    }
    if (nx == w->seeker.x && ny == w->seeker.y) {
        return 0;
    }
    if (nx == w->runner.x && ny == w->runner.y) {
        return 0;
    }
    w->block.x = nx;
    w->block.y = ny;
    return 1;
}

static int tagworld_bfs_next_step(const TagWorld *w, TagWorldPos start, TagWorldPos goal,
                                  TagWorldPos *next_out) {
    if (start.x == goal.x && start.y == goal.y) {
        return 0;
    }

    TagWorld copy = *w;
    int visited[TAGWORLD_MAX_DIM][TAGWORLD_MAX_DIM];
    int parent_x[TAGWORLD_MAX_DIM][TAGWORLD_MAX_DIM];
    int parent_y[TAGWORLD_MAX_DIM][TAGWORLD_MAX_DIM];
    int dist[TAGWORLD_MAX_DIM][TAGWORLD_MAX_DIM];
    int queue_x[TAGWORLD_MAX_DIM * TAGWORLD_MAX_DIM];
    int queue_y[TAGWORLD_MAX_DIM * TAGWORLD_MAX_DIM];
    memset(visited, 0, sizeof(visited));
    for (int y = 0; y < TAGWORLD_MAX_DIM; ++y) {
        for (int x = 0; x < TAGWORLD_MAX_DIM; ++x) {
            parent_x[y][x] = -1;
            parent_y[y][x] = -1;
            dist[y][x] = -1;
        }
    }

    int head = 0;
    int tail = 0;
    queue_x[tail] = start.x;
    queue_y[tail] = start.y;
    tail++;
    visited[start.y][start.x] = 1;
    dist[start.y][start.x] = 0;

    /* On tool map prefer south detour over east corridor when distances tie. */
    static const int dx_default[4] = {1, -1, 0, 0};
    static const int dy_default[4] = {0, 0, 1, -1};
    static const int dx_tool[4] = {0, 0, 1, -1};
    static const int dy_tool[4] = {1, -1, 0, 0};
    const int *dx = w->map_id == TAGWORLD_MAP_TOOL_PRESSURE ? dx_tool : dx_default;
    const int *dy = w->map_id == TAGWORLD_MAP_TOOL_PRESSURE ? dy_tool : dy_default;
    int found = 0;

    while (head < tail) {
        int cx = queue_x[head];
        int cy = queue_y[head];
        head++;
        if (cx == goal.x && cy == goal.y) {
            found = 1;
            break;
        }
        for (int i = 0; i < 4; ++i) {
            int nx = cx + dx[i];
            int ny = cy + dy[i];
            if (nx < 0 || ny < 0 || nx >= copy.width || ny >= copy.height) {
                continue;
            }
            if (!tagworld_cell_walkable_mut(&copy, nx, ny)) {
                continue;
            }
            if (visited[ny][nx]) {
                continue;
            }
            visited[ny][nx] = 1;
            parent_x[ny][nx] = cx;
            parent_y[ny][nx] = cy;
            dist[ny][nx] = dist[cy][cx] + 1;
            queue_x[tail] = nx;
            queue_y[tail] = ny;
            tail++;
        }
    }

    if (!found) {
        return 0;
    }

    int cx = goal.x;
    int cy = goal.y;
    while (!(parent_x[cy][cx] == start.x && parent_y[cy][cx] == start.y)) {
        int px = parent_x[cy][cx];
        int py = parent_y[cy][cx];
        if (px < 0 || py < 0) {
            return 0;
        }
        cx = px;
        cy = py;
    }
    next_out->x = cx;
    next_out->y = cy;
    return 1;
}

static int tagworld_step_toward(TagWorldPos *pos, TagWorldPos target, TagWorld *w) {
    int best_x = pos->x;
    int best_y = pos->y;
    int best_dist = tagworld_manhattan(*pos, target);
    static const int dx[4] = {1, -1, 0, 0};
    static const int dy[4] = {0, 0, 1, -1};
    for (int i = 0; i < 4; ++i) {
        int nx = pos->x + dx[i];
        int ny = pos->y + dy[i];
        TagWorldPos trial = {nx, ny};
        if (!tagworld_cell_walkable_mut(w, nx, ny)) {
            continue;
        }
        if (nx == w->seeker.x && ny == w->seeker.y && pos != &w->seeker) {
            continue;
        }
        int dist = tagworld_manhattan(trial, target);
        if (dist < best_dist || (dist == best_dist && (nx < best_x || (nx == best_x && ny < best_y)))) {
            best_dist = dist;
            best_x = nx;
            best_y = ny;
        }
    }
    if (best_x == pos->x && best_y == pos->y) {
        return 0;
    }
    pos->x = best_x;
    pos->y = best_y;
    return 1;
}

uint32_t tagworld_valid_action_mask(const TagWorld *w) {
    uint32_t mask = 1u << TAG_ACTION_WAIT;
    if (w->runner.x != w->safe.x || w->runner.y != w->safe.y) {
        mask |= 1u << TAG_ACTION_RUN_TO_SAFE;
    }
    if (tagworld_manhattan(w->runner, w->block) == 1) {
        mask |= 1u << TAG_ACTION_PUSH_BLOCK;
        mask |= 1u << TAG_ACTION_PUSH_BLOCK_TO_DOORWAY;
    }
    return mask;
}

int tagworld_apply_action(TagWorld *w, TagWorldAction action) {
    switch (action) {
    case TAG_ACTION_WAIT:
        return 1;
    case TAG_ACTION_RUN_TO_SAFE: {
        TagWorldPos next;
        if (tagworld_bfs_next_step(w, w->runner, w->safe, &next)) {
            w->runner = next;
            return 1;
        }
        return tagworld_step_toward(&w->runner, w->safe, w);
    }
    case TAG_ACTION_PUSH_BLOCK: {
        int dx = w->block.x - w->runner.x;
        int dy = w->block.y - w->runner.y;
        if (dx != 0) {
            dx = dx > 0 ? 1 : -1;
        }
        if (dy != 0) {
            dy = dy > 0 ? 1 : -1;
        }
        return tagworld_try_push_block(w, dx, dy);
    }
    case TAG_ACTION_PUSH_BLOCK_TO_DOORWAY: {
        int dx = 0;
        int dy = 0;
        if (w->block.x < w->doorway.x) {
            dx = 1;
        } else if (w->block.x > w->doorway.x) {
            dx = -1;
        } else if (w->block.y < w->doorway.y) {
            dy = 1;
        } else if (w->block.y > w->doorway.y) {
            dy = -1;
        }
        if (dx == 0 && dy == 0) {
            return 1;
        }
        return tagworld_try_push_block(w, dx, dy);
    }
    default:
        return 0;
    }
}

void tagworld_step_seeker(TagWorld *w) {
    if (w->done) {
        return;
    }
    if (w->map_id == TAGWORLD_MAP_TOOL_PRESSURE && tagworld_is_block_at_doorway(w)) {
        return;
    }
    (void)tagworld_step_toward(&w->seeker, w->runner, w);
}

void tagworld_check_outcome(TagWorld *w) {
    if (w->done) {
        return;
    }
    if (w->runner.x == w->safe.x && w->runner.y == w->safe.y) {
        w->done = true;
        w->outcome = TAGWORLD_OUTCOME_ESCAPED;
        return;
    }
    if (w->runner.x == w->seeker.x && w->runner.y == w->seeker.y) {
        w->done = true;
        w->outcome = TAGWORLD_OUTCOME_CAUGHT;
        return;
    }
    if (tagworld_manhattan(w->runner, w->seeker) <= 1) {
        w->done = true;
        w->outcome = TAGWORLD_OUTCOME_CAUGHT;
    }
}

TagWorldAction tagworld_scripted_action(const TagWorld *w) {
    if (w->map_id == TAGWORLD_MAP_TOOL_PRESSURE) {
        return tagworld_push_then_run_policy(w, NULL);
    }
    if (w->episode_variant == 0u) {
        return TAG_ACTION_WAIT;
    }
    if (w->episode_variant == 1u) {
        return TAG_ACTION_RUN_TO_SAFE;
    }
    if (!tagworld_is_block_at_doorway(w) && tagworld_manhattan(w->runner, w->block) == 1) {
        return TAG_ACTION_PUSH_BLOCK_TO_DOORWAY;
    }
    return TAG_ACTION_RUN_TO_SAFE;
}

TagWorldAction tagworld_always_run_action(const TagWorld *w) {
    if (w->runner.x == w->safe.x && w->runner.y == w->safe.y) {
        return TAG_ACTION_WAIT;
    }
    return TAG_ACTION_RUN_TO_SAFE;
}

TagWorldAction tagworld_push_then_run_policy(const TagWorld *w, void *ctx) {
    (void)ctx;
    if (!tagworld_is_block_at_doorway(w) && tagworld_manhattan(w->runner, w->block) == 1) {
        return TAG_ACTION_PUSH_BLOCK_TO_DOORWAY;
    }
    if (w->runner.x == w->safe.x && w->runner.y == w->safe.y) {
        return TAG_ACTION_WAIT;
    }
    return TAG_ACTION_RUN_TO_SAFE;
}

int tagworld_simulate_with_policy(TagWorld *w, TagWorldStepPolicy policy, void *ctx,
                                  uint32_t max_ticks) {
    w->done = false;
    w->outcome = TAGWORLD_OUTCOME_NONE;
    w->tick = 0;
    for (uint32_t t = 0; t < max_ticks && !w->done; ++t) {
        w->tick = t;
        TagWorldAction action = policy(w, ctx);
        tagworld_apply_action(w, action);
        tagworld_step_seeker(w);
        tagworld_check_outcome(w);
    }
    if (!w->done) {
        w->done = true;
        w->outcome = TAGWORLD_OUTCOME_TIMEOUT;
    }
    return (int)w->outcome;
}

int tagworld_simulate_until_outcome(TagWorld *w, TagWorldAction runner_action, uint32_t max_ticks) {
    w->done = false;
    w->outcome = TAGWORLD_OUTCOME_NONE;
    w->tick = 0;
    for (uint32_t t = 0; t < max_ticks && !w->done; ++t) {
        w->tick = t;
        tagworld_apply_action(w, runner_action);
        tagworld_step_seeker(w);
        tagworld_check_outcome(w);
    }
    if (!w->done) {
        w->done = true;
        w->outcome = TAGWORLD_OUTCOME_TIMEOUT;
    }
    return (int)w->outcome;
}

static double tagworld_baseline_escape_rate_with_policy(const TagWorldConfig *cfg, uint32_t episodes,
                                                        TagWorldAction (*policy)(const TagWorld *w,
                                                                                 uint32_t *rng)) {
    uint32_t escaped = 0;
    uint32_t rng = cfg->seed;
    for (uint32_t ep = 0; ep < episodes; ++ep) {
        TagWorld w;
        tagworld_reset_for_config(&w, cfg, ep);
        for (uint32_t t = 0; t < cfg->max_ticks && !w.done; ++t) {
            TagWorldAction action = policy(&w, &rng);
            tagworld_apply_action(&w, action);
            tagworld_step_seeker(&w);
            tagworld_check_outcome(&w);
        }
        if (!w.done) {
            w.outcome = TAGWORLD_OUTCOME_TIMEOUT;
        }
        if (w.outcome == TAGWORLD_OUTCOME_ESCAPED) {
            escaped++;
        }
    }
    return episodes > 0 ? (double)escaped / (double)episodes : 0.0;
}

static TagWorldAction tagworld_random_policy(const TagWorld *w, uint32_t *rng) {
    return tagworld_random_action(w, rng);
}

static TagWorldAction tagworld_always_run_policy(const TagWorld *w, uint32_t *rng) {
    (void)rng;
    return tagworld_always_run_action(w);
}

double tagworld_baseline_random_escape_rate(const TagWorldConfig *cfg, uint32_t episodes) {
    return tagworld_baseline_escape_rate_with_policy(cfg, episodes, tagworld_random_policy);
}

double tagworld_baseline_always_run_escape_rate(const TagWorldConfig *cfg, uint32_t episodes) {
    return tagworld_baseline_escape_rate_with_policy(cfg, episodes, tagworld_always_run_policy);
}

TagWorldAction tagworld_random_action(const TagWorld *w, uint32_t *rng) {
    g_tagworld_rng = *rng;
    uint32_t mask = tagworld_valid_action_mask(w);
    TagWorldAction choices[TAG_ACTION_COUNT];
    uint32_t count = 0;
    for (TagWorldAction a = 0; a < TAG_ACTION_COUNT; ++a) {
        if (mask & (1u << a)) {
            choices[count++] = a;
        }
    }
    if (count == 0) {
        *rng = g_tagworld_rng;
        return TAG_ACTION_WAIT;
    }
    TagWorldAction pick = choices[tagworld_xorshift() % count];
    *rng = g_tagworld_rng;
    return pick;
}

int tagworld_nerva_init(NervaEngine *e, TagWorldNerva *tn) {
    memset(tn, 0, sizeof(*tn));
    TagWorldEventIds *ev = &tn->ev;

    ev->runner_near_seeker = tagworld_get_or_create(e, "RUNNER_NEAR_SEEKER");
    ev->seeker_near_runner = tagworld_get_or_create(e, "SEEKER_NEAR_RUNNER");
    ev->doorway_open = tagworld_get_or_create(e, "DOORWAY_OPEN");
    ev->block_at_doorway = tagworld_get_or_create(e, "BLOCK_AT_DOORWAY");
    ev->path_blocked = tagworld_get_or_create(e, "PATH_BLOCKED");
    ev->path_open = tagworld_get_or_create(e, "PATH_OPEN");
    ev->runner_at_safe = tagworld_get_or_create(e, "RUNNER_AT_SAFE");
    ev->runner_caught = tagworld_get_or_create(e, "RUNNER_CAUGHT");
    ev->runner_escaped = tagworld_get_or_create(e, "RUNNER_ESCAPED");
    ev->expect_seeker_through_doorway = tagworld_get_or_create(e, "EXPECT_SEEKER_THROUGH_DOORWAY");
    ev->expect_path_blocked = tagworld_get_or_create(e, "EXPECT_PATH_BLOCKED");
    ev->expect_caught = tagworld_get_or_create(e, "EXPECT_CAUGHT");
    ev->expect_escaped = tagworld_get_or_create(e, "EXPECT_ESCAPED");
    ev->surprise_path_open = tagworld_get_or_create(e, "SURPRISE_PATH_OPEN");
    ev->surprise_path_blocked = tagworld_get_or_create(e, "SURPRISE_PATH_BLOCKED");
    ev->surprise_caught = tagworld_get_or_create(e, "SURPRISE_CAUGHT");
    ev->surprise_escaped = tagworld_get_or_create(e, "SURPRISE_ESCAPED");
    ev->action_wait = tagworld_get_or_create(e, "ACTION_WAIT");
    ev->action_run_to_safe = tagworld_get_or_create(e, "ACTION_RUN_TO_SAFE");
    ev->action_push_block = tagworld_get_or_create(e, "ACTION_PUSH_BLOCK");
    ev->action_push_block_to_doorway = tagworld_get_or_create(e, "ACTION_PUSH_BLOCK_TO_DOORWAY");

    TagWorldEdgeIds *ed = &tn->edge;
    ed->block_at_doorway_to_path_blocked =
        tagworld_create_edge(e, ev->block_at_doorway, ev->path_blocked, TAG_REL_PREDICTS);
    ed->doorway_open_to_path_open =
        tagworld_create_edge(e, ev->doorway_open, ev->path_open, TAG_REL_PREDICTS);
    ed->path_open_to_caught =
        tagworld_create_edge(e, ev->path_open, ev->runner_caught, TAG_REL_PREDICTS);
    ed->path_blocked_to_escaped =
        tagworld_create_edge(e, ev->path_blocked, ev->runner_escaped, TAG_REL_PREDICTS);
    ed->seeker_near_to_push_doorway =
        tagworld_create_edge(e, ev->seeker_near_runner, ev->action_push_block_to_doorway, TAG_REL_ENABLES);
    ed->doorway_open_to_push_doorway =
        tagworld_create_edge(e, ev->doorway_open, ev->action_push_block_to_doorway, TAG_REL_ENABLES);
    ed->push_doorway_to_block_at_doorway =
        tagworld_create_edge(e, ev->action_push_block_to_doorway, ev->block_at_doorway, TAG_REL_ACTION_LEADS_TO);
    ed->path_blocked_to_run_safe =
        tagworld_create_edge(e, ev->path_blocked, ev->action_run_to_safe, TAG_REL_ENABLES);
    ed->path_open_to_wait =
        tagworld_create_edge(e, ev->path_open, ev->action_wait, TAG_REL_ENABLES);
    ed->path_open_to_push_doorway =
        tagworld_create_edge(e, ev->path_open, ev->action_push_block_to_doorway, TAG_REL_ENABLES);

    nerva_graph_rebuild_adjacency(e);
    return 0;
}

uint32_t tagworld_nerva_edge_weight(const NervaEngine *e, uint32_t edge_id) {
    if (!e || edge_id >= e->edge_count) {
        return 0;
    }
    return (uint32_t)e->edges[edge_id].weight;
}

void tagworld_nerva_train_pair(NervaEngine *e, TagWorldNerva *tn, uint32_t source_ev,
                               uint32_t edge_id, uint32_t target_ev, uint32_t rounds) {
    (void)tn;
    (void)target_ev;
    nerva_set_prediction_mode(e, 0);
    for (uint32_t i = 0; i < rounds; ++i) {
        nerva_activate_node(e, source_ev, NERVA_Q8_8_ONE);
        nerva_tick_n(e, 4);
        nerva_inject_edge_event(e, edge_id, NERVA_Q8_8_ONE);
        nerva_tick(e);
        nerva_feedback_correct(e);
        nerva_apply_mutations(e);
    }
}

void tagworld_nerva_emit_actual(NervaEngine *e, TagWorldNerva *tn, uint32_t node_id) {
    (void)tn;
    nerva_activate_node(e, node_id, NERVA_Q8_8_ONE);
}

void tagworld_nerva_inject_actual_edge(NervaEngine *e, uint32_t edge_id) {
    nerva_inject_edge_event(e, edge_id, NERVA_Q8_8_ONE);
}

void tagworld_nerva_tick_quiet(NervaEngine *e, uint32_t budget) {
    for (uint32_t i = 0; i < budget; ++i) {
        if (e->event_count == 0 && e->active_count == 0) {
            break;
        }
        nerva_tick(e);
    }
}

static uint32_t tagworld_action_node(const TagWorldNerva *tn, TagWorldAction action) {
    switch (action) {
    case TAG_ACTION_WAIT:
        return tn->ev.action_wait;
    case TAG_ACTION_RUN_TO_SAFE:
        return tn->ev.action_run_to_safe;
    case TAG_ACTION_PUSH_BLOCK:
        return tn->ev.action_push_block;
    case TAG_ACTION_PUSH_BLOCK_TO_DOORWAY:
        return tn->ev.action_push_block_to_doorway;
    default:
        return UINT32_MAX;
    }
}

static TagWorldAction tagworld_action_from_node(const TagWorldNerva *tn, uint32_t node_id) {
    if (node_id == tn->ev.action_wait) {
        return TAG_ACTION_WAIT;
    }
    if (node_id == tn->ev.action_run_to_safe) {
        return TAG_ACTION_RUN_TO_SAFE;
    }
    if (node_id == tn->ev.action_push_block) {
        return TAG_ACTION_PUSH_BLOCK;
    }
    if (node_id == tn->ev.action_push_block_to_doorway) {
        return TAG_ACTION_PUSH_BLOCK_TO_DOORWAY;
    }
    return TAG_ACTION_COUNT;
}

static int tagworld_fire_log_contains_recent(const NervaEngine *e, uint32_t node_id) {
    for (uint32_t i = 0; i < e->fire_log_count; ++i) {
        if (e->fire_log[i].node_id == node_id) {
            return 1;
        }
    }
    return 0;
}

static int tagworld_is_policy_action_edge(const TagWorldNerva *tn, uint32_t edge_id) {
    const uint32_t ids[] = {
        tn->edge.path_blocked_to_run_safe,
        tn->edge.path_open_to_push_doorway,
        tn->edge.doorway_open_to_push_doorway,
        tn->edge.seeker_near_to_push_doorway,
        tn->edge.path_open_to_wait,
    };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        if (ids[i] == edge_id) {
            return 1;
        }
    }
    return 0;
}

TagWorldAction tagworld_nerva_select_action(NervaEngine *e, TagWorldNerva *tn,
                                            const TagWorld *w, uint32_t valid_mask) {
    (void)w;
    const uint32_t action_nodes[] = {
        tn->ev.action_wait,
        tn->ev.action_run_to_safe,
        tn->ev.action_push_block,
        tn->ev.action_push_block_to_doorway,
    };
    for (size_t i = 0; i < sizeof(action_nodes) / sizeof(action_nodes[0]); ++i) {
        uint32_t node_id = action_nodes[i];
        if (node_id < e->node_count) {
            e->nodes[node_id].v = e->nodes[node_id].v_rest;
        }
    }

    nerva_set_prediction_mode(e, 0);
    tagworld_nerva_tick_quiet(e, 4);

    nerva_q8_8_t scores[TAG_ACTION_COUNT];
    for (TagWorldAction a = 0; a < TAG_ACTION_COUNT; ++a) {
        scores[a] = 0;
    }

    for (uint32_t edge_id = 0; edge_id < e->edge_count; ++edge_id) {
        const NervaEdge *ed = &e->edges[edge_id];
        if (ed->flags & NERVA_EDGE_DELETED) {
            continue;
        }
        if (!tagworld_is_policy_action_edge(tn, edge_id)) {
            continue;
        }
        if (!tagworld_fire_log_contains_recent(e, ed->source)) {
            continue;
        }
        TagWorldAction action = tagworld_action_from_node(tn, ed->target);
        if (action >= TAG_ACTION_COUNT) {
            continue;
        }
        scores[action] += ed->weight;
    }

    TagWorldAction best = TAG_ACTION_WAIT;
    nerva_q8_8_t best_score = -32768;
    uint32_t best_id = UINT32_MAX;
    for (TagWorldAction a = 0; a < TAG_ACTION_COUNT; ++a) {
        if (!(valid_mask & (1u << a))) {
            continue;
        }
        uint32_t node = tagworld_action_node(tn, a);
        nerva_q8_8_t score = scores[a];
        if (e->nodes[node].v > score) {
            score = e->nodes[node].v;
        }
        if (score > best_score || (score == best_score && node < best_id)) {
            best_score = score;
            best = a;
            best_id = node;
        }
    }
    if (best_score <= 0) {
        TagWorldAction best_by_v = TAG_ACTION_COUNT;
        nerva_q8_8_t best_v = 0;
        for (TagWorldAction a = 0; a < TAG_ACTION_COUNT; ++a) {
            if (!(valid_mask & (1u << a))) {
                continue;
            }
            uint32_t node = tagworld_action_node(tn, a);
            if (e->nodes[node].v > best_v) {
                best_v = e->nodes[node].v;
                best_by_v = a;
            }
        }
        if (best_by_v < TAG_ACTION_COUNT) {
            best = best_by_v;
        } else {
            for (TagWorldAction a = 0; a < TAG_ACTION_COUNT; ++a) {
                if (valid_mask & (1u << a)) {
                    best = a;
                    break;
                }
            }
        }
    }
    tn->last_action = best;
    return best;
}

static void tagworld_metrics_track_queue(NervaEngine *e, TagWorldMetrics *m) {
    if (e->event_count > m->max_event_queue_depth) {
        m->max_event_queue_depth = e->event_count;
    }
    if ((uint64_t)e->mutation_count > m->max_mutation_queue_depth) {
        m->max_mutation_queue_depth = e->mutation_count;
    }
}

static void tagworld_metrics_apply_mutations(NervaEngine *e, TagWorldMetrics *m, uint64_t *applied_before) {
    uint64_t before = *applied_before;
    nerva_apply_mutations(e);
    uint64_t applied = e->debug.mutations_applied;
    if (applied >= before) {
        m->total_mutations_applied += applied - before;
    }
    *applied_before = applied;
    tagworld_metrics_track_queue(e, m);
}

static void tagworld_emit_state_events(NervaEngine *e, TagWorldNerva *tn, TagWorld *w,
                                       TagWorldMetrics *m) {
    if (tagworld_manhattan(w->runner, w->seeker) <= 1) {
        tagworld_nerva_emit_actual(e, tn, tn->ev.runner_near_seeker);
        tagworld_nerva_emit_actual(e, tn, tn->ev.seeker_near_runner);
        m->total_events += 2;
    }
    if (tagworld_is_block_at_doorway(w)) {
        tagworld_nerva_emit_actual(e, tn, tn->ev.block_at_doorway);
        m->total_events++;
    }
    if (tagworld_is_doorway_open(w)) {
        tagworld_nerva_emit_actual(e, tn, tn->ev.doorway_open);
        m->total_events++;
    }
    if (tagworld_is_block_at_doorway(w)) {
        tagworld_nerva_emit_actual(e, tn, tn->ev.path_blocked);
        m->total_events++;
    } else if (tagworld_seeker_can_reach_runner(w)) {
        tagworld_nerva_emit_actual(e, tn, tn->ev.path_open);
        m->total_events++;
    } else {
        tagworld_nerva_emit_actual(e, tn, tn->ev.path_blocked);
        m->total_events++;
    }
    if (w->runner.x == w->safe.x && w->runner.y == w->safe.y) {
        tagworld_nerva_emit_actual(e, tn, tn->ev.runner_at_safe);
        m->total_events++;
    }
    tagworld_metrics_track_queue(e, m);
}

static void tagworld_prediction_resolve_actual(NervaEngine *e, TagWorldNerva *tn, const TagWorld *w) {
    if (tagworld_is_block_at_doorway(w)) {
        nerva_inject_edge_event(e, tn->edge.block_at_doorway_to_path_blocked, NERVA_Q8_8_ONE);
    } else if (tagworld_is_doorway_open(w)) {
        nerva_inject_edge_event(e, tn->edge.doorway_open_to_path_open, NERVA_Q8_8_ONE);
    }
}

static void tagworld_prediction_step(NervaEngine *e, TagWorldNerva *tn, TagWorld *w,
                                     TagWorldMetrics *m, uint64_t *mut_applied_before) {
    uint64_t before_confirm = e->debug.predictions_confirmed;
    uint64_t before_miss = e->debug.predictions_missed;

    nerva_set_prediction_mode(e, 1);
    if (tagworld_is_block_at_doorway(w)) {
        tagworld_nerva_emit_actual(e, tn, tn->ev.block_at_doorway);
        m->total_events++;
    } else if (tagworld_is_doorway_open(w)) {
        tagworld_nerva_emit_actual(e, tn, tn->ev.doorway_open);
        m->total_events++;
    }
    tagworld_nerva_tick_quiet(e, 4);

    tagworld_prediction_resolve_actual(e, tn, w);
    tagworld_nerva_tick_quiet(e, 4);
    tagworld_metrics_apply_mutations(e, m, mut_applied_before);

    m->prediction_confirm_count += e->debug.predictions_confirmed - before_confirm;
    m->prediction_miss_count += e->debug.predictions_missed - before_miss;
    if (e->debug.predictions_missed > before_miss) {
        m->surprise_count++;
    }
    tagworld_metrics_track_queue(e, m);
}

void tagworld_nerva_observe_tick(NervaEngine *e, TagWorldNerva *tn, TagWorld *w,
                                 TagWorldMode mode, TagWorldMetrics *m,
                                 uint64_t *mut_applied_before) {
    if (mode == TAGWORLD_MODE_PREDICTION) {
        tagworld_prediction_step(e, tn, w, m, mut_applied_before);
        return;
    }
    nerva_set_prediction_mode(e, 0);
    tagworld_emit_state_events(e, tn, w, m);
    tagworld_nerva_tick_quiet(e, 8);
    tagworld_metrics_track_queue(e, m);
}

void tagworld_nerva_episode_feedback(NervaEngine *e, TagWorldNerva *tn, TagWorld *w,
                                     TagWorldAction action, TagWorldMode mode,
                                     bool online_tool_acquisition, TagWorldMetrics *m,
                                     uint64_t *mut_applied_before) {
    (void)action;
    if (g_online_phase == TAGWORLD_ONLINE_EVAL) {
        return;
    }

    nerva_set_prediction_mode(e, 0);
    nerva_prediction_clear(e);

    if (w->outcome == TAGWORLD_OUTCOME_ESCAPED) {
        tagworld_nerva_emit_actual(e, tn, tn->ev.runner_escaped);
        if (tagworld_is_block_at_doorway(w) || w->episode_variant == 1u || w->episode_variant == 2u) {
            nerva_inject_edge_event(e, tn->edge.path_blocked_to_escaped, NERVA_Q8_8_ONE);
        }
        nerva_feedback_correct(e);
    } else if (w->outcome == TAGWORLD_OUTCOME_CAUGHT) {
        tagworld_nerva_emit_actual(e, tn, tn->ev.runner_caught);
        if (tagworld_is_doorway_open(w)) {
            nerva_inject_edge_event(e, tn->edge.path_open_to_caught, NERVA_Q8_8_ONE);
        }
        nerva_feedback_wrong(e);
    }

    if (mode == TAGWORLD_MODE_OBSERVER) {
        if (tagworld_is_block_at_doorway(w)) {
            tagworld_nerva_train_pair(e, tn, tn->ev.block_at_doorway,
                                      tn->edge.block_at_doorway_to_path_blocked,
                                      tn->ev.path_blocked, 1);
        }
        if (tagworld_is_doorway_open(w)) {
            tagworld_nerva_train_pair(e, tn, tn->ev.doorway_open, tn->edge.doorway_open_to_path_open,
                                      tn->ev.path_open, 1);
        }
    }

    if (online_tool_acquisition && mode == TAGWORLD_MODE_ACTION &&
        g_online_phase != TAGWORLD_ONLINE_EVAL) {
        if (w->outcome == TAGWORLD_OUTCOME_ESCAPED &&
            (tn->episode_used_push_doorway || tagworld_is_block_at_doorway(w))) {
            tagworld_nerva_train_pair(e, tn, tn->ev.path_open, tn->edge.path_open_to_push_doorway,
                                      tn->ev.action_push_block_to_doorway, 8);
            tagworld_nerva_train_pair(e, tn, tn->ev.doorway_open, tn->edge.doorway_open_to_push_doorway,
                                      tn->ev.action_push_block_to_doorway, 6);
            tagworld_nerva_train_pair(e, tn, tn->ev.action_push_block_to_doorway,
                                      tn->edge.push_doorway_to_block_at_doorway, tn->ev.block_at_doorway, 6);
            tagworld_nerva_train_pair(e, tn, tn->ev.block_at_doorway,
                                      tn->edge.block_at_doorway_to_path_blocked, tn->ev.path_blocked, 4);
            tagworld_nerva_train_pair(e, tn, tn->ev.path_blocked, tn->edge.path_blocked_to_run_safe,
                                      tn->ev.action_run_to_safe, 12);
        } else if ((w->outcome == TAGWORLD_OUTCOME_CAUGHT ||
                    w->outcome == TAGWORLD_OUTCOME_TIMEOUT) &&
                   tagworld_is_doorway_open(w) && !tn->episode_used_push_doorway) {
            nerva_activate_node(e, tn->ev.path_open, NERVA_Q8_8_ONE);
            nerva_activate_node(e, tn->ev.doorway_open, NERVA_Q8_8_ONE);
            nerva_tick_n(e, 4);
            uint32_t action_node = tagworld_action_node(tn, tn->episode_first_action);
            if (action_node != UINT32_MAX) {
                nerva_activate_node(e, action_node, NERVA_Q8_8_ONE);
            }
            nerva_tick(e);
            nerva_feedback_wrong(e);
        } else if ((w->outcome == TAGWORLD_OUTCOME_CAUGHT ||
                    w->outcome == TAGWORLD_OUTCOME_TIMEOUT) &&
                   tn->episode_used_push_doorway && tagworld_is_block_at_doorway(w)) {
            tagworld_nerva_train_pair(e, tn, tn->ev.path_blocked, tn->edge.path_blocked_to_run_safe,
                                      tn->ev.action_run_to_safe, 8);
        }
    }

    tagworld_metrics_apply_mutations(e, m, mut_applied_before);
}

uint32_t tagworld_nerva_pending_expectation_target(const NervaEngine *e) {
    const NervaExpectation *exp = nerva_prediction_pending_for_query(e, e->active_query_tag);
    if (!exp) {
        exp = nerva_prediction_pending_for_query(e, 0);
    }
    return exp ? exp->target : UINT32_MAX;
}

int tagworld_nerva_prediction_confirm_pair(NervaEngine *e, TagWorldNerva *tn, uint32_t *confirm_out,
                                           uint32_t *miss_out, uint64_t *mut_applied_out) {
    if (!e || !tn) {
        return -1;
    }
    uint64_t before_confirm = e->debug.predictions_confirmed;
    uint64_t before_miss = e->debug.predictions_missed;
    uint64_t before_mut = e->debug.mutations_applied;

    nerva_set_prediction_mode(e, 1);
    nerva_mutation_clear_log(e);
    nerva_tick_n(e, 8);
    nerva_activate_node(e, tn->ev.block_at_doorway, NERVA_Q8_8_ONE);
    nerva_tick_n(e, 2);
    nerva_inject_edge_event(e, tn->edge.block_at_doorway_to_path_blocked, NERVA_Q8_8_ONE);
    nerva_tick_n(e, 2);
    nerva_apply_mutations(e);

    if (confirm_out) {
        *confirm_out = (uint32_t)(e->debug.predictions_confirmed - before_confirm);
    }
    if (miss_out) {
        *miss_out = (uint32_t)(e->debug.predictions_missed - before_miss);
    }
    if (mut_applied_out) {
        *mut_applied_out = e->debug.mutations_applied - before_mut;
    }
    return 0;
}

int tagworld_nerva_prediction_mismatch_pair(NervaEngine *e, TagWorldNerva *tn, uint32_t *confirm_out,
                                            uint32_t *miss_out) {
    if (!e || !tn) {
        return -1;
    }
    uint64_t before_confirm = e->debug.predictions_confirmed;
    uint64_t before_miss = e->debug.predictions_missed;

    nerva_set_prediction_mode(e, 1);
    nerva_mutation_clear_log(e);
    nerva_tick_n(e, 8);
    nerva_activate_node(e, tn->ev.block_at_doorway, NERVA_Q8_8_ONE);
    nerva_tick_n(e, 2);
    nerva_inject_edge_event(e, tn->edge.doorway_open_to_path_open, NERVA_Q8_8_ONE);
    nerva_tick_n(e, 2);
    nerva_apply_mutations(e);

    if (confirm_out) {
        *confirm_out = (uint32_t)(e->debug.predictions_confirmed - before_confirm);
    }
    if (miss_out) {
        *miss_out = (uint32_t)(e->debug.predictions_missed - before_miss);
    }
    return 0;
}

void tagworld_nerva_count_edges(const NervaEngine *e, uint32_t *learned, uint32_t *provisional) {
    uint32_t l = 0;
    uint32_t p = 0;
    for (uint32_t i = 0; i < e->edge_count; ++i) {
        if (e->edges[i].flags & NERVA_EDGE_DELETED) {
            continue;
        }
        if (e->edges[i].stability >= e->cfg.prediction_min_stability) {
            l++;
        } else if (e->edges[i].stability > 0) {
            p++;
        }
    }
    if (learned) {
        *learned = l;
    }
    if (provisional) {
        *provisional = p;
    }
}

static void tagworld_record_episode_action(TagWorldNerva *tn, TagWorldAction action) {
    if (tn->episode_first_action >= TAG_ACTION_COUNT) {
        tn->episode_first_action = action;
    }
    if (action == TAG_ACTION_PUSH_BLOCK_TO_DOORWAY) {
        tn->episode_used_push_doorway = true;
        tn->episode_push_doorway_count++;
    }
}

static TagWorldAction tagworld_select_action_for_episode(NervaEngine *e, TagWorldNerva *tn,
                                                         const TagWorld *w, uint32_t valid_mask,
                                                         const TagWorldConfig *cfg,
                                                         uint32_t episode) {
    (void)episode;
    if (g_online_phase != TAGWORLD_ONLINE_EVAL && cfg->online_tool_acquisition &&
        cfg->online_explore_pct > 0u &&
        (tagworld_xorshift() % 100u) < cfg->online_explore_pct) {
        uint32_t rng = g_tagworld_rng;
        TagWorldAction action = tagworld_random_action(w, &rng);
        g_tagworld_rng = rng;
        if (valid_mask & (1u << action)) {
            tn->last_action = action;
            return action;
        }
    }
    return tagworld_nerva_select_action(e, tn, w, valid_mask);
}

static void tagworld_record_action_metric(TagWorldMetrics *m, TagWorldAction action) {
    switch (action) {
    case TAG_ACTION_WAIT:
        m->action_wait_count++;
        break;
    case TAG_ACTION_RUN_TO_SAFE:
        m->action_run_count++;
        break;
    case TAG_ACTION_PUSH_BLOCK:
        m->action_push_block_count++;
        break;
    case TAG_ACTION_PUSH_BLOCK_TO_DOORWAY:
        m->action_push_doorway_count++;
        break;
    default:
        break;
    }
}

void tagworld_build_frame(const TagWorld *w, const TagWorldNerva *tn, const NervaEngine *e,
                          TagWorldFrame *frame) {
    memset(frame, 0, sizeof(*frame));
    frame->episode = w->episode;
    frame->tick = w->tick;
    frame->runner = w->runner;
    frame->seeker = w->seeker;
    frame->block = w->block;
    frame->action = tn->last_action;
    frame->outcome = w->outcome;
    frame->queued_mutations = e ? e->mutation_count : 0;

    for (int y = 0; y < w->height; ++y) {
        for (int x = 0; x < w->width; ++x) {
            char ch = '.';
            if (w->cells[y][x] == TAG_CELL_WALL) {
                ch = '#';
            } else if (w->cells[y][x] == TAG_CELL_DOORWAY) {
                ch = 'D';
            } else if (w->cells[y][x] == TAG_CELL_SAFE) {
                ch = 'Z';
            }
            if (x == w->runner.x && y == w->runner.y) {
                ch = 'R';
            }
            if (x == w->seeker.x && y == w->seeker.y) {
                ch = 'S';
            }
            if (x == w->block.x && y == w->block.y) {
                ch = 'B';
            }
            frame->grid[y][x] = ch;
        }
        frame->grid[y][w->width] = '\0';
    }

    if (tagworld_manhattan(w->runner, w->seeker) <= 1) {
        frame->active_events[frame->active_count++] = "SEEKER_NEAR_RUNNER";
    }
    if (tagworld_is_doorway_open(w)) {
        frame->active_events[frame->active_count++] = "DOORWAY_OPEN";
    }
    if (tagworld_is_block_at_doorway(w)) {
        frame->active_events[frame->active_count++] = "BLOCK_AT_DOORWAY";
        frame->active_events[frame->active_count++] = "PATH_BLOCKED";
    } else if (tagworld_seeker_can_reach_runner(w)) {
        frame->active_events[frame->active_count++] = "PATH_OPEN";
    } else {
        frame->active_events[frame->active_count++] = "PATH_BLOCKED";
    }

    if (e && nerva_prediction_count_pending((NervaEngine *)e) > 0) {
        frame->expected_events[frame->expected_count++] = "PATH_BLOCKED_OR_OPEN";
    }
    (void)tn;
}

static int tagworld_write_replay_frame(FILE *out, const TagWorldFrame *frame) {
    if (!out || !frame) {
        return -1;
    }
    fprintf(out, "{\"episode\":%u,\"tick\":%u,\"grid\":[",
            frame->episode, frame->tick);
    for (int y = 0; frame->grid[y][0] != '\0' && y < TAGWORLD_MAX_DIM; ++y) {
        if (y > 0) {
            fputc(',', out);
        }
        fprintf(out, "\"%s\"", frame->grid[y]);
    }
    fprintf(out, "],\"runner\":{\"x\":%d,\"y\":%d},\"seeker\":{\"x\":%d,\"y\":%d},"
                 "\"block\":{\"x\":%d,\"y\":%d},\"action\":\"%s\",\"outcome\":\"%s\","
                 "\"queued_mutations\":%u,\"active\":[",
            frame->runner.x, frame->runner.y, frame->seeker.x, frame->seeker.y,
            frame->block.x, frame->block.y, tagworld_action_name(frame->action),
            tagworld_outcome_name(frame->outcome), frame->queued_mutations);
    for (uint32_t i = 0; i < frame->active_count; ++i) {
        if (i > 0) {
            fputc(',', out);
        }
        fprintf(out, "\"%s\"", frame->active_events[i]);
    }
    fprintf(out, "],\"expected\":[");
    for (uint32_t i = 0; i < frame->expected_count; ++i) {
        if (i > 0) {
            fputc(',', out);
        }
        fprintf(out, "\"%s\"", frame->expected_events[i]);
    }
    fprintf(out, "]}\n");
    return 0;
}

int tagworld_parse_replay_line(const char *line, TagWorldFrame *frame) {
    memset(frame, 0, sizeof(*frame));
    const char *ep = strstr(line, "\"episode\":");
    const char *tk = strstr(line, "\"tick\":");
    if (!ep || !tk) {
        return -1;
    }
    frame->episode = (uint32_t)strtoul(ep + 10, NULL, 10);
    frame->tick = (uint32_t)strtoul(tk + 8, NULL, 10);

    const char *grid = strstr(line, "\"grid\":[");
    if (grid) {
        grid += 9;
        int row = 0;
        while (*grid && row < TAGWORLD_MAX_DIM) {
            if (*grid == '"') {
                grid++;
                int col = 0;
                while (*grid && *grid != '"' && col < TAGWORLD_MAX_DIM) {
                    frame->grid[row][col++] = *grid++;
                }
                frame->grid[row][col] = '\0';
                row++;
            }
            if (*grid == ']') {
                break;
            }
            grid++;
        }
    }
    return 0;
}

static void tagworld_policy_snap_free(TagWorldPolicySnap *snap) {
    if (!snap->active) {
        return;
    }
    free(snap->nodes);
    free(snap->edges);
    snap->nodes = NULL;
    snap->edges = NULL;
    snap->node_count = 0;
    snap->edge_count = 0;
    snap->active = 0;
}

static void tagworld_policy_snap_save(NervaEngine *e, TagWorldPolicySnap *snap) {
    tagworld_policy_snap_free(snap);
    snap->node_count = e->node_count;
    snap->edge_count = e->edge_count;
    snap->nodes = (NervaNode *)malloc(snap->node_count * sizeof(NervaNode));
    snap->edges = (NervaEdge *)malloc(snap->edge_count * sizeof(NervaEdge));
    if (!snap->nodes || !snap->edges) {
        tagworld_policy_snap_free(snap);
        return;
    }
    memcpy(snap->nodes, e->nodes, snap->node_count * sizeof(NervaNode));
    memcpy(snap->edges, e->edges, snap->edge_count * sizeof(NervaEdge));
    snap->active = 1;
}

static void tagworld_policy_snap_restore(NervaEngine *e, const TagWorldPolicySnap *snap) {
    if (!snap->active || snap->node_count != e->node_count || snap->edge_count != e->edge_count) {
        return;
    }
    memcpy(e->nodes, snap->nodes, snap->node_count * sizeof(NervaNode));
    memcpy(e->edges, snap->edges, snap->edge_count * sizeof(NervaEdge));
}

static void tagworld_finalize_run_metrics(NervaEngine *e, const TagWorldConfig *cfg,
                                        TagWorldMetrics *metrics) {
    tagworld_nerva_count_edges(e, &metrics->learned_edge_count, &metrics->provisional_edge_count);
    if (metrics->episodes > 0) {
        metrics->escape_rate = (double)metrics->escaped / (double)metrics->episodes;
        metrics->avg_ticks_per_episode = (double)metrics->total_ticks / (double)metrics->episodes;
        metrics->avg_events_per_episode = (double)metrics->total_events / (double)metrics->episodes;
        metrics->avg_mutations_per_episode =
            (double)metrics->total_mutations_applied / (double)metrics->episodes;
    }
    if (cfg->run_baseline && cfg->mode == TAGWORLD_MODE_ACTION) {
        metrics->baseline_escape_rate =
            tagworld_baseline_random_escape_rate(cfg, (uint32_t)metrics->episodes);
    }
}

static void tagworld_nerva_quiesce_engine(NervaEngine *e) {
    for (uint32_t i = 0; i < e->node_count; ++i) {
        e->nodes[i].v = e->nodes[i].v_rest;
        e->nodes[i].last_fired_tick = 0;
        e->nodes[i].memory_charge = 0.0f;
    }
    e->tick = 0;
    e->idle_ticks = 0;
    e->event_count = 0;
    e->active_count = 0;
    e->expectation_count = 0;
    e->mutation_count = 0;
    e->memory_count = 0;
    nerva_debug_clear_fire_log(e);
}

int tagworld_run_episode(NervaEngine *e, TagWorldNerva *tn, TagWorld *w,
                         const TagWorldConfig *cfg, TagWorldMetrics *m, FILE *replay_out) {
    tagworld_reset_for_config(w, cfg, (uint32_t)m->episodes);
    tn->last_action = TAG_ACTION_WAIT;
    tn->episode_first_action = TAG_ACTION_COUNT;
    tn->episode_used_push_doorway = false;
    tn->episode_push_doorway_count = 0u;
    uint64_t mut_applied_before = e->debug.mutations_applied;
    nerva_prediction_clear(e);
    if (cfg->mode == TAGWORLD_MODE_ACTION && cfg->map_id == TAGWORLD_MAP_TOOL_PRESSURE) {
        if (g_online_phase == TAGWORLD_ONLINE_EVAL) {
            tagworld_policy_snap_restore(e, &g_online_learned_snap);
        } else if (!cfg->online_tool_acquisition) {
            tagworld_policy_snap_restore(e, &g_tool_policy_snap);
        }
    }
    tagworld_nerva_quiesce_engine(e);
    nerva_set_prediction_mode(e, 0);

    if (cfg->mode == TAGWORLD_MODE_OBSERVER || cfg->mode == TAGWORLD_MODE_PREDICTION) {
        for (uint32_t t = 0; t < cfg->max_ticks && !w->done; ++t) {
            w->tick = t;
            tagworld_nerva_observe_tick(e, tn, w, cfg->mode, m, &mut_applied_before);

            TagWorldAction action = tagworld_scripted_action(w);
            tn->last_action = action;
            tagworld_record_action_metric(m, action);
            tagworld_apply_action(w, action);
            tagworld_step_seeker(w);
            tagworld_check_outcome(w);

            if (cfg->viz && !cfg->fast) {
                TagWorldFrame frame;
                tagworld_build_frame(w, tn, e, &frame);
                tagworld_viz_render_frame(stdout, &frame);
                m->viz_frames++;
            }
            if (replay_out) {
                TagWorldFrame frame;
                tagworld_build_frame(w, tn, e, &frame);
                tagworld_write_replay_frame(replay_out, &frame);
            }
        }
    } else {
        for (uint32_t t = 0; t < cfg->max_ticks && !w->done; ++t) {
            w->tick = t;
            e->event_count = 0;
            e->active_count = 0;
            e->expectation_count = 0;
            nerva_debug_clear_fire_log(e);
            tagworld_emit_state_events(e, tn, w, m);
            uint32_t valid_mask = tagworld_valid_action_mask(w);
            TagWorldAction action = tagworld_select_action_for_episode(
                e, tn, w, valid_mask, cfg, (uint32_t)m->episodes);
            tagworld_record_episode_action(tn, action);
            tagworld_record_action_metric(m, action);
            tagworld_apply_action(w, action);
            tagworld_step_seeker(w);
            tagworld_check_outcome(w);

            if (cfg->viz && !cfg->fast) {
                TagWorldFrame frame;
                tagworld_build_frame(w, tn, e, &frame);
                tagworld_viz_render_frame(stdout, &frame);
                m->viz_frames++;
            }
            if (replay_out) {
                TagWorldFrame frame;
                tagworld_build_frame(w, tn, e, &frame);
                tagworld_write_replay_frame(replay_out, &frame);
            }
        }
    }

    if (!w->done) {
        w->done = true;
        w->outcome = TAGWORLD_OUTCOME_TIMEOUT;
    }

    m->total_ticks += w->tick + 1u;
    if (w->outcome == TAGWORLD_OUTCOME_ESCAPED) {
        m->escaped++;
    } else if (w->outcome == TAGWORLD_OUTCOME_CAUGHT) {
        m->caught++;
    } else {
        m->timeouts++;
    }

    tagworld_nerva_episode_feedback(e, tn, w, tn->last_action, cfg->mode, cfg->online_tool_acquisition,
                                    m, &mut_applied_before);

    if (cfg->online_tool_acquisition && g_online_phase != TAGWORLD_ONLINE_EVAL) {
        static const uint32_t kWindow = 20u;
        uint32_t ep = (uint32_t)m->episodes;
        int escaped_ep = w->outcome == TAGWORLD_OUTCOME_ESCAPED;
        if (ep < kWindow) {
            m->push_doorway_first_window += tn->episode_push_doorway_count;
            if (tn->episode_used_push_doorway) {
                m->episodes_with_push_first_window++;
            }
            if (escaped_ep) {
                m->escaped_first_window++;
            }
        }
        if (cfg->episodes - ep <= kWindow) {
            m->push_doorway_last_window += tn->episode_push_doorway_count;
            if (tn->episode_used_push_doorway) {
                m->episodes_with_push_last_window++;
            }
            if (escaped_ep) {
                m->escaped_last_window++;
            }
        }
    }

    m->episodes++;

    return 0;
}

static void tagworld_pretrain_tool_action_context(NervaEngine *e, TagWorldNerva *tn, uint32_t rounds) {
    nerva_set_prediction_mode(e, 0);
    for (uint32_t i = 0; i < rounds; ++i) {
        nerva_activate_node(e, tn->ev.doorway_open, NERVA_Q8_8_ONE);
        nerva_activate_node(e, tn->ev.path_open, NERVA_Q8_8_ONE);
        nerva_tick_n(e, 4);
        nerva_inject_edge_event(e, tn->edge.doorway_open_to_push_doorway, NERVA_Q8_8_ONE);
        nerva_inject_edge_event(e, tn->edge.path_open_to_push_doorway, NERVA_Q8_8_ONE);
        nerva_tick(e);
        nerva_feedback_correct(e);
        nerva_apply_mutations(e);
    }
}

static void tagworld_pretrain_dynamics_only(NervaEngine *e, TagWorldNerva *tn) {
    tagworld_nerva_train_pair(e, tn, tn->ev.block_at_doorway, tn->edge.block_at_doorway_to_path_blocked,
                              tn->ev.path_blocked, 6);
    tagworld_nerva_train_pair(e, tn, tn->ev.doorway_open, tn->edge.doorway_open_to_path_open,
                              tn->ev.path_open, 4);
    tagworld_nerva_train_pair(e, tn, tn->ev.path_blocked, tn->edge.path_blocked_to_escaped,
                              tn->ev.runner_escaped, 4);
    tagworld_nerva_train_pair(e, tn, tn->ev.path_open, tn->edge.path_open_to_caught, tn->ev.runner_caught,
                              4);
    tagworld_nerva_train_pair(e, tn, tn->ev.path_blocked, tn->edge.path_blocked_to_run_safe,
                              tn->ev.action_run_to_safe, 12);
}

static void tagworld_pretrain(NervaEngine *e, TagWorldNerva *tn, TagWorldMode mode,
                              TagWorldMapId map_id) {
    if (mode == TAGWORLD_MODE_OBSERVER) {
        tagworld_nerva_train_pair(e, tn, tn->ev.block_at_doorway,
                                  tn->edge.block_at_doorway_to_path_blocked, tn->ev.path_blocked, 4);
        tagworld_nerva_train_pair(e, tn, tn->ev.doorway_open, tn->edge.doorway_open_to_path_open,
                                  tn->ev.path_open, 4);
    }
    if (mode == TAGWORLD_MODE_PREDICTION) {
        tagworld_nerva_train_pair(e, tn, tn->ev.block_at_doorway,
                                  tn->edge.block_at_doorway_to_path_blocked, tn->ev.path_blocked, 4);
        tagworld_nerva_train_pair(e, tn, tn->ev.doorway_open, tn->edge.doorway_open_to_path_open,
                                  tn->ev.path_open, 4);
    }
    if (mode == TAGWORLD_MODE_ACTION) {
        tagworld_nerva_train_pair(e, tn, tn->ev.block_at_doorway,
                                  tn->edge.block_at_doorway_to_path_blocked, tn->ev.path_blocked, 6);
        tagworld_nerva_train_pair(e, tn, tn->ev.doorway_open, tn->edge.doorway_open_to_path_open,
                                  tn->ev.path_open, 4);
        tagworld_nerva_train_pair(e, tn, tn->ev.seeker_near_runner, tn->edge.seeker_near_to_push_doorway,
                                  tn->ev.action_push_block_to_doorway, 6);
        tagworld_nerva_train_pair(e, tn, tn->ev.doorway_open, tn->edge.doorway_open_to_push_doorway,
                                  tn->ev.action_push_block_to_doorway, 6);
        tagworld_nerva_train_pair(e, tn, tn->ev.path_blocked, tn->edge.path_blocked_to_escaped,
                                  tn->ev.runner_escaped, 6);
        if (map_id == TAGWORLD_MAP_TOOL_PRESSURE) {
            tagworld_nerva_train_pair(e, tn, tn->ev.path_blocked, tn->edge.path_blocked_to_run_safe,
                                      tn->ev.action_run_to_safe, 6);
            tagworld_nerva_train_pair(e, tn, tn->ev.path_open, tn->edge.path_open_to_push_doorway,
                                      tn->ev.action_push_block_to_doorway, 24);
            tagworld_nerva_train_pair(e, tn, tn->ev.doorway_open, tn->edge.doorway_open_to_push_doorway,
                                      tn->ev.action_push_block_to_doorway, 16);
            tagworld_nerva_train_pair(e, tn, tn->ev.action_push_block_to_doorway,
                                      tn->edge.push_doorway_to_block_at_doorway, tn->ev.block_at_doorway, 16);
            tagworld_nerva_train_pair(e, tn, tn->ev.block_at_doorway,
                                      tn->edge.block_at_doorway_to_path_blocked, tn->ev.path_blocked, 12);
            tagworld_pretrain_tool_action_context(e, tn, 40);
            for (uint32_t i = 0; i < 12u; ++i) {
                nerva_set_prediction_mode(e, 0);
                nerva_activate_node(e, tn->ev.seeker_near_runner, NERVA_Q8_8_ONE);
                nerva_activate_node(e, tn->ev.doorway_open, NERVA_Q8_8_ONE);
                nerva_tick_n(e, 4);
                nerva_inject_edge_event(e, tn->edge.seeker_near_to_push_doorway, NERVA_Q8_8_ONE);
                nerva_inject_edge_event(e, tn->edge.doorway_open_to_push_doorway, NERVA_Q8_8_ONE);
                nerva_tick(e);
                nerva_feedback_correct(e);
                nerva_apply_mutations(e);
            }
        } else {
            tagworld_nerva_train_pair(e, tn, tn->ev.path_blocked, tn->edge.path_blocked_to_run_safe,
                                      tn->ev.action_run_to_safe, 12);
            tagworld_nerva_train_pair(e, tn, tn->ev.path_open, tn->edge.path_open_to_wait,
                                      tn->ev.action_wait, 10);
        }
    }
}

static void tagworld_zero_action_policy_edges(NervaEngine *e, TagWorldNerva *tn) {
    uint32_t ids[] = {
        tn->edge.path_open_to_wait,
        tn->edge.path_open_to_push_doorway,
        tn->edge.seeker_near_to_push_doorway,
        tn->edge.doorway_open_to_push_doorway,
        tn->edge.push_doorway_to_block_at_doorway,
    };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        if (ids[i] < e->edge_count) {
            e->edges[ids[i]].weight = 0;
            e->edges[ids[i]].stability = 0;
        }
    }
}

void tagworld_pretrain_for_config(NervaEngine *e, TagWorldNerva *tn, const TagWorldConfig *cfg) {
    if ((cfg->online_tool_acquisition || cfg->online_frozen_eval) &&
        cfg->map_id == TAGWORLD_MAP_TOOL_PRESSURE && cfg->mode == TAGWORLD_MODE_ACTION) {
        tagworld_pretrain_dynamics_only(e, tn);
        tagworld_zero_action_policy_edges(e, tn);
        return;
    }
    tagworld_pretrain(e, tn, cfg->mode, cfg->map_id);
}

void tagworld_ablate_learned_push_edges(NervaEngine *e, TagWorldNerva *tn) {
    uint32_t ids[] = {
        tn->edge.path_open_to_push_doorway,
        tn->edge.doorway_open_to_push_doorway,
        tn->edge.seeker_near_to_push_doorway,
        tn->edge.push_doorway_to_block_at_doorway,
    };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        if (ids[i] < e->edge_count) {
            e->edges[ids[i]].weight = 0;
            e->edges[ids[i]].stability = 0;
        }
    }
    tagworld_policy_snap_save(e, &g_online_learned_snap);
}

static int tagworld_run_episode_loop(NervaEngine *e, TagWorldNerva *tn, TagWorld *w,
                                     const TagWorldConfig *cfg, TagWorldMetrics *metrics,
                                     FILE *replay_out) {
    for (uint32_t ep = 0; ep < cfg->episodes; ++ep) {
        tagworld_run_episode(e, tn, w, cfg, metrics, replay_out);
        if (cfg->trace_every > 0 && ((ep + 1) % cfg->trace_every) == 0 && !cfg->fast) {
            nerva_trace_print_recent(e, stdout, 8);
        }
    }
    return 0;
}

static int tagworld_run_online_frozen(NervaEngine *e, const TagWorldConfig *cfg,
                                      TagWorldFrozenResult *out) {
    TagWorldNerva tn;
    TagWorld w;
    TagWorldMetrics learn;
    TagWorldMetrics eval;
    memset(&learn, 0, sizeof(learn));
    memset(&eval, 0, sizeof(eval));
    learn.seed = cfg->seed;
    learn.mode = cfg->mode;
    eval.seed = cfg->seed;
    eval.mode = cfg->mode;

    tagworld_nerva_init(e, &tn);
    if (cfg->map_id == TAGWORLD_MAP_TOOL_PRESSURE) {
        tagworld_init_map_tool_pressure(&w, cfg->grid);
    } else {
        tagworld_init_map(&w, cfg->grid);
    }

    if (cfg->snapshot_in) {
        if (nerva_persist_load(e, cfg->snapshot_in) != 0) {
            return -1;
        }
        tagworld_nerva_init(e, &tn);
    }

    if (!cfg->skip_pretrain) {
        tagworld_pretrain_for_config(e, &tn, cfg);
    } else if (cfg->mode == TAGWORLD_MODE_ACTION) {
        tagworld_zero_action_policy_edges(e, &tn);
    }

    tagworld_policy_snap_free(&g_tool_policy_snap);
    tagworld_policy_snap_free(&g_online_learned_snap);

    TagWorldConfig learn_cfg = *cfg;
    learn_cfg.online_tool_acquisition = true;
    uint32_t anneal_eps = cfg->online_anneal_episodes;
    uint32_t total_learn = cfg->online_learn_episodes > 0u ? cfg->online_learn_episodes : 200u;
    if (anneal_eps > total_learn) {
        anneal_eps = total_learn / 4u;
    }
    learn_cfg.episodes = total_learn - anneal_eps;

    FILE *replay_out = NULL;
    if (cfg->write_replay && cfg->replay_path) {
        replay_out = fopen(cfg->replay_path, "w");
    }

    g_tagworld_rng = cfg->seed;
    g_online_phase = TAGWORLD_ONLINE_LEARN;
    tagworld_run_episode_loop(e, &tn, &w, &learn_cfg, &learn, replay_out);

    if (anneal_eps > 0u) {
        learn_cfg.episodes = anneal_eps;
        learn_cfg.online_explore_pct = 0u;
        tagworld_run_episode_loop(e, &tn, &w, &learn_cfg, &learn, replay_out);
    }

    tagworld_policy_snap_save(e, &g_online_learned_snap);

    TagWorldConfig eval_cfg = *cfg;
    eval_cfg.online_tool_acquisition = false;
    eval_cfg.episodes = cfg->online_eval_episodes > 0u ? cfg->online_eval_episodes : 100u;

    g_online_phase = TAGWORLD_ONLINE_EVAL;
    tagworld_run_episode_loop(e, &tn, &w, &eval_cfg, &eval, replay_out);

    g_online_phase = TAGWORLD_ONLINE_NONE;

    if (replay_out) {
        fclose(replay_out);
    }

    if (cfg->snapshot_out) {
        if (nerva_persist_save(e, cfg->snapshot_out) != 0) {
            tagworld_policy_snap_free(&g_online_learned_snap);
            return -1;
        }
    }

    tagworld_finalize_run_metrics(e, &eval_cfg, &eval);
    tagworld_finalize_run_metrics(e, &learn_cfg, &learn);

    if (out) {
        out->learn = learn;
        out->eval = eval;
    }

    return 0;
}

int tagworld_run_frozen_result(NervaEngine *e, const TagWorldConfig *cfg, TagWorldFrozenResult *out) {
    if (!out) {
        return -1;
    }
    return tagworld_run_online_frozen(e, cfg, out);
}

int tagworld_run_frozen_eval_only(NervaEngine *e, TagWorldNerva *tn, const TagWorldConfig *cfg,
                                  TagWorldMetrics *out) {
    if (!g_online_learned_snap.active) {
        return -1;
    }
    TagWorld w;
    TagWorldMetrics eval;
    memset(&eval, 0, sizeof(eval));
    eval.seed = cfg->seed;
    eval.mode = cfg->mode;

    if (cfg->map_id == TAGWORLD_MAP_TOOL_PRESSURE) {
        tagworld_init_map_tool_pressure(&w, cfg->grid);
    } else {
        tagworld_init_map(&w, cfg->grid);
    }

    TagWorldConfig eval_cfg = *cfg;
    eval_cfg.online_tool_acquisition = false;
    eval_cfg.episodes = cfg->online_eval_episodes > 0u ? cfg->online_eval_episodes : 100u;

    g_tagworld_rng = cfg->seed;
    g_online_phase = TAGWORLD_ONLINE_EVAL;
    tagworld_run_episode_loop(e, tn, &w, &eval_cfg, &eval, NULL);
    g_online_phase = TAGWORLD_ONLINE_NONE;

    tagworld_finalize_run_metrics(e, &eval_cfg, &eval);
    if (out) {
        *out = eval;
    }
    return 0;
}

int tagworld_run(NervaEngine *e, const TagWorldConfig *cfg, TagWorldMetrics *out) {
    if (cfg->online_frozen_eval) {
        TagWorldFrozenResult frozen;
        if (tagworld_run_online_frozen(e, cfg, &frozen) != 0) {
            return -1;
        }
        if (out) {
            *out = frozen.eval;
        }
        return 0;
    }

    TagWorldNerva tn;
    TagWorld w;
    TagWorldMetrics metrics;
    memset(&metrics, 0, sizeof(metrics));
    metrics.seed = cfg->seed;
    metrics.mode = cfg->mode;

    tagworld_nerva_init(e, &tn);
    if (cfg->map_id == TAGWORLD_MAP_TOOL_PRESSURE) {
        tagworld_init_map_tool_pressure(&w, cfg->grid);
    } else {
        tagworld_init_map(&w, cfg->grid);
    }

    if (cfg->snapshot_in) {
        if (nerva_persist_load(e, cfg->snapshot_in) != 0) {
            return -1;
        }
        tagworld_nerva_init(e, &tn);
    }

    if (!cfg->skip_pretrain) {
        tagworld_pretrain_for_config(e, &tn, cfg);
    } else if (cfg->mode == TAGWORLD_MODE_ACTION) {
        tagworld_zero_action_policy_edges(e, &tn);
    }

    if (cfg->map_id == TAGWORLD_MAP_TOOL_PRESSURE && cfg->mode == TAGWORLD_MODE_ACTION &&
        !cfg->skip_pretrain && !cfg->online_tool_acquisition && !cfg->online_frozen_eval) {
        tagworld_policy_snap_save(e, &g_tool_policy_snap);
    } else {
        tagworld_policy_snap_free(&g_tool_policy_snap);
    }

    FILE *replay_out = NULL;
    if (cfg->write_replay && cfg->replay_path) {
        replay_out = fopen(cfg->replay_path, "w");
    }

    g_tagworld_rng = cfg->seed;
    g_online_phase = TAGWORLD_ONLINE_NONE;
    tagworld_run_episode_loop(e, &tn, &w, cfg, &metrics, replay_out);

    if (replay_out) {
        fclose(replay_out);
    }

    if (cfg->snapshot_out) {
        if (nerva_persist_save(e, cfg->snapshot_out) != 0) {
            tagworld_policy_snap_free(&g_tool_policy_snap);
            return -1;
        }
    }

    tagworld_policy_snap_free(&g_tool_policy_snap);

    tagworld_finalize_run_metrics(e, cfg, &metrics);

    if (out) {
        *out = metrics;
    }
    return 0;
}

void tagworld_print_summary(const TagWorldMetrics *m, FILE *out) {
    fprintf(out, "episodes=%llu\n", (unsigned long long)m->episodes);
    fprintf(out, "seed=%u\n", m->seed);
    fprintf(out, "mode=%s\n", tagworld_mode_name(m->mode));
    fprintf(out, "escaped=%llu\n", (unsigned long long)m->escaped);
    fprintf(out, "caught=%llu\n", (unsigned long long)m->caught);
    fprintf(out, "timeouts=%llu\n", (unsigned long long)m->timeouts);
    fprintf(out, "escape_rate=%.4f\n", m->escape_rate);
    fprintf(out, "baseline_escape_rate=%.4f\n", m->baseline_escape_rate);
    fprintf(out, "avg_ticks_per_episode=%.2f\n", m->avg_ticks_per_episode);
    fprintf(out, "avg_events_per_episode=%.2f\n", m->avg_events_per_episode);
    fprintf(out, "avg_mutations_per_episode=%.2f\n", m->avg_mutations_per_episode);
    fprintf(out, "max_event_queue_depth=%u\n", m->max_event_queue_depth);
    fprintf(out, "max_mutation_queue_depth=%llu\n", (unsigned long long)m->max_mutation_queue_depth);
    fprintf(out, "total_mutations_applied=%llu\n", (unsigned long long)m->total_mutations_applied);
    fprintf(out, "prediction_confirm_count=%llu\n", (unsigned long long)m->prediction_confirm_count);
    fprintf(out, "prediction_miss_count=%llu\n", (unsigned long long)m->prediction_miss_count);
    fprintf(out, "surprise_count=%llu\n", (unsigned long long)m->surprise_count);
    fprintf(out, "action_push_block_count=%llu\n", (unsigned long long)m->action_push_block_count);
    fprintf(out, "action_run_count=%llu\n", (unsigned long long)m->action_run_count);
    fprintf(out, "action_wait_count=%llu\n", (unsigned long long)m->action_wait_count);
    fprintf(out, "action_push_doorway_count=%llu\n", (unsigned long long)m->action_push_doorway_count);
    fprintf(out, "push_doorway_first_window=%llu\n", (unsigned long long)m->push_doorway_first_window);
    fprintf(out, "push_doorway_last_window=%llu\n", (unsigned long long)m->push_doorway_last_window);
    fprintf(out, "episodes_with_push_first_window=%llu\n",
            (unsigned long long)m->episodes_with_push_first_window);
    fprintf(out, "episodes_with_push_last_window=%llu\n",
            (unsigned long long)m->episodes_with_push_last_window);
    fprintf(out, "escaped_first_window=%llu\n", (unsigned long long)m->escaped_first_window);
    fprintf(out, "escaped_last_window=%llu\n", (unsigned long long)m->escaped_last_window);
    fprintf(out, "learned_edge_count=%u\n", m->learned_edge_count);
    fprintf(out, "provisional_edge_count=%u\n", m->provisional_edge_count);
    fprintf(out, "viz_frames=%llu\n", (unsigned long long)m->viz_frames);
}

void tagworld_print_frozen_summary(const TagWorldFrozenResult *r, FILE *out) {
    if (!r || !out) {
        return;
    }
    const TagWorldMetrics *learn = &r->learn;
    const TagWorldMetrics *eval = &r->eval;
    fprintf(out, "online_frozen_result=1\n");
    fprintf(out, "learn_escape_rate=%.4f\n", learn->escape_rate);
    fprintf(out, "learn_episodes_with_push_first_window=%llu\n",
            (unsigned long long)learn->episodes_with_push_first_window);
    fprintf(out, "learn_episodes_with_push_last_window=%llu\n",
            (unsigned long long)learn->episodes_with_push_last_window);
    fprintf(out, "learn_escaped_first_window=%llu\n",
            (unsigned long long)learn->escaped_first_window);
    fprintf(out, "learn_escaped_last_window=%llu\n",
            (unsigned long long)learn->escaped_last_window);
    fprintf(out, "eval_escape_rate=%.4f\n", eval->escape_rate);
    fprintf(out, "eval_baseline_escape_rate=%.4f\n", eval->baseline_escape_rate);
    fprintf(out, "eval_avg_mutations_per_episode=%.2f\n", eval->avg_mutations_per_episode);
    fprintf(out, "eval_action_push_doorway_count=%llu\n",
            (unsigned long long)eval->action_push_doorway_count);
    tagworld_print_summary(eval, out);
}

int tagworld_replay_file(const char *path, bool viz) {
    FILE *in = fopen(path, "r");
    if (!in) {
        return -1;
    }
    char line[4096];
    while (fgets(line, sizeof(line), in)) {
        TagWorldFrame frame;
        if (tagworld_parse_replay_line(line, &frame) != 0) {
            continue;
        }
        if (viz) {
            tagworld_viz_render_frame(stdout, &frame);
        }
    }
    fclose(in);
    return 0;
}
