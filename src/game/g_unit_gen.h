#ifndef G_UNIT_GEN_H
#define G_UNIT_GEN_H

#include "g_types.h"
#include <SDL3/SDL.h>

void g_unit_gen_textures(SDL_Renderer *renderer, SDL_Texture *out[ROLE_COUNT]);
void g_unit_gen_destroy(SDL_Texture *textures[ROLE_COUNT]);

#endif
