#ifndef MG_ELEVATION_H
#define MG_ELEVATION_H

#include "mg_types.h"

// Assign elevation to all centers and corners via coast-distance BFS.
// Water cells get elevation 0, land cells get elevation based on
// distance from coast normalized and curved by gamma.
void mg_assign_elevation(MapGraph *graph, const MapParams *params);

#endif
