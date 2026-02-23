#ifndef G_TYPES_H
#define G_TYPES_H

#include "../utils/q_util.h"
#include "../mapgen/mg_types.h"
#include <math.h>

/* ---- Vec2 math (inline) ---- */

static inline Vec2 vec2_add(Vec2 a, Vec2 b) { return (Vec2){a.x + b.x, a.y + b.y}; }
static inline Vec2 vec2_sub(Vec2 a, Vec2 b) { return (Vec2){a.x - b.x, a.y - b.y}; }
static inline Vec2 vec2_scale(Vec2 v, f32 s) { return (Vec2){v.x * s, v.y * s}; }
static inline f32  vec2_len(Vec2 v) { return sqrtf(v.x * v.x + v.y * v.y); }
static inline f32  vec2_dist(Vec2 a, Vec2 b) { return vec2_len(vec2_sub(a, b)); }

static inline Vec2 vec2_normalize(Vec2 v) {
    f32 l = vec2_len(v);
    if (l < 1e-8f) return (Vec2){0, 0};
    return vec2_scale(v, 1.0f / l);
}

/* ---- Game constants ---- */

#define MAX_SQUAD     4
#define MAX_ENEMIES   64
#define MAX_PROJECTILES 128
#define MAX_CAMPS     16

/* ---- Enums ---- */

typedef enum {
    ROLE_PLAYER,
    ROLE_MELEE,
    ROLE_ARCHER,
    ROLE_HEALER,
    ROLE_MAGE,
    ROLE_ENEMY_MELEE,
    ROLE_ENEMY_RANGED,
    ROLE_COUNT
} UnitRole;

typedef enum {
    STATE_IDLE,
    STATE_FOLLOW,
    STATE_ATTACK,
    STATE_RETREAT,
    STATE_CAST,
    STATE_HEAL,
    STATE_DEAD
} UnitState;

typedef enum {
    TEAM_PLAYER,
    TEAM_ENEMY
} Team;

/* ---- Structs ---- */

typedef struct {
    f32 separation;
    f32 cohesion;
    f32 follow_player;
    f32 avoid_water;
    f32 seek_target;
    f32 flee_target;
    f32 preferred_dist;
    f32 separation_radius;
} BoidWeights;

typedef struct {
    bool      alive;
    UnitRole  role;
    Team      team;
    UnitState state;
    Vec2      pos;
    Vec2      vel;
    f32       speed;
    f32       hp;
    f32       max_hp;
    f32       damage;
    f32       attack_range;
    f32       cooldown;
    f32       cooldown_timer;
    f32       radius;
    u32       target_id;
    u8        color[4]; // RGBA
    BoidWeights weights;
} Unit;

typedef struct {
    bool  active;
    Vec2  pos;
    Vec2  vel;
    f32   damage;
    f32   lifetime;
    Team  source_team;
    u8    color[4];
} Projectile;

typedef struct {
    Vec2  pos;
    f32   activation_radius;
    f32   spawn_radius;
    bool  activated;
    u32   enemy_ids[8];
    u32   num_enemies;
    u32   num_alive;
} EnemyCamp;

typedef struct {
    Vec2 pos;
    f32  zoom;
} Camera;

typedef struct {
    Unit        player;
    Unit        squad[MAX_SQUAD];
    u32         num_squad;
    Unit        enemies[MAX_ENEMIES];
    u32         num_enemies;
    Projectile  projectiles[MAX_PROJECTILES];
    u32         num_projectiles;
    EnemyCamp   camps[MAX_CAMPS];
    u32         num_camps;
    f32         elevation_speed_factor;
    bool        water_blocks_movement;
    bool        terrain_ready;
    Camera      camera;
} GameState;

#endif
