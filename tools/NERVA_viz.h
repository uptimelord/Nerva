// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Paul Odantabao II

#ifndef NERVA_VIZ_H
#define NERVA_VIZ_H

#include "NERVA_tagworld.h"

#include <stdio.h>

void tagworld_viz_render_frame(FILE *out, const TagWorldFrame *frame);
void tagworld_viz_print_help(void);

#endif
