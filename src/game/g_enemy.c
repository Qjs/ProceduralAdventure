#include "g_enemy.h"
#include "g_unit.h"

static u32 enemy_xorshift(u32 *state) {
    u32 x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

void g_enemy_place_camps(GameState *gs, const TerrainGrid *tg, const MapGraph *graph,
                         u32 level, u32 total_upgrades) {
    gs->num_camps = 0;
    gs->num_enemies = 0;

    // Collect valid camp candidate centers
    u32 candidates[512];
    u32 num_candidates = 0;

    // Camps spawn in the rocky/mountain transition zone (foothills).
    // A cell qualifies if it's land, in the upper-mid elevation band,
    // and has at least one neighbor at lower elevation (edge of highlands).
    f32 rocky_lo = 0.35f;  // lower bound — mid-green/brown
    f32 rocky_hi = 0.70f;  // upper bound — below snow line

    for (u32 i = 0; i < graph->num_centers && num_candidates < 512; i++) {
        const Center *c = &graph->centers[i];
        if (c->water || c->border) continue;
        if (c->elevation < rocky_lo || c->elevation > rocky_hi) continue;

        // Must neighbor a lower-elevation cell (camp sits on highland edge)
        bool near_low = false;
        for (u32 n = 0; n < c->num_neighbors; n++) {
            if (graph->centers[c->neighbors[n]].elevation < rocky_lo &&
                !graph->centers[c->neighbors[n]].water) {
                near_low = true;
                break;
            }
        }
        if (!near_low) continue;

        // Min spacing from player spawn (map center)
        f32 dist_center = vec2_dist(c->pos, (Vec2){0.5f, 0.5f});
        if (dist_center < 0.12f) continue;

        candidates[num_candidates++] = i;
    }

    // Shuffle and pick with min spacing enforcement
    u32 rng = (u32)(size_t)graph ^ 0xCAFEBABE;
    if (rng == 0) rng = 1;

    for (u32 i = 0; i < num_candidates; i++) {
        u32 j = i + (enemy_xorshift(&rng) % (num_candidates - i));
        u32 tmp = candidates[i];
        candidates[i] = candidates[j];
        candidates[j] = tmp;
    }

    Vec2 camp_positions[MAX_CAMPS];
    u32 placed = 0;

    for (u32 i = 0; i < num_candidates && placed < MAX_CAMPS; i++) {
        Vec2 pos = graph->centers[candidates[i]].pos;

        // Enforce min spacing between camps
        bool too_close = false;
        for (u32 j = 0; j < placed; j++) {
            if (vec2_dist(pos, camp_positions[j]) < 0.15f) {
                too_close = true;
                break;
            }
        }
        if (too_close) continue;

        camp_positions[placed] = pos;

        EnemyCamp *camp = &gs->camps[placed];
        camp->pos = pos;
        camp->activation_radius = 0.12f;
        camp->spawn_radius = 0.04f;
        camp->activated = false;
        camp->num_enemies = 0;
        camp->num_alive = 0;

        // Spawn enemies per camp, scaling with level
        u32 count = 3 + level / 2;
        if (count > 8) count = 8;

        for (u32 e = 0; e < count && gs->num_enemies < MAX_ENEMIES; e++) {
            u32 eid = gs->num_enemies;
            Unit *unit = &gs->enemies[eid];

            // Mix: ~60% melee, ~40% ranged
            UnitRole role = (enemy_xorshift(&rng) % 5 < 3) ? ROLE_ENEMY_MELEE : ROLE_ENEMY_RANGED;
            g_unit_init_enemy(unit, role, level, total_upgrades);

            // Scatter around camp center
            f32 ox = ((f32)(enemy_xorshift(&rng) % 1000) / 1000.0f - 0.5f) * camp->spawn_radius * 2.0f;
            f32 oy = ((f32)(enemy_xorshift(&rng) % 1000) / 1000.0f - 0.5f) * camp->spawn_radius * 2.0f;
            Vec2 spawn = {pos.x + ox, pos.y + oy};
            unit->pos = g_unit_move_with_terrain(pos, spawn, tg, graph, true);

            camp->enemy_ids[camp->num_enemies] = eid;
            camp->num_enemies++;
            camp->num_alive++;
            gs->num_enemies++;
        }

        placed++;
    }

    gs->num_camps = placed;
}

void g_enemy_update(GameState *gs, const TerrainGrid *tg, const MapGraph *graph, f32 dt) {
    // Camp activation
    for (u32 c = 0; c < gs->num_camps; c++) {
        EnemyCamp *camp = &gs->camps[c];

        // Update alive count
        camp->num_alive = 0;
        for (u32 i = 0; i < camp->num_enemies; i++) {
            if (gs->enemies[camp->enemy_ids[i]].alive)
                camp->num_alive++;
        }

        if (camp->num_alive == 0) continue;

        f32 dist = vec2_dist(gs->player.pos, camp->pos);

        if (!camp->activated && dist < camp->activation_radius) {
            camp->activated = true;
            for (u32 i = 0; i < camp->num_enemies; i++) {
                Unit *e = &gs->enemies[camp->enemy_ids[i]];
                if (e->alive) e->state = STATE_ATTACK;
            }
        }

        // Deactivate if player moves far away
        if (camp->activated && dist > camp->activation_radius * 2.5f) {
            camp->activated = false;
            for (u32 i = 0; i < camp->num_enemies; i++) {
                Unit *e = &gs->enemies[camp->enemy_ids[i]];
                if (e->alive) e->state = STATE_IDLE;
            }
        }
    }

    // Enemy AI tick
    for (u32 i = 0; i < gs->num_enemies; i++) {
        Unit *e = &gs->enemies[i];
        if (!e->alive || e->state == STATE_DEAD) continue;

        // Tick slow timer
        if (e->slow_timer > 0.0f) e->slow_timer -= dt;

        if (e->state == STATE_IDLE) continue;

        // Find nearest player-team unit
        u32 target = g_unit_find_nearest_enemy(gs, e);
        if (target == UINT32_MAX) continue;

        // Get target position
        Vec2 target_pos;
        if (target == 0) {
            target_pos = gs->player.pos;
        } else {
            target_pos = gs->squad[target - 1].pos;
        }

        e->target_id = target;
        f32 dist_target = vec2_dist(e->pos, target_pos);

        // Leash check: find which camp this enemy belongs to
        Vec2 camp_pos = e->pos; // fallback
        f32 leash_dist = 0.0f;
        for (u32 c = 0; c < gs->num_camps; c++) {
            for (u32 j = 0; j < gs->camps[c].num_enemies; j++) {
                if (gs->camps[c].enemy_ids[j] == i) {
                    camp_pos = gs->camps[c].pos;
                    leash_dist = gs->camps[c].spawn_radius * 3.0f;
                    goto found_camp;
                }
            }
        }
        found_camp:;

        Vec2 move_target;
        f32 dist_from_camp = vec2_dist(e->pos, camp_pos);

        if (leash_dist > 0.0f && dist_from_camp > leash_dist) {
            // Leash back toward camp
            move_target = camp_pos;
        } else if (dist_target > e->attack_range) {
            // Move toward target
            move_target = target_pos;
        } else {
            continue; // In range, combat system handles attacking
        }

        Vec2 dir = vec2_normalize(vec2_sub(move_target, e->pos));
        f32 target_facing = atan2f(dir.y, dir.x);
        f32 lerp_t = 1.0f - expf(-10.0f * dt);
        e->facing = angle_lerp(e->facing, target_facing, lerp_t);
        f32 elev = g_terrain_get_elevation(tg, graph, e->pos);
        f32 speed = e->speed * (1.0f - elev * 0.5f);
        if (e->slow_timer > 0.0f) speed *= 0.5f;
        if (speed < 0.01f) speed = 0.01f;

        Vec2 new_pos = vec2_add(e->pos, vec2_scale(dir, speed * dt));
        e->pos = g_unit_move_with_terrain(e->pos, new_pos, tg, graph, true);
    }
}
