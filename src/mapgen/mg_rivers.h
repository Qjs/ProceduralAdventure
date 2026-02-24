#ifndef MG_RIVERS_H
#define MG_RIVERS_H

#include "mg_types.h"

// Compute downslope for each corner, then trace rivers downhill along edges.
// Call after mg_assign_elevation, before mg_rasterize.
void mg_assign_rivers(MapGraph *graph, const MapParams *params, u32 seed);

// Draw rivers into the pixel buffer and populate river_mask.
// Call after mg_rasterize.
void mg_rasterize_rivers(Map *map);

#endif
