// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Paul Odantabao II

#include "nerva_config.h"
#include "nerva_engine.h"
#include "nerva_event.h"
#include "nerva_graph.h"
#include "nerva_learning.h"
#include "nerva_math.h"
#include "nerva_memory.h"
#include "nerva_test_fixtures.h"
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

static void expect_eq_u64(uint64_t actual, uint64_t expected, const char *message) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (expected %llu, got %llu)\n", message,
                (unsigned long long)expected, (unsigned long long)actual);
        g_failures++;
    }
}

static void nerva_test_run_idle(NervaEngine *e, uint32_t ticks) {
    for (uint32_t i = 0; i < ticks; ++i) {
        nerva_tick(e);
    }
}

static void nerva_test_run_useful_episode(NervaEngine *e, uint32_t poodle, uint32_t *mem_id) {
    nerva_trace_clear(e);
    expect_true(nerva_activate_node(e, poodle, NERVA_Q8_8_ONE), "activate poodle for episode");
    *mem_id = nerva_memory_begin_episode(e, e->active_query_tag);
    nerva_tick_n(e, 4);
    nerva_memory_end_episode(e, *mem_id);
    nerva_memory_charge_update(e, *mem_id, 6.0f, 0.0f, 1.0f, 0.0f);
}

static void test_memory_episode_captures_traces(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "episode init");

    uint32_t poodle, dog, animal;
    nerva_test_setup_poodle_graph(&e, &poodle, &dog, &animal);
    (void)dog;
    (void)animal;

    uint32_t mem_id;
    expect_true(nerva_activate_node(&e, poodle, NERVA_Q8_8_ONE), "activate for trace capture");
    mem_id = nerva_memory_begin_episode(&e, e.active_query_tag);
    nerva_tick_n(&e, 4);
    nerva_memory_end_episode(&e, mem_id);

    const NervaMemoryBlock *mem = nerva_memory_get(&e, mem_id);
    expect_true(mem != NULL, "episode memory exists");
    expect_eq_u32(mem->trace_count, 2u, "episode captured two used-path traces");
    expect_eq_u32(mem->query_tag, e.active_query_tag, "episode query tag stored");

    nerva_engine_free(&e);
}

static void test_memory_charge_consolidates(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "consolidate init");

    uint32_t poodle, dog, animal;
    nerva_test_setup_poodle_graph(&e, &poodle, &dog, &animal);
    (void)dog;
    (void)animal;

    uint32_t mem_id;
    nerva_test_run_useful_episode(&e, poodle, &mem_id);
    expect_true(nerva_memory_is_consolidated(&e, mem_id), "charge above store threshold");
    expect_true(!nerva_memory_is_marked_delete(&e, mem_id), "consolidated episode not deleted");

    nerva_engine_free(&e);
}

static void test_memory_useful_persists(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "useful persist init");

    uint32_t poodle, dog, animal;
    nerva_test_setup_poodle_graph(&e, &poodle, &dog, &animal);
    (void)dog;
    (void)animal;

    uint32_t mem_id;
    nerva_test_run_useful_episode(&e, poodle, &mem_id);
    nerva_memory_charge_update(&e, mem_id, 10.0f, 0.0f, 0.0f, 0.0f);

    nerva_test_run_idle(&e, e.cfg.idle_consolidate_ticks * 2u);

    expect_true(nerva_memory_is_consolidated(&e, mem_id), "useful memory stays consolidated");
    expect_true(!nerva_memory_is_marked_delete(&e, mem_id), "useful memory not marked delete");
    expect_true(nerva_memory_get(&e, mem_id)->charge > e.cfg.memory_forget_threshold,
                "useful charge stays above forget threshold");

    nerva_engine_free(&e);
}

static void test_memory_forget_requires_hold_period(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "forget hold init");

    uint32_t mem_id = nerva_memory_begin_episode(&e, 0x3003u);
    nerva_memory_charge_update(&e, mem_id, 0.0f, 0.8f, 0.0f, 0.0f);
    nerva_memory_end_episode(&e, mem_id);

    nerva_test_run_idle(&e, e.cfg.idle_consolidate_ticks);
    expect_true(!nerva_memory_is_marked_delete(&e, mem_id),
                "not marked delete immediately after first decay");

    const NervaMemoryBlock *mem = nerva_memory_get(&e, mem_id);
    nerva_tick_t low_since = mem ? mem->low_charge_since : 0;
    uint32_t ticks_before_hold =
        (low_since + e.cfg.memory_hold_period_ticks > e.tick)
            ? (uint32_t)(low_since + e.cfg.memory_hold_period_ticks - e.tick)
            : 0u;
    if (ticks_before_hold > 0u) {
        nerva_test_run_idle(&e, ticks_before_hold);
    }
    expect_true(!nerva_memory_is_marked_delete(&e, mem_id),
                "not marked delete before hold period elapses");

    nerva_test_run_idle(&e, e.cfg.idle_consolidate_ticks);
    expect_true(nerva_memory_is_marked_delete(&e, mem_id),
                "marked delete after sustained low charge");
    expect_eq_u64(e.debug.memory_forgotten, 1u, "forget counter incremented");

    nerva_engine_free(&e);
}

static void test_memory_idle_skips_when_events_pending(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "idle skip init");

    uint32_t poodle, dog, animal;
    nerva_test_setup_poodle_graph(&e, &poodle, &dog, &animal);
    (void)dog;
    (void)animal;

    uint32_t mem_id = nerva_memory_begin_episode(&e, 0x4004u);
    nerva_memory_charge_update(&e, mem_id, 0.0f, 0.8f, 0.0f, 0.0f);
    nerva_memory_end_episode(&e, mem_id);

    NervaEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.due_tick = e.tick + 100u;
    ev.target = poodle;
    ev.signal = NERVA_Q8_8_ONE;
    expect_true(nerva_event_push(&e, ev), "future event queued");

    nerva_test_run_idle(&e, e.cfg.idle_consolidate_ticks * 4u);
    expect_eq_u64(e.debug.memory_consolidations, 0u, "no consolidation while events pending");
    expect_true(!nerva_memory_is_marked_delete(&e, mem_id), "memory untouched without consolidation");

    nerva_engine_free(&e);
}

static void test_memory_replay_top_k(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "replay init");

    uint32_t poodle, dog, animal;
    nerva_test_setup_poodle_graph(&e, &poodle, &dog, &animal);
    (void)dog;
    (void)animal;

    uint32_t useful_id;
    nerva_test_run_useful_episode(&e, poodle, &useful_id);

    uint32_t unused_id = nerva_memory_begin_episode(&e, 0x5005u);
    nerva_memory_charge_update(&e, unused_id, 0.0f, 0.5f, 0.0f, 0.0f);
    nerva_memory_end_episode(&e, unused_id);

    nerva_test_run_idle(&e, e.cfg.idle_consolidate_ticks);
    expect_true(e.debug.memory_replayed >= 1u, "replay placeholder ran");
    expect_true((nerva_memory_get(&e, useful_id)->flags & NERVA_MEM_FLAG_REPLAYED) != 0,
                "high-charge episode replayed");

    nerva_engine_free(&e);
}

static void test_memory_open_episode_defers_consolidation(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "open episode init");

    uint32_t mem_id = nerva_memory_begin_episode(&e, 0x7007u);
    nerva_memory_charge_update(&e, mem_id, 0.0f, 0.8f, 0.0f, 0.0f);
    expect_true(nerva_memory_is_episode_open(&e, mem_id), "episode open before end");

    nerva_test_run_idle(&e, e.cfg.idle_consolidate_ticks);
    expect_eq_u64(e.debug.memory_consolidations, 0u, "consolidation deferred while episode open");
    expect_true(nerva_memory_get(&e, mem_id)->charge > 0.79f, "open episode charge unchanged");

    nerva_memory_end_episode(&e, mem_id);
    expect_true(!nerva_memory_is_episode_open(&e, mem_id), "episode closed after end");

    nerva_test_run_idle(&e, e.cfg.idle_consolidate_ticks);
    expect_true(e.debug.memory_consolidations >= 1u, "consolidation runs after episode close");
    expect_true(nerva_memory_get(&e, mem_id)->charge < 0.79f, "closed episode decayed");

    nerva_engine_free(&e);
}

static void test_memory_low_charge_since_at_tick_zero(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "tick zero init");
    expect_eq_u64(e.tick, 0u, "engine starts at tick zero");

    uint32_t mem_id = nerva_memory_begin_episode(&e, 0x8008u);
    nerva_memory_charge_update(&e, mem_id, 0.0f, 0.8f, 0.0f, 0.0f);
    nerva_memory_end_episode(&e, mem_id);

    expect_eq_u64(nerva_memory_get(&e, mem_id)->low_charge_since, 0u,
                  "low charge at tick zero recorded immediately");

    nerva_engine_free(&e);
}

static void test_memory_experiment_artifacts(void) {
    NervaEngine e;
    expect_true(nerva_engine_init(&e, nerva_config_test()) == 0, "artifact init");

    uint32_t poodle, dog, animal;
    nerva_test_setup_poodle_graph(&e, &poodle, &dog, &animal);
    (void)dog;
    (void)animal;

    uint32_t useful_id;
    nerva_test_run_useful_episode(&e, poodle, &useful_id);

    uint32_t unused_id = nerva_memory_begin_episode(&e, 0x6006u);
    nerva_memory_charge_update(&e, unused_id, 0.0f, 0.8f, 0.0f, 0.0f);
    nerva_memory_end_episode(&e, unused_id);

    nerva_test_run_idle(&e, e.cfg.idle_consolidate_ticks);

    const NervaMemoryBlock *useful = nerva_memory_get(&e, useful_id);
    const NervaMemoryBlock *unused = nerva_memory_get(&e, unused_id);
    expect_true(useful != NULL && (useful->flags & NERVA_MEM_FLAG_USEFUL) != 0,
                "useful episode flagged useful");
    expect_true(unused != NULL && (unused->flags & NERVA_MEM_FLAG_USEFUL) == 0,
                "low-charge episode not flagged useful");

    expect_true(nerva_trace_save_path(&e, "experiments/v06_memory_consolidation/trace.log", 8) == 0,
                "memory trace.log saved");
    expect_true(nerva_memory_save_log(&e, "experiments/v06_memory_consolidation/memory.log") == 0,
                "memory.log saved");

    nerva_engine_free(&e);
}

int test_memory_run(void) {
    g_failures = 0;
    test_memory_episode_captures_traces();
    test_memory_charge_consolidates();
    test_memory_useful_persists();
    test_memory_forget_requires_hold_period();
    test_memory_idle_skips_when_events_pending();
    test_memory_replay_top_k();
    test_memory_open_episode_defers_consolidation();
    test_memory_low_charge_since_at_tick_zero();
    test_memory_experiment_artifacts();
    return g_failures;
}
