// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Paul Odantabao II

#include "nerva_config.h"
#include "nerva_engine.h"
#include "nerva_parse.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: nerva_cli <script.nerva>\n");
        return 1;
    }

    NervaEngine e;
    if (nerva_engine_init(&e, nerva_config_default()) != 0) {
        fprintf(stderr, "engine init failed\n");
        return 1;
    }

    if (nerva_parse_run_file(&e, argv[1]) != 0) {
        fprintf(stderr, "script failed: %s\n", argv[1]);
        nerva_engine_free(&e);
        return 1;
    }

    nerva_engine_free(&e);
    return 0;
}
