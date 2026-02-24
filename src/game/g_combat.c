#include "g_combat.h"
#include "g_unit.h"
#include "g_terrain.h"
#include "g_particles.h"
#include "g_physics.h"

static inline f32 clamp01f(f32 x) {
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

static inline f32 smoothstepf(f32 edge0, f32 edge1, f32 x) {
    if (edge1 <= edge0) return x >= edge1 ? 1.0f : 0.0f;
    f32 t = clamp01f((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

static f32 archer_knockback_scale(const Unit *u) {
    // Baseline archer cooldown at init is 0.8s.
    const f32 base_cooldown = 0.8f;
    if (u->cooldown <= 1e-5f) return 0.35f;
    f32 ratio = u->cooldown / base_cooldown; // lower ratio => faster attacks
    if (ratio >= 1.0f) return 1.0f;
    // Taper knockback as attack speed increases, with a floor.
    f32 s = smoothstepf(0.45f, 1.0f, ratio);
    return 0.35f + 0.65f * s;
}

static f32 role_damage_multiplier(const GameState *gs, const Unit *u) {
    if (u->team != TEAM_PLAYER) return 1.0f;
    if (u->role == ROLE_MELEE && gs->melee_boost_timer > 0.0f) return 1.75f;
    if (u->role == ROLE_ARCHER && gs->archer_boost_timer > 0.0f) return 1.65f;
    if (u->role == ROLE_MAGE && gs->mage_boost_timer > 0.0f) return 1.65f;
    return 1.0f;
}

void g_combat_deal_damage(Unit *target, f32 damage, bool is_magic) {
    if (!target->alive) return;

    // Armor: flat reduction of 1 damage per armor point, min 0.5 damage
    // Does not apply to magic damage
    f32 total_armor = target->armor + target->bonus_armor;
    if (!is_magic && total_armor > 0.0f) {
        damage -= total_armor;
        if (damage < 0.5f) damage = 0.5f;
    }

    target->hp -= damage;
    if (target->hp <= 0.0f) {
        target->hp = 0.0f;
        target->alive = false;
        target->state = STATE_DEAD;
    }
}

void g_combat_spawn_projectile(GameState *gs, Vec2 from, Vec2 to,
                                f32 damage, Team source_team, const u8 color[4],
                                bool applies_slow, bool is_arrow, bool is_magic,
                                f32 knockback_scale) {
    if (gs->num_projectiles >= MAX_PROJECTILES) return;

    Projectile *p = &gs->projectiles[gs->num_projectiles++];
    p->active = true;
    p->pos = from;
    p->damage = damage;
    p->lifetime = 2.0f;
    p->knockback_scale = knockback_scale;
    p->source_team = source_team;
    p->color[0] = color[0];
    p->color[1] = color[1];
    p->color[2] = color[2];
    p->color[3] = color[3];
    p->applies_slow = applies_slow;
    p->is_arrow = is_arrow;
    p->is_magic = is_magic;
    p->has_pierced = false;

    Vec2 dir = vec2_normalize(vec2_sub(to, from));
    f32 speed = is_arrow ? 0.30f : 0.2f;
    p->vel = vec2_scale(dir, speed);
}

static void process_unit_attack(GameState *gs, const TerrainGrid *tg,
                                const MapGraph *graph, Unit *u, f32 dt) {
    if (!u->alive || u->state == STATE_DEAD) return;

    u->cooldown_timer -= dt;
    if (u->cooldown_timer < 0.0f) u->cooldown_timer = 0.0f;
    if (u->cooldown_timer > 0.0f) return;
    if (u->state == STATE_IDLE) return;

    if (u->state != STATE_ATTACK && u->state != STATE_HEAL) return;

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
            // Defensive stance: AoE heal at 40% to all allies in range
            if (u->team == TEAM_PLAYER && gs->squad_stance == STANCE_DEFENSIVE) {
                f32 aoe_heal = u->damage * 0.4f;
                if (gs->player.alive && gs->player.hp < gs->player.max_hp &&
                    vec2_dist(u->pos, gs->player.pos) < u->attack_range) {
                    gs->player.hp += aoe_heal;
                    if (gs->player.hp > gs->player.max_hp) gs->player.hp = gs->player.max_hp;
                    g_particles_heal(&gs->particles, gs->player.pos);
                }
                for (u32 j = 0; j < gs->num_squad; j++) {
                    Unit *ally = &gs->squad[j];
                    if (!ally->alive || ally->hp >= ally->max_hp) continue;
                    if (vec2_dist(u->pos, ally->pos) < u->attack_range) {
                        ally->hp += aoe_heal;
                        if (ally->hp > ally->max_hp) ally->hp = ally->max_hp;
                        g_particles_heal(&gs->particles, ally->pos);
                    }
                }
            } else {
                best->hp += u->damage; // healer "damage" is heal amount
                if (best->hp > best->max_hp) best->hp = best->max_hp;
                g_particles_heal(&gs->particles, best->pos);
            }
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
        f32 dmg = u->damage * role_damage_multiplier(gs, u);
        bool slow = false;
        bool arrow = (u->role == ROLE_ARCHER || u->role == ROLE_ENEMY_RANGED);
        f32 knockback_scale = 1.0f;
        static const u8 arrow_color[4] = {180, 180, 180, 255};
        const u8 *proj_color = arrow ? arrow_color : u->color;

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

        if (u->team == TEAM_PLAYER && u->role == ROLE_ARCHER) {
            knockback_scale = archer_knockback_scale(u);
        }
        bool magic = (u->role == ROLE_MAGE);
        g_combat_spawn_projectile(gs, u->pos, target_pos, dmg, u->team, proj_color,
                                  slow, arrow, magic, knockback_scale);
    } else {
        // Melee hit
        f32 melee_damage = u->damage * role_damage_multiplier(gs, u);
        static const u8 spark_color[4] = {220, 220, 230, 255};
        g_particles_slash(&gs->particles, target->pos, u->facing, spark_color);
        g_combat_deal_damage(target, melee_damage, false);

        // Boss melee has strong knockback on player team.
        if (u->team == TEAM_ENEMY && u->is_boss && target->alive) {
            Vec2 push = vec2_normalize(vec2_sub(target->pos, u->pos));
            if (vec2_len(push) < 1e-5f) push = (Vec2){1.0f, 0.0f};
            Vec2 old_pos = target->pos;
            Vec2 new_pos = vec2_add(target->pos, vec2_scale(push, 0.03f));
            new_pos = g_unit_move_with_terrain(old_pos, new_pos, tg, graph, true);
            target->pos = new_pos;
            target->vel = vec2_add(target->vel, vec2_scale(push, 0.12f));

            if (target == &gs->player) {
                g_physics_teleport_player(gs);
            } else {
                for (u32 s = 0; s < gs->num_squad; s++) {
                    if (&gs->squad[s] == target) {
                        g_physics_teleport_squad(gs, s);
                        break;
                    }
                }
            }
        }
        if (!target->alive) {
            g_particles_burst(&gs->particles, target->pos, 12, target->color);
            if (u->team == TEAM_PLAYER)
                gs->enemies_killed++;
        }

        // Melee Cleave: aggressive stance splash damage to nearby enemies
        if (u->role == ROLE_MELEE && u->team == TEAM_PLAYER &&
            gs->squad_stance == STANCE_AGGRESSIVE) {
            static const u8 cleave_color[4] = {255, 160, 60, 255};
            f32 splash_dmg = melee_damage * 0.5f;
            for (u32 e = 0; e < gs->num_enemies; e++) {
                Unit *other = &gs->enemies[e];
                if (!other->alive || other == target) continue;
                if (vec2_dist(other->pos, target_pos) < 0.02f) {
                    g_combat_deal_damage(other, splash_dmg, false);
                    g_particles_slash(&gs->particles, other->pos, u->facing, cleave_color);
                    if (!other->alive) {
                        g_particles_burst(&gs->particles, other->pos, 12, other->color);
                        gs->enemies_killed++;
                    }
                }
            }
        }

        // Melee Parry: when enemy hits a passive-stance player-team melee,
        // 33% chance to reflect 50% damage back
        if (u->team == TEAM_ENEMY && target->role == ROLE_MELEE &&
            target->team == TEAM_PLAYER && gs->squad_stance == STANCE_PASSIVE &&
            target->alive) {
            // Simple pseudo-random: use target position as seed
            u32 rcheck = (u32)(target->pos.x * 10000.0f + target->pos.y * 7777.0f +
                               u->cooldown_timer * 3333.0f);
            if (rcheck % 3 == 0) {
                static const u8 parry_color[4] = {180, 220, 255, 255};
                f32 reflect_dmg = u->damage * 0.5f;
                g_combat_deal_damage(u, reflect_dmg, false);
                g_particles_burst(&gs->particles, u->pos, 8, parry_color);
                if (!u->alive) {
                    g_particles_burst(&gs->particles, u->pos, 12, u->color);
                }
            }
        }
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

        // Mage bolt trail (skip for arrows)
        if (!p->is_arrow)
            g_particles_trail(&gs->particles, p->pos, p->color);

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
                    g_combat_deal_damage(enemy, p->damage, p->is_magic);
                    if (!enemy->alive) {
                        gs->enemies_killed++;
                        g_particles_burst(&gs->particles, enemy->pos, 12, enemy->color);
                    }
                    if (p->applies_slow) enemy->slow_timer = 2.0f;

                    // Archer Pushback: defensive stance knocks enemy back
                    if (p->is_arrow && gs->squad_stance == STANCE_DEFENSIVE && enemy->alive) {
                        Vec2 push_dir = vec2_normalize(p->vel);
                        if (g_physics_is_active(gs)) {
                            g_physics_apply_enemy_impulse(gs, e, vec2_scale(push_dir, 0.0010f * p->knockback_scale));
                        } else {
                            enemy->pos = vec2_add(enemy->pos, vec2_scale(push_dir, 0.015f * p->knockback_scale));
                            // Clamp to bounds
                            if (enemy->pos.x < 0.0f) enemy->pos.x = 0.0f;
                            if (enemy->pos.x > 1.0f) enemy->pos.x = 1.0f;
                            if (enemy->pos.y < 0.0f) enemy->pos.y = 0.0f;
                            if (enemy->pos.y > 1.0f) enemy->pos.y = 1.0f;
                            g_physics_teleport_enemy(gs, e);
                        }
                    }

                    // Archer Piercing: aggressive stance, arrow continues through first target
                    if (p->is_arrow && gs->squad_stance == STANCE_AGGRESSIVE && !p->has_pierced) {
                        p->has_pierced = true;
                        p->damage *= 0.5f;
                        // Don't destroy projectile — skip setting hit=true
                        g_particles_burst(&gs->particles, p->pos, 6, p->color);
                    } else {
                        hit = true;
                    }
                    break;
                }
            }
        } else {
            // Check player
            if (gs->player.alive &&
                vec2_dist(p->pos, gs->player.pos) < hit_radius + gs->player.radius) {
                g_combat_deal_damage(&gs->player, p->damage, p->is_magic);
                if (p->applies_slow) gs->player.slow_timer = 2.0f;
                hit = true;
            }
            // Check squad
            if (!hit) {
                for (u32 s = 0; s < gs->num_squad; s++) {
                    Unit *ally = &gs->squad[s];
                    if (!ally->alive) continue;
                    if (vec2_dist(p->pos, ally->pos) < hit_radius + ally->radius) {
                        g_combat_deal_damage(ally, p->damage, p->is_magic);
                        if (p->applies_slow) ally->slow_timer = 2.0f;
                        hit = true;
                        break;
                    }
                }
            }
        }

        if (hit) {
            g_particles_burst(&gs->particles, p->pos, 6, p->color);
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
