#include "g_unit.h"

void g_unit_init_player(Unit *unit, const TerrainGrid *tg, const MapGraph *graph) {
    *unit = (Unit){0};
    unit->alive = true;
    unit->role = ROLE_PLAYER;
    unit->team = TEAM_PLAYER;
    unit->state = STATE_IDLE;
    unit->speed = 0.08f;
    unit->hp = 150.0f;
    unit->max_hp = 150.0f;
    unit->damage = 20.0f;
    unit->attack_range = 0.02f;
    unit->cooldown = 0.5f;
    unit->cooldown_timer = 0.0f;
    unit->radius = 0.006f;
    unit->color[0] = 255;
    unit->color[1] = 255;
    unit->color[2] = 255;
    unit->color[3] = 255;

    // Spawn at map center; find nearest land cell
    Vec2 center = {0.5f, 0.5f};
    u32 cell = g_terrain_find_cell(tg, graph, center);

    // If center cell is water, search outward for land
    if (graph->centers[cell].water) {
        f32 best_dist = 1e30f;
        for (u32 i = 0; i < graph->num_centers; i++) {
            if (graph->centers[i].water) continue;
            f32 d = vec2_dist(graph->centers[i].pos, center);
            if (d < best_dist) {
                best_dist = d;
                cell = i;
            }
        }
    }

    unit->pos = graph->centers[cell].pos;
}

void g_unit_init_squad(GameState *gs, const TerrainGrid *tg, const MapGraph *graph,
                       const u32 stat_levels[][4]) {
    typedef struct {
        UnitRole role;
        u8 r, g, b;
        f32 speed;
        f32 hp;
    } SquadDef;

    static const SquadDef defs[MAX_SQUAD] = {
        { ROLE_MELEE,  220, 60,  60,  0.075f, 120.0f },
        { ROLE_ARCHER,  60, 180, 60,  0.08f,   80.0f },
        { ROLE_HEALER, 240, 220, 60,  0.07f,   90.0f },
        { ROLE_MAGE,    80, 120, 240, 0.065f,  70.0f },
    };

    static const Vec2 offsets[MAX_SQUAD] = {
        { -0.01f, -0.01f },
        {  0.01f, -0.01f },
        { -0.01f,  0.01f },
        {  0.01f,  0.01f },
    };

    gs->num_squad = MAX_SQUAD;

    for (u32 i = 0; i < MAX_SQUAD; i++) {
        Unit *u = &gs->squad[i];
        *u = (Unit){0};
        u->alive = true;
        u->role = defs[i].role;
        u->team = TEAM_PLAYER;
        u->state = STATE_FOLLOW;
        u->speed = defs[i].speed;
        u->hp = defs[i].hp;
        u->max_hp = defs[i].hp;
        u->damage = 10.0f;
        u->attack_range = (defs[i].role == ROLE_HEALER) ? 0.08f
                        : (defs[i].role == ROLE_ARCHER) ? 0.07f
                        : (defs[i].role == ROLE_MAGE)   ? 0.06f
                        : 0.015f;
        u->cooldown = 0.8f;
        u->cooldown_timer = 0.0f;
        u->radius = 0.005f;
        u->color[0] = defs[i].r;
        u->color[1] = defs[i].g;
        u->color[2] = defs[i].b;
        u->color[3] = 255;

        // Default boid weights
        u->weights = (BoidWeights){
            .follow_player = 1.0f,
            .separation = 1.8f,
            .cohesion = 0.2f,
            .avoid_water = 0.0f,
            .seek_target = 0.0f,
            .flee_target = 0.0f,
            .preferred_dist = 0.10f,
            .separation_radius = 0.02f,
        };

        // Apply upgrade multipliers
        if (stat_levels) {
            u->max_hp   *= (1.0f + 0.10f * stat_levels[i][0]);
            u->hp        = u->max_hp;
            u->damage   *= (1.0f + 0.10f * stat_levels[i][1]);
            u->attack_range *= (1.0f + 0.08f * stat_levels[i][2]);
            u->cooldown *= (1.0f - 0.05f * stat_levels[i][3]);
        }

        // Spawn near player
        Vec2 spawn = vec2_add(gs->player.pos, offsets[i]);
        u->pos = g_unit_move_with_terrain(gs->player.pos, spawn, tg, graph, true);
    }
}

void g_unit_init_enemy(Unit *unit, UnitRole role, u32 level, u32 total_upgrades) {
    *unit = (Unit){0};
    unit->alive = true;
    unit->role = role;
    unit->team = TEAM_ENEMY;
    unit->state = STATE_IDLE;
    unit->radius = 0.005f;
    unit->target_id = UINT32_MAX;

    if (role == ROLE_ENEMY_MELEE) {
        unit->hp = 60.0f;
        unit->max_hp = 60.0f;
        unit->speed = 0.05f;
        unit->damage = 10.0f;
        unit->attack_range = 0.015f;
        unit->cooldown = 1.0f;
        unit->color[0] = 180; unit->color[1] = 40; unit->color[2] = 40; unit->color[3] = 255;
    } else {
        // ROLE_ENEMY_RANGED
        unit->hp = 40.0f;
        unit->max_hp = 40.0f;
        unit->speed = 0.04f;
        unit->damage = 8.0f;
        unit->attack_range = 0.06f;
        unit->cooldown = 1.5f;
        unit->color[0] = 120; unit->color[1] = 40; unit->color[2] = 160; unit->color[3] = 255;
    }

    // Difficulty scaling by level + player upgrades (+2% per upgrade purchased)
    f32 hp_scale  = 1.0f + level * 0.15f + total_upgrades * 0.02f;
    f32 dmg_scale = 1.0f + level * 0.10f + total_upgrades * 0.02f;
    unit->hp     *= hp_scale;
    unit->max_hp *= hp_scale;
    unit->damage *= dmg_scale;
}

u32 g_unit_find_nearest_enemy(GameState *gs, const Unit *u) {
    f32 best_dist = 1e30f;
    u32 best_id = UINT32_MAX;

    if (u->team == TEAM_PLAYER) {
        // Find nearest enemy
        for (u32 i = 0; i < gs->num_enemies; i++) {
            if (!gs->enemies[i].alive) continue;
            f32 d = vec2_dist(u->pos, gs->enemies[i].pos);
            if (d < best_dist) {
                best_dist = d;
                best_id = i;
            }
        }
    } else {
        // Find nearest player-team unit: 0=player, 1+=squad
        if (gs->player.alive) {
            best_dist = vec2_dist(u->pos, gs->player.pos);
            best_id = 0;
        }
        for (u32 i = 0; i < gs->num_squad; i++) {
            if (!gs->squad[i].alive) continue;
            f32 d = vec2_dist(u->pos, gs->squad[i].pos);
            if (d < best_dist) {
                best_dist = d;
                best_id = i + 1;
            }
        }
    }

    return best_id;
}

Vec2 g_unit_move_with_terrain(Vec2 old_pos, Vec2 new_pos,
    const TerrainGrid *tg, const MapGraph *graph, bool water_blocks)
{
    // Clamp to map bounds
    if (new_pos.x < 0.0f) new_pos.x = 0.0f;
    if (new_pos.x > 1.0f) new_pos.x = 1.0f;
    if (new_pos.y < 0.0f) new_pos.y = 0.0f;
    if (new_pos.y > 1.0f) new_pos.y = 1.0f;

    // Block movement into water with axis-sliding
    if (water_blocks && g_terrain_is_water(tg, graph, new_pos)) {
        Vec2 try_x = { new_pos.x, old_pos.y };
        Vec2 try_y = { old_pos.x, new_pos.y };

        if (!g_terrain_is_water(tg, graph, try_x))
            new_pos = try_x;
        else if (!g_terrain_is_water(tg, graph, try_y))
            new_pos = try_y;
        else
            new_pos = old_pos;
    }

    return new_pos;
}
