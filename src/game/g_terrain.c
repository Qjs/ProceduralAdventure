#include "g_terrain.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

void g_terrain_build_grid(TerrainGrid *tg, const MapGraph *graph) {
    u32 n = graph->num_centers;
    u32 res = (u32)ceilf(sqrtf((f32)n));
    if (res < 1) res = 1;

    tg->grid_res = res;
    tg->cell_size = 1.0f / (f32)res;

    u32 total = res * res;
    tg->grid_cells = malloc(sizeof(u32) * total);
    memset(tg->grid_cells, 0xFF, sizeof(u32) * total);

    f32 cs = tg->cell_size;
    for (u32 i = 0; i < n; i++) {
        u32 gx = (u32)(graph->centers[i].pos.x / cs);
        u32 gy = (u32)(graph->centers[i].pos.y / cs);
        if (gx >= res) gx = res - 1;
        if (gy >= res) gy = res - 1;
        u32 gi = gy * res + gx;
        if (tg->grid_cells[gi] == UINT32_MAX)
            tg->grid_cells[gi] = i;
    }
}

void g_terrain_free(TerrainGrid *tg) {
    free(tg->grid_cells);
    tg->grid_cells = NULL;
}

u32 g_terrain_find_cell(const TerrainGrid *tg, const MapGraph *graph, Vec2 pos) {
    u32 res = tg->grid_res;
    f32 cs = tg->cell_size;

    s32 gx = (s32)(pos.x / cs);
    s32 gy = (s32)(pos.y / cs);

    f32 best_dist = 1e30f;
    u32 best_idx = 0;

    // 3x3 neighborhood search
    for (s32 dy = -1; dy <= 1; dy++) {
        for (s32 dx = -1; dx <= 1; dx++) {
            s32 nx = gx + dx;
            s32 ny = gy + dy;
            if (nx < 0 || ny < 0 || nx >= (s32)res || ny >= (s32)res)
                continue;
            u32 ci = tg->grid_cells[(u32)ny * res + (u32)nx];
            if (ci == UINT32_MAX) continue;
            f32 ddx = graph->centers[ci].pos.x - pos.x;
            f32 ddy = graph->centers[ci].pos.y - pos.y;
            f32 d2 = ddx * ddx + ddy * ddy;
            if (d2 < best_dist) {
                best_dist = d2;
                best_idx = ci;
            }
        }
    }

    // Brute-force fallback
    if (best_dist > 1e20f) {
        for (u32 i = 0; i < graph->num_centers; i++) {
            f32 ddx = graph->centers[i].pos.x - pos.x;
            f32 ddy = graph->centers[i].pos.y - pos.y;
            f32 d2 = ddx * ddx + ddy * ddy;
            if (d2 < best_dist) {
                best_dist = d2;
                best_idx = i;
            }
        }
    }

    return best_idx;
}

f32 g_terrain_get_elevation(const TerrainGrid *tg, const MapGraph *graph, Vec2 pos) {
    u32 idx = g_terrain_find_cell(tg, graph, pos);
    return graph->centers[idx].elevation;
}

bool g_terrain_is_water(const TerrainGrid *tg, const MapGraph *graph, Vec2 pos) {
    u32 idx = g_terrain_find_cell(tg, graph, pos);
    return graph->centers[idx].water;
}

u8 g_terrain_get_river(const Map *map, Vec2 pos) {
    if (!map->river_mask) return 0;
    s32 px = (s32)(pos.x * (f32)map->params.raster_w);
    s32 py = (s32)(pos.y * (f32)map->params.raster_h);
    if (px < 0 || py < 0 || px >= (s32)map->params.raster_w || py >= (s32)map->params.raster_h)
        return 0;
    return map->river_mask[(u32)py * map->params.raster_w + (u32)px];
}
