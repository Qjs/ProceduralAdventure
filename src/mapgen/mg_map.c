#include "mg_map.h"
#include "mg_points.h"
#include "mg_mesh.h"
#include "mg_island.h"
#include "mg_elevation.h"
#include "mg_raster.h"
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"
#include <stdlib.h>
#include <string.h>

void mg_map_init(Map *map) {
    memset(map, 0, sizeof(*map));
    map->params = (MapParams){
        .seed           = 420,
        .site_count     = 16000,
        .raster_w       = 1024,
        .raster_h       = 1024,
        .noise_freq     = 3.0f,
        .noise_octaves  = 6,
        .noise_gain     = 0.35f,
        .radial_falloff = 0.40f,
        .radial_power   = 2.0f,
        .land_threshold = 0.45f,
        .elevation_gamma = 0.8f,
        .snow_threshold  = 0.75f,
    };
}

void mg_map_generate(Map *map) {
    // Free previous graph (keeps params and pixel buffer)
    mg_free_mesh(&map->graph);

    // 1. Generate points
    u32 actual_count = 0;
    f32 *coords = mg_generate_points(map->params.site_count, map->params.seed, &actual_count);
    if (!coords || actual_count < 3) {
        free(coords);
        return;
    }

    // 2. Build mesh
    if (!mg_build_mesh(&map->graph, coords, actual_count)) {
        free(coords);
        return;
    }
    free(coords);

    // 3. Island shape classification
    mg_classify_island(&map->graph, &map->params);

    // 4. Elevation
    mg_assign_elevation(&map->graph, &map->params);

    // 5. Rasterize
    mg_rasterize(map);
}

void mg_map_free(Map *map) {
    mg_free_mesh(&map->graph);
    free(map->pixels);
    map->pixels = NULL;
}

bool mg_map_imgui_panel(Map *map) {
    bool regenerate = false;
    MapParams *p = &map->params;

    igBegin("Map Controls", NULL, 0);

    int seed_i = (int)p->seed;
    if (igSliderInt("Seed", &seed_i, 0, 1000, "%d", 0))
        p->seed = (u32)seed_i;

    int sites_i = (int)p->site_count;
    if (igSliderInt("Sites", &sites_i, 100, 20000, "%d", 0))
        p->site_count = (u32)sites_i;

    igSliderFloat("Noise Freq", &p->noise_freq, 0.5f, 10.0f, "%.1f", 0);

    igSliderInt("Octaves", &p->noise_octaves, 1, 8, "%d", 0);

    igSliderFloat("Noise Gain", &p->noise_gain, 0.1f, 0.9f, "%.2f", 0);

    igSliderFloat("Radial Falloff", &p->radial_falloff, 0.0f, 3.0f, "%.2f", 0);

    igSliderFloat("Radial Power", &p->radial_power, 0.5f, 5.0f, "%.1f", 0);

    igSliderFloat("Land Threshold", &p->land_threshold, -0.5f, 0.5f, "%.2f", 0);

    igSeparatorText("Elevation");
    igSliderFloat("Elev Gamma", &p->elevation_gamma, 0.2f, 3.0f, "%.2f", 0);
    igSliderFloat("Snow Threshold", &p->snow_threshold, 0.3f, 1.0f, "%.2f", 0);

    igSpacing();
    if (igButton("Regenerate", (ImVec2_c){0, 0}))
        regenerate = true;

    igEnd();
    return regenerate;
}
