// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Paul Odantabao II

#ifndef TAGWORLD_MAPS_H
#define TAGWORLD_MAPS_H

#include "tagworld.h"

int tagworld_map_is_tool(TagWorldMapId map_id);
int tagworld_map_is_train_tool(TagWorldMapId map_id);
int tagworld_map_is_held_out_tool(TagWorldMapId map_id);
int tagworld_map_is_d_geometry_alias(TagWorldMapId map_id);
TagWorldMapId tagworld_generalization_train_map(uint32_t episode);
const char *tagworld_generalization_map_letter(TagWorldMapId map_id);

void tagworld_init_map_for_id(TagWorld *w, TagWorldMapId map_id, int grid);
void tagworld_reset_tool_spawns(TagWorld *w, TagWorldMapId map_id);

int tagworld_block_can_reach_chokepoint(const TagWorld *w);
int tagworld_seeker_route_uses_chokepoint(const TagWorld *w);
int tagworld_is_block_at_chokepoint(const TagWorld *w);

#endif
