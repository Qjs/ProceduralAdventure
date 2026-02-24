#ifndef G_TYPES_H
#define G_TYPES_H

#include "../utils/q_util.h"
#include "../mapgen/mg_types.h"
#include "g_particles.h"
#include <SDL3/SDL.h>
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

// Smoothly interpolate angle (handles wraparound)
#define PI_F 3.14159265f
static inline f32 angle_lerp(f32 from, f32 to, f32 t) {
    f32 diff = to - from;
    // Wrap to [-PI, PI]
    while (diff > PI_F)  diff -= 2.0f * PI_F;
    while (diff < -PI_F) diff += 2.0f * PI_F;
    return from + diff * t;
}

/* ---- Game constants ---- */

#define MAX_SQUAD     4
#define MAX_ENEMIES   64
#define MAX_PROJECTILES 128
#define MAX_CAMPS     16
#define MAX_ORBS         5
#define NUM_COLLECT_ORBS 5

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

typedef enum {
    STANCE_AGGRESSIVE,
    STANCE_DEFENSIVE,
    STANCE_PASSIVE
} SquadStance;

typedef enum {
    ORB_EFFECT_HEAL_BOOST,
    ORB_EFFECT_MELEE_BOOST,
    ORB_EFFECT_ARCHER_BOOST,
    ORB_EFFECT_MAGE_BOOST,
    ORB_EFFECT_ENVIRONMENTAL
} OrbEffect;

typedef enum {
    ENV_EFFECT_NONE,
    ENV_EFFECT_BOULDERS,
    ENV_EFFECT_WAVE,
    ENV_EFFECT_LAVA
} EnvironmentalEffect;

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
    bool      is_boss;
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
    f32       armor;              // flat damage reduction (1 dmg per point, min 0.5)
    f32       bonus_armor;        // temporary per-frame armor from stance auras
    f32       slow_timer;         // while >0, speed halved (freeze)
    f32       speed_boost_timer;  // while >0, speed 1.5x (mage buff)
    f32       facing;             // radians, from atan2f(vel.y, vel.x)
} Unit;

typedef struct {
    bool  active;
    Vec2  pos;
    Vec2  vel;
    f32   damage;
    f32   lifetime;
    f32   knockback_scale; // used by archer defensive pushback
    Team  source_team;
    u8    color[4];
    bool  applies_slow;  // mage defensive freeze projectile
    bool  is_arrow;      // archer projectile (faster, no trail)
    bool  is_magic;      // mage projectile (bypasses armor)
    bool  has_pierced;   // archer piercing: already passed through one target
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
    bool  active;
    Vec2  pos;
    f32   radius;
    f32   pulse_timer;
    OrbEffect effect;
} Orb;

typedef struct {
    bool  active;
    Vec2  pos;
    f32   radius_x;
    f32   radius_y;
    f32   pulse_timer;
    Vec2  spawn_pos;    // pre-selected random land position
} Portal;

typedef struct {
    Vec2 pos;
    f32  zoom;
} Camera;

typedef struct GPhysicsState GPhysicsState;

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
    bool        is_boss_level;
    bool        boss_spawned;
    u32         boss_enemy_index;
    f32         elevation_speed_factor;
    bool        water_blocks_movement;
    bool        terrain_ready;
    Camera      camera;
    Orb         orbs[MAX_ORBS];
    u32         num_orbs;
    u32         orbs_collected;
    f32         melee_boost_timer;
    f32         archer_boost_timer;
    f32         mage_boost_timer;
    EnvironmentalEffect env_orb_effect;
    EnvironmentalEffect env_active_effect;
    f32         env_effect_timer;
    f32         env_tick_timer;
    f32         env_wave_front;
    f32         env_wave_dir;
    f32         env_view_min_x;
    f32         env_view_max_x;
    f32         env_view_min_y;
    f32         env_view_max_y;
    Vec2        env_peak_pos;
    Vec2        env_lava_pos;
    f32         env_lava_radius;
    Vec2        env_boulder_from;
    Vec2        env_boulder_to;
    f32         env_boulder_anim;
    bool        env_boulder_visible;
    u32         effect_rng;
    Portal      portal;
    bool        level_complete;
    SquadStance squad_stance;
    u32         enemies_killed;
    f32         river_damage_timer;
    ParticleSystem particles;
    GPhysicsState *physics;
    SDL_Texture *role_textures[ROLE_COUNT];
    SDL_Texture *orb_texture;
    SDL_Texture *portal_texture;
} GameState;

#endif
