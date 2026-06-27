// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Paul Odantabao II

#include "nerva_test_fixtures.h"

void nerva_test_setup_poodle_graph(NervaEngine *e, uint32_t *poodle, uint32_t *dog,
                                  uint32_t *animal) {
    *poodle = nerva_get_or_create_node(e, "poodle");
    *dog = nerva_get_or_create_node(e, "dog");
    *animal = nerva_get_or_create_node(e, "animal");

    nerva_graph_create_edge(e, *poodle, *dog, NERVA_REL_KIND_OF);
    nerva_graph_create_edge(e, *dog, *animal, NERVA_REL_KIND_OF);
    nerva_graph_rebuild_adjacency(e);
}
