#include "app.h"
#include "mapgen/mg_raster.h"
#include <string.h>
#include <stdio.h>

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

static f64 get_time_seconds(void) {
    return (f64)SDL_GetPerformanceCounter() / (f64)SDL_GetPerformanceFrequency();
}

bool app_init(App *app, const char *title, int w, int h) {
    memset(app, 0, sizeof(*app));
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
    g_game_init(&app->game, app->renderer, &app->map.graph, app->level, app->progression.stat_levels);
    app->upgrading = true;

    // Initialize timing
    app->last_time = get_time_seconds();
    app->dt = 0.0;

    // UI defaults
    app->show_map_gen = true;
    app->show_player_status = true;
    app->show_minimap = true;

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

    // Pause game while on upgrade screen
    if (app->upgrading) return;

    g_game_update(&app->game, &app->map.graph, app->dt);

    // Level transition — tally XP, regenerate map, show upgrade screen
    if (app->game.state.level_complete) {
        GameState *gs = &app->game.state;
        u32 earned = gs->enemies_killed * 5 + gs->orbs_collected * 10 + 50;
        app->progression.xp += earned;
        app->progression.total_xp += earned;

        app->level++;
        app->map.params.seed++;
        mg_map_generate(&app->map);
        if (app->map_texture) {
            SDL_DestroyTexture(app->map_texture);
            app->map_texture = NULL;
        }
        mg_upload_texture(&app->map, app->renderer, &app->map_texture);
        g_game_init(&app->game, app->renderer, &app->map.graph, app->level, app->progression.stat_levels);
        app->upgrading = true;
    }
}

void app_render(App *app) {
    ImGui_SDL3_NewFrame();

    SDL_SetRenderDrawColorFloat(app->renderer,
        app->bg[0], app->bg[1], app->bg[2], app->bg[3]);
    SDL_RenderClear(app->renderer);

    int win_w, win_h;
    SDL_GetWindowSize(app->window, &win_w, &win_h);

    // Right panel width
    float panel_w = 280.0f;
    float game_w = (float)win_w - panel_w;
    float game_h = (float)win_h;

    // ---- Render game viewport (left area) ----
    SDL_FRect dst_rect = {0};
    SDL_FRect virtual_map_rect = {0};
    if (app->map_texture) {
        float map_size = game_w < game_h ? game_w : game_h;
        dst_rect = (SDL_FRect){
            (game_w - map_size) * 0.5f,
            (game_h - map_size) * 0.5f,
            map_size, map_size
        };

        // Camera-based source rect
        Camera *cam = &app->game.state.camera;
        float view_size = 1.0f / cam->zoom;
        float view_x = cam->pos.x - view_size * 0.5f;
        float view_y = cam->pos.y - view_size * 0.5f;

        if (view_x < 0.0f) view_x = 0.0f;
        if (view_x > 1.0f - view_size) view_x = 1.0f - view_size;
        if (view_y < 0.0f) view_y = 0.0f;
        if (view_y > 1.0f - view_size) view_y = 1.0f - view_size;

        float tex_w, tex_h;
        SDL_GetTextureSize(app->map_texture, &tex_w, &tex_h);
        SDL_FRect src_rect = {
            view_x * tex_w, view_y * tex_h,
            view_size * tex_w, view_size * tex_h
        };
        SDL_RenderTexture(app->renderer, app->map_texture, &src_rect, &dst_rect);

        virtual_map_rect = (SDL_FRect){
            dst_rect.x - (view_x / view_size) * dst_rect.w,
            dst_rect.y - (view_y / view_size) * dst_rect.h,
            dst_rect.w / view_size,
            dst_rect.h / view_size
        };
    }

    g_game_render(&app->game, app->renderer, virtual_map_rect);

    // ---- Right-side ImGui panel ----
    igSetNextWindowPos((ImVec2_c){(float)win_w - panel_w, 0}, ImGuiCond_Always, (ImVec2_c){0, 0});
    igSetNextWindowSize((ImVec2_c){panel_w, (float)win_h}, ImGuiCond_Always);
    ImGuiWindowFlags panel_flags =
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;

    igBegin("##SidePanel", NULL, panel_flags);
    igSeparatorText("Panel");

    igCheckbox("Show Map Generation", &app->show_map_gen);
    igCheckbox("Show Player Status", &app->show_player_status);
    igCheckbox("Show Minimap", &app->show_minimap);

    igSpacing();

    // -- Map Generation section --
    bool regenerate = false;
    if (app->show_map_gen) {
        if (igCollapsingHeader_TreeNodeFlags("Map Generation", ImGuiTreeNodeFlags_DefaultOpen)) {
            regenerate = mg_map_imgui_controls(&app->map);
        }
    }

    // -- Player Status section --
    if (app->show_player_status) {
        if (igCollapsingHeader_TreeNodeFlags("Player Status", ImGuiTreeNodeFlags_DefaultOpen)) {
            Unit *player = &app->game.state.player;
            float hp_frac = player->hp / player->max_hp;
            if (hp_frac < 0.0f) hp_frac = 0.0f;
            if (hp_frac > 1.0f) hp_frac = 1.0f;

            char hp_buf[32];
            snprintf(hp_buf, sizeof(hp_buf), "%.0f / %.0f", player->hp, player->max_hp);
            igPushStyleColor_Vec4(ImGuiCol_PlotHistogram,
                (ImVec4_c){0.2f + 0.8f * (1.0f - hp_frac), 0.8f * hp_frac, 0.0f, 1.0f});
            igProgressBar(hp_frac, (ImVec2_c){-1, 0}, hp_buf);
            igPopStyleColor(1);

            float cd_frac = 0.0f;
            if (player->cooldown > 0.0f)
                cd_frac = 1.0f - (player->cooldown_timer / player->cooldown);
            if (cd_frac < 0.0f) cd_frac = 0.0f;
            if (cd_frac > 1.0f) cd_frac = 1.0f;

            const char *cd_label = cd_frac >= 1.0f ? "Ready" : "Cooldown";
            igPushStyleColor_Vec4(ImGuiCol_PlotHistogram,
                (ImVec4_c){0.2f, 0.5f, 0.9f, 1.0f});
            igProgressBar(cd_frac, (ImVec2_c){-1, 0}, cd_label);
            igPopStyleColor(1);

            igText("Pos: (%.2f, %.2f)", player->pos.x, player->pos.y);
            igText("Level: %u  Orbs: %u / %u", app->level + 1, app->game.state.orbs_collected, NUM_COLLECT_ORBS);
        }
    }

    // -- Squad Status section --
    if (app->show_player_status) {
        if (igCollapsingHeader_TreeNodeFlags("Squad Status", ImGuiTreeNodeFlags_DefaultOpen)) {
            static const char *stance_names[] = {
                [STANCE_AGGRESSIVE] = "Aggressive",
                [STANCE_DEFENSIVE]  = "Defensive",
                [STANCE_PASSIVE]    = "Passive",
            };
            const char *stance = stance_names[app->game.state.squad_stance];
            igText("Stance: %s  [1/2/3]", stance);
            igSpacing();

            static const char *role_names[] = {
                [ROLE_MELEE]  = "Melee",
                [ROLE_ARCHER] = "Archer",
                [ROLE_HEALER] = "Healer",
                [ROLE_MAGE]   = "Mage",
            };
            GameState *gs = &app->game.state;
            for (u32 i = 0; i < gs->num_squad; i++) {
                Unit *u = &gs->squad[i];
                if (!u->alive) continue;

                const char *name = (u->role < ROLE_COUNT) ? role_names[u->role] : "???";
                if (!name) name = "???";

                igPushID_Int((int)i);

                // Role name with colored bullet
                ImVec4_c role_col = {
                    u->color[0] / 255.0f,
                    u->color[1] / 255.0f,
                    u->color[2] / 255.0f,
                    1.0f
                };
                igTextColored(role_col, "%s", name);

                // HP bar
                float hp_frac = u->hp / u->max_hp;
                if (hp_frac < 0.0f) hp_frac = 0.0f;
                if (hp_frac > 1.0f) hp_frac = 1.0f;

                char hp_buf[32];
                snprintf(hp_buf, sizeof(hp_buf), "%.0f / %.0f", u->hp, u->max_hp);
                igPushStyleColor_Vec4(ImGuiCol_PlotHistogram,
                    (ImVec4_c){0.2f + 0.8f * (1.0f - hp_frac), 0.8f * hp_frac, 0.0f, 1.0f});
                igProgressBar(hp_frac, (ImVec2_c){-1, 0}, hp_buf);
                igPopStyleColor(1);

                // Cooldown bar
                float cd_frac = 0.0f;
                if (u->cooldown > 0.0f)
                    cd_frac = 1.0f - (u->cooldown_timer / u->cooldown);
                if (cd_frac < 0.0f) cd_frac = 0.0f;
                if (cd_frac > 1.0f) cd_frac = 1.0f;

                const char *cd_label = cd_frac >= 1.0f ? "Ready" : "Cooldown";
                igPushStyleColor_Vec4(ImGuiCol_PlotHistogram,
                    (ImVec4_c){0.2f, 0.5f, 0.9f, 1.0f});
                igProgressBar(cd_frac, (ImVec2_c){-1, 0}, cd_label);
                igPopStyleColor(1);

                igPopID();

                if (i < gs->num_squad - 1) igSpacing();
            }
        }
    }

    // -- Minimap section --
    if (app->show_minimap && app->map_texture) {
        if (igCollapsingHeader_TreeNodeFlags("World Map", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImVec2_c avail = igGetContentRegionAvail();
            float mm_size = avail.x;
            if (mm_size > avail.y) mm_size = avail.y;
            if (mm_size < 64.0f) mm_size = 64.0f;

            // Draw the map image
            ImTextureRef_c tex_ref = {NULL, (ImTextureID)(size_t)app->map_texture};
            ImVec2_c cursor = igGetCursorScreenPos();
            igImage(tex_ref, (ImVec2_c){mm_size, mm_size},
                    (ImVec2_c){0, 0}, (ImVec2_c){1, 1});

            // Overlay: camera viewport rect + player dot via draw list
            ImDrawList *dl = igGetWindowDrawList();

            Camera *cam = &app->game.state.camera;
            float view_size = 1.0f / cam->zoom;
            float vx = cam->pos.x - view_size * 0.5f;
            float vy = cam->pos.y - view_size * 0.5f;
            if (vx < 0.0f) vx = 0.0f;
            if (vx > 1.0f - view_size) vx = 1.0f - view_size;
            if (vy < 0.0f) vy = 0.0f;
            if (vy > 1.0f - view_size) vy = 1.0f - view_size;

            ImVec2_c rect_min = {cursor.x + vx * mm_size, cursor.y + vy * mm_size};
            ImVec2_c rect_max = {rect_min.x + view_size * mm_size, rect_min.y + view_size * mm_size};
            ImDrawList_AddRect(dl, rect_min, rect_max,
                               0xC8FFFFFF, 0.0f, 0, 1.5f);

            // Enemy dots (red)
            GameState *gs = &app->game.state;
            for (u32 i = 0; i < gs->num_enemies; i++) {
                if (!gs->enemies[i].alive) continue;
                float ex = cursor.x + gs->enemies[i].pos.x * mm_size;
                float ey = cursor.y + gs->enemies[i].pos.y * mm_size;
                ImVec2_c e_min = {ex - 1.5f, ey - 1.5f};
                ImVec2_c e_max = {ex + 1.5f, ey + 1.5f};
                ImDrawList_AddRectFilled(dl, e_min, e_max,
                                         0xFF2020CC, 0.0f, 0);
            }

            // Portal dot (purple, pulsing)
            if (gs->portal.active) {
                float ptx = cursor.x + gs->portal.pos.x * mm_size;
                float pty = cursor.y + gs->portal.pos.y * mm_size;
                ImVec2_c pt_min = {ptx - 3, pty - 3};
                ImVec2_c pt_max = {ptx + 3, pty + 3};
                ImDrawList_AddRectFilled(dl, pt_min, pt_max,
                                         0xFFF050AA, 0.0f, 0);
            }

            // Player dot
            float px = cursor.x + app->game.state.player.pos.x * mm_size;
            float py = cursor.y + app->game.state.player.pos.y * mm_size;
            ImVec2_c dot_min = {px - 2, py - 2};
            ImVec2_c dot_max = {px + 2, py + 2};
            ImDrawList_AddRectFilled(dl, dot_min, dot_max,
                                     0xFF64FF00, 0.0f, 0);
        }
    }

    igEnd();

    // ---- Upgrade screen modal ----
    if (app->upgrading) {
        static const char *role_names[] = { "Melee", "Archer", "Healer", "Mage" };
        static const char *stat_names[] = { "HP", "Damage", "Range", "Cooldown" };

        ImVec2_c center = {(float)win_w * 0.5f, (float)win_h * 0.5f};
        igSetNextWindowPos(center, ImGuiCond_Always, (ImVec2_c){0.5f, 0.5f});
        igSetNextWindowSize((ImVec2_c){420, 0}, ImGuiCond_Always);

        ImGuiWindowFlags modal_flags =
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize;

        igBegin("Upgrades", NULL, modal_flags);

        igText("Level %u", app->level + 1);
        igText("XP: %u  (Total: %u)", app->progression.xp, app->progression.total_xp);
        igSeparator();

        for (u32 i = 0; i < MAX_SQUAD; i++) {
            igPushID_Int((int)i);
            igText("%s", role_names[i]);

            for (u32 s = 0; s < 4; s++) {
                igPushID_Int((int)s);
                u32 lvl = app->progression.stat_levels[i][s];
                igText("  %s Lv.%u", stat_names[s], lvl);
                igSameLine(0, 8);
                bool can_buy = app->progression.xp >= 15;
                if (!can_buy) igBeginDisabled(true);
                if (igSmallButton("+")) {
                    app->progression.stat_levels[i][s]++;
                    app->progression.xp -= 15;
                }
                if (!can_buy) igEndDisabled();
                igPopID();
            }

            if (i < MAX_SQUAD - 1) igSeparator();
            igPopID();
        }

        igSpacing();
        igSeparator();
        if (igButton("Start Level", (ImVec2_c){-1, 32})) {
            app->upgrading = false;
            // Re-init game with updated stat levels
            g_game_init(&app->game, app->renderer, &app->map.graph, app->level, app->progression.stat_levels);
        }

        igEnd();
    }

    // Handle regeneration
    if (regenerate) {
        mg_map_generate(&app->map);
        if (app->map_texture) {
            SDL_DestroyTexture(app->map_texture);
            app->map_texture = NULL;
        }
        mg_upload_texture(&app->map, app->renderer, &app->map_texture);
        g_game_init(&app->game, app->renderer, &app->map.graph, app->level, app->progression.stat_levels);
    }

    ImGui_SDL3_Render(app->renderer);
    SDL_RenderPresent(app->renderer);
}
