#ifndef MG_RASTER_H
#define MG_RASTER_H

#include "mg_types.h"
#include <SDL3/SDL.h>

// Rasterize the map graph to an RGBA pixel buffer.
// Allocates pixels in map if needed.
void mg_rasterize(Map *map);

// Upload pixel buffer to an SDL streaming texture.
// Creates the texture if *tex is NULL.
void mg_upload_texture(const Map *map, SDL_Renderer *renderer, SDL_Texture **tex);

#endif
