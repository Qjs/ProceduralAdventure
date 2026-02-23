#include "mg_elevation.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

void mg_assign_elevation(MapGraph *graph, const MapParams *params) {
    u32 n = graph->num_centers;
    f32 gamma = params->elevation_gamma;

    // BFS from coastal land cells to assign distance-based elevation
    u32 *queue = malloc(sizeof(u32) * n);
    f32 *dist  = malloc(sizeof(f32) * n);
    if (!queue || !dist) { free(queue); free(dist); return; }

    for (u32 i = 0; i < n; i++)
        dist[i] = -1.0f;

    // Seed: coastal land cells at distance 0
    u32 head = 0, tail = 0;
    for (u32 i = 0; i < n; i++) {
        Center *c = &graph->centers[i];
        if (c->water) {
            c->elevation = 0.0f;
            continue;
        }
        if (c->coast) {
            dist[i] = 0.0f;
            queue[tail++] = i;
            c->elevation = 0.0f;
        }
    }

    // BFS over land neighbors
    f32 max_dist = 1.0f;
    while (head < tail) {
        u32 ci = queue[head++];
        Center *c = &graph->centers[ci];
        for (u32 j = 0; j < c->num_neighbors; j++) {
            u32 ni = c->neighbors[j];
            if (graph->centers[ni].water) continue;
            if (dist[ni] >= 0.0f) continue;
            dist[ni] = dist[ci] + 1.0f;
            if (dist[ni] > max_dist) max_dist = dist[ni];
            queue[tail++] = ni;
        }
    }

    // Normalize and apply gamma curve
    for (u32 i = 0; i < n; i++) {
        Center *c = &graph->centers[i];
        if (c->water) continue;
        if (dist[i] < 0.0f) { c->elevation = 0.0f; continue; }
        f32 norm = dist[i] / max_dist;
        c->elevation = powf(norm, gamma);
    }

    // Corner elevation = average of touching centers
    for (u32 i = 0; i < graph->num_corners; i++) {
        Corner *co = &graph->corners[i];
        if (co->num_touches == 0) { co->elevation = 0.0f; continue; }
        f32 sum = 0.0f;
        for (u32 j = 0; j < co->num_touches; j++)
            sum += graph->centers[co->touches[j]].elevation;
        co->elevation = sum / (f32)co->num_touches;
    }

    free(queue);
    free(dist);
}
