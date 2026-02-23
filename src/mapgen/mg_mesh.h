#ifndef MG_MESH_H
#define MG_MESH_H

#include "mg_types.h"

// Build Delaunay triangulation + Voronoi dual graph from point coords.
// coords: interleaved f32 pairs [x0,y0,x1,y1,...] in [0,1]²
// num_points: number of sites
// Populates graph with Centers, Corners, MapEdges, and adjacency.
// Returns true on success.
bool mg_build_mesh(MapGraph *graph, const f32 *coords, u32 num_points);

// Free all memory owned by graph.
void mg_free_mesh(MapGraph *graph);

#endif
