#ifndef G_COMBAT_H
#define G_COMBAT_H

#include "g_types.h"
#include "g_terrain.h"

// Process attacks: cooldown ticks, melee hits, ranged projectile spawning, healer logic
void g_combat_update(GameState *gs, const TerrainGrid *tg, const MapGraph *graph, f32 dt);

// Move projectiles, check collisions, deal damage
void g_combat_update_projectiles(GameState *gs, f32 dt);

// Apply damage to a unit, set dead if HP <= 0
void g_combat_deal_damage(Unit *target, f32 damage);

// Spawn a straight-line projectile
void g_combat_spawn_projectile(GameState *gs, Vec2 from, Vec2 to,
                                f32 damage, Team source_team, const u8 color[4],
                                bool applies_slow, bool is_arrow);

// Update squad state machine (FOLLOW/ATTACK/RETREAT/HEAL transitions)
void g_combat_update_squad_states(GameState *gs);

#endif
