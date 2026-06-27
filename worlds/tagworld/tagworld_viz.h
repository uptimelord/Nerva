// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Paul Odantabao II

#ifndef TAGWORLD_VIZ_H
#define TAGWORLD_VIZ_H

#include "tagworld.h"

#include <stdio.h>

void tagworld_viz_render_frame(FILE *out, const TagWorldFrame *frame);
void tagworld_viz_print_help(void);

#endif
