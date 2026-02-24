#include "mg_rivers.h"
#include <stdlib.h>
#include <string.h>

static u32 xorshift32(u32 *state) {
    u32 x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static u32 pack_rgba(u8 r, u8 g, u8 b, u8 a) {
    return ((u32)r) | ((u32)g << 8) | ((u32)b << 16) | ((u32)a << 24);
}

// Find the edge index connecting corners c0 and c1, or UINT32_MAX if none.
static u32 find_edge_between_corners(const MapGraph *graph, u32 c0, u32 c1) {
    const Corner *corner = &graph->corners[c0];
    for (u32 i = 0; i < corner->num_protrudes; i++) {
        u32 ei = corner->protrudes[i];
        const MapEdge *e = &graph->edges[ei];
        if ((e->v0 == c0 && e->v1 == c1) || (e->v0 == c1 && e->v1 == c0))
            return ei;
    }
    return UINT32_MAX;
}

void mg_assign_rivers(MapGraph *graph, const MapParams *params, u32 seed) {
    // 1. Compute downslope for every corner
    for (u32 i = 0; i < graph->num_corners; i++) {
        Corner *c = &graph->corners[i];
        c->downslope = i; // self = no downslope
        f32 lowest = c->elevation;
        for (u32 j = 0; j < c->num_adjacent; j++) {
            u32 ni = c->adjacent[j];
            f32 ne = graph->corners[ni].elevation;
            if (ne < lowest) {
                lowest = ne;
                c->downslope = ni;
            }
        }
    }

    // Reset river on all edges
    for (u32 i = 0; i < graph->num_edges; i++)
        graph->edges[i].river = 0;

    // 2. Collect valid source corners
    u32 *sources = malloc(graph->num_corners * sizeof(u32));
    u32 num_sources = 0;
    for (u32 i = 0; i < graph->num_corners; i++) {
        Corner *c = &graph->corners[i];
        if (c->border || c->water) continue;
        if (c->elevation < params->river_min_elev) continue;
        if (c->downslope == i) continue; // no downhill neighbor
        sources[num_sources++] = i;
    }

    // 3. Shuffle and pick first num_rivers
    u32 rng = seed ^ 0xB1FADE;
    if (rng == 0) rng = 1;
    for (u32 i = num_sources; i > 1; i--) {
        u32 j = xorshift32(&rng) % i;
        u32 tmp = sources[i - 1];
        sources[i - 1] = sources[j];
        sources[j] = tmp;
    }

    u32 count = params->num_rivers < num_sources ? params->num_rivers : num_sources;

    // Visited set to prevent cycles
    u8 *visited = calloc(graph->num_corners, 1);

    // 4. Trace each river downhill
    for (u32 r = 0; r < count; r++) {
        memset(visited, 0, graph->num_corners);
        u32 cur = sources[r];

        while (cur != UINT32_MAX) {
            if (visited[cur]) break;
            visited[cur] = 1;

            Corner *cc = &graph->corners[cur];
            if (cc->water || cc->coast) break;

            u32 next = cc->downslope;
            if (next == cur) break; // flat, no more downhill

            // Find edge and increment river flow
            u32 ei = find_edge_between_corners(graph, cur, next);
            if (ei != UINT32_MAX) {
                graph->edges[ei].river++;
            }

            cur = next;
        }
    }

    free(visited);
    free(sources);
}

// Bresenham thick line rasterization into pixel buffer + river_mask
static void draw_thick_line(u32 *pixels, u8 *mask, u32 w, u32 h,
                            s32 x0, s32 y0, s32 x1, s32 y1,
                            u32 color, u8 flow, s32 half_width) {
    s32 dx = abs(x1 - x0);
    s32 dy = abs(y1 - y0);
    s32 sx = x0 < x1 ? 1 : -1;
    s32 sy = y0 < y1 ? 1 : -1;
    s32 err = dx - dy;

    for (;;) {
        // Expand point by half_width
        for (s32 oy = -half_width; oy <= half_width; oy++) {
            for (s32 ox = -half_width; ox <= half_width; ox++) {
                s32 px = x0 + ox;
                s32 py = y0 + oy;
                if (px >= 0 && px < (s32)w && py >= 0 && py < (s32)h) {
                    u32 idx = (u32)py * w + (u32)px;
                    pixels[idx] = color;
                    mask[idx] = flow;
                }
            }
        }

        if (x0 == x1 && y0 == y1) break;
        s32 e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

void mg_rasterize_rivers(Map *map) {
    u32 w = map->params.raster_w;
    u32 h = map->params.raster_h;

    if (!map->river_mask)
        map->river_mask = malloc(w * h);
    memset(map->river_mask, 0, w * h);

    MapGraph *g = &map->graph;

    for (u32 i = 0; i < g->num_edges; i++) {
        MapEdge *e = &g->edges[i];
        if (e->river == 0) continue;
        if (e->v0 == UINT32_MAX || e->v1 == UINT32_MAX) continue;

        Corner *c0 = &g->corners[e->v0];
        Corner *c1 = &g->corners[e->v1];

        s32 x0 = (s32)(c0->pos.x * (f32)w);
        s32 y0 = (s32)(c0->pos.y * (f32)h);
        s32 x1 = (s32)(c1->pos.x * (f32)w);
        s32 y1 = (s32)(c1->pos.y * (f32)h);

        u32 flow = e->river;
        if (flow > 5) flow = 5;
        s32 half_w = (s32)(flow); // width = 1 + 2*half_w pixels
        u8 flow_val = (u8)flow;

        // Color based on lava vs water and flow amount
        u32 color;
        if (map->lava_rivers) {
            // Lava: orange-red, brighter with more flow
            f32 t = (f32)(flow - 1) / 4.0f;
            if (t > 1.0f) t = 1.0f;
            u8 r = (u8)(200.0f + t * 55.0f);
            u8 gr = (u8)(60.0f + t * 100.0f);
            u8 b = (u8)(20.0f + t * 20.0f);
            color = pack_rgba(r, gr, b, 255);
        } else if (map->params.boss_theme) {
            // Dark water on boss theme
            f32 t = (f32)(flow - 1) / 4.0f;
            if (t > 1.0f) t = 1.0f;
            u8 r = (u8)(20.0f + t * 20.0f);
            u8 gr = (u8)(40.0f + t * 30.0f);
            u8 b = (u8)(100.0f + t * 40.0f);
            color = pack_rgba(r, gr, b, 255);
        } else {
            // Water: blue, brighter with more flow
            f32 t = (f32)(flow - 1) / 4.0f;
            if (t > 1.0f) t = 1.0f;
            u8 r = (u8)(30.0f + t * 30.0f);
            u8 gr = (u8)(70.0f + t * 50.0f);
            u8 b = (u8)(170.0f + t * 40.0f);
            color = pack_rgba(r, gr, b, 255);
        }

        draw_thick_line(map->pixels, map->river_mask, w, h,
                        x0, y0, x1, y1, color, flow_val, half_w);
    }
}
