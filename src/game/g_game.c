#include "g_game.h"
#include "g_unit.h"
#include "g_render.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

void g_game_init(Game *game, const MapGraph *graph) {
    // Free old terrain if reinitializing
    if (game->state.terrain_ready) {
        g_terrain_free(&game->terrain);
    }

    game->state = (GameState){0};
    game->state.elevation_speed_factor = 0.5f;
    game->state.water_blocks_movement = true;

    g_terrain_build_grid(&game->terrain, graph);
    game->state.terrain_ready = true;

    g_unit_init_player(&game->state.player, &game->terrain, graph);

    // Initialize camera centered on player
    game->state.camera.pos = game->state.player.pos;
    game->state.camera.zoom = 5.0f;
}

void g_game_update(Game *game, const MapGraph *graph, f64 dt) {
    GameState *gs = &game->state;
    TerrainGrid *tg = &game->terrain;
    f32 fdt = (f32)dt;

    // Skip input if ImGui wants keyboard
    ImGuiIO *io = igGetIO_Nil();
    if (!io->WantCaptureKeyboard) {
        const bool *keys = SDL_GetKeyboardState(NULL);
        Vec2 dir = {0, 0};
        if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])    dir.y -= 1.0f;
        if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN])  dir.y += 1.0f;
        if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])  dir.x -= 1.0f;
        if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) dir.x += 1.0f;

        if (vec2_len(dir) > 0.0f) {
            dir = vec2_normalize(dir);

            // Terrain speed modifier
            f32 elev = g_terrain_get_elevation(tg, graph, gs->player.pos);
            f32 speed = gs->player.speed * (1.0f - elev * gs->elevation_speed_factor);
            if (speed < 0.01f) speed = 0.01f;

            Vec2 new_pos = vec2_add(gs->player.pos, vec2_scale(dir, speed * fdt));

            // Clamp to map bounds
            if (new_pos.x < 0.0f) new_pos.x = 0.0f;
            if (new_pos.x > 1.0f) new_pos.x = 1.0f;
            if (new_pos.y < 0.0f) new_pos.y = 0.0f;
            if (new_pos.y > 1.0f) new_pos.y = 1.0f;

            // Block movement into water
            if (gs->water_blocks_movement && g_terrain_is_water(tg, graph, new_pos)) {
                // Try sliding along axes
                Vec2 try_x = {new_pos.x, gs->player.pos.y};
                Vec2 try_y = {gs->player.pos.x, new_pos.y};

                if (!g_terrain_is_water(tg, graph, try_x))
                    new_pos = try_x;
                else if (!g_terrain_is_water(tg, graph, try_y))
                    new_pos = try_y;
                else
                    new_pos = gs->player.pos; // fully blocked
            }

            gs->player.pos = new_pos;
        }
    }

    // Smooth camera follow (lerp toward player)
    f32 lerp_speed = 5.0f;
    Camera *cam = &gs->camera;
    cam->pos = vec2_add(cam->pos,
        vec2_scale(vec2_sub(gs->player.pos, cam->pos), lerp_speed * fdt));

    // Clamp camera so view stays within [0,1]²
    f32 view_size = 1.0f / cam->zoom;
    f32 half_view = view_size * 0.5f;
    if (cam->pos.x < half_view)        cam->pos.x = half_view;
    if (cam->pos.x > 1.0f - half_view) cam->pos.x = 1.0f - half_view;
    if (cam->pos.y < half_view)        cam->pos.y = half_view;
    if (cam->pos.y > 1.0f - half_view) cam->pos.y = 1.0f - half_view;
}

void g_game_render(Game *game, SDL_Renderer *renderer, SDL_FRect map_rect) {
    g_render_game(&game->state, renderer, map_rect);
}

void g_game_shutdown(Game *game) {
    if (game->state.terrain_ready) {
        g_terrain_free(&game->terrain);
        game->state.terrain_ready = false;
    }
}
