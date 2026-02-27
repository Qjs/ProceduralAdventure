#include "g_game.h"
#include "g_unit.h"
#include "g_unit_gen.h"
#include "g_render.h"
#include "g_enemy.h"
#include "g_combat.h"
#include "g_particles.h"
#include "g_physics.h"
#include "g_audio.h"
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

static u32 game_rand(GameState *gs) {
    if (gs->effect_rng == 0) gs->effect_rng = 1;
    return xorshift32(&gs->effect_rng);
}

static f32 game_rand01(GameState *gs) {
    return (f32)(game_rand(gs) % 10000) / 10000.0f;
}

static void camera_shake(Camera *cam, f32 duration, f32 intensity) {
    if (cam->shake_timer > 0.0f && intensity <= cam->shake_intensity) return;
    cam->shake_timer = duration;
    cam->shake_intensity = intensity;
}

static void get_camera_view_bounds(const GameState *gs,
                                   f32 *min_x, f32 *max_x,
                                   f32 *min_y, f32 *max_y) {
    f32 view_size = 1.0f / gs->camera.zoom;
    f32 vx = gs->camera.pos.x - view_size * 0.5f;
    f32 vy = gs->camera.pos.y - view_size * 0.5f;
    if (vx < 0.0f) vx = 0.0f;
    if (vx > 1.0f - view_size) vx = 1.0f - view_size;
    if (vy < 0.0f) vy = 0.0f;
    if (vy > 1.0f - view_size) vy = 1.0f - view_size;
    *min_x = vx;
    *max_x = vx + view_size;
    *min_y = vy;
    *max_y = vy + view_size;
}

static Unit *pick_random_alive_ally(GameState *gs, u32 *ally_kind, u32 *ally_index) {
    // kind: 0=player, 1=squad
    u32 choices[1 + MAX_SQUAD];
    u32 num = 0;
    if (gs->player.alive) choices[num++] = 0;
    for (u32 i = 0; i < gs->num_squad; i++) {
        if (!gs->squad[i].alive) continue;
        choices[num++] = i + 1;
    }
    if (num == 0) return NULL;
    u32 pick = choices[game_rand(gs) % num];
    if (pick == 0) {
        if (ally_kind) *ally_kind = 0;
        if (ally_index) *ally_index = 0;
        return &gs->player;
    }
    if (ally_kind) *ally_kind = 1;
    if (ally_index) *ally_index = pick - 1;
    return &gs->squad[pick - 1];
}

static void apply_orb_effect(GameState *gs, const TerrainGrid *tg,
                             const MapGraph *graph, const Orb *orb) {
    switch (orb->effect) {
        case ORB_EFFECT_HEAL_BOOST: {
            f32 heal_frac = 0.35f;
            if (gs->player.alive) {
                gs->player.hp += gs->player.max_hp * heal_frac;
                if (gs->player.hp > gs->player.max_hp) gs->player.hp = gs->player.max_hp;
                g_particles_heal(&gs->particles, gs->player.pos);
            }
            for (u32 i = 0; i < gs->num_squad; i++) {
                Unit *u = &gs->squad[i];
                if (!u->alive) continue;
                u->hp += u->max_hp * heal_frac;
                if (u->hp > u->max_hp) u->hp = u->max_hp;
                g_particles_heal(&gs->particles, u->pos);
            }
            break;
        }
        case ORB_EFFECT_MELEE_BOOST:
            gs->melee_boost_timer = 15.0f;
            break;
        case ORB_EFFECT_ARCHER_BOOST:
            gs->archer_boost_timer = 15.0f;
            break;
        case ORB_EFFECT_MAGE_BOOST:
            gs->mage_boost_timer = 15.0f;
            break;
        case ORB_EFFECT_ENVIRONMENTAL:
            gs->env_active_effect = gs->env_orb_effect;
            gs->env_tick_timer = 0.0f;
            get_camera_view_bounds(gs, &gs->env_view_min_x, &gs->env_view_max_x,
                                   &gs->env_view_min_y, &gs->env_view_max_y);
            if (gs->env_active_effect == ENV_EFFECT_WAVE) {
                gs->env_effect_timer = 5.0f;
                gs->env_wave_dir = (game_rand(gs) & 1u) ? 1.0f : -1.0f;
                gs->env_wave_front = (gs->env_wave_dir > 0.0f) ? gs->env_view_min_x : gs->env_view_max_x;
            } else if (gs->env_active_effect == ENV_EFFECT_LAVA) {
                gs->env_effect_timer = 8.0f;
                gs->env_lava_radius = 0.05f;
                gs->env_lava_pos = orb->pos;
                for (u32 i = 0; i < 20; i++) {
                    Vec2 p = {
                        gs->env_view_min_x + game_rand01(gs) * (gs->env_view_max_x - gs->env_view_min_x),
                        gs->env_view_min_y + game_rand01(gs) * (gs->env_view_max_y - gs->env_view_min_y)
                    };
                    if (!g_terrain_is_water(tg, graph, p)) {
                        gs->env_lava_pos = p;
                        break;
                    }
                }
            } else if (gs->env_active_effect == ENV_EFFECT_BOULDERS) {
                gs->env_effect_timer = 6.0f;
                gs->env_boulder_visible = false;
                gs->env_boulder_anim = 0.0f;
            }
            break;
        default:
            break;
    }
}

static void setup_boss_enemy(GameState *gs, u32 level, u32 total_upgrades) {
    gs->boss_spawned = false;
    if (gs->num_enemies < MAX_ENEMIES) {
        gs->boss_enemy_index = gs->num_enemies;
        gs->num_enemies++;
    } else {
        // Fallback: reuse last enemy slot if camps filled all slots.
        gs->boss_enemy_index = MAX_ENEMIES - 1;
    }
    Unit *boss = &gs->enemies[gs->boss_enemy_index];
    g_unit_init_enemy(boss, ROLE_ENEMY_MELEE, level, total_upgrades);
    boss->is_boss = true;
    boss->alive = false; // summoned after orb objective
    boss->state = STATE_IDLE;
    boss->radius = 0.02f;
    boss->max_hp *= 8.0f;
    boss->hp = boss->max_hp;
    boss->damage *= 2.2f;
    boss->speed *= 0.9f;
    boss->cooldown *= 0.9f;
    boss->attack_range = 0.026f;
    boss->armor += 8.0f;
    boss->color[0] = 70;
    boss->color[1] = 25;
    boss->color[2] = 25;
    boss->color[3] = 255;
    boss->pos = gs->env_peak_pos;
    boss->vel = (Vec2){0.0f, 0.0f};
}

static void update_environment_effect(GameState *gs, const TerrainGrid *tg,
                                      const MapGraph *graph, f32 dt) {
    (void)tg;
    (void)graph;
    bool use_physics = g_physics_is_active(gs);
    if (gs->env_active_effect == ENV_EFFECT_NONE || gs->env_effect_timer <= 0.0f) {
        gs->env_active_effect = ENV_EFFECT_NONE;
        gs->env_effect_timer = 0.0f;
        return;
    }

    gs->env_effect_timer -= dt;
    gs->env_tick_timer -= dt;
    if (gs->env_boulder_visible) {
        gs->env_boulder_anim += dt * 1.8f;
        if (gs->env_boulder_anim >= 1.0f) {
            gs->env_boulder_anim = 1.0f;
            gs->env_boulder_visible = false;
        }
    }

    if (gs->env_active_effect == ENV_EFFECT_BOULDERS) {
        if (gs->env_tick_timer <= 0.0f) {
            gs->env_tick_timer = 0.55f;
            u32 ally_kind = 0, ally_idx = 0;
            Unit *ally = pick_random_alive_ally(gs, &ally_kind, &ally_idx);
            if (ally) {
                f32 jx = (game_rand01(gs) - 0.5f) * 0.02f;
                f32 jy = (game_rand01(gs) - 0.5f) * 0.02f;
                Vec2 impact = {ally->pos.x + jx, ally->pos.y + jy};
                if (impact.x < gs->env_view_min_x) impact.x = gs->env_view_min_x;
                if (impact.x > gs->env_view_max_x) impact.x = gs->env_view_max_x;
                if (impact.y < gs->env_view_min_y) impact.y = gs->env_view_min_y;
                if (impact.y > gs->env_view_max_y) impact.y = gs->env_view_max_y;

                gs->env_boulder_from = (Vec2){
                    impact.x + (game_rand01(gs) - 0.5f) * 0.05f,
                    gs->env_view_min_y - 0.03f
                };
                gs->env_boulder_to = impact;
                gs->env_boulder_anim = 0.0f;
                gs->env_boulder_visible = true;

                static const u8 rock_color[4] = {155, 145, 130, 255};
                g_particles_burst(&gs->particles, impact, 10, rock_color);
                g_combat_deal_damage(ally, 16.0f, false);
                ally->slow_timer = 0.8f;
                camera_shake(&gs->camera, 0.25f, 0.006f);

                Vec2 away = vec2_normalize(vec2_sub(impact, gs->env_boulder_from));
                if (use_physics) {
                    Vec2 impulse = vec2_scale(away, 0.055f);
                    if (ally_kind == 0) g_physics_apply_player_impulse(gs, impulse);
                    else g_physics_apply_squad_impulse(gs, ally_idx, impulse);
                } else {
                    ally->vel = vec2_add(ally->vel, vec2_scale(away, 0.10f));
                }
            }
        }
    } else if (gs->env_active_effect == ENV_EFFECT_WAVE) {
        gs->env_wave_front += gs->env_wave_dir * dt * 0.35f;
        if (gs->env_tick_timer <= 0.0f) {
            gs->env_tick_timer = 0.12f;
            f32 band = 0.03f;
            if (gs->player.alive &&
                gs->player.pos.y >= gs->env_view_min_y && gs->player.pos.y <= gs->env_view_max_y &&
                fabsf(gs->player.pos.x - gs->env_wave_front) < band) {
                if (use_physics) {
                    g_physics_apply_player_impulse(gs, (Vec2){gs->env_wave_dir * 0.040f, 0.0f});
                } else {
                    gs->player.vel.x += gs->env_wave_dir * 0.10f;
                }
                gs->player.slow_timer = 0.7f;
                g_combat_deal_damage(&gs->player, 4.0f, true);
                camera_shake(&gs->camera, 0.15f, 0.004f);
            }
            for (u32 i = 0; i < gs->num_squad; i++) {
                Unit *u = &gs->squad[i];
                if (!u->alive) continue;
                if (u->pos.y >= gs->env_view_min_y && u->pos.y <= gs->env_view_max_y &&
                    fabsf(u->pos.x - gs->env_wave_front) < band) {
                    if (use_physics) {
                        g_physics_apply_squad_impulse(gs, i, (Vec2){gs->env_wave_dir * 0.040f, 0.0f});
                    } else {
                        u->vel.x += gs->env_wave_dir * 0.10f;
                    }
                    u->slow_timer = 0.7f;
                    g_combat_deal_damage(u, 4.0f, true);
                }
            }
        }
        if ((gs->env_wave_dir > 0.0f && gs->env_wave_front > gs->env_view_max_x + 0.04f) ||
            (gs->env_wave_dir < 0.0f && gs->env_wave_front < gs->env_view_min_x - 0.04f)) {
            gs->env_effect_timer = 0.0f;
        }
    } else if (gs->env_active_effect == ENV_EFFECT_LAVA) {
        if (gs->env_tick_timer <= 0.0f) {
            gs->env_tick_timer = 0.30f;
            static const u8 lava_color[4] = {255, 110, 30, 255};
            g_particles_burst(&gs->particles, gs->env_lava_pos, 6, lava_color);
            if (gs->player.alive && vec2_dist(gs->player.pos, gs->env_lava_pos) < gs->env_lava_radius) {
                g_combat_deal_damage(&gs->player, 7.0f, true);
                gs->player.slow_timer = 0.6f;
                camera_shake(&gs->camera, 0.15f, 0.005f);
                Vec2 push = vec2_normalize(vec2_sub(gs->player.pos, gs->env_lava_pos));
                if (vec2_len(push) < 1e-5f) push = (Vec2){1.0f, 0.0f};
                if (use_physics) {
                    g_physics_apply_player_impulse(gs, vec2_scale(push, 0.030f));
                } else {
                    gs->player.vel = vec2_add(gs->player.vel, vec2_scale(push, 0.07f));
                }
            }
            for (u32 i = 0; i < gs->num_squad; i++) {
                Unit *u = &gs->squad[i];
                if (!u->alive) continue;
                if (vec2_dist(u->pos, gs->env_lava_pos) < gs->env_lava_radius) {
                    g_combat_deal_damage(u, 7.0f, true);
                    u->slow_timer = 0.6f;
                    Vec2 push = vec2_normalize(vec2_sub(u->pos, gs->env_lava_pos));
                    if (vec2_len(push) < 1e-5f) push = (Vec2){1.0f, 0.0f};
                    if (use_physics) {
                        g_physics_apply_squad_impulse(gs, i, vec2_scale(push, 0.030f));
                    } else {
                        u->vel = vec2_add(u->vel, vec2_scale(push, 0.07f));
                    }
                }
            }
        }
    }

    // Sustained low rumble while any environmental effect is active
    if (gs->env_active_effect != ENV_EFFECT_NONE && gs->env_effect_timer > 0.0f) {
        camera_shake(&gs->camera, 0.1f, 0.001f);
    }

    if (gs->env_effect_timer <= 0.0f) {
        gs->env_active_effect = ENV_EFFECT_NONE;
        gs->env_effect_timer = 0.0f;
        gs->env_boulder_visible = false;
    }
}

void g_game_init(Game *game, SDL_Renderer *renderer, const MapGraph *graph,
                 u32 level, const u32 stat_levels[][4]) {
    // Free old terrain if reinitializing
    if (game->state.terrain_ready) {
        g_terrain_free(&game->terrain);
    }

    // Destroy old textures before zeroing state
    g_physics_shutdown(&game->state);
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
    game->state.is_boss_level = ((level + 1) % 5) == 0;

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
    u32 rng_state = (u32)(size_t)graph ^ 0xDEADBEEF ^ (level * 2654435761u);
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
    OrbEffect orb_effects[NUM_COLLECT_ORBS] = {
        ORB_EFFECT_HEAL_BOOST,
        ORB_EFFECT_MELEE_BOOST,
        ORB_EFFECT_ARCHER_BOOST,
        ORB_EFFECT_MAGE_BOOST,
        ORB_EFFECT_ENVIRONMENTAL
    };
    for (u32 i = 0; i < NUM_COLLECT_ORBS; i++) {
        u32 j = i + (xorshift32(&rng_state) % (NUM_COLLECT_ORBS - i));
        OrbEffect tmp = orb_effects[i];
        orb_effects[i] = orb_effects[j];
        orb_effects[j] = tmp;
    }

    game->state.env_orb_effect = (EnvironmentalEffect)(ENV_EFFECT_BOULDERS + (xorshift32(&rng_state) % 3));
    game->state.env_active_effect = ENV_EFFECT_NONE;
    game->state.melee_boost_timer = 0.0f;
    game->state.archer_boost_timer = 0.0f;
    game->state.mage_boost_timer = 0.0f;
    game->state.effect_rng = xorshift32(&rng_state);
    if (game->state.effect_rng == 0) game->state.effect_rng = 1;

    // Cache a high-elevation point for the boulder effect.
    game->state.env_peak_pos = (Vec2){0.5f, 0.5f};
    f32 best_peak = -1.0f;
    for (u32 i = 0; i < graph->num_centers; i++) {
        if (graph->centers[i].water) continue;
        if (graph->centers[i].elevation > best_peak) {
            best_peak = graph->centers[i].elevation;
            game->state.env_peak_pos = graph->centers[i].pos;
        }
    }

    g_enemy_place_camps(&game->state, &game->terrain, graph, level, total_upgrades);
    if (game->state.is_boss_level) {
        setup_boss_enemy(&game->state, level, total_upgrades);
    }

    for (u32 i = 0; i < orb_count; i++) {
        Orb *orb = &game->state.orbs[i];
        orb->active = true;
        orb->pos = graph->centers[land_indices[i]].pos;
        orb->radius = 0.004f;
        orb->pulse_timer = (f32)i * 1.2f; // offset phase per orb
        orb->effect = orb_effects[i];
    }

    // Pre-select portal spawn position (last shuffled cell)
    u32 portal_idx = total_pick > NUM_COLLECT_ORBS ? NUM_COLLECT_ORBS : 0;
    game->state.portal.spawn_pos = graph->centers[land_indices[portal_idx]].pos;
    game->state.portal.pos = game->state.portal.spawn_pos;
    game->state.portal.radius_x = 0.006f;
    game->state.portal.radius_y = 0.012f;

    free(land_indices);

    g_physics_init(&game->state);
}

static void update_squad(GameState *gs, const TerrainGrid *tg, const MapGraph *graph, f32 dt) {
    bool use_physics = g_physics_is_active(gs);
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

            // Smooth velocity transitions to reduce follower choppiness.
            Vec2 desired_vel = vec2_scale(dir, speed);
            f32 vel_lerp_t = 1.0f - expf(-12.0f * dt);
            u->vel = vec2_add(u->vel, vec2_scale(vec2_sub(desired_vel, u->vel), vel_lerp_t));

            Vec2 old_pos = u->pos;
            Vec2 new_pos = vec2_add(u->pos, vec2_scale(u->vel, dt));
            new_pos = g_unit_move_with_terrain(u->pos, new_pos, tg, graph, water_blocks);
            if (dt > 1e-6f)
                u->vel = vec2_scale(vec2_sub(new_pos, old_pos), 1.0f / dt);
            else
                u->vel = (Vec2){0.0f, 0.0f};
            if (!use_physics) u->pos = new_pos;
        } else {
            // Ease out residual movement instead of snapping to zero.
            f32 damp_t = 1.0f - expf(-10.0f * dt);
            u->vel = vec2_add(u->vel, vec2_scale(vec2_scale(u->vel, -1.0f), damp_t));
            if (vec2_len(u->vel) < 1e-4f) u->vel = (Vec2){0.0f, 0.0f};
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

static void update_river_damage(GameState *gs, const Map *map, const TerrainGrid *tg,
                                const MapGraph *graph, f32 dt) {
    if (!map->lava_rivers) return;

    gs->river_damage_timer -= dt;
    if (gs->river_damage_timer > 0.0f) return;
    gs->river_damage_timer = 0.5f;

    static const u8 lava_color[4] = {255, 130, 40, 255};
    (void)tg;
    (void)graph;

    // Player
    if (gs->player.alive && g_terrain_get_river(map, gs->player.pos)) {
        g_combat_deal_damage(&gs->player, 5.0f, true);
        gs->player.slow_timer = 0.3f;
        g_particles_burst(&gs->particles, gs->player.pos, 4, lava_color);
    }

    // Squad
    for (u32 i = 0; i < gs->num_squad; i++) {
        Unit *u = &gs->squad[i];
        if (!u->alive) continue;
        if (g_terrain_get_river(map, u->pos)) {
            g_combat_deal_damage(u, 5.0f, true);
            u->slow_timer = 0.3f;
            g_particles_burst(&gs->particles, u->pos, 4, lava_color);
        }
    }

    // Enemies
    for (u32 i = 0; i < gs->num_enemies; i++) {
        Unit *e = &gs->enemies[i];
        if (!e->alive) continue;
        if (g_terrain_get_river(map, e->pos)) {
            g_combat_deal_damage(e, 5.0f, true);
            e->slow_timer = 0.3f;
            g_particles_burst(&gs->particles, e->pos, 4, lava_color);
        }
    }
}

void g_game_update(Game *game, const Map *map, f64 dt) {
    const MapGraph *graph = &map->graph;
    GameState *gs = &game->state;
    TerrainGrid *tg = &game->terrain;
    f32 fdt = (f32)dt;
    bool use_physics = g_physics_is_active(gs);

    // Tick player status timers
    if (gs->player.slow_timer > 0.0f) gs->player.slow_timer -= fdt;
    if (gs->player.speed_boost_timer > 0.0f) gs->player.speed_boost_timer -= fdt;
    if (gs->melee_boost_timer > 0.0f) gs->melee_boost_timer -= fdt;
    if (gs->archer_boost_timer > 0.0f) gs->archer_boost_timer -= fdt;
    if (gs->mage_boost_timer > 0.0f) gs->mage_boost_timer -= fdt;

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
            Vec2 old_pos = gs->player.pos;
            Vec2 new_pos = vec2_add(gs->player.pos, vec2_scale(dir, speed * fdt));
            new_pos = g_unit_move_with_terrain(gs->player.pos, new_pos, tg, graph, water_blocks);
            if (fdt > 1e-6f)
                gs->player.vel = vec2_scale(vec2_sub(new_pos, old_pos), 1.0f / fdt);
            else
                gs->player.vel = (Vec2){0.0f, 0.0f};
            if (!use_physics) gs->player.pos = new_pos;
        } else {
            gs->player.vel = (Vec2){0.0f, 0.0f};
        }
    } else {
        gs->player.vel = (Vec2){0.0f, 0.0f};
    }

    // Update squad boid steering
    update_squad(gs, tg, graph, fdt);

    // Enemy AI movement intent
    g_enemy_update(gs, tg, graph, fdt);

    // Physics integration and sensor events
    g_physics_step(gs, fdt);

    // Apply stance auras (bonus armor etc.) before combat
    apply_stance_auras(gs);

    // Combat
    g_combat_update_squad_states(gs);
    g_combat_update(gs, tg, graph, fdt);
    g_combat_update_projectiles(gs, fdt);
    update_environment_effect(gs, tg, graph, fdt);
    update_river_damage(gs, map, tg, graph, fdt);
    g_particles_update(&gs->particles, fdt);

    // Orb collection
    for (u32 i = 0; i < gs->num_orbs; i++) {
        Orb *orb = &gs->orbs[i];
        if (!orb->active) continue;
        orb->pulse_timer += fdt;
        bool picked = false;
        if (use_physics) {
            picked = g_physics_consume_orb_collected(gs, i);
        } else {
            picked = vec2_dist(gs->player.pos, orb->pos) < gs->player.radius + orb->radius;
        }
        if (picked) {
            apply_orb_effect(gs, tg, graph, orb);
            orb->active = false;
            gs->orbs_collected++;
            g_audio_play(SFX_ORB_PICKUP);
        }
    }

    // Portal spawn when all orbs collected
    if (gs->is_boss_level && gs->orbs_collected == gs->num_orbs && !gs->boss_spawned) {
        Unit *boss = &gs->enemies[gs->boss_enemy_index];
        boss->alive = true;
        boss->state = STATE_ATTACK;
        boss->hp = boss->max_hp;
        boss->pos = gs->env_peak_pos;
        boss->vel = (Vec2){0.0f, 0.0f};
        gs->boss_spawned = true;
        g_physics_teleport_enemy(gs, gs->boss_enemy_index);
    }

    if (gs->is_boss_level && gs->boss_spawned &&
        !gs->enemies[gs->boss_enemy_index].alive && !gs->portal.active) {
        gs->portal.active = true;
        gs->portal.pos = gs->portal.spawn_pos;
        gs->portal.radius_x = 0.006f;
        gs->portal.radius_y = 0.012f;
        gs->portal.pulse_timer = 0.0f;
        g_physics_update_portal_sensor(gs);
        g_audio_play(SFX_PORTAL_OPEN);
    } else if (!gs->is_boss_level && gs->orbs_collected == gs->num_orbs && !gs->portal.active) {
        gs->portal.active = true;
        gs->portal.pos = gs->portal.spawn_pos;
        gs->portal.radius_x = 0.006f;
        gs->portal.radius_y = 0.012f;
        gs->portal.pulse_timer = 0.0f;
        g_physics_update_portal_sensor(gs);
        g_audio_play(SFX_PORTAL_OPEN);
    }

    // Portal enter (ellipse collision using normalized distance)
    if (gs->portal.active) {
        gs->portal.pulse_timer += fdt;
        bool entered = false;
        if (use_physics) {
            entered = g_physics_consume_portal_entered(gs);
        } else {
            Vec2 d = vec2_sub(gs->player.pos, gs->portal.pos);
            f32 nx = d.x / gs->portal.radius_x;
            f32 ny = d.y / gs->portal.radius_y;
            entered = nx * nx + ny * ny < 1.0f;
        }
        if (entered) {
            gs->level_complete = true;
            g_audio_play(SFX_LEVEL_COMPLETE);
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

    // Camera shake update
    if (cam->shake_timer > 0.0f) {
        cam->shake_timer -= fdt;
        f32 decay = cam->shake_timer > 0.0f ? cam->shake_timer / (cam->shake_timer + fdt) : 0.0f;
        f32 amp = cam->shake_intensity * decay;
        cam->shake_offset.x = (game_rand01(gs) * 2.0f - 1.0f) * amp;
        cam->shake_offset.y = (game_rand01(gs) * 2.0f - 1.0f) * amp;
        if (cam->shake_timer <= 0.0f) {
            cam->shake_timer = 0.0f;
            cam->shake_offset = (Vec2){0.0f, 0.0f};
        }
    } else {
        cam->shake_offset = (Vec2){0.0f, 0.0f};
    }
}

void g_game_render(Game *game, SDL_Renderer *renderer, SDL_FRect map_rect) {
    g_render_game(&game->state, renderer, map_rect, game->state.role_textures);
}

void g_game_shutdown(Game *game) {
    g_physics_shutdown(&game->state);
    g_unit_gen_destroy(game->state.role_textures);
    if (game->state.orb_texture) SDL_DestroyTexture(game->state.orb_texture);
    if (game->state.portal_texture) SDL_DestroyTexture(game->state.portal_texture);
    if (game->state.terrain_ready) {
        g_terrain_free(&game->terrain);
        game->state.terrain_ready = false;
    }
}
