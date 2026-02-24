#ifndef G_RENDER_EFFECTS_H
#define G_RENDER_EFFECTS_H

#include "g_types.h"
#include <SDL3/SDL.h>

void gr_draw_orb(SDL_Renderer *renderer, const Orb *orb, SDL_FRect mr, SDL_Texture *orb_tex);
void gr_draw_portal(SDL_Renderer *renderer, const Portal *portal, SDL_FRect mr, SDL_Texture *portal_tex);
void gr_draw_camp_marker(SDL_Renderer *renderer, const EnemyCamp *camp, SDL_FRect mr);
void gr_draw_projectile(SDL_Renderer *renderer, const Projectile *p, SDL_FRect mr);
void gr_draw_environment_effect(SDL_Renderer *renderer, const GameState *gs, SDL_FRect mr);

#endif
