#ifndef G_PHYSICS_H
#define G_PHYSICS_H

#include "g_types.h"

typedef struct GPhysicsState GPhysicsState;

bool g_physics_init(GameState *gs);
void g_physics_shutdown(GameState *gs);
bool g_physics_is_active(const GameState *gs);
void g_physics_step(GameState *gs, f32 dt);
bool g_physics_consume_orb_collected(GameState *gs, u32 orb_index);
bool g_physics_consume_portal_entered(GameState *gs);
void g_physics_update_portal_sensor(GameState *gs);
void g_physics_teleport_player(GameState *gs);
void g_physics_teleport_squad(GameState *gs, u32 squad_index);
void g_physics_teleport_enemy(GameState *gs, u32 enemy_index);
void g_physics_apply_player_impulse(GameState *gs, Vec2 impulse);
void g_physics_apply_squad_impulse(GameState *gs, u32 squad_index, Vec2 impulse);
void g_physics_apply_enemy_impulse(GameState *gs, u32 enemy_index, Vec2 impulse);

#endif
