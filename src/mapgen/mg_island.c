#include "mg_island.h"
#include "stb_perlin.h"
#include <math.h>

void mg_classify_island(MapGraph *graph, const MapParams *params) {
    f32 freq   = params->noise_freq;
    s32 oct    = params->noise_octaves;
    f32 gain   = params->noise_gain;
    f32 k      = params->radial_falloff;
    f32 power  = params->radial_power;
    f32 thresh = params->land_threshold;
    f32 seed_z = (f32)(params->seed % 256) + 0.5f;

    // Classify each center
    for (u32 i = 0; i < graph->num_centers; i++) {
        Center *c = &graph->centers[i];

        if (c->border) {
            c->water = true;
            c->coast = false;
            continue;
        }

        f32 px = c->pos.x - 0.5f;
        f32 py = c->pos.y - 0.5f;
        f32 r  = sqrtf(px*px + py*py) * 2.0f; // r in [0, sqrt(2)]

        f32 n = stb_perlin_fbm_noise3(c->pos.x * freq, c->pos.y * freq, seed_z,
                                       2.0f, gain, oct);
        f32 landness = n - k * powf(r, power) + thresh;
        c->water = (landness < 0.0f);
        c->coast = false;
    }

    // Determine coast: land cell with at least one water neighbor
    for (u32 i = 0; i < graph->num_centers; i++) {
        Center *c = &graph->centers[i];
        if (c->water) continue;
        for (u32 j = 0; j < c->num_neighbors; j++) {
            if (graph->centers[c->neighbors[j]].water) {
                c->coast = true;
                break;
            }
        }
    }

    // Classify corners: water if all touching centers are water
    for (u32 i = 0; i < graph->num_corners; i++) {
        Corner *co = &graph->corners[i];
        u32 water_count = 0;
        for (u32 j = 0; j < co->num_touches; j++) {
            if (graph->centers[co->touches[j]].water)
                water_count++;
        }
        co->water = (water_count == co->num_touches);
        co->coast = (water_count > 0 && water_count < co->num_touches);
        co->border = false;
        for (u32 j = 0; j < co->num_touches; j++) {
            if (graph->centers[co->touches[j]].border) {
                co->border = true;
                break;
            }
        }
    }
}
