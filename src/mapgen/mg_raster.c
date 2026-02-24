#include "mg_raster.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

static u32 pack_rgba(u8 r, u8 g, u8 b, u8 a) {
    // SDL_PIXELFORMAT_RGBA32 = native byte order
    return ((u32)r) | ((u32)g << 8) | ((u32)b << 16) | ((u32)a << 24);
}

void mg_rasterize(Map *map) {
    u32 w = map->params.raster_w;
    u32 h = map->params.raster_h;

    if (!map->pixels)
        map->pixels = malloc(sizeof(u32) * w * h);

    MapGraph *g = &map->graph;
    u32 n = g->num_centers;

    // Build uniform grid accelerator
    u32 grid_res = (u32)ceilf(sqrtf((f32)n));
    if (grid_res < 1) grid_res = 1;
    f32 cell_size = 1.0f / (f32)grid_res;

    // grid_cells[gy * grid_res + gx] = index of nearest center (or UINT32_MAX)
    u32 grid_total = grid_res * grid_res;
    u32 *grid_cells = malloc(sizeof(u32) * grid_total);
    memset(grid_cells, 0xFF, sizeof(u32) * grid_total);

    // Place each center in its grid cell
    for (u32 i = 0; i < n; i++) {
        u32 gx = (u32)(g->centers[i].pos.x / cell_size);
        u32 gy = (u32)(g->centers[i].pos.y / cell_size);
        if (gx >= grid_res) gx = grid_res - 1;
        if (gy >= grid_res) gy = grid_res - 1;
        u32 gi = gy * grid_res + gx;
        // Store just one center per cell (first wins); nearest-search checks neighbors
        if (grid_cells[gi] == UINT32_MAX)
            grid_cells[gi] = i;
    }

    f32 snow_thresh = map->params.snow_threshold;

    for (u32 py = 0; py < h; py++) {
        for (u32 px = 0; px < w; px++) {
            f32 fx = ((f32)px + 0.5f) / (f32)w;
            f32 fy = ((f32)py + 0.5f) / (f32)h;

            s32 gx = (s32)(fx / cell_size);
            s32 gy = (s32)(fy / cell_size);

            // Search 3x3 neighborhood for nearest center
            f32 best_dist = 1e30f;
            u32 best_idx = 0;

            for (s32 dy = -1; dy <= 1; dy++) {
                for (s32 dx = -1; dx <= 1; dx++) {
                    s32 nx = gx + dx;
                    s32 ny = gy + dy;
                    if (nx < 0 || ny < 0 || nx >= (s32)grid_res || ny >= (s32)grid_res)
                        continue;
                    u32 gi = (u32)ny * grid_res + (u32)nx;
                    u32 ci = grid_cells[gi];
                    if (ci == UINT32_MAX) continue;
                    f32 ddx = g->centers[ci].pos.x - fx;
                    f32 ddy = g->centers[ci].pos.y - fy;
                    f32 d2 = ddx*ddx + ddy*ddy;
                    if (d2 < best_dist) {
                        best_dist = d2;
                        best_idx = ci;
                    }
                }
            }

            // Fallback: brute force
            if (best_dist > 1e20f) {
                for (u32 i = 0; i < n; i++) {
                    f32 ddx = g->centers[i].pos.x - fx;
                    f32 ddy = g->centers[i].pos.y - fy;
                    f32 d2 = ddx*ddx + ddy*ddy;
                    if (d2 < best_dist) {
                        best_dist = d2;
                        best_idx = i;
                    }
                }
            }

            Center *c = &g->centers[best_idx];
            u32 col;
            if (map->params.boss_theme) {
                if (c->water) {
                    col = pack_rgba(35, 35, 42, 255);
                } else if (c->coast) {
                    col = pack_rgba(85, 72, 70, 255);
                } else {
                    f32 e = c->elevation;
                    if (e >= snow_thresh) {
                        f32 t = (e - snow_thresh) / (1.0f - snow_thresh + 0.001f);
                        if (t > 1.0f) t = 1.0f;
                        u8 r = (u8)(90 + t * 40);
                        u8 g = (u8)(85 + t * 30);
                        u8 b = (u8)(90 + t * 35);
                        col = pack_rgba(r, g, b, 255);
                    } else if (e >= snow_thresh * 0.7f) {
                        f32 t = (e - snow_thresh * 0.7f) / (snow_thresh * 0.3f + 0.001f);
                        if (t > 1.0f) t = 1.0f;
                        u8 r = (u8)(70 + t * 50);
                        u8 g = (u8)(62 + t * 36);
                        u8 b = (u8)(64 + t * 38);
                        col = pack_rgba(r, g, b, 255);
                    } else {
                        f32 t = e / (snow_thresh * 0.7f + 0.001f);
                        if (t > 1.0f) t = 1.0f;
                        u8 r = (u8)(52 + t * 36);
                        u8 g = (u8)(50 + t * 26);
                        u8 b = (u8)(54 + t * 30);
                        if (e > 0.42f && e < 0.62f) {
                            r = (u8)(r + 25);
                        }
                        col = pack_rgba(r, g, b, 255);
                    }
                }
            } else {
                if (c->water) {
                    // Ocean: deeper = darker blue
                    col = pack_rgba(30, 50, 120, 255);
                } else if (c->coast) {
                    col = pack_rgba(210, 190, 130, 255);
                } else {
                    f32 e = c->elevation;
                    if (e >= snow_thresh) {
                        // Snow: lerp white from snow_thresh to 1.0
                        f32 t = (e - snow_thresh) / (1.0f - snow_thresh + 0.001f);
                        if (t > 1.0f) t = 1.0f;
                        u8 base = (u8)(200 + t * 55);
                        col = pack_rgba(base, base, base, 255);
                    } else if (e >= snow_thresh * 0.7f) {
                        // Rocky/mountain: grey-brown
                        f32 t = (e - snow_thresh * 0.7f) / (snow_thresh * 0.3f + 0.001f);
                        if (t > 1.0f) t = 1.0f;
                        u8 r = (u8)(120 + t * 60);
                        u8 gr = (u8)(110 + t * 50);
                        u8 b = (u8)(90 + t * 50);
                        col = pack_rgba(r, gr, b, 255);
                    } else {
                        // Land: low=green, mid=darker green/brown
                        f32 t = e / (snow_thresh * 0.7f + 0.001f);
                        if (t > 1.0f) t = 1.0f;
                        u8 r = (u8)(60 + t * 60);
                        u8 gr = (u8)(140 - t * 30);
                        u8 b = (u8)(40 + t * 50);
                        col = pack_rgba(r, gr, b, 255);
                    }
                }
            }
            map->pixels[py * w + px] = col;
        }
    }

    free(grid_cells);
}

void mg_upload_texture(const Map *map, SDL_Renderer *renderer, SDL_Texture **tex) {
    u32 w = map->params.raster_w;
    u32 h = map->params.raster_h;

    if (!*tex) {
        *tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                 SDL_TEXTUREACCESS_STREAMING, w, h);
    }

    void *px;
    int pitch;
    if (SDL_LockTexture(*tex, NULL, &px, &pitch)) {
        for (u32 y = 0; y < h; y++) {
            memcpy((u8*)px + y * pitch, map->pixels + y * w, w * sizeof(u32));
        }
        SDL_UnlockTexture(*tex);
    }
}
