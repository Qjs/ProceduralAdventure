#ifndef G_UNIT_H
#define G_UNIT_H

#include "g_types.h"
#include "g_terrain.h"
#include "../mapgen/mg_types.h"

// Initialize the player unit, spawning at map center (nearest land cell)
void g_unit_init_player(Unit *unit, const TerrainGrid *tg, const MapGraph *graph);

// Initialize the 4 squad companions near the player
void g_unit_init_squad(GameState *gs, const TerrainGrid *tg, const MapGraph *graph);

// Shared terrain-aware movement: clamps to [0,1]², blocks water with axis-sliding
Vec2 g_unit_move_with_terrain(Vec2 old_pos, Vec2 new_pos,
    const TerrainGrid *tg, const MapGraph *graph, bool water_blocks);

// Initialize an enemy unit with role-specific defaults
void g_unit_init_enemy(Unit *unit, UnitRole role);

// Find nearest alive unit on the opposing team.
// Returns: for TEAM_PLAYER units → enemy index into gs->enemies[]
//          for TEAM_ENEMY units  → 0 for player, 1+ for squad[idx-1]
//          UINT32_MAX if no target found
u32 g_unit_find_nearest_enemy(GameState *gs, const Unit *u);

#endif
