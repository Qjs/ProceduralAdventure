#ifndef G_ENEMY_H
#define G_ENEMY_H

#include "g_types.h"
#include "g_terrain.h"
#include "../mapgen/mg_types.h"

// Scan map centers and place enemy camps on valid land tiles
void g_enemy_place_camps(GameState *gs, const TerrainGrid *tg, const MapGraph *graph);

// Update enemy AI: camp activation, seek/leash behavior
void g_enemy_update(GameState *gs, const TerrainGrid *tg, const MapGraph *graph, f32 dt);

#endif
