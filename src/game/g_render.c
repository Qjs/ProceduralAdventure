#include "g_render.h"

// Convert game coords [0,1]² to screen coords via map_rect
static SDL_FRect unit_screen_rect(const Unit *u, SDL_FRect mr) {
    f32 screen_r = u->radius * mr.w;
    f32 sx = mr.x + u->pos.x * mr.w - screen_r;
    f32 sy = mr.y + u->pos.y * mr.h - screen_r;
    return (SDL_FRect){sx, sy, screen_r * 2.0f, screen_r * 2.0f};
}

static void draw_unit(SDL_Renderer *renderer, const Unit *u, SDL_FRect map_rect) {
    if (!u->alive) return;
    SDL_SetRenderDrawColor(renderer, u->color[0], u->color[1], u->color[2], u->color[3]);
    SDL_FRect r = unit_screen_rect(u, map_rect);
    SDL_RenderFillRect(renderer, &r);
}

static void draw_health_bar(SDL_Renderer *renderer, const Unit *u, SDL_FRect map_rect) {
    if (!u->alive) return;
    SDL_FRect r = unit_screen_rect(u, map_rect);
    f32 bar_w = r.w;
    f32 bar_h = 3.0f;
    f32 bar_x = r.x;
    f32 bar_y = r.y - bar_h - 2.0f;
    f32 hp_frac = u->hp / u->max_hp;
    if (hp_frac < 0.0f) hp_frac = 0.0f;
    if (hp_frac > 1.0f) hp_frac = 1.0f;

    // Background (dark)
    SDL_SetRenderDrawColor(renderer, 40, 40, 40, 200);
    SDL_FRect bg = {bar_x, bar_y, bar_w, bar_h};
    SDL_RenderFillRect(renderer, &bg);

    // Health fill (green -> red)
    u8 gr = (u8)(hp_frac * 200);
    u8 rd = (u8)((1.0f - hp_frac) * 200);
    SDL_SetRenderDrawColor(renderer, rd, gr, 0, 220);
    SDL_FRect fg = {bar_x, bar_y, bar_w * hp_frac, bar_h};
    SDL_RenderFillRect(renderer, &fg);
}

void g_render_game(GameState *gs, SDL_Renderer *renderer, SDL_FRect map_rect) {
    // Squad companions
    for (u32 i = 0; i < gs->num_squad; i++) {
        draw_unit(renderer, &gs->squad[i], map_rect);
        draw_health_bar(renderer, &gs->squad[i], map_rect);
    }

    // Player (drawn last, on top)
    draw_unit(renderer, &gs->player, map_rect);
    draw_health_bar(renderer, &gs->player, map_rect);
}
