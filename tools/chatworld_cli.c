// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Paul Odantabao II

#include "chatworld.h"
#include "nerva_config.h"
#include "nerva_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *prog) {
    printf("Usage: %s [options]\n", prog);
    printf("  --train             run training phase\n");
    printf("  --eval              run frozen eval phase\n");
    printf("  --train-epochs N    training epochs (default 20)\n");
    printf("  --seed N            deterministic seed (default 1)\n");
    printf("  --ablate            zero learned response edges before eval\n");
    printf("  --train-data PATH   train dataset TSV\n");
    printf("  --frozen-data PATH  frozen eval dataset TSV\n");
    printf("  --trace PATH        write ChatWorld decision trace\n");
}

int main(int argc, char **argv) {
    ChatWorldConfig cfg;
    chatworld_config_defaults(&cfg);
    const char *prog = argc > 0 ? argv[0] : "nerva_chatworld";

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) {
            print_usage(prog);
            return 0;
        } else if (strcmp(argv[i], "--train") == 0) {
            cfg.train = true;
        } else if (strcmp(argv[i], "--no-train") == 0) {
            cfg.train = false;
        } else if (strcmp(argv[i], "--eval") == 0) {
            cfg.eval = true;
        } else if (strcmp(argv[i], "--no-eval") == 0) {
            cfg.eval = false;
        } else if (strcmp(argv[i], "--train-epochs") == 0 && i + 1 < argc) {
            cfg.train_epochs = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            cfg.seed = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--ablate") == 0) {
            cfg.ablate_response_edges = true;
        } else if (strcmp(argv[i], "--train-data") == 0 && i + 1 < argc) {
            cfg.train_path = argv[++i];
        } else if (strcmp(argv[i], "--frozen-data") == 0 && i + 1 < argc) {
            cfg.frozen_path = argv[++i];
        } else if (strcmp(argv[i], "--trace") == 0 && i + 1 < argc) {
            cfg.trace_path = argv[++i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(prog);
            return 1;
        }
    }

    NervaEngine e;
    if (nerva_engine_init(&e, nerva_config_default()) != 0) {
        fprintf(stderr, "failed to initialize Nerva engine\n");
        return 2;
    }

    ChatWorldResult result;
    int rc = chatworld_run(&e, &cfg, &result);
    if (rc != 0) {
        fprintf(stderr, "chatworld run failed\n");
        nerva_engine_free(&e);
        return 2;
    }
    chatworld_print_metrics(&result.metrics, stdout);
    nerva_engine_free(&e);
    return 0;
}
