#include "app.h"
#include "mapgen/mg_raster.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

static f64 get_time_seconds(void) {
    return (f64)SDL_GetPerformanceCounter() / (f64)SDL_GetPerformanceFrequency();
}

static const char *env_effect_name(EnvironmentalEffect e) {
    switch (e) {
        case ENV_EFFECT_BOULDERS: return "Boulder Storm";
        case ENV_EFFECT_WAVE:     return "Tidal Surge";
        case ENV_EFFECT_LAVA:     return "Molten Inferno";
        case ENV_EFFECT_NONE:
        default:                  return "None";
    }
}

static bool is_boss_level_index(u32 level) {
    return ((level + 1) % 5) == 0;
}

static void restart_game(App *app) {
    app->level = 0;
    memset(&app->progression, 0, sizeof(app->progression));
    app->map.params.seed = (u32)time(NULL) % 1001;
    app->map.params.boss_theme = is_boss_level_index(app->level);
    app->map.lava_rivers = is_boss_level_index(app->level);
    mg_map_generate(&app->map);
    if (app->map_texture) {
        SDL_DestroyTexture(app->map_texture);
        app->map_texture = NULL;
    }
    mg_upload_texture(&app->map, app->renderer, &app->map_texture);
    g_game_init(&app->game, app->renderer, &app->map.graph, app->level, app->progression.stat_levels);
    app->paused = false;
    app->game_over = false;
    app->upgrading = false;
    app->show_intro = true;
}

static void apply_custom_imgui_style(void) {
    ImGuiStyle *style = igGetStyle();
    style->WindowRounding = 8.0f;
    style->FrameRounding = 6.0f;
    style->GrabRounding = 6.0f;
    style->ScrollbarRounding = 6.0f;
    style->FrameBorderSize = 1.0f;
    style->WindowBorderSize = 1.0f;
    style->ItemSpacing = (ImVec2){8.0f, 6.0f};
    style->FramePadding = (ImVec2){8.0f, 6.0f};

    ImVec4 *c = style->Colors;
    // Replace default blue accents with warm bronze/red tones.
    c[ImGuiCol_WindowBg]         = (ImVec4){0.09f, 0.09f, 0.10f, 0.97f};
    c[ImGuiCol_ChildBg]          = (ImVec4){0.12f, 0.11f, 0.11f, 0.80f};
    c[ImGuiCol_PopupBg]          = (ImVec4){0.12f, 0.10f, 0.10f, 0.98f};
    c[ImGuiCol_Border]           = (ImVec4){0.42f, 0.27f, 0.20f, 0.65f};
    c[ImGuiCol_FrameBg]          = (ImVec4){0.17f, 0.14f, 0.13f, 0.90f};
    c[ImGuiCol_FrameBgHovered]   = (ImVec4){0.31f, 0.20f, 0.16f, 0.95f};
    c[ImGuiCol_FrameBgActive]    = (ImVec4){0.40f, 0.23f, 0.18f, 1.00f};
    c[ImGuiCol_TitleBg]          = (ImVec4){0.14f, 0.11f, 0.10f, 1.00f};
    c[ImGuiCol_TitleBgActive]    = (ImVec4){0.23f, 0.15f, 0.12f, 1.00f};
    c[ImGuiCol_Header]           = (ImVec4){0.30f, 0.19f, 0.15f, 0.85f};
    c[ImGuiCol_HeaderHovered]    = (ImVec4){0.45f, 0.27f, 0.19f, 0.95f};
    c[ImGuiCol_HeaderActive]     = (ImVec4){0.52f, 0.29f, 0.20f, 1.00f};
    // Dropdown arrow block uses button colors; make it distinct from standard dark themes.
    c[ImGuiCol_Button]           = (ImVec4){0.42f, 0.22f, 0.16f, 0.88f};
    c[ImGuiCol_ButtonHovered]    = (ImVec4){0.58f, 0.31f, 0.20f, 0.95f};
    c[ImGuiCol_ButtonActive]     = (ImVec4){0.66f, 0.35f, 0.23f, 1.00f};
    c[ImGuiCol_CheckMark]        = (ImVec4){0.92f, 0.74f, 0.44f, 1.00f};
    c[ImGuiCol_SliderGrab]       = (ImVec4){0.76f, 0.44f, 0.25f, 0.95f};
    c[ImGuiCol_SliderGrabActive] = (ImVec4){0.88f, 0.54f, 0.30f, 1.00f};
    c[ImGuiCol_Separator]        = (ImVec4){0.45f, 0.30f, 0.22f, 0.60f};
    c[ImGuiCol_SeparatorHovered] = (ImVec4){0.63f, 0.40f, 0.25f, 0.80f};
    c[ImGuiCol_SeparatorActive]  = (ImVec4){0.72f, 0.46f, 0.28f, 0.95f};
}

bool app_init(App *app, const char *title, int w, int h, s32 seed) {
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
    if (!ImGui_SDL3_LoadFont("assets/Almendra-Regular.ttf", 18.0f) &&
        !ImGui_SDL3_LoadFont("../assets/Almendra-Regular.ttf", 18.0f)) {
        SDL_Log("Font load failed, using default");
    }
    apply_custom_imgui_style();

    app->running = true;
    app->bg[0] = 0.1f;
    app->bg[1] = 0.1f;
    app->bg[2] = 0.1f;
    app->bg[3] = 1.0f;

    // Initialize and generate map
    mg_map_init(&app->map);
    if (seed >= 0) {
        app->map.params.seed = (u32)seed;
    } else {
        app->map.params.seed = (u32)time(NULL) % 1001;
    }
    app->map.params.boss_theme = is_boss_level_index(app->level);
    app->map.lava_rivers = is_boss_level_index(app->level);
    mg_map_generate(&app->map);
    app->map_texture = NULL;
    mg_upload_texture(&app->map, app->renderer, &app->map_texture);

    // Initialize game state
    g_game_init(&app->game, app->renderer, &app->map.graph, app->level, app->progression.stat_levels);
    app->upgrading = false;
    app->show_intro = true;

    // Initialize timing
    app->last_time = get_time_seconds();
    app->dt = 0.0;

    // UI defaults
    app->show_map_gen = false;
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
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
            if (!app->show_intro && !app->upgrading && !app->game_over) {
                app->paused = !app->paused;
            }
        }
    }
}

void app_update(App *app) {
    // Compute delta time
    f64 now = get_time_seconds();
    app->dt = now - app->last_time;
    app->last_time = now;
    if (app->dt > 0.1) app->dt = 0.1; // clamp to avoid spiral of death

    // Pause game while on intro / upgrade / pause / game-over modal
    if (app->upgrading || app->show_intro || app->paused || app->game_over) return;

    g_game_update(&app->game, &app->map, app->dt);

    // Check for player death
    if (!app->game.state.player.alive) {
        app->game_over = true;
        return;
    }

    // Level transition — tally XP, regenerate map, show upgrade screen
    if (app->game.state.level_complete) {
        GameState *gs = &app->game.state;
        u32 earned = gs->enemies_killed * 5 + gs->orbs_collected * 10 + 50;
        app->progression.xp += earned;
        app->progression.total_xp += earned;

        app->level++;
        app->map.params.seed++;
        app->map.params.boss_theme = is_boss_level_index(app->level);
        app->map.lava_rivers = is_boss_level_index(app->level);
        mg_map_generate(&app->map);
        if (app->map_texture) {
            SDL_DestroyTexture(app->map_texture);
            app->map_texture = NULL;
        }
        mg_upload_texture(&app->map, app->renderer, &app->map_texture);
        g_game_init(&app->game, app->renderer, &app->map.graph, app->level, app->progression.stat_levels);
        app->upgrading = true;
        app->show_intro = false;
    }
}

void app_render(App *app) {
    ImGui_SDL3_NewFrame();

    SDL_SetRenderDrawColorFloat(app->renderer,
        app->bg[0], app->bg[1], app->bg[2], app->bg[3]);
    SDL_RenderClear(app->renderer);

    int win_w, win_h;
    SDL_GetWindowSize(app->window, &win_w, &win_h);

    // Right panel width (responsive; keep game viewport usable on smaller canvases)
    float panel_w = 360.0f;
    float min_game_w = 220.0f;
    if (win_w < 1200) panel_w = (float)win_w * 0.33f;
    if (panel_w < 220.0f) panel_w = 220.0f;
    if (panel_w > 420.0f) panel_w = 420.0f;
    if (panel_w > (float)win_w - min_game_w)
        panel_w = (float)win_w - min_game_w;
    if (panel_w < 180.0f) panel_w = 180.0f;
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
    igSeparatorText("Command Tent");

    igCheckbox("Cartographer's Table", &app->show_map_gen);
    igCheckbox("Expedition Log", &app->show_player_status);
    igCheckbox("Scout's Map", &app->show_minimap);

    igSpacing();

    // -- Map Generation section --
    bool regenerate = false;
    if (app->show_map_gen) {
        if (igCollapsingHeader_TreeNodeFlags("Cartographer's Table", ImGuiTreeNodeFlags_DefaultOpen)) {
            regenerate = mg_map_imgui_controls(&app->map);
        }
    }

    // -- Player Status section --
    if (app->show_player_status) {
        if (igCollapsingHeader_TreeNodeFlags("Expedition Log", ImGuiTreeNodeFlags_DefaultOpen)) {
            Unit *player = &app->game.state.player;
            GameState *gs = &app->game.state;
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

            const char *cd_label = cd_frac >= 1.0f ? "Strike Ready" : "Recovering...";
            igPushStyleColor_Vec4(ImGuiCol_PlotHistogram,
                (ImVec4_c){0.2f, 0.5f, 0.9f, 1.0f});
            igProgressBar(cd_frac, (ImVec2_c){-1, 0}, cd_label);
            igPopStyleColor(1);

            igText("Bearing: (%.2f, %.2f)", player->pos.x, player->pos.y);
            igText("Expedition %u  Relics: %u / %u", app->level + 1, app->game.state.orbs_collected, NUM_COLLECT_ORBS);
            igText("Glory: %u  Lifetime Glory: %u", app->progression.xp, app->progression.total_xp);

            igSpacing();
            igSeparatorText("Arcane Auras");
            bool any_effect = false;
            if (gs->melee_boost_timer > 0.0f) {
                igTextColored((ImVec4_c){1.0f, 0.45f, 0.45f, 1.0f},
                              "Warrior's Fury: %.1fs", gs->melee_boost_timer);
                any_effect = true;
            }
            if (gs->archer_boost_timer > 0.0f) {
                igTextColored((ImVec4_c){1.0f, 0.8f, 0.4f, 1.0f},
                              "Eagle Eye: %.1fs", gs->archer_boost_timer);
                any_effect = true;
            }
            if (gs->mage_boost_timer > 0.0f) {
                igTextColored((ImVec4_c){0.7f, 0.5f, 1.0f, 1.0f},
                              "Arcane Surge: %.1fs", gs->mage_boost_timer);
                any_effect = true;
            }
            if (gs->env_active_effect != ENV_EFFECT_NONE) {
                igTextColored((ImVec4_c){1.0f, 0.6f, 0.3f, 1.0f},
                              "Peril — %s: %.1fs",
                              env_effect_name(gs->env_active_effect), gs->env_effect_timer);
                any_effect = true;
            }
            if (!any_effect) {
                igTextDisabled("The air is calm... for now");
            }
        }
    }

    // -- Squad Status section --
    if (app->show_player_status) {
        if (igCollapsingHeader_TreeNodeFlags("War Party", ImGuiTreeNodeFlags_DefaultOpen)) {
            static const char *stance_names[] = {
                [STANCE_AGGRESSIVE] = "Reckless Assault",
                [STANCE_DEFENSIVE]  = "Iron Guard",
                [STANCE_PASSIVE]    = "Watchful Calm",
            };
            const char *stance = stance_names[app->game.state.squad_stance];
            igText("Formation: %s  [1/2/3]", stance);
            igSpacing();

            static const char *role_names[] = {
                [ROLE_MELEE]  = "Knight",
                [ROLE_ARCHER] = "Ranger",
                [ROLE_HEALER] = "Cleric",
                [ROLE_MAGE]   = "Sorcerer",
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

                const char *cd_label = cd_frac >= 1.0f ? "Strike Ready" : "Recovering...";
                igPushStyleColor_Vec4(ImGuiCol_PlotHistogram,
                    (ImVec4_c){0.2f, 0.5f, 0.9f, 1.0f});
                igProgressBar(cd_frac, (ImVec2_c){-1, 0}, cd_label);
                igPopStyleColor(1);

                igPopID();

                if (i < gs->num_squad - 1) igSpacing();
            }
        }
    }

    // -- Stance Info section (collapsed by default) --
    if (igCollapsingHeader_TreeNodeFlags("Battle Codex", 0)) {
        igTextColored((ImVec4_c){0.9f, 0.6f, 0.2f, 1.0f}, "Knight");
        igBulletText("[1] Cleave: Carve through nearby foes (50%% splash)");
        igBulletText("[2] Fortify: Don steel-forged armor (+3 defense)");
        igBulletText("[3] Riposte: Deflect blows back at attackers (33%%)");
        igSpacing();

        igTextColored((ImVec4_c){0.4f, 0.8f, 0.3f, 1.0f}, "Ranger");
        igBulletText("[1] Piercing Shot: Arrow punches through all in its path");
        igBulletText("[2] Gale Arrow: Blast enemies backward");
        igBulletText("[3] Pathfinder: Traverse any terrain with haste");
        igSpacing();

        igTextColored((ImVec4_c){0.3f, 0.9f, 0.7f, 1.0f}, "Cleric");
        igBulletText("[1] Mending Light: Restore a single ally's wounds");
        igBulletText("[2] Radiant Burst: Heal all nearby allies (40%%)");
        igBulletText("[3] Blessed Ward: Shield allies with holy armor (+2)");
        igSpacing();

        igTextColored((ImVec4_c){0.6f, 0.4f, 1.0f, 1.0f}, "Sorcerer");
        igBulletText("[1] Fireball: Unleash searing flames (1.5x damage)");
        igBulletText("[2] Frostbolt: Chill enemies to a crawl");
        igBulletText("[3] Levitate: Walk on water with arcane swiftness");
    }

    // -- Minimap section --
    if (app->show_minimap && app->map_texture) {
        if (igCollapsingHeader_TreeNodeFlags("Scout's Map", ImGuiTreeNodeFlags_DefaultOpen)) {
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

    // ---- Intro modal (startup only) ----
    if (app->show_intro) {
        ImVec2_c center = {(float)win_w * 0.5f, (float)win_h * 0.5f};
        igSetNextWindowPos(center, ImGuiCond_Always, (ImVec2_c){0.5f, 0.5f});
        igSetNextWindowSize((ImVec2_c){600, 0}, ImGuiCond_Always);

        ImGuiWindowFlags intro_flags =
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize;

        igBegin("The Wilds Await", NULL, intro_flags);
        igTextWrapped("Brave adventurer! A chain of uncharted islands stretches before you. Rally your war party, gather 5 ancient relics, and step through the portal to reach the next frontier.");
        igSpacing();
        igBulletText("Chart your course with WASD / Arrow Keys");
        igBulletText("Command your formation: [1] Reckless Assault  [2] Iron Guard  [3] Watchful Calm");
        igBulletText("Your party: Knight, Ranger, Cleric, Sorcerer — each fights differently per formation");
        igBulletText("Every 5th expedition: gather relics, then slay the guardian to unseal the portal");
        igSpacing();
        igTextWrapped("Remember: a wise commander shifts formation often. It is your greatest weapon.");
        igSeparator();
        if (igButton("Embark!", (ImVec2_c){-1, 36})) {
            app->show_intro = false;
        }
        igEnd();
    }

    // ---- Upgrade screen modal ----
    if (app->upgrading) {
        static const char *role_names[] = { "Knight", "Ranger", "Cleric", "Sorcerer" };
        // Per-role stat names with adventurous labels
        static const char *stat_names[MAX_SQUAD][4] = {
            { "Vitality", "Might",     "Armor",  "Swiftness" },  // Knight
            { "Vitality", "Precision",  "Reach",  "Swiftness" },  // Ranger
            { "Vitality", "Mending",   "Reach",  "Swiftness" },  // Cleric
            { "Vitality", "Sorcery",   "Reach",  "Swiftness" },  // Sorcerer
        };

        ImVec2_c center = {(float)win_w * 0.5f, (float)win_h * 0.5f};
        igSetNextWindowPos(center, ImGuiCond_Always, (ImVec2_c){0.5f, 0.5f});
        igSetNextWindowSize((ImVec2_c){420, 0}, ImGuiCond_Always);

        ImGuiWindowFlags modal_flags =
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize;

        igBegin("Forge of Champions", NULL, modal_flags);

        igText("Expedition %u", app->level + 1);
        igText("Glory: %u  (Lifetime: %u)", app->progression.xp, app->progression.total_xp);
        igSeparator();

        for (u32 i = 0; i < MAX_SQUAD; i++) {
            igPushID_Int((int)i);
            igText("%s", role_names[i]);

            for (u32 s = 0; s < 4; s++) {
                igPushID_Int((int)s);
                u32 lvl = app->progression.stat_levels[i][s];
                igText("  %s Lv.%u", stat_names[i][s], lvl);
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
        if (igButton("March Forth!", (ImVec2_c){-1, 32})) {
            app->upgrading = false;
            // Re-init game with updated stat levels
            g_game_init(&app->game, app->renderer, &app->map.graph, app->level, app->progression.stat_levels);
        }

        igEnd();
    }

    // ---- Pause modal ----
    if (app->paused) {
        ImVec2_c center = {(float)win_w * 0.5f, (float)win_h * 0.5f};
        igSetNextWindowPos(center, ImGuiCond_Always, (ImVec2_c){0.5f, 0.5f});
        igSetNextWindowSize((ImVec2_c){320, 0}, ImGuiCond_Always);

        ImGuiWindowFlags pause_flags =
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize;

        igBegin("Camp Rest", NULL, pause_flags);
        igText("Your party rests by the fire...");
        igSpacing();
        if (igButton("Break Camp", (ImVec2_c){-1, 36})) {
            app->paused = false;
        }
        if (igButton("Abandon Quest", (ImVec2_c){-1, 36})) {
            restart_game(app);
        }
        igEnd();
    }

    // ---- Game Over modal ----
    if (app->game_over) {
        ImVec2_c center = {(float)win_w * 0.5f, (float)win_h * 0.5f};
        igSetNextWindowPos(center, ImGuiCond_Always, (ImVec2_c){0.5f, 0.5f});
        igSetNextWindowSize((ImVec2_c){380, 0}, ImGuiCond_Always);

        ImGuiWindowFlags go_flags =
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize;

        igBegin("Fallen in Battle", NULL, go_flags);
        igTextColored((ImVec4_c){1.0f, 0.3f, 0.3f, 1.0f}, "Your quest ends here, brave adventurer...");
        igSpacing();
        igSeparator();
        igText("Expeditions Survived: %u", app->level + 1);
        igText("Foes Vanquished: %u", app->game.state.enemies_killed);
        igText("Glory Earned: %u", app->progression.total_xp);
        igSeparator();
        igSpacing();
        if (igButton("Rise Again", (ImVec2_c){-1, 36})) {
            restart_game(app);
        }
        igEnd();
    }

    // Handle regeneration
    if (regenerate) {
        app->map.params.boss_theme = is_boss_level_index(app->level);
        app->map.lava_rivers = is_boss_level_index(app->level);
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
