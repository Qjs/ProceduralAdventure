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

void g_unit_init_squad(GameState *gs, const TerrainGrid *tg, const MapGraph *graph) {
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
        u->attack_range = 0.015f;
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

        // Spawn near player
        Vec2 spawn = vec2_add(gs->player.pos, offsets[i]);
        u->pos = g_unit_move_with_terrain(gs->player.pos, spawn, tg, graph, true);
    }
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
