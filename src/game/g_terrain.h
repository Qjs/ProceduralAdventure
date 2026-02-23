#ifndef G_TERRAIN_H
#define G_TERRAIN_H

#include "g_types.h"
#include "../mapgen/mg_types.h"

typedef struct {
    u32  grid_res;
    f32  cell_size;
    u32 *grid_cells;  // grid_res * grid_res, each stores a center index or UINT32_MAX
} TerrainGrid;

void g_terrain_build_grid(TerrainGrid *tg, const MapGraph *graph);
void g_terrain_free(TerrainGrid *tg);

// Find the nearest Voronoi center index for a position in [0,1]²
u32  g_terrain_find_cell(const TerrainGrid *tg, const MapGraph *graph, Vec2 pos);

// Convenience queries
f32  g_terrain_get_elevation(const TerrainGrid *tg, const MapGraph *graph, Vec2 pos);
bool g_terrain_is_water(const TerrainGrid *tg, const MapGraph *graph, Vec2 pos);

#endif
