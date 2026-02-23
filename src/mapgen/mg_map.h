#ifndef MG_MAP_H
#define MG_MAP_H

#include "mg_types.h"
#include <SDL3/SDL.h>

// Set default parameters.
void mg_map_init(Map *map);

// Generate (or regenerate) the full map pipeline.
void mg_map_generate(Map *map);

// Free all map resources.
void mg_map_free(Map *map);

// Draw ImGui controls panel. Returns true if Regenerate was pressed.
bool mg_map_imgui_panel(Map *map);

// Draw just the map gen controls (no window). For embedding in another panel.
bool mg_map_imgui_controls(Map *map);

#endif
