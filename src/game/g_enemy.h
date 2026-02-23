#ifndef G_ENEMY_H
#define G_ENEMY_H

#include "g_types.h"
#include "g_terrain.h"
#include "../mapgen/mg_types.h"

// Scan map centers and place enemy camps on valid land tiles, scaled by level
void g_enemy_place_camps(GameState *gs, const TerrainGrid *tg, const MapGraph *graph,
                         u32 level, u32 total_upgrades);

// Update enemy AI: camp activation, seek/leash behavior
void g_enemy_update(GameState *gs, const TerrainGrid *tg, const MapGraph *graph, f32 dt);

#endif
