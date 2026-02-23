#ifndef APP_H
#define APP_H

#include <SDL3/SDL.h>
#include <stdbool.h>
#include "imgui_sdl3.h"
#include "mapgen/mg_map.h"

typedef struct {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    bool          running;
    float         bg[4]; // RGBA clear color
    Map           map;
    SDL_Texture  *map_texture;
} App;

// Lifecycle
bool app_init(App *app, const char *title, int w, int h);
void app_shutdown(App *app);

// Per-frame
void app_process_events(App *app);
void app_update(App *app);
void app_render(App *app);

#endif
