#include "g_combat.h"
#include "g_unit.h"
#include "g_terrain.h"

void g_combat_deal_damage(Unit *target, f32 damage) {
    if (!target->alive) return;
    target->hp -= damage;
    if (target->hp <= 0.0f) {
        target->hp = 0.0f;
        target->alive = false;
        target->state = STATE_DEAD;
    }
}

void g_combat_spawn_projectile(GameState *gs, Vec2 from, Vec2 to,
                                f32 damage, Team source_team, const u8 color[4],
                                bool applies_slow) {
    if (gs->num_projectiles >= MAX_PROJECTILES) return;

    Projectile *p = &gs->projectiles[gs->num_projectiles++];
    p->active = true;
    p->pos = from;
    p->damage = damage;
    p->lifetime = 2.0f;
    p->source_team = source_team;
    p->color[0] = color[0];
    p->color[1] = color[1];
    p->color[2] = color[2];
    p->color[3] = color[3];
    p->applies_slow = applies_slow;

    Vec2 dir = vec2_normalize(vec2_sub(to, from));
    f32 speed = 0.2f;
    p->vel = vec2_scale(dir, speed);
}

static void process_unit_attack(GameState *gs, const TerrainGrid *tg,
                                const MapGraph *graph, Unit *u, f32 dt) {
    if (!u->alive || u->state == STATE_DEAD || u->state == STATE_IDLE) return;

    u->cooldown_timer -= dt;
    if (u->cooldown_timer < 0.0f) u->cooldown_timer = 0.0f;

    if (u->state != STATE_ATTACK && u->state != STATE_HEAL) return;
    if (u->cooldown_timer > 0.0f) return;

    // Healer logic
    if (u->role == ROLE_HEALER && u->state == STATE_HEAL) {
        // Find most injured ally
        Unit *best = NULL;
        f32 best_frac = 1.0f;

        if (gs->player.alive && gs->player.hp / gs->player.max_hp < best_frac) {
            f32 d = vec2_dist(u->pos, gs->player.pos);
            if (d < u->attack_range) {
                best = &gs->player;
                best_frac = gs->player.hp / gs->player.max_hp;
            }
        }
        for (u32 i = 0; i < gs->num_squad; i++) {
            Unit *ally = &gs->squad[i];
            if (!ally->alive) continue;
            f32 frac = ally->hp / ally->max_hp;
            if (frac < best_frac && vec2_dist(u->pos, ally->pos) < u->attack_range) {
                best = ally;
                best_frac = frac;
            }
        }

        if (best && best_frac < 1.0f) {
            best->hp += u->damage; // healer "damage" is heal amount
            if (best->hp > best->max_hp) best->hp = best->max_hp;
            u->cooldown_timer = u->cooldown;
        }
        return;
    }

    // Mage passive stance: buff nearest ally speed instead of attacking
    if (u->role == ROLE_MAGE && u->team == TEAM_PLAYER &&
        gs->squad_stance == STANCE_PASSIVE) {
        Unit *nearest = NULL;
        f32 best_dist = 1e9f;
        for (u32 i = 0; i < gs->num_squad; i++) {
            Unit *ally = &gs->squad[i];
            if (!ally->alive || ally == u) continue;
            f32 d = vec2_dist(u->pos, ally->pos);
            if (d < best_dist) {
                best_dist = d;
                nearest = ally;
            }
        }
        // Also consider the player
        if (gs->player.alive) {
            f32 d = vec2_dist(u->pos, gs->player.pos);
            if (d < best_dist) {
                nearest = &gs->player;
            }
        }
        if (nearest) {
            nearest->speed_boost_timer = 3.0f;
            u->cooldown_timer = u->cooldown;
        }
        return;
    }

    // Find target
    u32 target_idx = g_unit_find_nearest_enemy(gs, u);
    if (target_idx == UINT32_MAX) return;

    Vec2 target_pos;
    Unit *target;

    if (u->team == TEAM_PLAYER) {
        // target_idx is enemy index
        target = &gs->enemies[target_idx];
        target_pos = target->pos;
    } else {
        // target_idx: 0=player, 1+=squad
        if (target_idx == 0) {
            target = &gs->player;
        } else {
            target = &gs->squad[target_idx - 1];
        }
        target_pos = target->pos;
    }

    if (!target->alive) return;

    // Archer elevation range bonus
    f32 effective_range = u->attack_range;
    if (u->role == ROLE_ARCHER) {
        f32 elev = g_terrain_get_elevation(tg, graph, u->pos);
        effective_range = u->attack_range * (1.0f + elev * 0.5f);
    }

    f32 dist = vec2_dist(u->pos, target_pos);
    if (dist > effective_range) return;

    // Attack!
    u->cooldown_timer = u->cooldown;

    if (u->role == ROLE_ARCHER || u->role == ROLE_MAGE || u->role == ROLE_ENEMY_RANGED) {
        f32 dmg = u->damage;
        bool slow = false;
        const u8 *proj_color = u->color;

        if (u->role == ROLE_MAGE && u->team == TEAM_PLAYER) {
            static const u8 fire_color[4]   = {255, 80, 20, 255};
            static const u8 freeze_color[4] = {80, 180, 255, 255};
            if (gs->squad_stance == STANCE_AGGRESSIVE) {
                dmg *= 1.5f;
                proj_color = fire_color;
            } else if (gs->squad_stance == STANCE_DEFENSIVE) {
                slow = true;
                proj_color = freeze_color;
            }
        }

        g_combat_spawn_projectile(gs, u->pos, target_pos, dmg, u->team, proj_color, slow);
    } else {
        g_combat_deal_damage(target, u->damage);
    }
}

void g_combat_update(GameState *gs, const TerrainGrid *tg, const MapGraph *graph, f32 dt) {
    // Player attacks
    process_unit_attack(gs, tg, graph, &gs->player, dt);

    // Squad attacks
    for (u32 i = 0; i < gs->num_squad; i++) {
        process_unit_attack(gs, tg, graph, &gs->squad[i], dt);
    }

    // Enemy attacks
    for (u32 i = 0; i < gs->num_enemies; i++) {
        process_unit_attack(gs, tg, graph, &gs->enemies[i], dt);
    }
}

void g_combat_update_projectiles(GameState *gs, f32 dt) {
    for (u32 i = 0; i < gs->num_projectiles; ) {
        Projectile *p = &gs->projectiles[i];
        if (!p->active) { i++; continue; }

        p->lifetime -= dt;
        p->pos = vec2_add(p->pos, vec2_scale(p->vel, dt));

        // Out of bounds or expired
        if (p->lifetime <= 0.0f ||
            p->pos.x < 0.0f || p->pos.x > 1.0f ||
            p->pos.y < 0.0f || p->pos.y > 1.0f) {
            p->active = false;
            // Swap with last
            gs->projectiles[i] = gs->projectiles[gs->num_projectiles - 1];
            gs->num_projectiles--;
            continue;
        }

        // Collision with opposing team
        f32 hit_radius = 0.008f;
        bool hit = false;

        if (p->source_team == TEAM_PLAYER) {
            // Check enemies
            for (u32 e = 0; e < gs->num_enemies; e++) {
                Unit *enemy = &gs->enemies[e];
                if (!enemy->alive) continue;
                if (vec2_dist(p->pos, enemy->pos) < hit_radius + enemy->radius) {
                    g_combat_deal_damage(enemy, p->damage);
                    if (p->applies_slow) enemy->slow_timer = 2.0f;
                    hit = true;
                    break;
                }
            }
        } else {
            // Check player
            if (gs->player.alive &&
                vec2_dist(p->pos, gs->player.pos) < hit_radius + gs->player.radius) {
                g_combat_deal_damage(&gs->player, p->damage);
                if (p->applies_slow) gs->player.slow_timer = 2.0f;
                hit = true;
            }
            // Check squad
            if (!hit) {
                for (u32 s = 0; s < gs->num_squad; s++) {
                    Unit *ally = &gs->squad[s];
                    if (!ally->alive) continue;
                    if (vec2_dist(p->pos, ally->pos) < hit_radius + ally->radius) {
                        g_combat_deal_damage(ally, p->damage);
                        if (p->applies_slow) ally->slow_timer = 2.0f;
                        hit = true;
                        break;
                    }
                }
            }
        }

        if (hit) {
            p->active = false;
            gs->projectiles[i] = gs->projectiles[gs->num_projectiles - 1];
            gs->num_projectiles--;
            continue;
        }

        i++;
    }
}

void g_combat_update_squad_states(GameState *gs) {
    // Stance-dependent thresholds
    f32 detect_range, retreat_hp, recover_hp;
    switch (gs->squad_stance) {
        case STANCE_AGGRESSIVE:
            detect_range = 0.12f;
            retreat_hp   = 0.15f;
            recover_hp   = 0.4f;
            break;
        case STANCE_DEFENSIVE:
        default:
            detect_range = 0.08f;
            retreat_hp   = 0.25f;
            recover_hp   = 0.5f;
            break;
        case STANCE_PASSIVE:
            detect_range = 0.0f;
            retreat_hp   = 0.0f;
            recover_hp   = 0.0f;
            break;
    }

    for (u32 i = 0; i < gs->num_squad; i++) {
        Unit *u = &gs->squad[i];
        if (!u->alive) continue;

        // Passive: force follow (healers can still heal)
        if (gs->squad_stance == STANCE_PASSIVE) {
            if (u->role == ROLE_HEALER) {
                bool ally_hurt = false;
                if (gs->player.alive && gs->player.hp < gs->player.max_hp)
                    ally_hurt = true;
                for (u32 j = 0; j < gs->num_squad && !ally_hurt; j++) {
                    if (!gs->squad[j].alive) continue;
                    if (gs->squad[j].hp < gs->squad[j].max_hp)
                        ally_hurt = true;
                }
                bool all_full = true;
                if (gs->player.alive && gs->player.hp < gs->player.max_hp)
                    all_full = false;
                for (u32 j = 0; j < gs->num_squad && all_full; j++) {
                    if (!gs->squad[j].alive) continue;
                    if (gs->squad[j].hp < gs->squad[j].max_hp)
                        all_full = false;
                }
                if (ally_hurt)
                    u->state = STATE_HEAL;
                else if (all_full)
                    u->state = STATE_FOLLOW;
            } else {
                u->state = STATE_FOLLOW;
            }
            continue;
        }

        f32 hp_frac = u->hp / u->max_hp;

        // Check if any enemy is nearby
        bool enemy_nearby = false;
        for (u32 e = 0; e < gs->num_enemies; e++) {
            if (!gs->enemies[e].alive) continue;
            if (vec2_dist(u->pos, gs->enemies[e].pos) < detect_range) {
                enemy_nearby = true;
                break;
            }
        }

        // Healer special logic: heal lowest HP ally within range until full
        if (u->role == ROLE_HEALER) {
            bool ally_hurt = false;
            if (gs->player.alive && gs->player.hp < gs->player.max_hp)
                ally_hurt = true;
            for (u32 j = 0; j < gs->num_squad && !ally_hurt; j++) {
                if (!gs->squad[j].alive) continue;
                if (gs->squad[j].hp < gs->squad[j].max_hp)
                    ally_hurt = true;
            }

            bool all_full = true;
            if (gs->player.alive && gs->player.hp < gs->player.max_hp)
                all_full = false;
            for (u32 j = 0; j < gs->num_squad && all_full; j++) {
                if (!gs->squad[j].alive) continue;
                if (gs->squad[j].hp < gs->squad[j].max_hp)
                    all_full = false;
            }

            if (u->state == STATE_FOLLOW && ally_hurt) {
                u->state = STATE_HEAL;
            } else if (u->state == STATE_HEAL && all_full) {
                u->state = STATE_FOLLOW;
            } else if (u->state == STATE_FOLLOW && enemy_nearby) {
                u->state = STATE_ATTACK;
            } else if (u->state == STATE_ATTACK && !enemy_nearby) {
                u->state = STATE_FOLLOW;
            }
            continue;
        }

        // Non-healer state machine
        switch (u->state) {
            case STATE_FOLLOW:
                if (enemy_nearby)
                    u->state = STATE_ATTACK;
                break;

            case STATE_ATTACK:
                if (hp_frac < retreat_hp)
                    u->state = STATE_RETREAT;
                else if (!enemy_nearby)
                    u->state = STATE_FOLLOW;
                break;

            case STATE_RETREAT:
                if (hp_frac > recover_hp || !enemy_nearby)
                    u->state = STATE_FOLLOW;
                break;

            default:
                break;
        }
    }

    // Player auto-attack state
    if (gs->player.alive && gs->player.state != STATE_DEAD) {
        bool enemy_nearby = false;
        for (u32 e = 0; e < gs->num_enemies; e++) {
            if (!gs->enemies[e].alive) continue;
            if (vec2_dist(gs->player.pos, gs->enemies[e].pos) < gs->player.attack_range * 1.5f) {
                enemy_nearby = true;
                break;
            }
        }
        gs->player.state = enemy_nearby ? STATE_ATTACK : STATE_IDLE;
    }
}
