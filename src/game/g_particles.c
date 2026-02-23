#include "g_particles.h"
#include <math.h>

#define PI2 6.28318530f
#define PI_F 3.14159265f

// Simple fast random [0,1)
static u32 particle_rng_state = 0x12345678;

static f32 randf(void) {
    particle_rng_state ^= particle_rng_state << 13;
    particle_rng_state ^= particle_rng_state >> 17;
    particle_rng_state ^= particle_rng_state << 5;
    return (f32)(particle_rng_state & 0xFFFF) / 65536.0f;
}

static f32 randf_range(f32 lo, f32 hi) {
    return lo + randf() * (hi - lo);
}

void g_particles_emit(ParticleSystem *ps, Vec2 pos, const EmitParams *params) {
    for (u32 i = 0; i < params->count; i++) {
        if (ps->count >= MAX_PARTICLES) return;

        Particle *p = &ps->particles[ps->count++];
        f32 angle = randf_range(params->arc_start, params->arc_end);
        f32 speed = randf_range(params->speed_min, params->speed_max);
        p->pos = pos;
        p->vel = (Vec2){cosf(angle) * speed, sinf(angle) * speed};
        p->life = randf_range(params->life_min, params->life_max);
        p->max_life = p->life;
        p->size = randf_range(params->size_min, params->size_max);
        p->color[0] = params->color[0];
        p->color[1] = params->color[1];
        p->color[2] = params->color[2];
        p->color[3] = params->color[3];
    }
}

void g_particles_update(ParticleSystem *ps, f32 dt) {
    for (u32 i = 0; i < ps->count; ) {
        Particle *p = &ps->particles[i];
        p->life -= dt;
        if (p->life <= 0.0f) {
            // Swap-remove
            ps->particles[i] = ps->particles[ps->count - 1];
            ps->count--;
            continue;
        }
        p->pos.x += p->vel.x * dt;
        p->pos.y += p->vel.y * dt;
        // Fade alpha linearly
        f32 frac = p->life / p->max_life;
        p->color[3] = (u8)(frac * 255.0f);
        i++;
    }
}

void g_particles_render(const ParticleSystem *ps, SDL_Renderer *renderer, SDL_FRect map_rect) {
    for (u32 i = 0; i < ps->count; i++) {
        const Particle *p = &ps->particles[i];
        f32 sx = map_rect.x + p->pos.x * map_rect.w;
        f32 sy = map_rect.y + p->pos.y * map_rect.h;
        f32 size = p->size * map_rect.w;
        SDL_SetRenderDrawColor(renderer, p->color[0], p->color[1], p->color[2], p->color[3]);
        SDL_FRect rect = {sx - size * 0.5f, sy - size * 0.5f, size, size};
        SDL_RenderFillRect(renderer, &rect);
    }
}

void g_particles_burst(ParticleSystem *ps, Vec2 pos, u32 count, const u8 color[4]) {
    EmitParams params = {
        .count = count,
        .color = {color[0], color[1], color[2], color[3]},
        .speed_min = 0.03f,
        .speed_max = 0.10f,
        .life_min = 0.3f,
        .life_max = 0.6f,
        .size_min = 0.002f,
        .size_max = 0.004f,
        .arc_start = 0.0f,
        .arc_end = PI2,
        .gravity_y = 0.0f,
    };
    g_particles_emit(ps, pos, &params);
}

void g_particles_slash(ParticleSystem *ps, Vec2 pos, f32 facing, const u8 color[4]) {
    EmitParams params = {
        .count = 6,
        .color = {color[0], color[1], color[2], color[3]},
        .speed_min = 0.05f,
        .speed_max = 0.12f,
        .life_min = 0.15f,
        .life_max = 0.3f,
        .size_min = 0.001f,
        .size_max = 0.003f,
        .arc_start = facing - PI_F * 0.25f,
        .arc_end = facing + PI_F * 0.25f,
        .gravity_y = 0.0f,
    };
    g_particles_emit(ps, pos, &params);
}

void g_particles_heal(ParticleSystem *ps, Vec2 pos) {
    EmitParams params = {
        .count = 8,
        .color = {100, 255, 100, 255},
        .speed_min = 0.01f,
        .speed_max = 0.03f,
        .life_min = 0.5f,
        .life_max = 0.8f,
        .size_min = 0.001f,
        .size_max = 0.003f,
        .arc_start = -PI_F * 0.75f,  // upward arc (negative Y is up in screen)
        .arc_end = -PI_F * 0.25f,
        .gravity_y = -0.05f,
    };
    g_particles_emit(ps, pos, &params);
}

void g_particles_trail(ParticleSystem *ps, Vec2 pos, const u8 color[4]) {
    EmitParams params = {
        .count = 1,
        .color = {color[0], color[1], color[2], (u8)(color[3] / 2)},
        .speed_min = 0.0f,
        .speed_max = 0.005f,
        .life_min = 0.2f,
        .life_max = 0.3f,
        .size_min = 0.001f,
        .size_max = 0.002f,
        .arc_start = 0.0f,
        .arc_end = PI2,
        .gravity_y = 0.0f,
    };
    g_particles_emit(ps, pos, &params);
}
