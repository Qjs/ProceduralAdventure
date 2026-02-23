#ifndef G_PARTICLES_H
#define G_PARTICLES_H

#include "../utils/q_util.h"
#include "../mapgen/mg_types.h"
#include <SDL3/SDL.h>

typedef struct {
    Vec2 pos, vel;
    f32  life, max_life;
    f32  size;
    u8   color[4];
} Particle;

#define MAX_PARTICLES 512

typedef struct {
    Particle particles[MAX_PARTICLES];
    u32      count;
} ParticleSystem;

typedef struct {
    u32 count;
    u8  color[4];
    f32 speed_min, speed_max;
    f32 life_min, life_max;
    f32 size_min, size_max;
    f32 arc_start, arc_end;   // radians (0..2PI = full circle)
    f32 gravity_y;            // downward pull (positive = down)
} EmitParams;

// Core API
void g_particles_emit(ParticleSystem *ps, Vec2 pos, const EmitParams *params);
void g_particles_update(ParticleSystem *ps, f32 dt);
void g_particles_render(const ParticleSystem *ps, SDL_Renderer *renderer, SDL_FRect map_rect);

// Convenience helpers
void g_particles_burst(ParticleSystem *ps, Vec2 pos, u32 count, const u8 color[4]);
void g_particles_slash(ParticleSystem *ps, Vec2 pos, f32 facing, const u8 color[4]);
void g_particles_heal(ParticleSystem *ps, Vec2 pos);
void g_particles_trail(ParticleSystem *ps, Vec2 pos, const u8 color[4]);

#endif
