#ifndef MG_TYPES_H
#define MG_TYPES_H

#include "../utils/q_util.h"

typedef struct { f32 x, y; } Vec2;

typedef struct {
    Vec2  pos;
    f32   elevation;
    bool  water;
    bool  coast;
    bool  border;
    u32  *neighbors;   u32 num_neighbors;
    u32  *borders;     u32 num_borders;
    u32  *corners;     u32 num_corners;
} Center;

typedef struct {
    Vec2  pos;
    f32   elevation;
    bool  water;
    bool  coast;
    bool  border;
    u32  *adjacent;    u32 num_adjacent;   // neighboring corners
    u32  *touches;     u32 num_touches;    // adjacent centers
    u32  *protrudes;   u32 num_protrudes;  // adjacent edges
} Corner;

typedef struct {
    u32 d0, d1;        // two Delaunay sites (Centers)
    u32 v0, v1;        // two Voronoi vertices (Corners), v1 may be UINT32_MAX for border
    bool border;
} MapEdge;

typedef struct {
    Center  *centers;   u32 num_centers;
    Corner  *corners;   u32 num_corners;
    MapEdge *edges;     u32 num_edges;
    u32     *adj_buf;   // single allocation for all adjacency index arrays
} MapGraph;

typedef struct {
    u32  seed;
    u32  site_count;
    u32  raster_w, raster_h;
    f32  noise_freq;
    s32  noise_octaves;
    f32  noise_gain;
    f32  radial_falloff;
    f32  radial_power;
    f32  land_threshold;
    f32  elevation_gamma;
    f32  snow_threshold;
    bool boss_theme;
} MapParams;

typedef struct {
    MapParams  params;
    MapGraph   graph;
    u32       *pixels;    // RGBA pixel buffer (raster_w * raster_h)
} Map;

#endif
