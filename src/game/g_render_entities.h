#ifndef G_RENDER_ENTITIES_H
#define G_RENDER_ENTITIES_H

#include "g_types.h"
#include <SDL3/SDL.h>

void gr_draw_unit_composite(SDL_Renderer *renderer, const Unit *u,
                            SDL_FRect map_rect, SDL_Texture *role_tex);

void gr_draw_squad_layered(SDL_Renderer *renderer, const GameState *gs, const Unit *u,
                           SDL_FRect map_rect, SDL_Texture *role_tex);

void gr_draw_health_bar(SDL_Renderer *renderer, const Unit *u, SDL_FRect map_rect);

#endif
