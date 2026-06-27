// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Paul Odantabao II

#ifndef NERVA_TAGWORLD_H
#define NERVA_TAGWORLD_H

#include "nerva_types.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define TAGWORLD_MAX_DIM 16

#define TAG_REL_PREDICTS (NERVA_REL_CUSTOM_BASE + 0u)
#define TAG_REL_CAUSES (NERVA_REL_CUSTOM_BASE + 1u)
#define TAG_REL_ENABLES (NERVA_REL_CUSTOM_BASE + 2u)
#define TAG_REL_ACTION_LEADS_TO (NERVA_REL_CUSTOM_BASE + 3u)
#define TAG_REL_OUTCOME (NERVA_REL_CUSTOM_BASE + 4u)
#define TAG_REL_CONTRADICTS (NERVA_REL_CUSTOM_BASE + 5u)

typedef enum TagWorldCell {
    TAG_CELL_EMPTY = 0,
    TAG_CELL_WALL,
    TAG_CELL_DOORWAY,
    TAG_CELL_SAFE
} TagWorldCell;

typedef enum TagWorldOutcome {
    TAGWORLD_OUTCOME_NONE = 0,
    TAGWORLD_OUTCOME_CAUGHT,
    TAGWORLD_OUTCOME_ESCAPED,
    TAGWORLD_OUTCOME_TIMEOUT
} TagWorldOutcome;

typedef enum TagWorldMode {
    TAGWORLD_MODE_OBSERVER = 0,
    TAGWORLD_MODE_PREDICTION,
    TAGWORLD_MODE_ACTION
} TagWorldMode;

typedef enum TagWorldAction {
    TAG_ACTION_WAIT = 0,
    TAG_ACTION_RUN_TO_SAFE,
    TAG_ACTION_PUSH_BLOCK,
    TAG_ACTION_PUSH_BLOCK_TO_DOORWAY,
    TAG_ACTION_COUNT
} TagWorldAction;

typedef struct TagWorldPos {
    int x;
    int y;
} TagWorldPos;

typedef struct TagWorld {
    uint32_t seed;
    uint32_t episode;
    uint32_t tick;
    int width;
    int height;
    TagWorldCell cells[TAGWORLD_MAX_DIM][TAGWORLD_MAX_DIM];
    TagWorldPos runner;
    TagWorldPos seeker;
    TagWorldPos block;
    TagWorldPos safe;
    TagWorldPos doorway;
    bool done;
    TagWorldOutcome outcome;
    uint32_t episode_variant;
} TagWorld;

typedef struct TagWorldEventIds {
    uint32_t runner_near_seeker;
    uint32_t seeker_near_runner;
    uint32_t doorway_open;
    uint32_t block_at_doorway;
    uint32_t path_blocked;
    uint32_t path_open;
    uint32_t runner_at_safe;
    uint32_t runner_caught;
    uint32_t runner_escaped;
    uint32_t expect_seeker_through_doorway;
    uint32_t expect_path_blocked;
    uint32_t expect_caught;
    uint32_t expect_escaped;
    uint32_t surprise_path_open;
    uint32_t surprise_path_blocked;
    uint32_t surprise_caught;
    uint32_t surprise_escaped;
    uint32_t action_wait;
    uint32_t action_run_to_safe;
    uint32_t action_push_block;
    uint32_t action_push_block_to_doorway;
} TagWorldEventIds;

typedef struct TagWorldEdgeIds {
    uint32_t block_at_doorway_to_path_blocked;
    uint32_t doorway_open_to_path_open;
    uint32_t path_open_to_caught;
    uint32_t path_blocked_to_escaped;
    uint32_t seeker_near_to_push_doorway;
    uint32_t doorway_open_to_push_doorway;
    uint32_t push_doorway_to_block_at_doorway;
    uint32_t path_blocked_to_run_safe;
    uint32_t path_open_to_wait;
} TagWorldEdgeIds;

typedef struct TagWorldNerva {
    TagWorldEventIds ev;
    TagWorldEdgeIds edge;
    TagWorldAction last_action;
    uint32_t last_active_count;
    uint32_t last_expected_count;
    uint32_t last_surprise_count;
} TagWorldNerva;

typedef struct TagWorldConfig {
    uint32_t seed;
    uint32_t episodes;
    uint32_t max_ticks;
    uint32_t trace_every;
    int grid;
    TagWorldMode mode;
    bool fast;
    bool viz;
    bool replay;
    bool write_replay;
    const char *replay_path;
    const char *snapshot_in;
    const char *snapshot_out;
    bool run_baseline;
    bool skip_pretrain;
} TagWorldConfig;

typedef struct TagWorldMetrics {
    uint64_t episodes;
    uint32_t seed;
    TagWorldMode mode;
    uint64_t escaped;
    uint64_t caught;
    uint64_t timeouts;
    double escape_rate;
    double baseline_escape_rate;
    double avg_ticks_per_episode;
    double avg_events_per_episode;
    double avg_mutations_per_episode;
    uint32_t max_event_queue_depth;
    uint64_t max_mutation_queue_depth;
    uint64_t prediction_confirm_count;
    uint64_t prediction_miss_count;
    uint64_t surprise_count;
    uint64_t action_push_block_count;
    uint64_t action_run_count;
    uint64_t action_wait_count;
    uint64_t action_push_doorway_count;
    uint32_t learned_edge_count;
    uint32_t provisional_edge_count;
    uint64_t viz_frames;
    uint64_t total_ticks;
    uint64_t total_events;
    uint64_t total_mutations_applied;
} TagWorldMetrics;

typedef struct TagWorldFrame {
    uint32_t episode;
    uint32_t tick;
    char grid[TAGWORLD_MAX_DIM][TAGWORLD_MAX_DIM + 1];
    TagWorldPos runner;
    TagWorldPos seeker;
    TagWorldPos block;
    const char *active_events[16];
    uint32_t active_count;
    const char *expected_events[8];
    uint32_t expected_count;
    const char *surprise_events[8];
    uint32_t surprise_count;
    TagWorldAction action;
    TagWorldOutcome outcome;
    uint32_t queued_mutations;
    uint32_t applied_mutations;
    int32_t edge_delta;
    const char *edge_delta_label;
} TagWorldFrame;

void tagworld_config_defaults(TagWorldConfig *cfg);
void tagworld_init_map(TagWorld *w, int grid);
void tagworld_reset(TagWorld *w, uint32_t seed, uint32_t episode);
int tagworld_seeker_can_reach_runner(const TagWorld *w);
int tagworld_is_block_at_doorway(const TagWorld *w);
int tagworld_is_doorway_open(const TagWorld *w);
int tagworld_manhattan(TagWorldPos a, TagWorldPos b);
void tagworld_step_seeker(TagWorld *w);
uint32_t tagworld_valid_action_mask(const TagWorld *w);
int tagworld_apply_action(TagWorld *w, TagWorldAction action);
void tagworld_check_outcome(TagWorld *w);
int tagworld_simulate_until_outcome(TagWorld *w, TagWorldAction runner_action, uint32_t max_ticks);
TagWorldAction tagworld_scripted_action(const TagWorld *w);
TagWorldAction tagworld_random_action(const TagWorld *w, uint32_t *rng);
TagWorldAction tagworld_always_run_action(const TagWorld *w);

double tagworld_baseline_random_escape_rate(const TagWorldConfig *cfg, uint32_t episodes);
double tagworld_baseline_always_run_escape_rate(const TagWorldConfig *cfg, uint32_t episodes);

int tagworld_nerva_init(NervaEngine *e, TagWorldNerva *tn);
uint32_t tagworld_nerva_edge_weight(const NervaEngine *e, uint32_t edge_id);
void tagworld_nerva_train_pair(NervaEngine *e, TagWorldNerva *tn, uint32_t source_ev,
                               uint32_t edge_id, uint32_t target_ev, uint32_t rounds);
void tagworld_nerva_emit_actual(NervaEngine *e, TagWorldNerva *tn, uint32_t node_id);
void tagworld_nerva_inject_actual_edge(NervaEngine *e, uint32_t edge_id);
void tagworld_nerva_tick_quiet(NervaEngine *e, uint32_t budget);
TagWorldAction tagworld_nerva_select_action(NervaEngine *e, TagWorldNerva *tn,
                                            const TagWorld *w, uint32_t valid_mask);
void tagworld_nerva_observe_tick(NervaEngine *e, TagWorldNerva *tn, TagWorld *w,
                                 TagWorldMode mode, TagWorldMetrics *m,
                                 uint64_t *mut_applied_before);
void tagworld_nerva_episode_feedback(NervaEngine *e, TagWorldNerva *tn, TagWorld *w,
                                     TagWorldAction action, TagWorldMode mode,
                                     TagWorldMetrics *m, uint64_t *mut_applied_before);
uint32_t tagworld_nerva_pending_expectation_target(const NervaEngine *e);
int tagworld_nerva_prediction_confirm_pair(NervaEngine *e, TagWorldNerva *tn, uint32_t *confirm_out,
                                           uint32_t *miss_out, uint64_t *mut_applied_out);
int tagworld_nerva_prediction_mismatch_pair(NervaEngine *e, TagWorldNerva *tn, uint32_t *confirm_out,
                                            uint32_t *miss_out);

int tagworld_run(NervaEngine *e, const TagWorldConfig *cfg, TagWorldMetrics *out);
int tagworld_run_episode(NervaEngine *e, TagWorldNerva *tn, TagWorld *w,
                         const TagWorldConfig *cfg, TagWorldMetrics *m, FILE *replay_out);
int tagworld_replay_file(const char *path, bool viz);

void tagworld_build_frame(const TagWorld *w, const TagWorldNerva *tn, const NervaEngine *e,
                          TagWorldFrame *frame);
void tagworld_print_summary(const TagWorldMetrics *m, FILE *out);
int tagworld_parse_replay_line(const char *line, TagWorldFrame *frame);

const char *tagworld_action_name(TagWorldAction action);
const char *tagworld_outcome_name(TagWorldOutcome outcome);
const char *tagworld_mode_name(TagWorldMode mode);

#endif
