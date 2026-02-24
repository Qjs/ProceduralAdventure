#include "g_render_effects.h"
#include "g_render_primitives.h"
#include <math.h>

void gr_draw_orb(SDL_Renderer *renderer, const Orb *orb, SDL_FRect mr,
                 SDL_Texture *orb_tex) {
    if (!orb->active) return;
    f32 pulse = (sinf(orb->pulse_timer * 3.0f) + 1.0f) * 0.5f;

    u8 cr = 140, cg = 210, cb = 255;
    switch (orb->effect) {
        case ORB_EFFECT_HEAL_BOOST:    cr = 120; cg = 240; cb = 120; break;
        case ORB_EFFECT_MELEE_BOOST:   cr = 255; cg = 110; cb = 110; break;
        case ORB_EFFECT_ARCHER_BOOST:  cr = 255; cg = 210; cb = 90;  break;
        case ORB_EFFECT_MAGE_BOOST:    cr = 170; cg = 120; cb = 255; break;
        case ORB_EFFECT_ENVIRONMENTAL: cr = 255; cg = 140; cb = 60;  break;
        default: break;
    }

    f32 cx = mr.x + orb->pos.x * mr.w;
    f32 cy = mr.y + orb->pos.y * mr.h;
    f32 core_r = orb->radius * mr.w;
    f32 ring_inner = core_r * 1.8f;
    f32 ring_outer = core_r * 2.8f + pulse * core_r * 0.5f;

    u8 ring_alpha = (u8)(50 + pulse * 50);
    gr_draw_ring(renderer, cx, cy, ring_inner, ring_outer, cr, cg, cb, ring_alpha);

    if (orb_tex) {
        f32 size = core_r * 2.0f;
        SDL_FRect dst = {cx - core_r, cy - core_r, size, size};
        f32 angle_deg = orb->pulse_timer * 20.0f;
        SDL_SetTextureAlphaModFloat(orb_tex, (180 + pulse * 75) / 255.0f);
        SDL_RenderTextureRotated(renderer, orb_tex, NULL, &dst, (double)angle_deg, NULL, SDL_FLIP_NONE);
        SDL_SetTextureAlphaModFloat(orb_tex, 1.0f);
    } else {
        u8 core_alpha = (u8)(180 + pulse * 75);
        gr_fill_circle(renderer, cx, cy, core_r, cr, cg, cb, core_alpha);
    }
}

void gr_draw_portal(SDL_Renderer *renderer, const Portal *portal, SDL_FRect mr,
                    SDL_Texture *portal_tex) {
    if (!portal->active) return;
    f32 pulse = (sinf(portal->pulse_timer * 2.0f) + 1.0f) * 0.5f;

    f32 cx = mr.x + portal->pos.x * mr.w;
    f32 cy = mr.y + portal->pos.y * mr.h;
    f32 core_rx = portal->radius_x * mr.w;
    f32 core_ry = portal->radius_y * mr.h;

    f32 ring_inner_rx = core_rx * 1.4f;
    f32 ring_inner_ry = core_ry * 1.4f;
    f32 ring_outer_rx = core_rx * 2.2f + pulse * core_rx * 0.6f;
    f32 ring_outer_ry = core_ry * 2.2f + pulse * core_ry * 0.6f;

    u8 ring_alpha = (u8)(60 + pulse * 60);
    gr_draw_ellipse_ring(renderer, cx, cy, ring_inner_rx, ring_inner_ry,
                         ring_outer_rx, ring_outer_ry, 120, 80, 240, ring_alpha);

    if (portal_tex) {
        f32 size_x = core_rx * 2.0f;
        f32 size_y = core_ry * 2.0f;
        SDL_FRect dst = {cx - core_rx, cy - core_ry, size_x, size_y};
        f32 angle_deg = portal->pulse_timer * -15.0f;
        SDL_SetTextureAlphaModFloat(portal_tex, (200 + pulse * 55) / 255.0f);
        SDL_RenderTextureRotated(renderer, portal_tex, NULL, &dst, (double)angle_deg, NULL, SDL_FLIP_NONE);
        SDL_SetTextureAlphaModFloat(portal_tex, 1.0f);
    } else {
        u8 core_alpha = (u8)(200 + pulse * 55);
        gr_fill_ellipse(renderer, cx, cy, core_rx, core_ry, 120, 80, 240, core_alpha);
    }
}

void gr_draw_camp_marker(SDL_Renderer *renderer, const EnemyCamp *camp, SDL_FRect mr) {
    if (camp->num_alive == 0) return;
    f32 cx = mr.x + camp->pos.x * mr.w;
    f32 cy = mr.y + camp->pos.y * mr.h;
    f32 inner_r = camp->spawn_radius * mr.w * 0.9f;
    f32 outer_r = camp->spawn_radius * mr.w;
    u8 alpha = camp->activated ? 60 : 30;
    u8 r = camp->activated ? 200 : 120;
    gr_draw_ring(renderer, cx, cy, inner_r, outer_r, r, 40, 40, alpha);
}

void gr_draw_projectile(SDL_Renderer *renderer, const Projectile *p, SDL_FRect mr) {
    if (!p->active) return;
    f32 sx = mr.x + p->pos.x * mr.w;
    f32 sy = mr.y + p->pos.y * mr.h;
    f32 size = 0.003f * mr.w;
    SDL_SetRenderDrawColor(renderer, p->color[0], p->color[1], p->color[2], p->color[3]);
    SDL_FRect rect = {sx - size * 0.5f, sy - size * 0.5f, size, size};
    SDL_RenderFillRect(renderer, &rect);
}

void gr_draw_environment_effect(SDL_Renderer *renderer, const GameState *gs, SDL_FRect mr) {
    if (gs->env_active_effect == ENV_EFFECT_NONE) return;

    if (gs->env_active_effect == ENV_EFFECT_WAVE) {
        f32 sx = mr.x + gs->env_wave_front * mr.w;
        f32 sy0 = mr.y + gs->env_view_min_y * mr.h;
        f32 sy1 = mr.y + gs->env_view_max_y * mr.h;
        f32 band = 0.03f * mr.w;
        SDL_SetRenderDrawColor(renderer, 120, 190, 255, 90);
        SDL_FRect rect = {sx - band, sy0, band * 2.0f, sy1 - sy0};
        SDL_RenderFillRect(renderer, &rect);
    } else if (gs->env_active_effect == ENV_EFFECT_LAVA) {
        f32 cx = mr.x + gs->env_lava_pos.x * mr.w;
        f32 cy = mr.y + gs->env_lava_pos.y * mr.h;
        f32 r = gs->env_lava_radius * mr.w;
        gr_draw_ring(renderer, cx, cy, r * 0.75f, r, 255, 120, 40, 180);
        gr_fill_circle(renderer, cx, cy, r * 0.7f, 255, 90, 30, 90);
    } else if (gs->env_active_effect == ENV_EFFECT_BOULDERS && gs->env_boulder_visible) {
        Vec2 d = vec2_sub(gs->env_boulder_to, gs->env_boulder_from);
        Vec2 p = vec2_add(gs->env_boulder_from, vec2_scale(d, gs->env_boulder_anim));
        f32 x0 = mr.x + gs->env_boulder_from.x * mr.w;
        f32 y0 = mr.y + gs->env_boulder_from.y * mr.h;
        f32 x1 = mr.x + p.x * mr.w;
        f32 y1 = mr.y + p.y * mr.h;
        SDL_SetRenderDrawColor(renderer, 170, 160, 140, 200);
        SDL_RenderLine(renderer, x0, y0, x1, y1);
        gr_fill_circle(renderer, x1, y1, 0.006f * mr.w, 160, 150, 130, 230);
    }
}
