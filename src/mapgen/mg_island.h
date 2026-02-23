#ifndef MG_ISLAND_H
#define MG_ISLAND_H

#include "mg_types.h"

// Classify centers and corners as water/land/coast based on island shape function.
void mg_classify_island(MapGraph *graph, const MapParams *params);

#endif
