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
