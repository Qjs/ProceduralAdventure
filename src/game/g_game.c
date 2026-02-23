#include "g_game.h"

void g_game_init(Game *game, const MapGraph *graph) {
    game->state = (GameState){0};
    game->state.elevation_speed_factor = 0.5f;
    game->state.water_blocks_movement = true;

    g_terrain_build_grid(&game->terrain, graph);
    game->state.terrain_ready = true;
}

void g_game_update(Game *game, const MapGraph *graph, f64 dt) {
    (void)game;
    (void)graph;
    (void)dt;
}

void g_game_render(Game *game, SDL_Renderer *renderer, SDL_FRect map_rect) {
    (void)game;
    (void)renderer;
    (void)map_rect;
}

void g_game_shutdown(Game *game) {
    if (game->state.terrain_ready) {
        g_terrain_free(&game->terrain);
        game->state.terrain_ready = false;
    }
}
