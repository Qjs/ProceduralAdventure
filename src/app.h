#ifndef APP_H
#define APP_H

#include <SDL3/SDL.h>
#include <stdbool.h>
#include "imgui_sdl3.h"
#include "mapgen/mg_map.h"
#include "game/g_game.h"

typedef struct {
    u32 xp;                         // unspent XP pool
    u32 total_xp;                   // lifetime earned (for display)
    u32 stat_levels[MAX_SQUAD][4];  // [unit][stat] upgrade counts
    // stat indices: 0=HP, 1=Damage, 2=Range, 3=Cooldown
} Progression;

typedef struct {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    bool          running;
    float         bg[4]; // RGBA clear color
    Map           map;
    SDL_Texture  *map_texture;
    Game          game;
    f64           last_time;  // seconds (perf counter)
    f64           dt;         // delta time for current frame
    u32           level;
    Progression   progression;
    bool          upgrading;
    bool          show_intro;

    // UI toggle state
    bool          show_map_gen;
    bool          show_player_status;
    bool          show_minimap;
} App;

// Lifecycle
// seed < 0 means randomize
bool app_init(App *app, const char *title, int w, int h, s32 seed);
void app_shutdown(App *app);

// Per-frame
void app_process_events(App *app);
void app_update(App *app);
void app_render(App *app);

#endif
