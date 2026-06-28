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
    printf("  --online-frozen     learn online then frozen eval (200+100 episodes)\n");
    printf("  --generalization    train maps A/B/C, frozen eval on held-out map\n");
    printf("  --pure-feedback     learn from traces + outcomes only (no oracle train_pair)\n");
    printf("  --eval-map MAP      held-out eval map D|E|F (default D)\n");
    printf("  --learn-episodes N  online learn phase length (default 200)\n");
    printf("  --eval-episodes N   frozen eval phase length (default 100)\n");
    printf("  --action-score-trace  log action score fallback traces to stderr\n");
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
    if (strcmp(s, "tool_a") == 0 || strcmp(s, "A") == 0) {
        *map_id = TAGWORLD_MAP_TOOL_A;
        return 0;
    }
    if (strcmp(s, "tool_b") == 0 || strcmp(s, "B") == 0) {
        *map_id = TAGWORLD_MAP_TOOL_B;
        return 0;
    }
    if (strcmp(s, "tool_c") == 0 || strcmp(s, "C") == 0) {
        *map_id = TAGWORLD_MAP_TOOL_C;
        return 0;
    }
    if (strcmp(s, "tool_d") == 0 || strcmp(s, "D") == 0) {
        *map_id = TAGWORLD_MAP_TOOL_D;
        return 0;
    }
    if (strcmp(s, "tool_e") == 0 || strcmp(s, "E") == 0) {
        *map_id = TAGWORLD_MAP_TOOL_E;
        return 0;
    }
    if (strcmp(s, "tool_f") == 0 || strcmp(s, "F") == 0) {
        *map_id = TAGWORLD_MAP_TOOL_F;
        return 0;
    }
    if (strcmp(s, "tool_d_alias") == 0 || strcmp(s, "D'") == 0 || strcmp(s, "D_alias") == 0 ||
        strcmp(s, "D_copy") == 0) {
        *map_id = TAGWORLD_MAP_TOOL_D_ALIAS;
        return 0;
    }
    if (strcmp(s, "tool_g") == 0 || strcmp(s, "G") == 0) {
        *map_id = TAGWORLD_MAP_TOOL_G;
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
        } else if (strcmp(argv[i], "--online-frozen") == 0) {
            cfg.online_frozen_eval = true;
        } else if (strcmp(argv[i], "--generalization") == 0) {
            cfg.tool_generalization = true;
        } else if (strcmp(argv[i], "--pure-feedback") == 0) {
            cfg.pure_feedback = true;
        } else if (strcmp(argv[i], "--eval-map") == 0 && i + 1 < argc) {
            if (parse_map(argv[++i], &cfg.generalization_eval_map) != 0) {
                fprintf(stderr, "Unknown eval map\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--learn-episodes") == 0 && i + 1 < argc) {
            cfg.online_learn_episodes = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--eval-episodes") == 0 && i + 1 < argc) {
            cfg.online_eval_episodes = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--action-score-trace") == 0) {
            cfg.action_score_trace = true;
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
    if (cfg.tool_generalization) {
        TagWorldGeneralizationResult gen;
        if (tagworld_run_generalization_result(&e, &cfg, &gen) != 0) {
            nerva_engine_free(&e);
            return 1;
        }
        tagworld_print_generalization_summary(&gen, stdout);
    } else if (cfg.online_frozen_eval) {
        TagWorldFrozenResult frozen;
        if (tagworld_run_frozen_result(&e, &cfg, &frozen) != 0) {
            nerva_engine_free(&e);
            return 1;
        }
        tagworld_print_frozen_summary(&frozen, stdout);
    } else {
        if (tagworld_run(&e, &cfg, &metrics) != 0) {
            nerva_engine_free(&e);
            return 1;
        }
        tagworld_print_summary(&metrics, stdout);
    }
    nerva_engine_free(&e);
    return 0;
}
