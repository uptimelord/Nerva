// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Paul Odantabao II

#include "chatworld.h"

#include <stdio.h>
#include <string.h>

static void print_usage(const char *prog) {
    printf("Usage: %s --stage <stage> --input <path> [--frozen]\n", prog);
    printf("  --stage <stage>   stage5.2, stage6.1, phase5, phase6, phase8, promotion\n");
    printf("  --input PATH      TSV rows: utterance<TAB>expected<TAB>key<TAB>value<TAB>learn\n");
    printf("  --frozen          reject rows with learn=1\n");
}

int main(int argc, char **argv) {
    const char *prog = argc > 0 ? argv[0] : "nerva_chatworld_validate_rows";
    const char *stage = NULL;
    const char *input = NULL;
    int frozen = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) {
            print_usage(prog);
            return 0;
        } else if (strcmp(argv[i], "--stage") == 0 && i + 1 < argc) {
            stage = argv[++i];
        } else if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
            input = argv[++i];
        } else if (strcmp(argv[i], "--frozen") == 0) {
            frozen = 1;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(prog);
            return 1;
        }
    }

    if (!stage || !input) {
        print_usage(prog);
        return 1;
    }

    ChatWorldValidationResult result;
    if (chatworld_validate_rows_file(stage, input, frozen != 0, stderr, &result) != 0) {
        fprintf(stderr, "validation_failed rows=%u errors=%u first_line=%u first_error=\"%s\"\n",
                result.row_count, result.error_count, result.first_error_line,
                result.first_error);
        return 2;
    }

    printf("validation_ok rows=%u\n", result.row_count);
    return 0;
}
