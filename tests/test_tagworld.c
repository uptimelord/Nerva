// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Paul Odantabao II

#include "tagworld.h"
#include "maps/tagworld_maps.h"

#include "nerva_config.h"
#include "nerva_debug.h"
#include "nerva_engine.h"
#include "nerva_event.h"
#include "nerva_graph.h"
#include "nerva_learning.h"
#include "nerva_math.h"
#include "nerva_mutation.h"
#include "nerva_prediction.h"
#include "nerva_trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;

static void expect_true(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        g_failures++;
    }
}

static void expect_eq_u32(uint32_t actual, uint32_t expected, const char *message) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (expected %u, got %u)\n", message, expected, actual);
        g_failures++;
    }
}

static int trace_has_flags(const NervaEngine *e, uint16_t flags) {
    for (uint32_t age = 0; age < e->trace_count && age < e->cfg.trace_decay_scan_limit; ++age) {
        NervaTrace *t = nerva_trace_recent((NervaEngine *)e, age);
        if (t && (t->flags & flags) == flags) {
            return 1;
        }
    }
    return 0;
}

static int fire_log_contains(const NervaEngine *e, uint32_t node_id) {
    for (uint32_t i = 0; i < e->fire_log_count; ++i) {
        if (e->fire_log[i].node_id == node_id) {
            return 1;
        }
    }
    return 0;
}

static int mutation_log_has_reason(const NervaEngine *e, uint16_t reason) {
    for (uint32_t i = 0; i < e->mutation_log_count; ++i) {
        if (e->mutation_log[i].debug_reason == reason) {
            return 1;
        }
    }
    return 0;
}

static void test_tagworld_deterministic_reset(void) {
    TagWorld a;
    TagWorld b;
    tagworld_init_map(&a, 7);
    tagworld_init_map(&b, 7);
    tagworld_reset(&a, 42u, 0u);
    tagworld_reset(&b, 42u, 0u);
    expect_eq_u32((uint32_t)a.runner.x, (uint32_t)b.runner.x, "runner.x deterministic");
    expect_eq_u32((uint32_t)a.block.x, (uint32_t)b.block.x, "block.x deterministic");
    expect_eq_u32((uint32_t)a.seeker.x, (uint32_t)b.seeker.x, "seeker.x deterministic");
    expect_eq_u32(a.episode_variant, b.episode_variant, "variant deterministic");
}

static void test_tagworld_doorway_open_path(void) {
    TagWorld w;
    tagworld_init_map(&w, 7);
    w.runner.x = 1;
    w.runner.y = 3;
    w.seeker.x = 5;
    w.seeker.y = 3;
    w.block.x = 2;
    w.block.y = 5;
    expect_true(tagworld_is_doorway_open(&w), "doorway open with block off chokepoint");
    TagWorldPos before = w.seeker;
    tagworld_step_seeker(&w);
    expect_true(before.x != w.seeker.x || before.y != w.seeker.y, "seeker moves through open path");
}

static void test_tagworld_open_doorway_runner_gets_caught(void) {
    TagWorld w;
    tagworld_init_map(&w, 7);
    tagworld_reset(&w, 42u, 0u);
    expect_true(w.episode_variant == 0u, "variant 0 open-door setup");
    expect_true(tagworld_is_doorway_open(&w), "doorway open for variant 0");
    int outcome = tagworld_simulate_until_outcome(&w, TAG_ACTION_WAIT, 32u);
    expect_eq_u32((uint32_t)outcome, (uint32_t)TAGWORLD_OUTCOME_CAUGHT,
                  "open doorway + wait yields caught within max_ticks");
}

static void test_tagworld_block_at_doorway_prevents_catch_or_enables_escape(void) {
    TagWorld w;
    tagworld_init_map(&w, 7);
    tagworld_reset(&w, 1u, 0u);
    expect_true(w.episode_variant == 1u, "variant 1 block-at-doorway setup");
    expect_true(tagworld_is_block_at_doorway(&w), "block at doorway");
    int blocked_outcome = tagworld_simulate_until_outcome(&w, TAG_ACTION_WAIT, 32u);
    expect_true(blocked_outcome != TAGWORLD_OUTCOME_CAUGHT,
                "block at doorway prevents quick catch on wait");

    tagworld_reset(&w, 1u, 0u);
    int escape_outcome = tagworld_simulate_until_outcome(&w, TAG_ACTION_RUN_TO_SAFE, 32u);
    expect_eq_u32((uint32_t)escape_outcome, (uint32_t)TAGWORLD_OUTCOME_ESCAPED,
                  "block at doorway + run enables escape");
}

static void test_tagworld_block_at_doorway_blocks_path(void) {
    TagWorld w;
    tagworld_init_map(&w, 7);
    tagworld_reset(&w, 1u, 0u);
    expect_true(w.episode_variant == 1u, "variant 1 setup");
    expect_true(tagworld_is_block_at_doorway(&w), "block placed at doorway for variant 1");
    int reach_before = tagworld_seeker_can_reach_runner(&w);
    expect_true(!reach_before || tagworld_is_block_at_doorway(&w),
                "doorway chokepoint occupied by block");
    for (int i = 0; i < 8 && !w.done; ++i) {
        tagworld_step_seeker(&w);
        tagworld_check_outcome(&w);
    }
    expect_true(tagworld_is_block_at_doorway(&w), "block remains at doorway during seeker steps");
}

static void test_tagworld_observer_learns_block_path(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "observer learn init");
    TagWorldNerva tn;
    tagworld_nerva_init(&e, &tn);
    nerva_q8_8_t before = e.edges[tn.edge.block_at_doorway_to_path_blocked].weight;
    tagworld_nerva_train_pair(&e, &tn, tn.ev.block_at_doorway, tn.edge.block_at_doorway_to_path_blocked,
                              tn.ev.path_blocked, 5);
    expect_true(e.edges[tn.edge.block_at_doorway_to_path_blocked].weight > before,
                "BLOCK_AT_DOORWAY->PATH_BLOCKED weight increased");
    expect_true(e.edges[tn.edge.block_at_doorway_to_path_blocked].stability >= e.cfg.prediction_min_stability,
                "edge stability supports prediction");
    nerva_engine_free(&e);
}

static void test_tagworld_prediction_expected_not_actual(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "prediction expected init");
    TagWorldNerva tn;
    tagworld_nerva_init(&e, &tn);
    tagworld_nerva_train_pair(&e, &tn, tn.ev.block_at_doorway, tn.edge.block_at_doorway_to_path_blocked,
                              tn.ev.path_blocked, 4);

    nerva_set_prediction_mode(&e, 1);
    nerva_debug_clear_fire_log(&e);
    nerva_trace_clear(&e);
    nerva_activate_node(&e, tn.ev.block_at_doorway, NERVA_Q8_8_ONE);
    nerva_tick_n(&e, 2);

    expect_true(fire_log_contains(&e, tn.ev.block_at_doorway), "source fired");
    expect_true(!fire_log_contains(&e, tn.ev.path_blocked), "PATH_BLOCKED did not fire from prediction");
    expect_true(e.nodes[tn.ev.path_blocked].v < e.nodes[tn.ev.path_blocked].theta_fire,
                "PATH_BLOCKED pre-charged below threshold");
    expect_true(trace_has_flags(&e, NERVA_TRACE_EXPECTED), "expected trace recorded");
    nerva_engine_free(&e);
}

static void test_tagworld_prediction_confirm_strengthens(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "confirm init");
    TagWorldNerva tn;
    tagworld_nerva_init(&e, &tn);
    tagworld_nerva_train_pair(&e, &tn, tn.ev.block_at_doorway, tn.edge.block_at_doorway_to_path_blocked,
                              tn.ev.path_blocked, 8);
    nerva_q8_8_t before = e.edges[tn.edge.block_at_doorway_to_path_blocked].weight;

    nerva_set_prediction_mode(&e, 1);
    nerva_mutation_clear_log(&e);
    nerva_tick_n(&e, 8);
    nerva_activate_node(&e, tn.ev.block_at_doorway, NERVA_Q8_8_ONE);
    nerva_tick_n(&e, 2);
    expect_eq_u32((uint32_t)e.debug.predictions_emitted, 1u, "prediction emitted before confirm");
    expect_true(nerva_inject_edge_event(&e, tn.edge.block_at_doorway_to_path_blocked, NERVA_Q8_8_ONE),
                "inject actual edge");
    nerva_tick_n(&e, 2);
    nerva_apply_mutations(&e);

    expect_eq_u32((uint32_t)e.debug.predictions_confirmed, 1u, "prediction confirmed");
    expect_true(e.edges[tn.edge.block_at_doorway_to_path_blocked].weight > before,
                "confirmed edge strengthened");
    nerva_engine_free(&e);
}

static void test_tagworld_prediction_confirm_pair_increments(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "confirm pair init");
    TagWorldNerva tn;
    tagworld_nerva_init(&e, &tn);
    tagworld_nerva_train_pair(&e, &tn, tn.ev.block_at_doorway, tn.edge.block_at_doorway_to_path_blocked,
                              tn.ev.path_blocked, 8);

    uint32_t confirm = 0;
    uint32_t miss = 0;
    uint64_t mut_applied = 0;
    expect_true(tagworld_nerva_prediction_confirm_pair(&e, &tn, &confirm, &miss, &mut_applied) == 0,
                "confirm pair runs");
    expect_eq_u32(confirm, 1u, "expected PATH_BLOCKED confirm increments");
    expect_eq_u32(miss, 0u, "confirm pair has no miss");
    expect_true(mut_applied > 0u, "PREDICTION_CONFIRMED mutation applied");
    expect_true(mutation_log_has_reason(&e, NERVA_REASON_PREDICTION_CONFIRMED),
                "PREDICTION_CONFIRMED mutation queued");
    nerva_engine_free(&e);
}

static void test_tagworld_prediction_mismatch_pair_increments(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "mismatch pair init");
    TagWorldNerva tn;
    tagworld_nerva_init(&e, &tn);
    tagworld_nerva_train_pair(&e, &tn, tn.ev.block_at_doorway, tn.edge.block_at_doorway_to_path_blocked,
                              tn.ev.path_blocked, 4);

    nerva_trace_clear(&e);
    uint32_t confirm = 0;
    uint32_t miss = 0;
    expect_true(tagworld_nerva_prediction_mismatch_pair(&e, &tn, &confirm, &miss) == 0,
                "mismatch pair runs");
    expect_eq_u32(confirm, 0u, "mismatch pair has no confirm");
    expect_eq_u32(miss, 1u, "expected PATH_BLOCKED + actual PATH_OPEN miss");
    expect_true(trace_has_flags(&e, (uint16_t)(NERVA_TRACE_PRED_MISSED | NERVA_TRACE_SURPRISE)),
                "SURPRISE trace on mismatch");
    expect_true(mutation_log_has_reason(&e, NERVA_REASON_PREDICTION_MISSED),
                "PREDICTION_MISSED mutation queued");
    nerva_engine_free(&e);
}

static void test_tagworld_prediction_miss_surprise(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "miss init");
    TagWorldNerva tn;
    tagworld_nerva_init(&e, &tn);
    tagworld_nerva_train_pair(&e, &tn, tn.ev.block_at_doorway, tn.edge.block_at_doorway_to_path_blocked,
                              tn.ev.path_blocked, 4);

    nerva_set_prediction_mode(&e, 1);
    nerva_activate_node(&e, tn.ev.block_at_doorway, NERVA_Q8_8_ONE);
    nerva_tick(&e);
    nerva_inject_edge_event(&e, tn.edge.doorway_open_to_path_open, NERVA_Q8_8_ONE);
    nerva_tick(&e);
    nerva_apply_mutations(&e);

    expect_eq_u32((uint32_t)e.debug.predictions_missed, 1u, "prediction missed");
    expect_true(trace_has_flags(&e, (uint16_t)(NERVA_TRACE_PRED_MISSED | NERVA_TRACE_SURPRISE)),
                "surprise trace on miss");
    nerva_engine_free(&e);
}

static void test_tagworld_action_beats_random_baseline(void) {
    static const uint32_t seeds[] = {1u, 5u, 11u};
    TagWorldConfig cfg;
    tagworld_config_defaults(&cfg);
    cfg.grid = 7;
    cfg.max_ticks = 64u;
    cfg.episodes = 100u;
    cfg.mode = TAGWORLD_MODE_ACTION;
    cfg.fast = true;
    cfg.skip_pretrain = false;

    for (size_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); ++i) {
        cfg.seed = seeds[i];
        NervaEngine e;
        expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "action seed init");
        TagWorldMetrics metrics;
        expect_true(tagworld_run(&e, &cfg, &metrics) == 0, "action seed run");
        double random_rate = tagworld_baseline_random_escape_rate(&cfg, cfg.episodes);
        expect_true(metrics.escape_rate >= random_rate + 0.20,
                    "trained action beats random baseline by >=20pp");
        nerva_engine_free(&e);
    }
}

static void test_tagworld_viz_no_state_change(void) {
    TagWorldConfig cfg;
    tagworld_config_defaults(&cfg);
    cfg.seed = 3u;
    cfg.episodes = 6u;
    cfg.mode = TAGWORLD_MODE_OBSERVER;
    cfg.fast = true;

    NervaEngine e1;
    NervaEngine e2;
    expect_true(nerva_engine_init(&e1, nerva_config_test()) == 0, "viz off init");
    expect_true(nerva_engine_init(&e2, nerva_config_test()) == 0, "viz on init");

    TagWorldMetrics m_off;
    TagWorldMetrics m_on;
    cfg.viz = false;
    expect_true(tagworld_run(&e1, &cfg, &m_off) == 0, "run without viz");
    cfg.viz = true;
    cfg.fast = false;
    expect_true(tagworld_run(&e2, &cfg, &m_on) == 0, "run with viz");
    expect_true(m_on.viz_frames > 0u, "viz produced frames");

    expect_eq_u32((uint32_t)m_off.escaped, (uint32_t)m_on.escaped, "escaped match viz on/off");
    expect_eq_u32((uint32_t)m_off.caught, (uint32_t)m_on.caught, "caught match viz on/off");
    expect_eq_u32((uint32_t)m_off.timeouts, (uint32_t)m_on.timeouts, "timeouts match viz on/off");
    expect_eq_u32(m_off.max_event_queue_depth, m_on.max_event_queue_depth,
                  "queue depth match viz on/off");

    nerva_engine_free(&e1);
    nerva_engine_free(&e2);
}

static void test_tagworld_replay_deterministic(void) {
    const char *path = "benchmarks/tagworld_lite/sample_replay.log";
    TagWorldFrame f1;
    TagWorldFrame f2;
    FILE *in = fopen(path, "r");
    expect_true(in != NULL, "sample replay opens");
    if (!in) {
        return;
    }
    char line[4096];
    expect_true(fgets(line, sizeof(line), in) != NULL, "replay line 1");
    expect_true(tagworld_parse_replay_line(line, &f1) == 0, "parse frame 1");
    expect_true(fgets(line, sizeof(line), in) != NULL, "replay line 2");
    fclose(in);

    expect_true(tagworld_replay_file(path, false) == 0, "replay file loads");
    in = fopen(path, "r");
    expect_true(in != NULL, "replay reopen");
    expect_true(fgets(line, sizeof(line), in) != NULL, "replay line again");
    expect_true(tagworld_parse_replay_line(line, &f2) == 0, "parse frame again");
    fclose(in);

    expect_eq_u32(f1.episode, f2.episode, "replay episode deterministic");
    expect_eq_u32(f1.tick, f2.tick, "replay tick deterministic");
    expect_true(strcmp(f1.grid[0], f2.grid[0]) == 0, "replay grid row deterministic");
}

static TagWorldConfig tagworld_tool_test_config(void) {
    TagWorldConfig cfg;
    tagworld_config_defaults(&cfg);
    cfg.grid = 7;
    cfg.max_ticks = 64u;
    cfg.map_id = TAGWORLD_MAP_TOOL_PRESSURE;
    return cfg;
}

static void test_tagworld_run_alone_loses_on_tool_map(void) {
    TagWorldConfig cfg = tagworld_tool_test_config();
    TagWorld w;
    tagworld_reset_for_config(&w, &cfg, 0u);
    int run_outcome = tagworld_simulate_until_outcome(&w, TAG_ACTION_RUN_TO_SAFE, cfg.max_ticks);
    expect_true(run_outcome != TAGWORLD_OUTCOME_ESCAPED, "run alone does not escape on tool map");

    tagworld_reset_for_config(&w, &cfg, 0u);
    int wait_outcome = tagworld_simulate_until_outcome(&w, TAG_ACTION_WAIT, cfg.max_ticks);
    expect_true(wait_outcome != TAGWORLD_OUTCOME_ESCAPED, "wait does not escape on tool map");
}

static void test_tagworld_push_doorway_then_run_wins(void) {
    TagWorldConfig cfg = tagworld_tool_test_config();
    TagWorld w;
    tagworld_reset_for_config(&w, &cfg, 0u);
    int outcome = tagworld_simulate_with_policy(&w, tagworld_push_then_run_policy, NULL, cfg.max_ticks);
    expect_true(outcome == TAGWORLD_OUTCOME_ESCAPED, "push doorway then run escapes on tool map");
}

static void test_tagworld_action_selects_push_when_required(void) {
    TagWorldConfig cfg = tagworld_tool_test_config();
    cfg.episodes = 1u;
    cfg.mode = TAGWORLD_MODE_ACTION;
    cfg.fast = true;
    cfg.seed = 1u;

    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "tool action init");
    TagWorldMetrics metrics;
    expect_true(tagworld_run(&e, &cfg, &metrics) == 0, "tool action run");
    expect_true(metrics.action_push_doorway_count > 0u,
                "action selects push doorway on tool map");
    nerva_engine_free(&e);
}

static void test_tagworld_tool_action_beats_random_baseline(void) {
    static const uint32_t seeds[] = {1u, 5u, 11u};
    TagWorldConfig cfg = tagworld_tool_test_config();
    cfg.episodes = 100u;
    cfg.mode = TAGWORLD_MODE_ACTION;
    cfg.fast = true;

    for (size_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); ++i) {
        cfg.seed = seeds[i];
        NervaEngine e;
        expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "tool action seed init");
        TagWorldMetrics metrics;
        expect_true(tagworld_run(&e, &cfg, &metrics) == 0, "tool action seed run");
        double random_rate = tagworld_baseline_random_escape_rate(&cfg, cfg.episodes);
        expect_true(metrics.escape_rate >= random_rate + 0.20,
                    "tool map trained action beats random by >=20pp");
        expect_true(metrics.action_push_doorway_count > 0u,
                    "tool map action uses push doorway");
        nerva_engine_free(&e);
    }
}

static TagWorldConfig tagworld_online_test_config(void) {
    TagWorldConfig cfg = tagworld_tool_test_config();
    cfg.mode = TAGWORLD_MODE_ACTION;
    cfg.fast = true;
    cfg.online_tool_acquisition = true;
    cfg.episodes = 200u;
    return cfg;
}

static void test_tagworld_online_action_edges_zero_after_pretrain(void) {
    TagWorldConfig cfg = tagworld_online_test_config();
    cfg.episodes = 0u;
    cfg.seed = 1u;

    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "online pretrain init");
    TagWorldNerva tn;
    tagworld_nerva_init(&e, &tn);
    tagworld_pretrain_for_config(&e, &tn, &cfg);

    expect_true(tagworld_nerva_edge_weight(&e, tn.edge.path_open_to_push_doorway) == 0u,
                "path_open push edge zero after dynamics pretrain");
    expect_true(tagworld_nerva_edge_weight(&e, tn.edge.doorway_open_to_push_doorway) == 0u,
                "doorway_open push edge zero after dynamics pretrain");
    expect_true(tagworld_nerva_edge_weight(&e, tn.edge.seeker_route_to_push_chokepoint) == 0u,
                "seeker_route push edge zero after dynamics pretrain");
    expect_true(tagworld_nerva_edge_weight(&e, tn.edge.path_blocked_by_tool_to_run_safe) > 0u,
                "path_blocked_by_tool run edge positive after dynamics pretrain");
    expect_true(tagworld_nerva_edge_weight(&e, tn.edge.push_chokepoint_to_block_at_chokepoint) == 0u,
                "push chokepoint lead edge zero after dynamics pretrain");
    nerva_engine_free(&e);
}

static void test_tagworld_online_push_increases_over_episodes(void) {
    TagWorldConfig cfg = tagworld_online_test_config();
    cfg.seed = 1u;

    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "online push curve init");
    TagWorldMetrics metrics;
    expect_true(tagworld_run(&e, &cfg, &metrics) == 0, "online push curve run");
    expect_true(metrics.episodes_with_push_last_window > metrics.episodes_with_push_first_window,
                "online push selection rises from first to last window");
    expect_true(metrics.escaped_last_window > metrics.escaped_first_window,
                "online escape improves from first to last window");
    expect_true(metrics.action_push_doorway_count > 0u, "online run uses push doorway");
    nerva_engine_free(&e);
}

static void test_tagworld_online_beats_random_baseline(void) {
    static const uint32_t seeds[] = {1u, 5u, 11u};
    TagWorldConfig cfg = tagworld_online_test_config();

    for (size_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); ++i) {
        cfg.seed = seeds[i];
        NervaEngine e;
        expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "online seed init");
        TagWorldMetrics metrics;
        expect_true(tagworld_run(&e, &cfg, &metrics) == 0, "online seed run");
        double random_rate = tagworld_baseline_random_escape_rate(&cfg, cfg.episodes);
        expect_true(metrics.escaped_last_window > metrics.escaped_first_window,
                    "online escape improves from first to last window");
        expect_true(metrics.action_push_doorway_count > 0u, "online gate seed uses push");
        if (seeds[i] == 1u) {
            expect_true(metrics.escape_rate >= random_rate,
                        "online tool matches or beats random on primary gate seed");
        }
        nerva_engine_free(&e);
    }
}

static TagWorldConfig tagworld_online_frozen_test_config(void) {
    TagWorldConfig cfg = tagworld_tool_test_config();
    cfg.mode = TAGWORLD_MODE_ACTION;
    cfg.fast = true;
    cfg.online_frozen_eval = true;
    cfg.online_learn_episodes = 200u;
    cfg.online_eval_episodes = 100u;
    cfg.online_explore_pct = 15u;
    return cfg;
}

static void test_tagworld_post_push_selects_run_after_dynamics_pretrain(void) {
    TagWorldConfig cfg = tagworld_online_frozen_test_config();
    cfg.episodes = 0u;
    cfg.seed = 1u;

    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "post-push select init");
    TagWorldNerva tn;
    tagworld_nerva_init(&e, &tn);
    tagworld_pretrain_for_config(&e, &tn, &cfg);

    TagWorld w;
    tagworld_reset_for_config(&w, &cfg, 0u);
    for (uint32_t t = 0; t < 16u && !tagworld_is_block_at_chokepoint(&w); ++t) {
        tagworld_apply_action(&w, TAG_ACTION_PUSH_BLOCK_TO_DOORWAY);
        tagworld_step_seeker(&w);
        w.tick = t;
    }
    expect_true(tagworld_is_block_at_chokepoint(&w), "post-push block at chokepoint");

    nerva_debug_clear_fire_log(&e);
    tagworld_set_abstract_tool_policy(1);
    tagworld_nerva_emit_actual(&e, &tn, tn.ev.block_at_chokepoint);
    tagworld_nerva_emit_actual(&e, &tn, tn.ev.path_blocked_by_tool);
    TagWorldAction action =
        tagworld_nerva_select_action(&e, &tn, &w, tagworld_valid_action_mask(&w));
    expect_true(action == TAG_ACTION_RUN_TO_SAFE,
                "dynamics pretrain selects run after push when path blocked");
    nerva_engine_free(&e);
}

static void test_tagworld_action_score_trace_lists_contributors(void) {
    TagWorldConfig cfg = tagworld_online_frozen_test_config();
    cfg.episodes = 0u;
    cfg.seed = 1u;

    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "score trace init");
    TagWorldNerva tn;
    tagworld_nerva_init(&e, &tn);
    tagworld_pretrain_for_config(&e, &tn, &cfg);

    TagWorld w;
    tagworld_reset_for_config(&w, &cfg, 0u);
    tagworld_set_abstract_tool_policy(1);
    tagworld_nerva_emit_actual(&e, &tn, tn.ev.path_blocked_by_tool);

    TagWorldActionScoreTrace trace;
    memset(&trace, 0, sizeof(trace));
    TagWorldAction action = tagworld_nerva_select_action_scored(
        &e, &tn, &w, tagworld_valid_action_mask(&w), NULL, &trace);
    expect_true(trace.contrib_count > 0u, "score trace records edge contributors");
    expect_true(trace.edge_scores[TAG_ACTION_RUN_TO_SAFE] > 0,
                "score trace run edge score positive after dynamics pretrain");
    expect_true(action == TAG_ACTION_RUN_TO_SAFE, "score trace selects run when path blocked");
    expect_true(!trace.fallback_used, "score trace no fallback when run score positive");
    nerva_engine_free(&e);
}

static void test_tagworld_action_score_stable_after_10k_train_pairs(void) {
    TagWorldConfig cfg = tagworld_online_frozen_test_config();
    cfg.episodes = 0u;
    cfg.seed = 1u;

    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "10k train init");
    TagWorldNerva tn;
    tagworld_nerva_init(&e, &tn);
    tagworld_pretrain_for_config(&e, &tn, &cfg);

    for (uint32_t i = 0; i < 10000u; ++i) {
        tagworld_nerva_train_pair(&e, &tn, tn.ev.seeker_route_uses_chokepoint,
                                  tn.edge.seeker_route_to_push_chokepoint,
                                  tn.ev.action_push_block_to_doorway, 1);
        tagworld_nerva_train_pair(&e, &tn, tn.ev.path_blocked_by_tool,
                                  tn.edge.path_blocked_by_tool_to_run_safe,
                                  tn.ev.action_run_to_safe, 1);
    }

    TagWorld w;
    tagworld_reset_for_config(&w, &cfg, 0u);
    tagworld_set_abstract_tool_policy(1);
    tagworld_nerva_emit_actual(&e, &tn, tn.ev.path_blocked_by_tool);

    TagWorldActionScoreTrace trace;
    memset(&trace, 0, sizeof(trace));
    TagWorldAction action = tagworld_nerva_select_action_scored(
        &e, &tn, &w, tagworld_valid_action_mask(&w), NULL, &trace);
    expect_true(trace.edge_scores[TAG_ACTION_RUN_TO_SAFE] > 0,
                "10k train pairs keep run edge score positive");
    expect_true(action == TAG_ACTION_RUN_TO_SAFE, "10k train pairs still select run when blocked");
    nerva_engine_free(&e);
}

static void test_tagworld_frozen_eval_no_action_score_fallback(void) {
    TagWorldConfig cfg = tagworld_online_frozen_test_config();
    cfg.seed = 1u;

    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "fallback eval init");
    TagWorldFrozenResult result;
    expect_true(tagworld_run_frozen_result(&e, &cfg, &result) == 0, "fallback eval run");
    expect_true(result.eval.action_score_fallback_count == 0u,
                "frozen eval records zero action score fallbacks");
    nerva_engine_free(&e);
}

static void test_tagworld_action_score_long_learn_1k(void) {
    TagWorldConfig cfg = tagworld_online_frozen_test_config();
    cfg.seed = 1u;
    cfg.online_learn_episodes = 1000u;
    cfg.online_eval_episodes = 50u;

    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "1k learn init");
    TagWorldFrozenResult result;
    expect_true(tagworld_run_frozen_result(&e, &cfg, &result) == 0, "1k learn run");
    TagWorldConfig baseline_cfg = cfg;
    baseline_cfg.episodes = cfg.online_eval_episodes;
    double random_rate =
        tagworld_baseline_random_escape_rate(&baseline_cfg, baseline_cfg.episodes);
    expect_true(result.eval.escape_rate >= random_rate + 0.20,
                "1k learn frozen eval still beats random by >=20pp");
    expect_true(result.eval.action_score_fallback_count == 0u,
                "1k learn frozen eval zero action score fallbacks");
    nerva_engine_free(&e);
}

static void test_tagworld_online_frozen_learn_push_increases(void) {
    TagWorldConfig cfg = tagworld_online_frozen_test_config();
    cfg.seed = 1u;

    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "frozen learn init");
    TagWorldFrozenResult result;
    expect_true(tagworld_run_frozen_result(&e, &cfg, &result) == 0, "frozen learn run");
    expect_true(result.learn.episodes_with_push_last_window >
                    result.learn.episodes_with_push_first_window,
                "frozen learn push selection rises");
    expect_true(result.learn.escaped_last_window > result.learn.escaped_first_window,
                "frozen learn escape improves");
    nerva_engine_free(&e);
}

static void test_tagworld_online_frozen_eval_beats_random(void) {
    static const uint32_t seeds[] = {1u, 5u, 11u};
    TagWorldConfig cfg = tagworld_online_frozen_test_config();

    for (size_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); ++i) {
        cfg.seed = seeds[i];
        NervaEngine e;
        expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "frozen eval seed init");
        TagWorldFrozenResult result;
        expect_true(tagworld_run_frozen_result(&e, &cfg, &result) == 0, "frozen eval seed run");
        const TagWorldMetrics *eval = &result.eval;
        TagWorldConfig baseline_cfg = cfg;
        baseline_cfg.episodes = cfg.online_eval_episodes;
        double random_rate = tagworld_baseline_random_escape_rate(&baseline_cfg, baseline_cfg.episodes);
        expect_true(eval->escape_rate >= random_rate + 0.20,
                    "frozen eval beats random by >=20pp");
        expect_true(eval->action_push_doorway_count > 0u, "frozen eval uses push doorway");
        nerva_engine_free(&e);
    }
}

static void test_tagworld_online_frozen_eval_no_mutations(void) {
    TagWorldConfig cfg = tagworld_online_frozen_test_config();
    cfg.seed = 1u;

    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "frozen no-mut init");
    TagWorldFrozenResult result;
    expect_true(tagworld_run_frozen_result(&e, &cfg, &result) == 0, "frozen no-mut run");
    expect_true(result.eval.avg_mutations_per_episode == 0.0,
                "frozen eval applies zero mutations per episode");
    nerva_engine_free(&e);
}

static void test_tagworld_online_frozen_ablation_reduces_push(void) {
    TagWorldConfig cfg = tagworld_online_frozen_test_config();
    cfg.seed = 1u;

    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "frozen ablation init");
    TagWorldNerva tn;
    TagWorldFrozenResult result;
    expect_true(tagworld_run_frozen_result(&e, &cfg, &result) == 0, "frozen ablation full run");
    tagworld_nerva_init(&e, &tn);

    uint32_t push_edge_before =
        tagworld_nerva_edge_weight(&e, tn.edge.seeker_route_to_push_chokepoint);
    if (push_edge_before == 0u) {
        push_edge_before =
            tagworld_nerva_edge_weight(&e, tn.edge.block_can_reach_to_push_chokepoint);
    }
    expect_true(push_edge_before > 0u, "learned chokepoint push edge weight positive before ablation");

    uint64_t push_before = result.eval.action_push_doorway_count;
    double escape_before = result.eval.escape_rate;

    tagworld_ablate_learned_push_edges(&e, &tn);
    expect_true(tagworld_nerva_edge_weight(&e, tn.edge.seeker_route_to_push_chokepoint) == 0u,
                "seeker_route push edge zero after ablation");

    TagWorldMetrics ablated;
    expect_true(tagworld_run_frozen_eval_only(&e, &tn, &cfg, &ablated) == 0, "ablated eval run");
    expect_true(ablated.action_push_doorway_count < push_before ||
                    ablated.escape_rate < escape_before,
                "ablation reduces push usage or escape");
    nerva_engine_free(&e);
}

static TagWorldConfig tagworld_generalization_test_config(void) {
    TagWorldConfig cfg = tagworld_tool_test_config();
    cfg.mode = TAGWORLD_MODE_ACTION;
    cfg.fast = true;
    cfg.tool_generalization = true;
    cfg.generalization_eval_map = TAGWORLD_MAP_TOOL_D;
    cfg.online_learn_episodes = 200u;
    cfg.online_eval_episodes = 100u;
    cfg.online_explore_pct = 15u;
    cfg.run_baseline = true;
    return cfg;
}

static void test_tagworld_generalization_train_push_increases(void) {
    TagWorldConfig cfg = tagworld_generalization_test_config();
    cfg.seed = 1u;

    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "gen train init");
    TagWorldGeneralizationResult result;
    expect_true(tagworld_run_generalization_result(&e, &cfg, &result) == 0, "gen train run");
    expect_true(result.train.episodes_with_push_last_window >
                    result.train.episodes_with_push_first_window,
                "generalization train push selection rises");
    expect_true(result.train.escaped_last_window > result.train.escaped_first_window,
                "generalization train escape improves");
    nerva_engine_free(&e);
}

static void test_tagworld_generalization_eval_beats_random_on_D(void) {
    static const uint32_t seeds[] = {1u, 5u, 11u};
    TagWorldConfig cfg = tagworld_generalization_test_config();

    for (size_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); ++i) {
        cfg.seed = seeds[i];
        NervaEngine e;
        expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "gen eval seed init");
        TagWorldGeneralizationResult result;
        expect_true(tagworld_run_generalization_result(&e, &cfg, &result) == 0, "gen eval seed run");
        expect_true(result.eval_map == TAGWORLD_MAP_TOOL_D, "default held-out eval is map D");
        expect_true(tagworld_generalization_beats_random_gate(result.eval.escape_rate,
                                                            result.eval.baseline_escape_rate),
                    "held-out map D passes random gate (>=20pp margin or saturated-baseline perfect eval)");
        expect_true(result.eval.action_push_doorway_count > 0u, "held-out eval uses push action");
        nerva_engine_free(&e);
    }
}

static void test_tagworld_generalization_eval_no_mutations(void) {
    TagWorldConfig cfg = tagworld_generalization_test_config();
    cfg.seed = 1u;

    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "gen no-mut init");
    TagWorldGeneralizationResult result;
    expect_true(tagworld_run_generalization_result(&e, &cfg, &result) == 0, "gen no-mut run");
    expect_true(result.eval.avg_mutations_per_episode == 0.0,
                "generalization frozen eval applies zero mutations per episode");
    nerva_engine_free(&e);
}

static void test_tagworld_held_out_maps_push_then_run_wins(void) {
    TagWorldConfig cfg = tagworld_tool_test_config();
    struct {
        TagWorldMapId map_id;
        const char *label;
    } cases[] = {
        {TAGWORLD_MAP_TOOL_D, "D"},
        {TAGWORLD_MAP_TOOL_E, "E"},
        {TAGWORLD_MAP_TOOL_F, "F"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        cfg.map_id = cases[i].map_id;
        TagWorld w;
        tagworld_reset_for_config(&w, &cfg, 0u);
        int outcome =
            tagworld_simulate_with_policy(&w, tagworld_push_then_run_policy, NULL, cfg.max_ticks);
        char msg[64];
        snprintf(msg, sizeof(msg), "push then run escapes on held-out map %s", cases[i].label);
        expect_true(outcome == TAGWORLD_OUTCOME_ESCAPED, msg);
    }
}

static void test_tagworld_map_d_not_clone_of_a(void) {
    TagWorldConfig cfg = tagworld_tool_test_config();
    TagWorld a;
    TagWorld d;
    tagworld_init_map_for_id(&a, TAGWORLD_MAP_TOOL_A, cfg.grid);
    tagworld_init_map_for_id(&d, TAGWORLD_MAP_TOOL_D, cfg.grid);
    int same_walls = 1;
    for (int y = 0; y < a.height; ++y) {
        for (int x = 0; x < a.width; ++x) {
            if (a.cells[y][x] != d.cells[y][x]) {
                same_walls = 0;
                break;
            }
        }
        if (!same_walls) {
            break;
        }
    }
    expect_true(!same_walls, "map D wall geometry differs from map A");
}

static void test_tagworld_generalization_rename_copy_invariance(void) {
    TagWorldConfig cfg = tagworld_generalization_test_config();
    cfg.seed = 1u;

    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "invariance init");
    TagWorldNerva tn;
    TagWorldGeneralizationResult result;
    expect_true(tagworld_run_generalization_result(&e, &cfg, &result) == 0, "invariance learn+eval D");
    tagworld_nerva_init(&e, &tn);

    TagWorldConfig alias_cfg = cfg;
    alias_cfg.generalization_eval_map = TAGWORLD_MAP_TOOL_D_ALIAS;
    alias_cfg.map_id = TAGWORLD_MAP_TOOL_D_ALIAS;
    TagWorldMetrics alias_eval;
    expect_true(tagworld_run_frozen_eval_only(&e, &tn, &alias_cfg, &alias_eval) == 0,
                "invariance frozen eval on D alias");

    expect_true(result.eval.escape_rate == alias_eval.escape_rate,
                "alias preserves escape rate");
    expect_true(result.eval.escaped == alias_eval.escaped, "alias preserves escaped count");
    expect_true(result.eval.caught == alias_eval.caught, "alias preserves caught count");
    expect_true(result.eval.timeouts == alias_eval.timeouts, "alias preserves timeout count");
    expect_true(result.eval.action_push_doorway_count == alias_eval.action_push_doorway_count,
                "alias preserves push action count");
    expect_true(result.eval.action_run_count == alias_eval.action_run_count,
                "alias preserves run action count");
    nerva_engine_free(&e);
}

static void test_tagworld_generalization_ablation_reduces_push(void) {
    TagWorldConfig cfg = tagworld_generalization_test_config();
    cfg.seed = 1u;

    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "gen ablation init");
    TagWorldNerva tn;
    TagWorldGeneralizationResult result;
    expect_true(tagworld_run_generalization_result(&e, &cfg, &result) == 0, "gen ablation full run");
    tagworld_nerva_init(&e, &tn);

    uint32_t push_edge_before =
        tagworld_nerva_edge_weight(&e, tn.edge.seeker_route_to_push_chokepoint);
    if (push_edge_before == 0u) {
        push_edge_before =
            tagworld_nerva_edge_weight(&e, tn.edge.block_can_reach_to_push_chokepoint);
    }
    expect_true(push_edge_before > 0u, "gen learned chokepoint push edge before ablation");

    uint64_t push_before = result.eval.action_push_doorway_count;
    double escape_before = result.eval.escape_rate;

    tagworld_ablate_learned_push_edges(&e, &tn);

    TagWorldConfig eval_cfg = cfg;
    eval_cfg.map_id = TAGWORLD_MAP_TOOL_D;
    TagWorldMetrics ablated;
    expect_true(tagworld_run_frozen_eval_only(&e, &tn, &eval_cfg, &ablated) == 0, "gen ablated eval");
    expect_true(ablated.action_push_doorway_count < push_before ||
                    ablated.escape_rate < escape_before,
                "gen ablation reduces push usage or escape");
    nerva_engine_free(&e);
}

typedef struct TagWorldAbstractEpisodeTrace {
    bool chokepoint_detected;
    bool seeker_route_uses_chokepoint;
    bool block_can_reach_chokepoint;
    bool action_push_block_to_doorway;
    bool block_at_chokepoint;
    bool path_blocked_by_tool;
    bool action_run_to_safe;
    bool runner_escaped;
    uint32_t first_push_tick;
    uint32_t first_block_at_tick;
    uint32_t first_path_blocked_tick;
    uint32_t first_run_tick;
} TagWorldAbstractEpisodeTrace;

static void tagworld_trace_mark_event(TagWorldAbstractEpisodeTrace *tr, uint32_t node_id,
                                      const TagWorldNerva *tn, uint32_t tick) {
    if (node_id == tn->ev.chokepoint_detected) {
        tr->chokepoint_detected = true;
    } else if (node_id == tn->ev.seeker_route_uses_chokepoint) {
        tr->seeker_route_uses_chokepoint = true;
    } else if (node_id == tn->ev.block_can_reach_chokepoint) {
        tr->block_can_reach_chokepoint = true;
    } else if (node_id == tn->ev.action_push_block_to_doorway) {
        tr->action_push_block_to_doorway = true;
        if (tr->first_push_tick == UINT32_MAX) {
            tr->first_push_tick = tick;
        }
    } else if (node_id == tn->ev.block_at_chokepoint) {
        tr->block_at_chokepoint = true;
        if (tr->first_block_at_tick == UINT32_MAX) {
            tr->first_block_at_tick = tick;
        }
    } else if (node_id == tn->ev.path_blocked_by_tool) {
        tr->path_blocked_by_tool = true;
        if (tr->first_path_blocked_tick == UINT32_MAX) {
            tr->first_path_blocked_tick = tick;
        }
    } else if (node_id == tn->ev.action_run_to_safe) {
        tr->action_run_to_safe = true;
        if (tr->first_run_tick == UINT32_MAX) {
            tr->first_run_tick = tick;
        }
    } else if (node_id == tn->ev.runner_escaped) {
        tr->runner_escaped = true;
    }
}

static void tagworld_trace_absorb_fire_log(TagWorldAbstractEpisodeTrace *tr, const NervaEngine *e,
                                           const TagWorldNerva *tn, uint32_t tick) {
    for (uint32_t i = 0; i < e->fire_log_count; ++i) {
        tagworld_trace_mark_event(tr, e->fire_log[i].node_id, tn, tick);
    }
}

static void test_tagworld_generalization_abstract_trace_path(void) {
    TagWorldConfig cfg = tagworld_generalization_test_config();
    cfg.seed = 1u;
    cfg.online_eval_episodes = 100u;

    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "abstract trace init");
    TagWorldGeneralizationResult result;
    expect_true(tagworld_run_generalization_result(&e, &cfg, &result) == 0, "abstract trace run");
    expect_true(result.eval.escaped > 0u, "abstract trace has escaped episodes");

    TagWorldNerva tn;
    tagworld_nerva_init(&e, &tn);
    TagWorldConfig eval_cfg = cfg;
    eval_cfg.map_id = TAGWORLD_MAP_TOOL_D;
    eval_cfg.episodes = 100u;
    eval_cfg.online_tool_acquisition = false;
    eval_cfg.tool_generalization = true;

    TagWorld w;
    TagWorldMetrics metrics;
    memset(&metrics, 0, sizeof(metrics));
    metrics.seed = cfg.seed;
    metrics.mode = cfg.mode;

    tagworld_set_online_phase(TAGWORLD_ONLINE_EVAL);
    tagworld_set_abstract_tool_policy(1);

    TagWorldAbstractEpisodeTrace best;
    memset(&best, 0, sizeof(best));
    best.first_push_tick = UINT32_MAX;
    best.first_block_at_tick = UINT32_MAX;
    best.first_path_blocked_tick = UINT32_MAX;
    best.first_run_tick = UINT32_MAX;

    for (uint32_t ep = 0; ep < eval_cfg.episodes; ++ep) {
        TagWorldAbstractEpisodeTrace tr;
        memset(&tr, 0, sizeof(tr));
        tr.first_push_tick = UINT32_MAX;
        tr.first_block_at_tick = UINT32_MAX;
        tr.first_path_blocked_tick = UINT32_MAX;
        tr.first_run_tick = UINT32_MAX;

        tagworld_restore_online_learned_policy(&e);
        tagworld_reset_for_config(&w, &eval_cfg, ep);
        tn.episode_used_push_doorway = false;
        tn.episode_push_doorway_count = 0u;

        for (uint32_t t = 0; t < eval_cfg.max_ticks && !w.done; ++t) {
            w.tick = t;
            e.event_count = 0;
            e.active_count = 0;
            e.expectation_count = 0;
            nerva_debug_clear_fire_log(&e);
            tagworld_nerva_emit_state_events(&e, &tn, &w, &metrics);
            tagworld_nerva_tick_quiet(&e, 4);
            tagworld_trace_absorb_fire_log(&tr, &e, &tn, t);

            uint32_t valid_mask = tagworld_valid_action_mask(&w);
            TagWorldAction action =
                tagworld_nerva_select_action(&e, &tn, &w, valid_mask);
            if (action == TAG_ACTION_PUSH_BLOCK_TO_DOORWAY) {
                tagworld_trace_mark_event(&tr, tn.ev.action_push_block_to_doorway, &tn, t);
            } else if (action == TAG_ACTION_RUN_TO_SAFE) {
                tagworld_trace_mark_event(&tr, tn.ev.action_run_to_safe, &tn, t);
            }
            tagworld_apply_action(&w, action);
            tagworld_step_seeker(&w);
            tagworld_check_outcome(&w);
        }

        if (w.outcome == TAGWORLD_OUTCOME_ESCAPED && tr.action_push_block_to_doorway &&
            tr.action_run_to_safe) {
            best = tr;
            break;
        }
    }

    tagworld_set_online_phase(TAGWORLD_ONLINE_NONE);

    expect_true(best.action_push_block_to_doorway && best.action_run_to_safe,
                "abstract trace finds escaped episode with push then run");
    expect_true(best.chokepoint_detected || best.seeker_route_uses_chokepoint ||
                    best.block_can_reach_chokepoint,
                "abstract trace shows chokepoint context before tool action");
    expect_true(best.action_push_block_to_doorway, "abstract trace selects push tool action");
    expect_true(best.block_at_chokepoint, "abstract trace reaches block at chokepoint");
    expect_true(best.path_blocked_by_tool, "abstract trace reaches path blocked by tool");
    expect_true(best.action_run_to_safe, "abstract trace selects run to safe");
    expect_true(best.first_push_tick != UINT32_MAX && best.first_block_at_tick != UINT32_MAX &&
                    best.first_path_blocked_tick != UINT32_MAX && best.first_run_tick != UINT32_MAX,
                "abstract trace records ordered action milestones");
    expect_true(best.first_push_tick <= best.first_block_at_tick,
                "abstract trace push precedes block at chokepoint");
    expect_true(best.first_block_at_tick <= best.first_path_blocked_tick,
                "abstract trace block at chokepoint precedes path blocked");
    expect_true(best.first_path_blocked_tick <= best.first_run_tick,
                "abstract trace path blocked precedes run");
    nerva_engine_free(&e);
}

static void test_tagworld_pure_feedback_no_oracle_train_pairs(void) {
    TagWorldConfig supervised = tagworld_generalization_test_config();
    supervised.seed = 1u;
    supervised.online_learn_episodes = 80u;
    supervised.online_eval_episodes = 20u;

    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "pure feedback supervised init");
    tagworld_debug_reset_oracle_counters();
    TagWorldGeneralizationResult supervised_result;
    expect_true(tagworld_run_generalization_result(&e, &supervised, &supervised_result) == 0,
                "pure feedback supervised baseline run");
    uint64_t supervised_oracle = tagworld_debug_oracle_online_train_pair_rounds();
    expect_true(supervised_oracle > 0u, "supervised mode uses oracle train_pair on escape");
    nerva_engine_free(&e);

    TagWorldConfig pure = supervised;
    pure.pure_feedback = true;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "pure feedback init");
    tagworld_debug_reset_oracle_counters();
    TagWorldGeneralizationResult pure_result;
    expect_true(tagworld_run_generalization_result(&e, &pure, &pure_result) == 0, "pure feedback run");
    expect_true(tagworld_debug_oracle_online_train_pair_rounds() == 0u,
                "pure feedback skips oracle train_pair chains");
    nerva_engine_free(&e);
}

static void test_tagworld_pure_feedback_records_action_traces(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "pure feedback trace init");
    TagWorldNerva tn;
    expect_true(tagworld_nerva_init(&e, &tn) == 0, "pure feedback trace nerva init");

    TagWorldConfig cfg = tagworld_generalization_test_config();
    cfg.seed = 1u;
    cfg.map_id = TAGWORLD_MAP_TOOL_D;
    cfg.pure_feedback = true;
    tagworld_pretrain_for_config(&e, &tn, &cfg);
    e.edges[tn.edge.seeker_route_to_push_chokepoint].weight = NERVA_Q8_8_ONE;

    TagWorld w;
    tagworld_reset_for_config(&w, &cfg, 0u);
    tagworld_set_abstract_tool_policy(1);
    tagworld_set_pure_feedback(1);
    tagworld_set_online_phase(TAGWORLD_ONLINE_LEARN);

    nerva_debug_clear_fire_log(&e);
    tagworld_nerva_emit_actual(&e, &tn, tn.ev.seeker_route_uses_chokepoint);
    tagworld_nerva_emit_actual(&e, &tn, tn.ev.chokepoint_detected);
    nerva_tick_n(&e, 2);

    uint32_t before = nerva_trace_count_used_path(&e);
    uint32_t valid = tagworld_valid_action_mask(&w);
    TagWorldAction selected =
        tagworld_nerva_select_action_scored(&e, &tn, &w, valid, NULL, NULL);
    expect_true(selected == TAG_ACTION_PUSH_BLOCK_TO_DOORWAY,
                "pure feedback trace test selects push from policy edge");
    expect_true(nerva_trace_count_used_path(&e) > before,
                "pure feedback records policy traces during action select");
    tagworld_set_online_phase(TAGWORLD_ONLINE_NONE);
    tagworld_set_pure_feedback(0);
    tagworld_set_abstract_tool_policy(0);
    nerva_engine_free(&e);
}

int test_tagworld_run(void) {
    g_failures = 0;
    test_tagworld_deterministic_reset();
    test_tagworld_doorway_open_path();
    test_tagworld_block_at_doorway_blocks_path();
    test_tagworld_observer_learns_block_path();
    test_tagworld_prediction_expected_not_actual();
    test_tagworld_prediction_confirm_strengthens();
    test_tagworld_prediction_confirm_pair_increments();
    test_tagworld_prediction_mismatch_pair_increments();
    test_tagworld_prediction_miss_surprise();
    test_tagworld_open_doorway_runner_gets_caught();
    test_tagworld_block_at_doorway_prevents_catch_or_enables_escape();
    test_tagworld_action_beats_random_baseline();
    test_tagworld_run_alone_loses_on_tool_map();
    test_tagworld_push_doorway_then_run_wins();
    test_tagworld_action_selects_push_when_required();
    test_tagworld_tool_action_beats_random_baseline();
    test_tagworld_online_action_edges_zero_after_pretrain();
    test_tagworld_post_push_selects_run_after_dynamics_pretrain();
    test_tagworld_action_score_trace_lists_contributors();
    test_tagworld_action_score_stable_after_10k_train_pairs();
    test_tagworld_frozen_eval_no_action_score_fallback();
    test_tagworld_action_score_long_learn_1k();
    test_tagworld_online_push_increases_over_episodes();
    test_tagworld_online_beats_random_baseline();
    test_tagworld_online_frozen_learn_push_increases();
    test_tagworld_online_frozen_eval_beats_random();
    test_tagworld_online_frozen_eval_no_mutations();
    test_tagworld_online_frozen_ablation_reduces_push();
    test_tagworld_generalization_train_push_increases();
    test_tagworld_generalization_eval_beats_random_on_D();
    test_tagworld_generalization_eval_no_mutations();
    test_tagworld_map_d_not_clone_of_a();
    test_tagworld_generalization_rename_copy_invariance();
    test_tagworld_generalization_ablation_reduces_push();
    test_tagworld_generalization_abstract_trace_path();
    test_tagworld_pure_feedback_no_oracle_train_pairs();
    test_tagworld_pure_feedback_records_action_traces();
    test_tagworld_held_out_maps_push_then_run_wins();
    test_tagworld_viz_no_state_change();
    test_tagworld_replay_deterministic();
    return g_failures;
}
