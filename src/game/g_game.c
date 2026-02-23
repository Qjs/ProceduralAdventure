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
    g_unit_init_squad(&game->state, &game->terrain, graph);

    // Initialize camera centered on player
    game->state.camera.pos = game->state.player.pos;
    game->state.camera.zoom = 5.0f;
}

static void update_squad(GameState *gs, const TerrainGrid *tg, const MapGraph *graph, f32 dt) {
    for (u32 i = 0; i < gs->num_squad; i++) {
        Unit *u = &gs->squad[i];
        if (!u->alive) continue;

        Vec2 force = {0, 0};

        // 1. Follow player — attenuate when within preferred_dist
        Vec2 to_player = vec2_sub(gs->player.pos, u->pos);
        f32 dist_to_player = vec2_len(to_player);
        if (dist_to_player > 1e-3f) {
            f32 follow_strength = u->weights.follow_player;
            if (dist_to_player < u->weights.preferred_dist) {
                follow_strength *= dist_to_player / u->weights.preferred_dist;
            }
            Vec2 follow = vec2_scale(vec2_normalize(to_player), follow_strength);
            force = vec2_add(force, follow);
        }

        // 2. Separation from other squad members
        Vec2 sep = {0, 0};
        for (u32 j = 0; j < gs->num_squad; j++) {
            if (j == i || !gs->squad[j].alive) continue;
            Vec2 diff = vec2_sub(u->pos, gs->squad[j].pos);
            f32 d = vec2_len(diff);
            if (d < u->weights.separation_radius && d > 1e-6f) {
                // Stronger repulsion when closer
                sep = vec2_add(sep, vec2_scale(vec2_normalize(diff), 1.0f / d));
            }
        }
        force = vec2_add(force, vec2_scale(sep, u->weights.separation));

        // 3. Cohesion — steer toward squad centroid
        Vec2 centroid = {0, 0};
        u32 count = 0;
        for (u32 j = 0; j < gs->num_squad; j++) {
            if (j == i || !gs->squad[j].alive) continue;
            centroid = vec2_add(centroid, gs->squad[j].pos);
            count++;
        }
        if (count > 0) {
            centroid = vec2_scale(centroid, 1.0f / (f32)count);
            Vec2 to_centroid = vec2_sub(centroid, u->pos);
            force = vec2_add(force, vec2_scale(to_centroid, u->weights.cohesion));
        }

        // Normalize and apply speed with terrain modifier (skip negligible forces)
        f32 force_len = vec2_len(force);
        if (force_len > 0.25f) {
            Vec2 dir = vec2_scale(force, 1.0f / force_len);
            f32 elev = g_terrain_get_elevation(tg, graph, u->pos);
            f32 speed = u->speed * (1.0f - elev * gs->elevation_speed_factor);
            if (speed < 0.01f) speed = 0.01f;

            Vec2 new_pos = vec2_add(u->pos, vec2_scale(dir, speed * dt));
            u->pos = g_unit_move_with_terrain(u->pos, new_pos, tg, graph,
                                               gs->water_blocks_movement);
        }
    }
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
            gs->player.pos = g_unit_move_with_terrain(gs->player.pos, new_pos,
                                                       tg, graph, gs->water_blocks_movement);
        }
    }

    // Update squad boid steering
    update_squad(gs, tg, graph, fdt);

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
