// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Paul Odantabao II

#include "tagworld_viz.h"

#include <stdio.h>

void tagworld_viz_print_help(void) {
    printf("TagWorld-lite terminal visualizer (observe-only, no learning side effects).\n");
}

void tagworld_viz_render_frame(FILE *out, const TagWorldFrame *frame) {
    if (!out || !frame) {
        return;
    }
    fprintf(out, "\n[t=%u episode=%u]\n", frame->tick, frame->episode);
    fprintf(out, "WORLD\n");
    for (int y = 0; frame->grid[y][0] != '\0' && y < TAGWORLD_MAX_DIM; ++y) {
        fprintf(out, "%s\n", frame->grid[y]);
    }
    fprintf(out, "\nNERVA\n");
    fprintf(out, "tick=%u\n", frame->tick);
    fprintf(out, "ACTIVE:\n");
    for (uint32_t i = 0; i < frame->active_count; ++i) {
        fprintf(out, "  %s\n", frame->active_events[i]);
    }
    fprintf(out, "EXPECTED:\n");
    for (uint32_t i = 0; i < frame->expected_count; ++i) {
        fprintf(out, "  %s\n", frame->expected_events[i]);
    }
    fprintf(out, "SURPRISE:\n");
    for (uint32_t i = 0; i < frame->surprise_count; ++i) {
        fprintf(out, "  %s\n", frame->surprise_events[i]);
    }
    fprintf(out, "ACTION:\n  %s\n", tagworld_action_name(frame->action));
    fprintf(out, "OUTCOME:\n  %s\n", tagworld_outcome_name(frame->outcome));
    fprintf(out, "LEARNING:\n");
    fprintf(out, "  queued_mutations=%u\n", frame->queued_mutations);
    fprintf(out, "  applied_mutations=%u\n", frame->applied_mutations);
    if (frame->edge_delta_label) {
        fprintf(out, "  edge_delta=%s %+d\n", frame->edge_delta_label, frame->edge_delta);
    } else {
        fprintf(out, "  edge_delta=none\n");
    }
    fprintf(out, "  prediction confirms/misses tracked in summary\n");
}
