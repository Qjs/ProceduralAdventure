#include "mg_points.h"
#include <math.h>
#include <stdlib.h>

static u32 xorshift32(u32 *state) {
    u32 x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static f32 xor_f32(u32 *state) {
    return (f32)(xorshift32(state) & 0xFFFFFF) / (f32)0xFFFFFF;
}

f32 *mg_generate_points(u32 site_count, u32 seed, u32 *out_count) {
    u32 grid_dim = (u32)ceilf(sqrtf((f32)site_count));
    u32 n = grid_dim * grid_dim;
    f32 *coords = malloc(sizeof(f32) * 2 * n);
    if (!coords) { *out_count = 0; return NULL; }

    u32 rng = seed ? seed : 1;
    f32 cell = 1.0f / (f32)grid_dim;

    u32 idx = 0;
    for (u32 row = 0; row < grid_dim; row++) {
        for (u32 col = 0; col < grid_dim; col++) {
            f32 jx = xor_f32(&rng);
            f32 jy = xor_f32(&rng);
            f32 x = ((f32)col + jx) * cell;
            f32 y = ((f32)row + jy) * cell;
            // clamp to (0,1) to avoid exact boundary
            if (x <= 0.0f) x = 0.001f;
            if (x >= 1.0f) x = 0.999f;
            if (y <= 0.0f) y = 0.001f;
            if (y >= 1.0f) y = 0.999f;
            coords[idx++] = x;
            coords[idx++] = y;
        }
    }

    *out_count = n;
    return coords;
}
