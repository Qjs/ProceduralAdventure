#include "g_render_entities.h"
#include "g_render_primitives.h"
#include <math.h>

static inline f32 clamp01f(f32 x) {
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

static inline f32 ease_out_quad(f32 t) {
    t = clamp01f(t);
    return 1.0f - (1.0f - t) * (1.0f - t);
}

static SDL_FRect unit_screen_rect(const Unit *u, SDL_FRect mr) {
    f32 screen_r = u->radius * mr.w;
    f32 sx = mr.x + u->pos.x * mr.w - screen_r;
    f32 sy = mr.y + u->pos.y * mr.h - screen_r;
    return (SDL_FRect){sx, sy, screen_r * 2.0f, screen_r * 2.0f};
}

static void draw_slash_arc(SDL_Renderer *renderer, f32 cx, f32 cy, f32 radius,
                           f32 start_ang, f32 end_ang, u8 r, u8 g, u8 b, u8 a) {
    const int steps = 10;
    if (steps < 2) return;

    SDL_SetRenderDrawColor(renderer, r, g, b, a);
    for (int pass = 0; pass < 2; pass++) {
        f32 rr = radius + (f32)pass * 1.3f;
        for (int i = 0; i < steps; i++) {
            f32 t0 = (f32)i / (f32)steps;
            f32 t1 = (f32)(i + 1) / (f32)steps;
            f32 a0 = start_ang + (end_ang - start_ang) * t0;
            f32 a1 = start_ang + (end_ang - start_ang) * t1;
            f32 x0 = cx + cosf(a0) * rr;
            f32 y0 = cy + sinf(a0) * rr;
            f32 x1 = cx + cosf(a1) * rr;
            f32 y1 = cy + sinf(a1) * rr;
            SDL_RenderLine(renderer, x0, y0, x1, y1);
        }
    }
}

static void draw_weapon(SDL_Renderer *renderer, const Unit *u, f32 cx, f32 cy,
                        f32 screen_r, f32 facing) {
    bool is_melee = (u->role == ROLE_PLAYER || u->role == ROLE_MELEE || u->role == ROLE_ENEMY_MELEE);
    f32 swing = 0.0f;
    f32 swing_t = 0.0f;
    if (is_melee && u->cooldown > 1e-5f) {
        f32 attack_phase = 1.0f - clamp01f(u->cooldown_timer / u->cooldown);
        if (attack_phase < 0.42f) {
            f32 t = ease_out_quad(attack_phase / 0.42f);
            swing = -0.95f + t * 1.45f;
            swing_t = t;
        } else if (attack_phase < 0.78f) {
            f32 t = (attack_phase - 0.42f) / 0.36f;
            swing = 0.50f - t * 0.70f;
            swing_t = 1.0f - t;
        }
    }

    f32 weapon_angle = facing + swing;
    f32 cos_f = cosf(weapon_angle);
    f32 sin_f = sinf(weapon_angle);
    f32 perp_x = -sin_f;
    f32 perp_y = cos_f;
    f32 side_offset = screen_r * (0.35f + 0.05f * swing_t);
    f32 base_x = cx + cos_f * screen_r * 0.8f + perp_x * side_offset;
    f32 base_y = cy + sin_f * screen_r * 0.8f + perp_y * side_offset;
    f32 tip_x, tip_y;

    switch (u->role) {
        case ROLE_PLAYER:
        case ROLE_MELEE: {
            tip_x = base_x + cos_f * screen_r * 1.0f;
            tip_y = base_y + sin_f * screen_r * 1.0f;
            f32 px = -sin_f * 1.0f, py = cos_f * 1.0f;
            SDL_SetRenderDrawColor(renderer, 200, 200, 210, 255);
            SDL_RenderLine(renderer, base_x + px, base_y + py, tip_x + px, tip_y + py);
            SDL_RenderLine(renderer, base_x - px, base_y - py, tip_x - px, tip_y - py);
            SDL_RenderLine(renderer, base_x, base_y, tip_x, tip_y);
            if (swing_t > 0.15f) {
                f32 span = 2.35f;
                f32 arc_mid = weapon_angle + 0.08f;
                draw_slash_arc(renderer, cx + cos_f * screen_r * 0.40f, cy + sin_f * screen_r * 0.40f,
                               screen_r * 1.05f, arc_mid - span * 0.5f, arc_mid + span * 0.5f,
                               255, 225, 120, (u8)(55.0f + 130.0f * swing_t));
            }
            break;
        }
        case ROLE_ARCHER: {
            tip_x = base_x + cos_f * screen_r * 0.9f;
            tip_y = base_y + sin_f * screen_r * 0.9f;
            SDL_SetRenderDrawColor(renderer, 139, 90, 43, 255);
            SDL_RenderLine(renderer, base_x, base_y, tip_x, tip_y);
            f32 px = -sin_f * screen_r * 0.5f;
            f32 py = cos_f * screen_r * 0.5f;
            SDL_RenderLine(renderer, tip_x + px, tip_y + py, tip_x - px, tip_y - py);
            break;
        }
        case ROLE_HEALER: {
            tip_x = base_x + cos_f * screen_r * 1.1f;
            tip_y = base_y + sin_f * screen_r * 1.1f;
            SDL_SetRenderDrawColor(renderer, 200, 180, 100, 255);
            SDL_RenderLine(renderer, base_x, base_y, tip_x, tip_y);
            gr_fill_circle(renderer, tip_x, tip_y, screen_r * 0.2f, 255, 230, 100, 220);
            break;
        }
        case ROLE_MAGE: {
            tip_x = base_x + cos_f * screen_r * 1.1f;
            tip_y = base_y + sin_f * screen_r * 1.1f;
            SDL_SetRenderDrawColor(renderer, 140, 100, 200, 255);
            SDL_RenderLine(renderer, base_x, base_y, tip_x, tip_y);
            f32 d = screen_r * 0.25f;
            f32 px = -sin_f, py = cos_f;
            SDL_SetRenderDrawColor(renderer, 160, 120, 255, 255);
            SDL_RenderLine(renderer, tip_x + cos_f * d, tip_y + sin_f * d, tip_x + px * d, tip_y + py * d);
            SDL_RenderLine(renderer, tip_x + px * d, tip_y + py * d, tip_x - cos_f * d, tip_y - sin_f * d);
            SDL_RenderLine(renderer, tip_x - cos_f * d, tip_y - sin_f * d, tip_x - px * d, tip_y - py * d);
            SDL_RenderLine(renderer, tip_x - px * d, tip_y - py * d, tip_x + cos_f * d, tip_y + sin_f * d);
            break;
        }
        case ROLE_ENEMY_MELEE: {
            tip_x = base_x + cos_f * screen_r * 0.8f;
            tip_y = base_y + sin_f * screen_r * 0.8f;
            f32 px = -sin_f * 1.5f, py = cos_f * 1.5f;
            SDL_SetRenderDrawColor(renderer, 100, 60, 30, 255);
            SDL_RenderLine(renderer, base_x + px, base_y + py, tip_x + px, tip_y + py);
            SDL_RenderLine(renderer, base_x - px, base_y - py, tip_x - px, tip_y - py);
            SDL_RenderLine(renderer, base_x, base_y, tip_x, tip_y);
            if (swing_t > 0.15f) {
                f32 span = 2.05f;
                f32 arc_mid = weapon_angle;
                draw_slash_arc(renderer, cx + cos_f * screen_r * 0.35f, cy + sin_f * screen_r * 0.35f,
                               screen_r * 0.80f, arc_mid - span * 0.5f, arc_mid + span * 0.5f,
                               238, 205, 110, (u8)(45.0f + 120.0f * swing_t));
            }
            break;
        }
        case ROLE_ENEMY_RANGED: {
            tip_x = base_x + cos_f * screen_r * 1.2f;
            tip_y = base_y + sin_f * screen_r * 1.2f;
            SDL_SetRenderDrawColor(renderer, 120, 80, 140, 255);
            SDL_RenderLine(renderer, base_x, base_y, tip_x, tip_y);
            break;
        }
        default:
            break;
    }
}

void gr_draw_unit_composite(SDL_Renderer *renderer, const Unit *u,
                            SDL_FRect map_rect, SDL_Texture *role_tex) {
    if (!u->alive) return;

    f32 screen_r = u->radius * map_rect.w;
    f32 cx = map_rect.x + u->pos.x * map_rect.w;
    f32 cy = map_rect.y + u->pos.y * map_rect.h;
    f32 facing = u->facing;

    if (role_tex) {
        f32 size = screen_r * 2.0f;
        SDL_FRect dst = {cx - screen_r, cy - screen_r, size, size};
        f32 angle_deg = facing * (180.0f / 3.14159265f);
        SDL_RenderTextureRotated(renderer, role_tex, NULL, &dst, (double)angle_deg, NULL, SDL_FLIP_NONE);
    } else {
        gr_fill_circle(renderer, cx, cy, screen_r, u->color[0], u->color[1], u->color[2], u->color[3]);
    }

    f32 head_offset = screen_r * 0.4f;
    f32 head_r = screen_r * 0.25f;
    f32 head_x = cx + cosf(facing) * head_offset;
    f32 head_y = cy + sinf(facing) * head_offset;
    gr_fill_circle(renderer, head_x, head_y, head_r, 210, 180, 140, 255);

    draw_weapon(renderer, u, cx, cy, screen_r, facing);
}

void gr_draw_health_bar(SDL_Renderer *renderer, const Unit *u, SDL_FRect map_rect) {
    if (!u->alive) return;
    SDL_FRect r = unit_screen_rect(u, map_rect);
    f32 bar_w = r.w;
    f32 bar_h = 3.0f;
    f32 bar_x = r.x;
    f32 bar_y = r.y - bar_h - 2.0f;
    f32 hp_frac = u->hp / u->max_hp;
    if (hp_frac < 0.0f) hp_frac = 0.0f;
    if (hp_frac > 1.0f) hp_frac = 1.0f;

    SDL_SetRenderDrawColor(renderer, 40, 40, 40, 200);
    SDL_FRect bg = {bar_x, bar_y, bar_w, bar_h};
    SDL_RenderFillRect(renderer, &bg);

    u8 gr = (u8)(hp_frac * 200);
    u8 rd = (u8)((1.0f - hp_frac) * 200);
    SDL_SetRenderDrawColor(renderer, rd, gr, 0, 220);
    SDL_FRect fg = {bar_x, bar_y, bar_w * hp_frac, bar_h};
    SDL_RenderFillRect(renderer, &fg);
}

void gr_draw_squad_layered(SDL_Renderer *renderer, const GameState *gs, const Unit *u,
                           SDL_FRect map_rect, SDL_Texture *role_tex) {
    if (!u->alive) return;

    f32 t = (f32)SDL_GetTicks() * 0.001f;
    f32 r = u->radius * map_rect.w;
    f32 cx = map_rect.x + u->pos.x * map_rect.w;
    f32 cy = map_rect.y + u->pos.y * map_rect.h;
    f32 facing = u->facing;
    f32 cs = cosf(facing), sn = sinf(facing);
    f32 px = -sn, py = cs;

    f32 speed_norm = clamp01f(vec2_len(u->vel) / (u->speed * 1.5f + 1e-3f));
    f32 phase = (f32)u->role * 1.31f + (u->pos.x + u->pos.y) * 7.0f;
    f32 stride = sinf(t * 11.0f + phase) * speed_norm;
    f32 bob = sinf(t * 5.0f + phase) * r * 0.05f + stride * r * 0.04f;
    cy += bob;

    gr_fill_ellipse(renderer, cx, cy + r * 0.9f, r * 0.95f, r * 0.4f, 18, 18, 22, 105);

    if (gs->squad_stance == STANCE_AGGRESSIVE) {
        gr_draw_ring(renderer, cx, cy, r * 1.02f, r * 1.12f, 255, 130, 60, 40);
    } else if (gs->squad_stance == STANCE_DEFENSIVE) {
        gr_draw_ring(renderer, cx, cy, r * 1.04f, r * 1.14f, 110, 165, 255, 42);
    } else {
        gr_draw_ring(renderer, cx, cy, r * 1.05f, r * 1.15f, 130, 230, 170, 38);
    }

    if (u->role == ROLE_MAGE) {
        gr_fill_ellipse(renderer, cx - cs * r * 0.2f, cy - sn * r * 0.2f + r * 0.1f,
                        r * 0.9f, r * 0.7f, 60, 45, 120, 120);
    } else if (u->role == ROLE_HEALER) {
        gr_draw_ring(renderer, cx, cy - r * 0.7f, r * 0.18f, r * 0.25f, 245, 225, 130, 150);
    } else if (u->role == ROLE_ARCHER) {
        f32 qx = cx - cs * r * 0.45f - px * r * 0.25f;
        f32 qy = cy - sn * r * 0.45f - py * r * 0.25f;
        SDL_SetRenderDrawColor(renderer, 82, 58, 38, 220);
        SDL_FRect quiver = {qx - r * 0.14f, qy - r * 0.35f, r * 0.28f, r * 0.7f};
        SDL_RenderFillRect(renderer, &quiver);
    } else if (u->role == ROLE_PLAYER) {
        gr_draw_ring(renderer, cx, cy - r * 0.75f, r * 0.20f, r * 0.30f, 255, 240, 180, 145);
    }

    if (role_tex) {
        f32 size = r * 2.2f;
        SDL_FRect dst = {cx - size * 0.5f, cy - size * 0.5f, size, size};
        f32 angle_deg = (facing + stride * 0.09f) * (180.0f / 3.14159265f);
        SDL_SetTextureAlphaModFloat(role_tex, 0.98f);
        SDL_RenderTextureRotated(renderer, role_tex, NULL, &dst, (double)angle_deg, NULL, SDL_FLIP_NONE);
        SDL_SetTextureAlphaModFloat(role_tex, 1.0f);
    } else {
        gr_fill_circle(renderer, cx, cy, r * 1.05f, u->color[0], u->color[1], u->color[2], 240);
    }

    gr_draw_ring(renderer, cx, cy + r * 0.05f, r * 0.52f, r * 0.62f, 35, 35, 42, 120);

    gr_fill_circle(renderer, cx + px * r * (0.42f + stride * 0.08f), cy + py * r * (0.42f + stride * 0.08f),
                   r * 0.2f, 200, 170, 135, 235);
    gr_fill_circle(renderer, cx - px * r * (0.42f - stride * 0.08f), cy - py * r * (0.42f - stride * 0.08f),
                   r * 0.2f, 185, 155, 125, 215);

    f32 head_x = cx + cs * r * 0.38f;
    f32 head_y = cy + sn * r * 0.38f - r * 0.08f;
    gr_fill_circle(renderer, head_x, head_y, r * 0.28f, 215, 185, 145, 255);

    if (u->role == ROLE_MELEE || u->role == ROLE_PLAYER) {
        gr_fill_circle(renderer, cx + px * r * 0.35f, cy + py * r * 0.35f, r * 0.12f, 170, 70, 70, 220);
        gr_fill_circle(renderer, cx - px * r * 0.35f, cy - py * r * 0.35f, r * 0.12f, 170, 70, 70, 220);
        if (u->role == ROLE_PLAYER) {
            gr_draw_ring(renderer, cx, cy, r * 0.66f, r * 0.76f, 225, 225, 240, 120);
        }
    } else if (u->role == ROLE_ARCHER) {
        gr_fill_circle(renderer, head_x - cs * r * 0.04f, head_y - sn * r * 0.04f, r * 0.18f, 80, 120, 65, 210);
    } else if (u->role == ROLE_HEALER) {
        SDL_SetRenderDrawColor(renderer, 240, 235, 150, 220);
        SDL_RenderLine(renderer, cx - px * r * 0.20f, cy - py * r * 0.20f,
                       cx + px * r * 0.20f, cy + py * r * 0.20f);
        SDL_RenderLine(renderer, cx - cs * r * 0.20f, cy - sn * r * 0.20f,
                       cx + cs * r * 0.20f, cy + sn * r * 0.20f);
    } else if (u->role == ROLE_MAGE) {
        for (int i = 0; i < 3; i++) {
            f32 a = t * 2.1f + phase + i * 2.1f;
            gr_fill_circle(renderer, cx + cosf(a) * r * 0.52f, cy + sinf(a) * r * 0.52f,
                           r * 0.07f, 170, 140, 255, 170);
        }
    }

    draw_weapon(renderer, u, cx, cy, r, facing);
}
