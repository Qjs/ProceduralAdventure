#include "app.h"
#include "mapgen/mg_raster.h"

static f64 get_time_seconds(void) {
    return (f64)SDL_GetPerformanceCounter() / (f64)SDL_GetPerformanceFrequency();
}

bool app_init(App *app, const char *title, int w, int h) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    app->window = SDL_CreateWindow(title, w, h, SDL_WINDOW_RESIZABLE);
    if (!app->window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }

    app->renderer = SDL_CreateRenderer(app->window, NULL);
    if (!app->renderer) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        return false;
    }

    if (!ImGui_SDL3_Init(app->window, app->renderer)) {
        SDL_Log("ImGui_SDL3_Init failed");
        return false;
    }

    app->running = true;
    app->bg[0] = 0.1f;
    app->bg[1] = 0.1f;
    app->bg[2] = 0.1f;
    app->bg[3] = 1.0f;

    // Initialize and generate map
    mg_map_init(&app->map);
    mg_map_generate(&app->map);
    app->map_texture = NULL;
    mg_upload_texture(&app->map, app->renderer, &app->map_texture);

    // Initialize game state
    g_game_init(&app->game, &app->map.graph);

    // Initialize timing
    app->last_time = get_time_seconds();
    app->dt = 0.0;

    return true;
}

void app_shutdown(App *app) {
    g_game_shutdown(&app->game);
    if (app->map_texture) SDL_DestroyTexture(app->map_texture);
    mg_map_free(&app->map);
    ImGui_SDL3_Shutdown();
    if (app->renderer) SDL_DestroyRenderer(app->renderer);
    if (app->window)   SDL_DestroyWindow(app->window);
    SDL_Quit();
}

void app_process_events(App *app) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_SDL3_ProcessEvent(&event);
        if (event.type == SDL_EVENT_QUIT) {
            app->running = false;
        }
    }
}

void app_update(App *app) {
    // Compute delta time
    f64 now = get_time_seconds();
    app->dt = now - app->last_time;
    app->last_time = now;
    if (app->dt > 0.1) app->dt = 0.1; // clamp to avoid spiral of death

    g_game_update(&app->game, &app->map.graph, app->dt);
}

void app_render(App *app) {
    ImGui_SDL3_NewFrame();

    SDL_SetRenderDrawColorFloat(app->renderer,
        app->bg[0], app->bg[1], app->bg[2], app->bg[3]);
    SDL_RenderClear(app->renderer);

    // Compute map rect (fit to window, centered)
    SDL_FRect map_rect = {0};
    if (app->map_texture) {
        int win_w, win_h;
        SDL_GetWindowSize(app->window, &win_w, &win_h);
        float map_size = (float)(win_w < win_h ? win_w : win_h);
        map_rect = (SDL_FRect){
            ((float)win_w - map_size) * 0.5f,
            ((float)win_h - map_size) * 0.5f,
            map_size, map_size
        };
        SDL_RenderTexture(app->renderer, app->map_texture, NULL, &map_rect);
    }

    // Game rendering (on top of map)
    g_game_render(&app->game, app->renderer, map_rect);

    // ImGui panel
    if (mg_map_imgui_panel(&app->map)) {
        mg_map_generate(&app->map);
        if (app->map_texture) {
            SDL_DestroyTexture(app->map_texture);
            app->map_texture = NULL;
        }
        mg_upload_texture(&app->map, app->renderer, &app->map_texture);
        // Reset game state on map regeneration
        g_game_init(&app->game, &app->map.graph);
    }

    ImGui_SDL3_Render(app->renderer);
    SDL_RenderPresent(app->renderer);
}
