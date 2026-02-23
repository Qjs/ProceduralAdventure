#ifndef G_RENDER_H
#define G_RENDER_H

#include "g_types.h"
#include <SDL3/SDL.h>

// Render all game entities on top of the map texture
void g_render_game(GameState *gs, SDL_Renderer *renderer, SDL_FRect map_rect,
                   SDL_Texture *role_textures[ROLE_COUNT]);

#endif
