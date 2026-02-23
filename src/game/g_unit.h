#ifndef G_UNIT_H
#define G_UNIT_H

#include "g_types.h"
#include "g_terrain.h"
#include "../mapgen/mg_types.h"

// Initialize the player unit, spawning at map center (nearest land cell)
void g_unit_init_player(Unit *unit, const TerrainGrid *tg, const MapGraph *graph);

#endif
