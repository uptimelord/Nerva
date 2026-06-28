// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Paul Odantabao II

#include "maps/tagworld_maps.h"

#include <string.h>

static void tagworld_paint_border(TagWorld *w, int grid) {
    for (int y = 0; y < grid; ++y) {
        for (int x = 0; x < grid; ++x) {
            if (x == 0 || y == 0 || x == grid - 1 || y == grid - 1) {
                w->cells[y][x] = TAG_CELL_WALL;
            } else {
                w->cells[y][x] = TAG_CELL_EMPTY;
            }
        }
    }
}

static void tagworld_set_wall(TagWorld *w, int x, int y) {
    if (x >= 0 && y >= 0 && x < w->width && y < w->height) {
        w->cells[y][x] = TAG_CELL_WALL;
    }
}

static void tagworld_place_chokepoint(TagWorld *w, TagWorldPos doorway, TagWorldPos safe) {
    w->doorway = doorway;
    w->safe = safe;
    w->cells[doorway.y][doorway.x] = TAG_CELL_DOORWAY;
    w->cells[safe.y][safe.x] = TAG_CELL_SAFE;
}

static void tagworld_init_tool_a(TagWorld *w, int grid) {
    tagworld_paint_border(w, grid);
    for (int x = 2; x <= 4; ++x) {
        tagworld_set_wall(w, x, 2);
        tagworld_set_wall(w, x, 4);
    }
    for (int x = 2; x <= 4; ++x) {
        tagworld_set_wall(w, x, 1);
    }
    tagworld_set_wall(w, 3, 4);
    w->cells[4][2] = TAG_CELL_EMPTY;
    w->cells[4][4] = TAG_CELL_EMPTY;
    tagworld_place_chokepoint(w, (TagWorldPos){3, 3}, (TagWorldPos){5, 4});
    w->map_id = TAGWORLD_MAP_TOOL_A;
}

static void tagworld_init_tool_b(TagWorld *w, int grid) {
    tagworld_paint_border(w, grid);
    for (int x = 2; x <= 4; ++x) {
        tagworld_set_wall(w, x, 2);
        tagworld_set_wall(w, x, 4);
    }
    for (int x = 2; x <= 4; ++x) {
        tagworld_set_wall(w, x, 1);
    }
    tagworld_set_wall(w, 3, 4);
    w->cells[4][2] = TAG_CELL_EMPTY;
    w->cells[4][4] = TAG_CELL_EMPTY;
    tagworld_place_chokepoint(w, (TagWorldPos){4, 3}, (TagWorldPos){1, 4});
    w->map_id = TAGWORLD_MAP_TOOL_B;
}

static void tagworld_init_tool_c(TagWorld *w, int grid) {
    tagworld_paint_border(w, grid);
    for (int x = 1; x <= 5; ++x) {
        tagworld_set_wall(w, x, 2);
    }
    for (int y = 2; y <= 4; ++y) {
        tagworld_set_wall(w, 2, y);
    }
    tagworld_set_wall(w, 4, 3);
    tagworld_set_wall(w, 4, 4);
    w->cells[3][3] = TAG_CELL_EMPTY;
    w->cells[5][3] = TAG_CELL_EMPTY;
    tagworld_place_chokepoint(w, (TagWorldPos){3, 4}, (TagWorldPos){5, 5});
    w->map_id = TAGWORLD_MAP_TOOL_C;
}

static void tagworld_init_tool_d(TagWorld *w, int grid) {
    tagworld_init_tool_a(w, grid);
    w->map_id = TAGWORLD_MAP_TOOL_D;
}

static void tagworld_init_tool_e(TagWorld *w, int grid) {
    tagworld_init_tool_a(w, grid);
    w->map_id = TAGWORLD_MAP_TOOL_E;
}

static void tagworld_init_tool_f(TagWorld *w, int grid) {
    tagworld_init_tool_a(w, grid);
    w->map_id = TAGWORLD_MAP_TOOL_F;
}

int tagworld_map_is_tool(TagWorldMapId map_id) {
    return map_id >= TAGWORLD_MAP_TOOL_A && map_id <= TAGWORLD_MAP_TOOL_F;
}

int tagworld_map_is_train_tool(TagWorldMapId map_id) {
    return map_id >= TAGWORLD_MAP_TOOL_A && map_id <= TAGWORLD_MAP_TOOL_C;
}

int tagworld_map_is_held_out_tool(TagWorldMapId map_id) {
    return map_id >= TAGWORLD_MAP_TOOL_D && map_id <= TAGWORLD_MAP_TOOL_F;
}

TagWorldMapId tagworld_generalization_train_map(uint32_t episode) {
    static const TagWorldMapId train[] = {
        TAGWORLD_MAP_TOOL_A,
        TAGWORLD_MAP_TOOL_B,
        TAGWORLD_MAP_TOOL_C,
    };
    return train[episode % (sizeof(train) / sizeof(train[0]))];
}

const char *tagworld_generalization_map_letter(TagWorldMapId map_id) {
    switch (map_id) {
    case TAGWORLD_MAP_TOOL_A:
        return "A";
    case TAGWORLD_MAP_TOOL_B:
        return "B";
    case TAGWORLD_MAP_TOOL_C:
        return "C";
    case TAGWORLD_MAP_TOOL_D:
        return "D";
    case TAGWORLD_MAP_TOOL_E:
        return "E";
    case TAGWORLD_MAP_TOOL_F:
        return "F";
    default:
        return "?";
    }
}

void tagworld_init_map_for_id(TagWorld *w, TagWorldMapId map_id, int grid) {
    if (grid < 5) {
        grid = 5;
    }
    if (grid > TAGWORLD_MAX_DIM) {
        grid = TAGWORLD_MAX_DIM;
    }
    memset(w, 0, sizeof(*w));
    w->width = grid;
    w->height = grid;
    w->map_id = map_id;

    switch (map_id) {
    case TAGWORLD_MAP_TOOL_B:
        tagworld_init_tool_b(w, grid);
        break;
    case TAGWORLD_MAP_TOOL_C:
        tagworld_init_tool_c(w, grid);
        break;
    case TAGWORLD_MAP_TOOL_D:
        tagworld_init_tool_d(w, grid);
        break;
    case TAGWORLD_MAP_TOOL_E:
        tagworld_init_tool_e(w, grid);
        break;
    case TAGWORLD_MAP_TOOL_F:
        tagworld_init_tool_f(w, grid);
        break;
    case TAGWORLD_MAP_TOOL_A:
    default:
        tagworld_init_tool_a(w, grid);
        if (map_id != TAGWORLD_MAP_TOOL_A) {
            w->map_id = map_id;
        }
        break;
    }
}

void tagworld_reset_tool_spawns(TagWorld *w, TagWorldMapId map_id) {
    w->episode_variant = 3u;
    switch (map_id) {
    case TAGWORLD_MAP_TOOL_B:
        w->runner.x = 5;
        w->runner.y = 3;
        w->seeker.x = 1;
        w->seeker.y = 3;
        w->block.x = 4;
        w->block.y = 3;
        break;
    case TAGWORLD_MAP_TOOL_C:
        w->runner.x = 1;
        w->runner.y = 3;
        w->seeker.x = 5;
        w->seeker.y = 3;
        w->block.x = 2;
        w->block.y = 3;
        break;
    case TAGWORLD_MAP_TOOL_D:
        w->runner.x = 1;
        w->runner.y = 3;
        w->seeker.x = 5;
        w->seeker.y = 3;
        w->block.x = 2;
        w->block.y = 3;
        break;
    case TAGWORLD_MAP_TOOL_E:
        w->runner.x = 3;
        w->runner.y = 5;
        w->seeker.x = 5;
        w->seeker.y = 3;
        w->block.x = 3;
        w->block.y = 4;
        break;
    case TAGWORLD_MAP_TOOL_F:
        w->runner.x = 1;
        w->runner.y = 3;
        w->seeker.x = 5;
        w->seeker.y = 3;
        w->block.x = 2;
        w->block.y = 3;
        w->cells[w->safe.y][w->safe.x] = TAG_CELL_EMPTY;
        w->safe.x = 1;
        w->safe.y = 5;
        w->cells[w->safe.y][w->safe.x] = TAG_CELL_SAFE;
        break;
    case TAGWORLD_MAP_TOOL_A:
    default:
        w->runner.x = 1;
        w->runner.y = 3;
        w->seeker.x = 5;
        w->seeker.y = 3;
        w->block.x = 2;
        w->block.y = 3;
        break;
    }
}

int tagworld_is_block_at_chokepoint(const TagWorld *w) {
    return w->block.x == w->doorway.x && w->block.y == w->doorway.y;
}

int tagworld_block_can_reach_chokepoint(const TagWorld *w) {
    if (tagworld_is_block_at_chokepoint(w)) {
        return 0;
    }
    if (tagworld_manhattan(w->runner, w->block) != 1) {
        return 0;
    }
    int dx = 0;
    int dy = 0;
    if (w->block.x < w->doorway.x) {
        dx = 1;
    } else if (w->block.x > w->doorway.x) {
        dx = -1;
    } else if (w->block.y < w->doorway.y) {
        dy = 1;
    } else if (w->block.y > w->doorway.y) {
        dy = -1;
    }
    if (dx == 0 && dy == 0) {
        return 0;
    }
    int nx = w->block.x + dx;
    int ny = w->block.y + dy;
    if (nx < 0 || ny < 0 || nx >= w->width || ny >= w->height) {
        return 0;
    }
    if (w->cells[ny][nx] == TAG_CELL_WALL) {
        return 0;
    }
    if (nx == w->seeker.x && ny == w->seeker.y) {
        return 0;
    }
    return 1;
}

int tagworld_seeker_route_uses_chokepoint(const TagWorld *w) {
    if (tagworld_is_block_at_chokepoint(w)) {
        return 0;
    }
    return tagworld_seeker_can_reach_runner(w);
}
