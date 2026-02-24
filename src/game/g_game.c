#include "g_game.h"
#include "g_unit.h"
#include "g_unit_gen.h"
#include "g_render.h"
#include "g_enemy.h"
#include "g_combat.h"
#include "g_particles.h"
#include <stdlib.h>

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

static u32 xorshift32(u32 *state) {
    u32 x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

void g_game_init(Game *game, SDL_Renderer *renderer, const MapGraph *graph,
                 u32 level, const u32 stat_levels[][4]) {
    // Free old terrain if reinitializing
    if (game->state.terrain_ready) {
        g_terrain_free(&game->terrain);
    }

    // Destroy old textures before zeroing state
    g_unit_gen_destroy(game->state.role_textures);
    if (game->state.orb_texture) SDL_DestroyTexture(game->state.orb_texture);
    if (game->state.portal_texture) SDL_DestroyTexture(game->state.portal_texture);

    game->state = (GameState){0};
    g_unit_gen_textures(renderer, game->state.role_textures);
    game->state.orb_texture = g_gen_orb_texture(renderer);
    game->state.portal_texture = g_gen_portal_texture(renderer);
    game->state.elevation_speed_factor = 0.5f;
    game->state.water_blocks_movement = true;
    game->state.squad_stance = STANCE_DEFENSIVE;

    g_terrain_build_grid(&game->terrain, graph);
    game->state.terrain_ready = true;

    // Sum total player upgrades for enemy scaling
    u32 total_upgrades = 0;
    if (stat_levels) {
        for (u32 i = 0; i < MAX_SQUAD; i++)
            for (u32 s = 0; s < 4; s++)
                total_upgrades += stat_levels[i][s];
    }

    g_unit_init_player(&game->state.player, &game->terrain, graph);
    g_unit_init_squad(&game->state, &game->terrain, graph, stat_levels);
    g_enemy_place_camps(&game->state, &game->terrain, graph, level, total_upgrades);

    // Initialize camera centered on player
    game->state.camera.pos = game->state.player.pos;
    game->state.camera.zoom = 5.0f;

    // Spawn orbs on random land cells
    game->state.orbs_collected = 0;
    game->state.portal.active = false;
    game->state.level_complete = false;

    // Collect valid land cell indices
    u32 *land_indices = NULL;
    u32 num_land = 0;
    land_indices = malloc(graph->num_centers * sizeof(u32));
    for (u32 i = 0; i < graph->num_centers; i++) {
        if (!graph->centers[i].water && !graph->centers[i].border) {
            land_indices[num_land++] = i;
        }
    }

    // Fisher-Yates partial shuffle to pick NUM_COLLECT_ORBS + 1 random cells
    // (5 for orbs, 1 for portal spawn location)
    u32 rng_state = (u32)(size_t)graph ^ 0xDEADBEEF;
    if (rng_state == 0) rng_state = 1;
    u32 total_pick = (NUM_COLLECT_ORBS + 1) < num_land ? (NUM_COLLECT_ORBS + 1) : num_land;
    for (u32 i = 0; i < total_pick; i++) {
        u32 j = i + (xorshift32(&rng_state) % (num_land - i));
        u32 tmp = land_indices[i];
        land_indices[i] = land_indices[j];
        land_indices[j] = tmp;
    }

    u32 orb_count = total_pick > NUM_COLLECT_ORBS ? NUM_COLLECT_ORBS : total_pick;
    game->state.num_orbs = orb_count;
    for (u32 i = 0; i < orb_count; i++) {
        Orb *orb = &game->state.orbs[i];
        orb->active = true;
        orb->pos = graph->centers[land_indices[i]].pos;
        orb->radius = 0.004f;
        orb->pulse_timer = (f32)i * 1.2f; // offset phase per orb
    }

    // Pre-select portal spawn position (last shuffled cell)
    u32 portal_idx = total_pick > NUM_COLLECT_ORBS ? NUM_COLLECT_ORBS : 0;
    game->state.portal.spawn_pos = graph->centers[land_indices[portal_idx]].pos;

    free(land_indices);
}

static void update_squad(GameState *gs, const TerrainGrid *tg, const MapGraph *graph, f32 dt) {
    for (u32 i = 0; i < gs->num_squad; i++) {
        Unit *u = &gs->squad[i];
        if (!u->alive) continue;

        // Override boid weights based on stance
        switch (gs->squad_stance) {
            case STANCE_AGGRESSIVE:
                u->weights.follow_player = 0.5f;
                u->weights.preferred_dist = 0.15f;
                break;
            case STANCE_DEFENSIVE:
                u->weights.follow_player = 1.0f;
                u->weights.preferred_dist = 0.10f;
                break;
            case STANCE_PASSIVE:
                u->weights.follow_player = 1.2f;
                u->weights.preferred_dist = 0.06f;
                break;
        }

        // Tick status timers
        if (u->slow_timer > 0.0f) u->slow_timer -= dt;
        if (u->speed_boost_timer > 0.0f) u->speed_boost_timer -= dt;

        Vec2 force = {0, 0};

        if (u->state == STATE_ATTACK) {
            // Seek nearest enemy instead of following player
            u32 target_idx = g_unit_find_nearest_enemy(gs, u);
            if (target_idx != UINT32_MAX && target_idx < gs->num_enemies) {
                Vec2 to_target = vec2_sub(gs->enemies[target_idx].pos, u->pos);
                f32 dist_target = vec2_len(to_target);
                if (dist_target > 1e-3f) {
                    // Move toward target but stop at attack range
                    f32 seek_str = 1.5f;
                    if (dist_target < u->attack_range)
                        seek_str *= 0.1f; // slow down when in range
                    force = vec2_add(force, vec2_scale(vec2_normalize(to_target), seek_str));
                }
            } else {
                // No enemies — fall through to follow player
                u->state = STATE_FOLLOW;
            }
        }

        if (u->state == STATE_RETREAT) {
            // Flee from nearest enemy, head toward player
            u32 target_idx = g_unit_find_nearest_enemy(gs, u);
            if (target_idx != UINT32_MAX && target_idx < gs->num_enemies) {
                Vec2 away = vec2_sub(u->pos, gs->enemies[target_idx].pos);
                f32 d = vec2_len(away);
                if (d > 1e-3f)
                    force = vec2_add(force, vec2_scale(vec2_normalize(away), 1.0f));
            }
            // Also pull toward player
            Vec2 to_player = vec2_sub(gs->player.pos, u->pos);
            if (vec2_len(to_player) > 1e-3f)
                force = vec2_add(force, vec2_scale(vec2_normalize(to_player), 0.8f));
        }

        if (u->state == STATE_FOLLOW || u->state == STATE_HEAL) {
            // Follow player — attenuate when within preferred_dist
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
        }

        // Separation from other squad members (always active)
        Vec2 sep = {0, 0};
        for (u32 j = 0; j < gs->num_squad; j++) {
            if (j == i || !gs->squad[j].alive) continue;
            Vec2 diff = vec2_sub(u->pos, gs->squad[j].pos);
            f32 d = vec2_len(diff);
            if (d < u->weights.separation_radius && d > 1e-6f) {
                sep = vec2_add(sep, vec2_scale(vec2_normalize(diff), 1.0f / d));
            }
        }
        force = vec2_add(force, vec2_scale(sep, u->weights.separation));

        // Cohesion — steer toward squad centroid (always active)
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
            f32 target_facing = atan2f(dir.y, dir.x);
            f32 lerp_t = 1.0f - expf(-8.0f * dt);
            u->facing = angle_lerp(u->facing, target_facing, lerp_t);
            f32 elev = g_terrain_get_elevation(tg, graph, u->pos);
            f32 speed = u->speed * (1.0f - elev * gs->elevation_speed_factor);
            if (u->slow_timer > 0.0f) speed *= 0.5f;
            if (u->speed_boost_timer > 0.0f) speed *= 1.5f;
            if (speed < 0.01f) speed = 0.01f;

            bool water_blocks = gs->squad_stance != STANCE_PASSIVE;
            if (!water_blocks && g_terrain_is_water(tg, graph, u->pos))
                speed *= 0.5f;
            Vec2 new_pos = vec2_add(u->pos, vec2_scale(dir, speed * dt));
            u->pos = g_unit_move_with_terrain(u->pos, new_pos, tg, graph,
                                               water_blocks);
        }
    }
}

static void apply_stance_auras(GameState *gs) {
    // Reset bonus_armor each frame
    gs->player.bonus_armor = 0.0f;
    for (u32 i = 0; i < gs->num_squad; i++)
        gs->squad[i].bonus_armor = 0.0f;

    // Melee defensive: +3 bonus armor to self
    if (gs->squad_stance == STANCE_DEFENSIVE) {
        for (u32 i = 0; i < gs->num_squad; i++) {
            if (!gs->squad[i].alive) continue;
            if (gs->squad[i].role == ROLE_MELEE)
                gs->squad[i].bonus_armor += 3.0f;
        }
    }

    // Healer passive: +2 bonus armor to all allies within attack_range
    if (gs->squad_stance == STANCE_PASSIVE) {
        for (u32 i = 0; i < gs->num_squad; i++) {
            Unit *healer = &gs->squad[i];
            if (!healer->alive || healer->role != ROLE_HEALER) continue;
            f32 range = healer->attack_range;
            if (gs->player.alive && vec2_dist(healer->pos, gs->player.pos) < range)
                gs->player.bonus_armor += 2.0f;
            for (u32 j = 0; j < gs->num_squad; j++) {
                if (j == i || !gs->squad[j].alive) continue;
                if (vec2_dist(healer->pos, gs->squad[j].pos) < range)
                    gs->squad[j].bonus_armor += 2.0f;
            }
        }
    }
}

void g_game_update(Game *game, const MapGraph *graph, f64 dt) {
    GameState *gs = &game->state;
    TerrainGrid *tg = &game->terrain;
    f32 fdt = (f32)dt;

    // Tick player status timers
    if (gs->player.slow_timer > 0.0f) gs->player.slow_timer -= fdt;
    if (gs->player.speed_boost_timer > 0.0f) gs->player.speed_boost_timer -= fdt;

    // Skip input if ImGui wants keyboard
    ImGuiIO *io = igGetIO_Nil();
    if (!io->WantCaptureKeyboard) {
        const bool *keys = SDL_GetKeyboardState(NULL);
        Vec2 dir = {0, 0};
        if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])    dir.y -= 1.0f;
        if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN])  dir.y += 1.0f;
        if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])  dir.x -= 1.0f;
        if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) dir.x += 1.0f;

        // Stance hotkeys (block switching while player is on water)
        SquadStance prev_stance = gs->squad_stance;
        bool on_water = g_terrain_is_water(tg, graph, gs->player.pos);
        if (!on_water) {
            if (keys[SDL_SCANCODE_1]) gs->squad_stance = STANCE_AGGRESSIVE;
            if (keys[SDL_SCANCODE_2]) gs->squad_stance = STANCE_DEFENSIVE;
            if (keys[SDL_SCANCODE_3]) gs->squad_stance = STANCE_PASSIVE;
        }

        // Mage one-shot speed boost on switching to passive
        if (gs->squad_stance == STANCE_PASSIVE && prev_stance != STANCE_PASSIVE) {
            for (u32 i = 0; i < gs->num_squad; i++) {
                if (!gs->squad[i].alive) continue;
                if (gs->squad[i].role != ROLE_MAGE) continue;
                // Boost all alive squad allies + player
                if (gs->player.alive)
                    gs->player.speed_boost_timer = 3.0f;
                for (u32 j = 0; j < gs->num_squad; j++) {
                    if (j == i || !gs->squad[j].alive) continue;
                    gs->squad[j].speed_boost_timer = 3.0f;
                }
                break; // one mage is enough
            }
        }

        if (vec2_len(dir) > 0.0f) {
            dir = vec2_normalize(dir);
            f32 target_facing = atan2f(dir.y, dir.x);
            f32 lerp_t = 1.0f - expf(-15.0f * fdt); // fast but smooth
            gs->player.facing = angle_lerp(gs->player.facing, target_facing, lerp_t);

            // Terrain speed modifier
            f32 elev = g_terrain_get_elevation(tg, graph, gs->player.pos);
            f32 speed = gs->player.speed * (1.0f - elev * gs->elevation_speed_factor);
            if (gs->player.slow_timer > 0.0f) speed *= 0.5f;
            if (gs->player.speed_boost_timer > 0.0f) speed *= 1.5f;
            if (speed < 0.01f) speed = 0.01f;

            bool water_blocks = gs->squad_stance != STANCE_PASSIVE;
            if (!water_blocks && g_terrain_is_water(tg, graph, gs->player.pos))
                speed *= 0.5f;
            Vec2 new_pos = vec2_add(gs->player.pos, vec2_scale(dir, speed * fdt));
            gs->player.pos = g_unit_move_with_terrain(gs->player.pos, new_pos,
                                                       tg, graph, water_blocks);
        }
    }

    // Update squad boid steering
    update_squad(gs, tg, graph, fdt);

    // Apply stance auras (bonus armor etc.) before combat
    apply_stance_auras(gs);

    // Enemy AI + combat
    g_enemy_update(gs, tg, graph, fdt);
    g_combat_update_squad_states(gs);
    g_combat_update(gs, tg, graph, fdt);
    g_combat_update_projectiles(gs, fdt);
    g_particles_update(&gs->particles, fdt);

    // Orb collection
    for (u32 i = 0; i < gs->num_orbs; i++) {
        Orb *orb = &gs->orbs[i];
        if (!orb->active) continue;
        orb->pulse_timer += fdt;
        if (vec2_dist(gs->player.pos, orb->pos) < gs->player.radius + orb->radius) {
            orb->active = false;
            gs->orbs_collected++;
        }
    }

    // Portal spawn when all orbs collected
    if (gs->orbs_collected == NUM_COLLECT_ORBS && !gs->portal.active) {
        gs->portal.active = true;
        gs->portal.pos = gs->portal.spawn_pos;
        gs->portal.radius_x = 0.006f;
        gs->portal.radius_y = 0.012f;
        gs->portal.pulse_timer = 0.0f;
    }

    // Portal enter (ellipse collision using normalized distance)
    if (gs->portal.active) {
        gs->portal.pulse_timer += fdt;
        Vec2 d = vec2_sub(gs->player.pos, gs->portal.pos);
        f32 nx = d.x / gs->portal.radius_x;
        f32 ny = d.y / gs->portal.radius_y;
        if (nx * nx + ny * ny < 1.0f) {
            gs->level_complete = true;
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
    g_render_game(&game->state, renderer, map_rect, game->state.role_textures);
}

void g_game_shutdown(Game *game) {
    g_unit_gen_destroy(game->state.role_textures);
    if (game->state.orb_texture) SDL_DestroyTexture(game->state.orb_texture);
    if (game->state.portal_texture) SDL_DestroyTexture(game->state.portal_texture);
    if (game->state.terrain_ready) {
        g_terrain_free(&game->terrain);
        game->state.terrain_ready = false;
    }
}
