// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Paul Odantabao II

#include "tagworld.h"
#include "tagworld_viz.h"

#include "nerva_config.h"
#include "nerva_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *prog) {
    printf("Usage: %s [options]\n", prog);
    printf("  --help\n");
    printf("  --episodes N        (default 1)\n");
    printf("  --seed N            (default 1)\n");
    printf("  --grid N            (default 7)\n");
    printf("  --max-ticks N       (default 64)\n");
    printf("  --trace-every N     (default 1000)\n");
    printf("  --mode MODE         observer|prediction|action (default observer)\n");
    printf("  --map MAP           corridor|tool (default corridor)\n");
    printf("  --fast              no rendering, summary only\n");
    printf("  --viz               enable terminal ASCII visualizer\n");
    printf("  --baseline          track random action baseline (action mode)\n");
    printf("  --online-tool       outcome-driven tool acquisition (no action pretrain)\n");
    printf("  --replay PATH       replay log (no learning)\n");
    printf("  --write-replay PATH write JSONL replay log\n");
    printf("  --snapshot-in PATH  load graph snapshot\n");
    printf("  --snapshot-out PATH save graph snapshot\n");
}

static int parse_mode(const char *s, TagWorldMode *mode) {
    if (strcmp(s, "observer") == 0) {
        *mode = TAGWORLD_MODE_OBSERVER;
        return 0;
    }
    if (strcmp(s, "prediction") == 0) {
        *mode = TAGWORLD_MODE_PREDICTION;
        return 0;
    }
    if (strcmp(s, "action") == 0) {
        *mode = TAGWORLD_MODE_ACTION;
        return 0;
    }
    return -1;
}

static int parse_map(const char *s, TagWorldMapId *map_id) {
    if (strcmp(s, "corridor") == 0) {
        *map_id = TAGWORLD_MAP_CORRIDOR;
        return 0;
    }
    if (strcmp(s, "tool") == 0 || strcmp(s, "tool_pressure") == 0) {
        *map_id = TAGWORLD_MAP_TOOL_PRESSURE;
        return 0;
    }
    return -1;
}

int main(int argc, char **argv) {
    TagWorldConfig cfg;
    tagworld_config_defaults(&cfg);

    const char *prog = argc > 0 ? argv[0] : "nerva_tagworld";

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) {
            print_usage(prog);
            tagworld_viz_print_help();
            return 0;
        }
        if (strcmp(argv[i], "--episodes") == 0 && i + 1 < argc) {
            cfg.episodes = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            cfg.seed = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--grid") == 0 && i + 1 < argc) {
            cfg.grid = (int)strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--max-ticks") == 0 && i + 1 < argc) {
            cfg.max_ticks = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--trace-every") == 0 && i + 1 < argc) {
            cfg.trace_every = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            if (parse_mode(argv[++i], &cfg.mode) != 0) {
                fprintf(stderr, "Unknown mode\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--map") == 0 && i + 1 < argc) {
            if (parse_map(argv[++i], &cfg.map_id) != 0) {
                fprintf(stderr, "Unknown map\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--fast") == 0) {
            cfg.fast = true;
        } else if (strcmp(argv[i], "--viz") == 0) {
            cfg.viz = true;
        } else if (strcmp(argv[i], "--baseline") == 0) {
            cfg.run_baseline = true;
        } else if (strcmp(argv[i], "--online-tool") == 0) {
            cfg.online_tool_acquisition = true;
        } else if (strcmp(argv[i], "--replay") == 0 && i + 1 < argc) {
            cfg.replay = true;
            cfg.replay_path = argv[++i];
        } else if (strcmp(argv[i], "--write-replay") == 0 && i + 1 < argc) {
            cfg.write_replay = true;
            cfg.replay_path = argv[++i];
        } else if (strcmp(argv[i], "--snapshot-in") == 0 && i + 1 < argc) {
            cfg.snapshot_in = argv[++i];
        } else if (strcmp(argv[i], "--snapshot-out") == 0 && i + 1 < argc) {
            cfg.snapshot_out = argv[++i];
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            print_usage(prog);
            return 1;
        }
    }

    if (cfg.replay && cfg.replay_path) {
        return tagworld_replay_file(cfg.replay_path, cfg.viz) == 0 ? 0 : 1;
    }

    NervaEngine e;
    if (nerva_engine_init(&e, nerva_config_default()) != 0) {
        fprintf(stderr, "engine init failed\n");
        return 1;
    }

    TagWorldMetrics metrics;
    if (tagworld_run(&e, &cfg, &metrics) != 0) {
        nerva_engine_free(&e);
        return 1;
    }

    tagworld_print_summary(&metrics, stdout);
    nerva_engine_free(&e);
    return 0;
}
