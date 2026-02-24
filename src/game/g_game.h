#ifndef G_GAME_H
#define G_GAME_H

#include "g_types.h"
#include "g_terrain.h"
#include "../mapgen/mg_types.h"
#include <SDL3/SDL.h>

typedef struct {
    GameState    state;
    TerrainGrid  terrain;
} Game;

void g_game_init(Game *game, SDL_Renderer *renderer, const MapGraph *graph,
                 u32 level, const u32 stat_levels[][4]);
void g_game_update(Game *game, const Map *map, f64 dt);
void g_game_render(Game *game, SDL_Renderer *renderer, SDL_FRect map_rect);
void g_game_shutdown(Game *game);

#endif
