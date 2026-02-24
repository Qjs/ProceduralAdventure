#include "g_physics.h"

#include <stdlib.h>
#include <string.h>

#if defined(HAVE_BOX2D)
#include <box2d/box2d.h>
#include <stdint.h>

#define G_PHYS_TAG_PLAYER ((void *)(uintptr_t)1)
#define G_PHYS_TAG_PORTAL ((void *)(uintptr_t)2)
#define G_PHYS_TAG_SQUAD_BASE ((uintptr_t)100)
#define G_PHYS_TAG_ENEMY_BASE ((uintptr_t)200)
#define G_PHYS_TAG_ORB_BASE ((uintptr_t)300)

#define G_PHYS_DT (1.0f / 120.0f)
#define G_PHYS_MAX_STEPS 8
#define G_PHYS_CAT_PLAYER ((uint64_t)1u << 0)
#define G_PHYS_CAT_ENEMY  ((uint64_t)1u << 1)
#define G_PHYS_CAT_SENSOR ((uint64_t)1u << 2)

typedef struct GPhysicsState {
    bool active;
    b2WorldId world;
    b2BodyId player_body;
    b2BodyId squad_bodies[MAX_SQUAD];
    b2BodyId enemy_bodies[MAX_ENEMIES];
    b2BodyId orb_bodies[MAX_ORBS];
    b2BodyId portal_body;
    bool orb_collected[MAX_ORBS];
    bool portal_entered;
    f32 accumulator;
} GPhysicsState;

static inline bool g_phys_tag_is_squad(void *tag, u32 *idx) {
    uintptr_t t = (uintptr_t)tag;
    if (t < G_PHYS_TAG_SQUAD_BASE || t >= G_PHYS_TAG_SQUAD_BASE + MAX_SQUAD) return false;
    if (idx) *idx = (u32)(t - G_PHYS_TAG_SQUAD_BASE);
    return true;
}

static inline bool g_phys_tag_is_enemy(void *tag, u32 *idx) {
    uintptr_t t = (uintptr_t)tag;
    if (t < G_PHYS_TAG_ENEMY_BASE || t >= G_PHYS_TAG_ENEMY_BASE + MAX_ENEMIES) return false;
    if (idx) *idx = (u32)(t - G_PHYS_TAG_ENEMY_BASE);
    return true;
}

static inline bool g_phys_tag_is_orb(void *tag, u32 *idx) {
    uintptr_t t = (uintptr_t)tag;
    if (t < G_PHYS_TAG_ORB_BASE || t >= G_PHYS_TAG_ORB_BASE + MAX_ORBS) return false;
    if (idx) *idx = (u32)(t - G_PHYS_TAG_ORB_BASE);
    return true;
}

static inline b2BodyId g_phys_make_unit_body(b2WorldId world, const Unit *u, void *tag,
                                             uint64_t category_bits, uint64_t mask_bits) {
    b2BodyDef body_def = b2DefaultBodyDef();
    body_def.type = b2_dynamicBody;
    body_def.position = (b2Vec2){u->pos.x, u->pos.y};
    body_def.motionLocks.angularZ = true;
    body_def.linearDamping = 6.0f;

    b2BodyId body = b2CreateBody(world, &body_def);

    b2ShapeDef shape_def = b2DefaultShapeDef();
    shape_def.userData = tag;
    shape_def.density = 1.0f;
    shape_def.material.friction = 0.2f;
    shape_def.filter.categoryBits = category_bits;
    shape_def.filter.maskBits = mask_bits;
    shape_def.enableSensorEvents = true;
    shape_def.enableContactEvents = true;

    b2Circle c = {.center = {0.0f, 0.0f}, .radius = u->radius};
    b2CreateCircleShape(body, &shape_def, &c);
    return body;
}

static void g_phys_sync_unit_body(const Unit *u, b2BodyId body) {
    if (!b2Body_IsValid(body)) return;
    if (!u->alive) {
        b2Body_SetLinearVelocity(body, (b2Vec2){0.0f, 0.0f});
        b2Body_Disable(body);
        return;
    }
    b2Body_Enable(body);
    b2Body_SetLinearVelocity(body, (b2Vec2){u->vel.x, u->vel.y});
}

static void g_phys_sync_body_to_unit(Unit *u, b2BodyId body) {
    if (!b2Body_IsValid(body) || !u->alive) return;
    b2Vec2 p = b2Body_GetPosition(body);
    b2Vec2 v = b2Body_GetLinearVelocity(body);
    u->pos = (Vec2){p.x, p.y};
    u->vel = (Vec2){v.x, v.y};
}

static void g_phys_create_orb_sensor(GPhysicsState *ps, const Orb *orb, u32 idx) {
    b2BodyDef body_def = b2DefaultBodyDef();
    body_def.type = b2_staticBody;
    body_def.position = (b2Vec2){orb->pos.x, orb->pos.y};
    b2BodyId body = b2CreateBody(ps->world, &body_def);

    b2ShapeDef shape_def = b2DefaultShapeDef();
    shape_def.userData = (void *)(uintptr_t)(G_PHYS_TAG_ORB_BASE + idx);
    shape_def.filter.categoryBits = G_PHYS_CAT_SENSOR;
    shape_def.filter.maskBits = G_PHYS_CAT_PLAYER;
    shape_def.isSensor = true;
    shape_def.enableSensorEvents = true;

    b2Circle c = {.center = {0.0f, 0.0f}, .radius = orb->radius};
    b2CreateCircleShape(body, &shape_def, &c);
    ps->orb_bodies[idx] = body;
    if (!orb->active) b2Body_Disable(body);
}

static void g_phys_update_orb_sensor(GPhysicsState *ps, const Orb *orb, u32 idx) {
    if (!b2Body_IsValid(ps->orb_bodies[idx])) return;
    b2Body_SetTransform(ps->orb_bodies[idx], (b2Vec2){orb->pos.x, orb->pos.y}, b2Rot_identity);
    if (orb->active) b2Body_Enable(ps->orb_bodies[idx]);
    else b2Body_Disable(ps->orb_bodies[idx]);
}

static void g_phys_update_portal_sensor_inner(GameState *gs) {
    GPhysicsState *ps = gs->physics;
    if (!ps || !b2Body_IsValid(ps->portal_body)) return;
    b2Body_SetTransform(ps->portal_body,
                        (b2Vec2){gs->portal.pos.x, gs->portal.pos.y},
                        b2Rot_identity);
    if (gs->portal.active) b2Body_Enable(ps->portal_body);
    else b2Body_Disable(ps->portal_body);
}

bool g_physics_init(GameState *gs) {
    if (gs->physics) {
        g_physics_shutdown(gs);
    }

    GPhysicsState *ps = calloc(1, sizeof(*ps));
    if (!ps) return false;

    b2WorldDef world_def = b2DefaultWorldDef();
    world_def.gravity = (b2Vec2){0.0f, 0.0f};
    ps->world = b2CreateWorld(&world_def);
    ps->active = true;

    ps->player_body = g_phys_make_unit_body(ps->world, &gs->player, G_PHYS_TAG_PLAYER,
                                            G_PHYS_CAT_PLAYER, G_PHYS_CAT_ENEMY | G_PHYS_CAT_SENSOR);
    for (u32 i = 0; i < gs->num_squad; i++) {
        ps->squad_bodies[i] = g_phys_make_unit_body(
            ps->world, &gs->squad[i], (void *)(uintptr_t)(G_PHYS_TAG_SQUAD_BASE + i),
            G_PHYS_CAT_PLAYER, G_PHYS_CAT_ENEMY);
    }
    for (u32 i = 0; i < gs->num_enemies; i++) {
        ps->enemy_bodies[i] = g_phys_make_unit_body(
            ps->world, &gs->enemies[i], (void *)(uintptr_t)(G_PHYS_TAG_ENEMY_BASE + i),
            G_PHYS_CAT_ENEMY, G_PHYS_CAT_PLAYER);
    }

    for (u32 i = 0; i < gs->num_orbs; i++) {
        g_phys_create_orb_sensor(ps, &gs->orbs[i], i);
    }

    {
        b2BodyDef body_def = b2DefaultBodyDef();
        body_def.type = b2_staticBody;
        body_def.position = (b2Vec2){gs->portal.spawn_pos.x, gs->portal.spawn_pos.y};
        ps->portal_body = b2CreateBody(ps->world, &body_def);

        b2ShapeDef shape_def = b2DefaultShapeDef();
        shape_def.userData = G_PHYS_TAG_PORTAL;
        shape_def.filter.categoryBits = G_PHYS_CAT_SENSOR;
        shape_def.filter.maskBits = G_PHYS_CAT_PLAYER;
        shape_def.isSensor = true;
        shape_def.enableSensorEvents = true;

        f32 rx = gs->portal.radius_x > 0.0f ? gs->portal.radius_x : 0.006f;
        f32 ry = gs->portal.radius_y > 0.0f ? gs->portal.radius_y : 0.012f;
        b2Circle c = {.center = {0.0f, 0.0f}, .radius = (rx + ry) * 0.5f};
        b2CreateCircleShape(ps->portal_body, &shape_def, &c);
        if (!gs->portal.active) b2Body_Disable(ps->portal_body);
    }

    gs->physics = ps;
    return true;
}

void g_physics_shutdown(GameState *gs) {
    GPhysicsState *ps = gs->physics;
    if (!ps) return;
    if (ps->active) {
        b2DestroyWorld(ps->world);
    }
    free(ps);
    gs->physics = NULL;
}

bool g_physics_is_active(const GameState *gs) {
    return gs->physics != NULL;
}

void g_physics_step(GameState *gs, f32 dt) {
    GPhysicsState *ps = gs->physics;
    if (!ps || !ps->active) return;

    g_phys_sync_unit_body(&gs->player, ps->player_body);
    for (u32 i = 0; i < gs->num_squad; i++) {
        g_phys_sync_unit_body(&gs->squad[i], ps->squad_bodies[i]);
    }
    for (u32 i = 0; i < gs->num_enemies; i++) {
        g_phys_sync_unit_body(&gs->enemies[i], ps->enemy_bodies[i]);
    }
    for (u32 i = 0; i < gs->num_orbs; i++) {
        g_phys_update_orb_sensor(ps, &gs->orbs[i], i);
    }
    g_phys_update_portal_sensor_inner(gs);

    ps->accumulator += dt;
    int steps = 0;
    while (ps->accumulator >= G_PHYS_DT && steps < G_PHYS_MAX_STEPS) {
        b2World_Step(ps->world, G_PHYS_DT, 4);
        ps->accumulator -= G_PHYS_DT;
        steps++;

        b2SensorEvents sensors = b2World_GetSensorEvents(ps->world);
        for (int i = 0; i < sensors.beginCount; i++) {
            void *sensor_tag = b2Shape_GetUserData(sensors.beginEvents[i].sensorShapeId);
            void *visitor_tag = b2Shape_GetUserData(sensors.beginEvents[i].visitorShapeId);
            if (visitor_tag != G_PHYS_TAG_PLAYER) continue;

            u32 idx = 0;
            if (g_phys_tag_is_orb(sensor_tag, &idx) && idx < gs->num_orbs) {
                ps->orb_collected[idx] = true;
            } else if (sensor_tag == G_PHYS_TAG_PORTAL) {
                ps->portal_entered = true;
            }
        }
    }
    if (ps->accumulator > G_PHYS_DT) ps->accumulator = 0.0f;

    g_phys_sync_body_to_unit(&gs->player, ps->player_body);
    for (u32 i = 0; i < gs->num_squad; i++) {
        g_phys_sync_body_to_unit(&gs->squad[i], ps->squad_bodies[i]);
    }
    for (u32 i = 0; i < gs->num_enemies; i++) {
        g_phys_sync_body_to_unit(&gs->enemies[i], ps->enemy_bodies[i]);
    }
}

bool g_physics_consume_orb_collected(GameState *gs, u32 orb_index) {
    GPhysicsState *ps = gs->physics;
    if (!ps || orb_index >= MAX_ORBS || !ps->orb_collected[orb_index]) return false;
    ps->orb_collected[orb_index] = false;
    return true;
}

bool g_physics_consume_portal_entered(GameState *gs) {
    GPhysicsState *ps = gs->physics;
    if (!ps || !ps->portal_entered) return false;
    ps->portal_entered = false;
    return true;
}

void g_physics_update_portal_sensor(GameState *gs) {
    if (!gs->physics) return;
    g_phys_update_portal_sensor_inner(gs);
}

void g_physics_teleport_player(GameState *gs) {
    GPhysicsState *ps = gs->physics;
    if (!ps) return;
    if (!b2Body_IsValid(ps->player_body)) return;
    b2Body_SetTransform(ps->player_body, (b2Vec2){gs->player.pos.x, gs->player.pos.y},
                        b2Rot_identity);
    b2Body_SetLinearVelocity(ps->player_body, (b2Vec2){gs->player.vel.x, gs->player.vel.y});
}

void g_physics_teleport_squad(GameState *gs, u32 squad_index) {
    GPhysicsState *ps = gs->physics;
    if (!ps || squad_index >= gs->num_squad) return;
    b2BodyId body = ps->squad_bodies[squad_index];
    if (!b2Body_IsValid(body)) return;
    b2Body_SetTransform(body, (b2Vec2){gs->squad[squad_index].pos.x, gs->squad[squad_index].pos.y},
                        b2Rot_identity);
    b2Body_SetLinearVelocity(body, (b2Vec2){gs->squad[squad_index].vel.x, gs->squad[squad_index].vel.y});
}

void g_physics_teleport_enemy(GameState *gs, u32 enemy_index) {
    GPhysicsState *ps = gs->physics;
    if (!ps || enemy_index >= gs->num_enemies) return;
    b2BodyId body = ps->enemy_bodies[enemy_index];
    if (!b2Body_IsValid(body)) return;
    b2Body_SetTransform(body, (b2Vec2){gs->enemies[enemy_index].pos.x, gs->enemies[enemy_index].pos.y},
                        b2Rot_identity);
    b2Body_SetLinearVelocity(body, (b2Vec2){gs->enemies[enemy_index].vel.x, gs->enemies[enemy_index].vel.y});
}

#else

typedef struct GPhysicsState {
    int unused;
} GPhysicsState;

bool g_physics_init(GameState *gs) {
    (void)gs;
    return false;
}

void g_physics_shutdown(GameState *gs) {
    (void)gs;
}

bool g_physics_is_active(const GameState *gs) {
    (void)gs;
    return false;
}

void g_physics_step(GameState *gs, f32 dt) {
    (void)gs;
    (void)dt;
}

bool g_physics_consume_orb_collected(GameState *gs, u32 orb_index) {
    (void)gs;
    (void)orb_index;
    return false;
}

bool g_physics_consume_portal_entered(GameState *gs) {
    (void)gs;
    return false;
}

void g_physics_update_portal_sensor(GameState *gs) {
    (void)gs;
}

void g_physics_teleport_player(GameState *gs) {
    (void)gs;
}

void g_physics_teleport_squad(GameState *gs, u32 squad_index) {
    (void)gs;
    (void)squad_index;
}

void g_physics_teleport_enemy(GameState *gs, u32 enemy_index) {
    (void)gs;
    (void)enemy_index;
}

#endif
