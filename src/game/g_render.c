#include "g_render.h"
#include "g_particles.h"
#include <math.h>

#define CIRCLE_SEGMENTS 24

// Forward declarations
static void fill_circle(SDL_Renderer *renderer, f32 cx, f32 cy, f32 radius,
                        u8 r, u8 g, u8 b, u8 a);

// Convert game coords [0,1]² to screen coords via map_rect
static SDL_FRect unit_screen_rect(const Unit *u, SDL_FRect mr) {
    f32 screen_r = u->radius * mr.w;
    f32 sx = mr.x + u->pos.x * mr.w - screen_r;
    f32 sy = mr.y + u->pos.y * mr.h - screen_r;
    return (SDL_FRect){sx, sy, screen_r * 2.0f, screen_r * 2.0f};
}

static void draw_weapon(SDL_Renderer *renderer, const Unit *u, f32 cx, f32 cy,
                        f32 screen_r, f32 facing) {
    f32 cos_f = cosf(facing);
    f32 sin_f = sinf(facing);

    // Weapon base: from body edge outward, offset to the right side
    f32 perp_x = -sin_f; // perpendicular (right-hand side)
    f32 perp_y =  cos_f;
    f32 side_offset = screen_r * 0.35f;
    f32 base_x = cx + cos_f * screen_r * 0.8f + perp_x * side_offset;
    f32 base_y = cy + sin_f * screen_r * 0.8f + perp_y * side_offset;
    f32 tip_x, tip_y;

    switch (u->role) {
        case ROLE_PLAYER:
        case ROLE_MELEE: {
            // Short thick sword — draw 2 parallel lines for thickness
            tip_x = base_x + cos_f * screen_r * 1.0f;
            tip_y = base_y + sin_f * screen_r * 1.0f;
            f32 px = -sin_f * 1.0f, py = cos_f * 1.0f;
            SDL_SetRenderDrawColor(renderer, 200, 200, 210, 255);
            SDL_RenderLine(renderer, base_x + px, base_y + py, tip_x + px, tip_y + py);
            SDL_RenderLine(renderer, base_x - px, base_y - py, tip_x - px, tip_y - py);
            SDL_RenderLine(renderer, base_x, base_y, tip_x, tip_y);
            break;
        }
        case ROLE_ARCHER: {
            // Bow: thin line + perpendicular bar
            tip_x = base_x + cos_f * screen_r * 0.9f;
            tip_y = base_y + sin_f * screen_r * 0.9f;
            SDL_SetRenderDrawColor(renderer, 139, 90, 43, 255);
            SDL_RenderLine(renderer, base_x, base_y, tip_x, tip_y);
            // Perpendicular bar (bowstring)
            f32 px = -sin_f * screen_r * 0.5f;
            f32 py = cos_f * screen_r * 0.5f;
            SDL_RenderLine(renderer, tip_x + px, tip_y + py, tip_x - px, tip_y - py);
            break;
        }
        case ROLE_HEALER: {
            // Staff with circle at tip
            tip_x = base_x + cos_f * screen_r * 1.1f;
            tip_y = base_y + sin_f * screen_r * 1.1f;
            SDL_SetRenderDrawColor(renderer, 200, 180, 100, 255);
            SDL_RenderLine(renderer, base_x, base_y, tip_x, tip_y);
            fill_circle(renderer, tip_x, tip_y, screen_r * 0.2f, 255, 230, 100, 220);
            break;
        }
        case ROLE_MAGE: {
            // Staff with diamond at tip
            tip_x = base_x + cos_f * screen_r * 1.1f;
            tip_y = base_y + sin_f * screen_r * 1.1f;
            SDL_SetRenderDrawColor(renderer, 140, 100, 200, 255);
            SDL_RenderLine(renderer, base_x, base_y, tip_x, tip_y);
            // Diamond shape (4 lines)
            f32 d = screen_r * 0.25f;
            f32 px = -sin_f, py = cos_f;
            SDL_SetRenderDrawColor(renderer, 160, 120, 255, 255);
            SDL_RenderLine(renderer, tip_x + cos_f * d, tip_y + sin_f * d,
                          tip_x + px * d, tip_y + py * d);
            SDL_RenderLine(renderer, tip_x + px * d, tip_y + py * d,
                          tip_x - cos_f * d, tip_y - sin_f * d);
            SDL_RenderLine(renderer, tip_x - cos_f * d, tip_y - sin_f * d,
                          tip_x - px * d, tip_y - py * d);
            SDL_RenderLine(renderer, tip_x - px * d, tip_y - py * d,
                          tip_x + cos_f * d, tip_y + sin_f * d);
            break;
        }
        case ROLE_ENEMY_MELEE: {
            // Club — short thick line
            tip_x = base_x + cos_f * screen_r * 0.8f;
            tip_y = base_y + sin_f * screen_r * 0.8f;
            f32 px = -sin_f * 1.5f, py = cos_f * 1.5f;
            SDL_SetRenderDrawColor(renderer, 100, 60, 30, 255);
            SDL_RenderLine(renderer, base_x + px, base_y + py, tip_x + px, tip_y + py);
            SDL_RenderLine(renderer, base_x - px, base_y - py, tip_x - px, tip_y - py);
            SDL_RenderLine(renderer, base_x, base_y, tip_x, tip_y);
            break;
        }
        case ROLE_ENEMY_RANGED: {
            // Spear — thin line
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

static void draw_unit_composite(SDL_Renderer *renderer, const Unit *u,
                                SDL_FRect map_rect, SDL_Texture *role_tex) {
    if (!u->alive) return;

    f32 screen_r = u->radius * map_rect.w;
    f32 cx = map_rect.x + u->pos.x * map_rect.w;
    f32 cy = map_rect.y + u->pos.y * map_rect.h;
    f32 facing = u->facing;

    // 1. Body — textured circle (rotated)
    if (role_tex) {
        f32 size = screen_r * 2.0f;
        SDL_FRect dst = {cx - screen_r, cy - screen_r, size, size};
        f32 angle_deg = facing * (180.0f / 3.14159265f);
        SDL_RenderTextureRotated(renderer, role_tex, NULL, &dst,
                                 (double)angle_deg, NULL, SDL_FLIP_NONE);
    } else {
        // Fallback: colored circle
        fill_circle(renderer, cx, cy, screen_r,
                    u->color[0], u->color[1], u->color[2], u->color[3]);
    }

    // 2. Head — small skin-toned circle, offset forward
    f32 head_offset = screen_r * 0.4f;
    f32 head_r = screen_r * 0.25f;
    f32 head_x = cx + cosf(facing) * head_offset;
    f32 head_y = cy + sinf(facing) * head_offset;
    fill_circle(renderer, head_x, head_y, head_r, 210, 180, 140, 255);

    // 3. Weapon
    draw_weapon(renderer, u, cx, cy, screen_r, facing);
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

// Draw a filled circle as a triangle fan using SDL_RenderGeometry
static void fill_circle(SDL_Renderer *renderer, f32 cx, f32 cy, f32 radius,
                        u8 r, u8 g, u8 b, u8 a) {
    SDL_Vertex verts[CIRCLE_SEGMENTS + 2];
    int indices[CIRCLE_SEGMENTS * 3];

    // Center vertex
    verts[0].position = (SDL_FPoint){cx, cy};
    verts[0].color.r = r / 255.0f;
    verts[0].color.g = g / 255.0f;
    verts[0].color.b = b / 255.0f;
    verts[0].color.a = a / 255.0f;

    for (int i = 0; i <= CIRCLE_SEGMENTS; i++) {
        f32 angle = (f32)i / (f32)CIRCLE_SEGMENTS * 2.0f * 3.14159265f;
        verts[i + 1].position = (SDL_FPoint){cx + cosf(angle) * radius,
                                              cy + sinf(angle) * radius};
        verts[i + 1].color = verts[0].color;
    }

    for (int i = 0; i < CIRCLE_SEGMENTS; i++) {
        indices[i * 3 + 0] = 0;
        indices[i * 3 + 1] = i + 1;
        indices[i * 3 + 2] = i + 2;
    }

    SDL_RenderGeometry(renderer, NULL, verts, CIRCLE_SEGMENTS + 2,
                       indices, CIRCLE_SEGMENTS * 3);
}

// Draw a ring (annulus) as a triangle strip via indexed geometry
static void draw_ring(SDL_Renderer *renderer, f32 cx, f32 cy,
                      f32 inner_r, f32 outer_r,
                      u8 r, u8 g, u8 b, u8 a) {
    SDL_Vertex verts[(CIRCLE_SEGMENTS + 1) * 2];
    int indices[CIRCLE_SEGMENTS * 6];

    SDL_FColor col = {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};

    for (int i = 0; i <= CIRCLE_SEGMENTS; i++) {
        f32 angle = (f32)i / (f32)CIRCLE_SEGMENTS * 2.0f * 3.14159265f;
        f32 cs = cosf(angle), sn = sinf(angle);
        int vi = i * 2;
        verts[vi].position = (SDL_FPoint){cx + cs * inner_r, cy + sn * inner_r};
        verts[vi].color = col;
        verts[vi + 1].position = (SDL_FPoint){cx + cs * outer_r, cy + sn * outer_r};
        verts[vi + 1].color = col;
    }

    for (int i = 0; i < CIRCLE_SEGMENTS; i++) {
        int vi = i * 2;
        int idx = i * 6;
        indices[idx + 0] = vi;
        indices[idx + 1] = vi + 1;
        indices[idx + 2] = vi + 2;
        indices[idx + 3] = vi + 1;
        indices[idx + 4] = vi + 3;
        indices[idx + 5] = vi + 2;
    }

    SDL_RenderGeometry(renderer, NULL, verts, (CIRCLE_SEGMENTS + 1) * 2,
                       indices, CIRCLE_SEGMENTS * 6);
}

static void draw_orb(SDL_Renderer *renderer, const Orb *orb, SDL_FRect mr,
                     SDL_Texture *orb_tex) {
    if (!orb->active) return;
    f32 pulse = (sinf(orb->pulse_timer * 3.0f) + 1.0f) * 0.5f; // 0..1

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

    // Outer light ring
    u8 ring_alpha = (u8)(50 + pulse * 50);
    draw_ring(renderer, cx, cy, ring_inner, ring_outer, cr, cg, cb, ring_alpha);

    // Core — textured swirly orb (rotates slowly)
    if (orb_tex) {
        f32 size = core_r * 2.0f;
        SDL_FRect dst = {cx - core_r, cy - core_r, size, size};
        f32 angle_deg = orb->pulse_timer * 20.0f; // slow rotation
        SDL_SetTextureAlphaModFloat(orb_tex, (180 + pulse * 75) / 255.0f);
        SDL_RenderTextureRotated(renderer, orb_tex, NULL, &dst,
                                 (double)angle_deg, NULL, SDL_FLIP_NONE);
        SDL_SetTextureAlphaModFloat(orb_tex, 1.0f);
    } else {
        u8 core_alpha = (u8)(180 + pulse * 75);
        fill_circle(renderer, cx, cy, core_r, cr, cg, cb, core_alpha);
    }
}

// Draw a filled ellipse as a triangle fan
static void fill_ellipse(SDL_Renderer *renderer, f32 cx, f32 cy,
                         f32 rx, f32 ry, u8 r, u8 g, u8 b, u8 a) {
    SDL_Vertex verts[CIRCLE_SEGMENTS + 2];
    int indices[CIRCLE_SEGMENTS * 3];

    verts[0].position = (SDL_FPoint){cx, cy};
    verts[0].color = (SDL_FColor){r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};

    for (int i = 0; i <= CIRCLE_SEGMENTS; i++) {
        f32 angle = (f32)i / (f32)CIRCLE_SEGMENTS * 2.0f * 3.14159265f;
        verts[i + 1].position = (SDL_FPoint){cx + cosf(angle) * rx,
                                              cy + sinf(angle) * ry};
        verts[i + 1].color = verts[0].color;
    }

    for (int i = 0; i < CIRCLE_SEGMENTS; i++) {
        indices[i * 3 + 0] = 0;
        indices[i * 3 + 1] = i + 1;
        indices[i * 3 + 2] = i + 2;
    }

    SDL_RenderGeometry(renderer, NULL, verts, CIRCLE_SEGMENTS + 2,
                       indices, CIRCLE_SEGMENTS * 3);
}

// Draw an elliptical ring (annulus)
static void draw_ellipse_ring(SDL_Renderer *renderer, f32 cx, f32 cy,
                              f32 inner_rx, f32 inner_ry,
                              f32 outer_rx, f32 outer_ry,
                              u8 r, u8 g, u8 b, u8 a) {
    SDL_Vertex verts[(CIRCLE_SEGMENTS + 1) * 2];
    int indices[CIRCLE_SEGMENTS * 6];

    SDL_FColor col = {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};

    for (int i = 0; i <= CIRCLE_SEGMENTS; i++) {
        f32 angle = (f32)i / (f32)CIRCLE_SEGMENTS * 2.0f * 3.14159265f;
        f32 cs = cosf(angle), sn = sinf(angle);
        int vi = i * 2;
        verts[vi].position = (SDL_FPoint){cx + cs * inner_rx, cy + sn * inner_ry};
        verts[vi].color = col;
        verts[vi + 1].position = (SDL_FPoint){cx + cs * outer_rx, cy + sn * outer_ry};
        verts[vi + 1].color = col;
    }

    for (int i = 0; i < CIRCLE_SEGMENTS; i++) {
        int vi = i * 2;
        int idx = i * 6;
        indices[idx + 0] = vi;
        indices[idx + 1] = vi + 1;
        indices[idx + 2] = vi + 2;
        indices[idx + 3] = vi + 1;
        indices[idx + 4] = vi + 3;
        indices[idx + 5] = vi + 2;
    }

    SDL_RenderGeometry(renderer, NULL, verts, (CIRCLE_SEGMENTS + 1) * 2,
                       indices, CIRCLE_SEGMENTS * 6);
}

static void draw_portal(SDL_Renderer *renderer, const Portal *portal, SDL_FRect mr,
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

    // Outer glow ring
    u8 ring_alpha = (u8)(60 + pulse * 60);
    draw_ellipse_ring(renderer, cx, cy,
                      ring_inner_rx, ring_inner_ry,
                      ring_outer_rx, ring_outer_ry,
                      120, 80, 240, ring_alpha);

    // Core — textured swirly portal (rotates)
    if (portal_tex) {
        f32 size_x = core_rx * 2.0f;
        f32 size_y = core_ry * 2.0f;
        SDL_FRect dst = {cx - core_rx, cy - core_ry, size_x, size_y};
        f32 angle_deg = portal->pulse_timer * -15.0f; // slow counter-rotation
        SDL_SetTextureAlphaModFloat(portal_tex, (200 + pulse * 55) / 255.0f);
        SDL_RenderTextureRotated(renderer, portal_tex, NULL, &dst,
                                 (double)angle_deg, NULL, SDL_FLIP_NONE);
        SDL_SetTextureAlphaModFloat(portal_tex, 1.0f);
    } else {
        u8 core_alpha = (u8)(200 + pulse * 55);
        fill_ellipse(renderer, cx, cy, core_rx, core_ry, 120, 80, 240, core_alpha);
    }
}

static void draw_camp_marker(SDL_Renderer *renderer, const EnemyCamp *camp, SDL_FRect mr) {
    if (camp->num_alive == 0) return;
    f32 cx = mr.x + camp->pos.x * mr.w;
    f32 cy = mr.y + camp->pos.y * mr.h;
    f32 inner_r = camp->spawn_radius * mr.w * 0.9f;
    f32 outer_r = camp->spawn_radius * mr.w;

    u8 alpha = camp->activated ? 60 : 30;
    u8 r = camp->activated ? 200 : 120;
    draw_ring(renderer, cx, cy, inner_r, outer_r, r, 40, 40, alpha);
}

static void draw_projectile(SDL_Renderer *renderer, const Projectile *p, SDL_FRect mr) {
    if (!p->active) return;
    f32 sx = mr.x + p->pos.x * mr.w;
    f32 sy = mr.y + p->pos.y * mr.h;
    f32 size = 0.003f * mr.w;
    SDL_SetRenderDrawColor(renderer, p->color[0], p->color[1], p->color[2], p->color[3]);
    SDL_FRect rect = {sx - size * 0.5f, sy - size * 0.5f, size, size};
    SDL_RenderFillRect(renderer, &rect);
}

static void draw_environment_effect(SDL_Renderer *renderer, const GameState *gs, SDL_FRect mr) {
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
        draw_ring(renderer, cx, cy, r * 0.75f, r, 255, 120, 40, 180);
        fill_circle(renderer, cx, cy, r * 0.7f, 255, 90, 30, 90);
    } else if (gs->env_active_effect == ENV_EFFECT_BOULDERS && gs->env_boulder_visible) {
        Vec2 d = vec2_sub(gs->env_boulder_to, gs->env_boulder_from);
        Vec2 p = vec2_add(gs->env_boulder_from, vec2_scale(d, gs->env_boulder_anim));
        f32 x0 = mr.x + gs->env_boulder_from.x * mr.w;
        f32 y0 = mr.y + gs->env_boulder_from.y * mr.h;
        f32 x1 = mr.x + p.x * mr.w;
        f32 y1 = mr.y + p.y * mr.h;
        SDL_SetRenderDrawColor(renderer, 170, 160, 140, 200);
        SDL_RenderLine(renderer, x0, y0, x1, y1);
        fill_circle(renderer, x1, y1, 0.006f * mr.w, 160, 150, 130, 230);
    }
}

void g_render_game(GameState *gs, SDL_Renderer *renderer, SDL_FRect map_rect,
                   SDL_Texture *role_textures[ROLE_COUNT]) {
    // Orbs
    for (u32 i = 0; i < gs->num_orbs; i++) {
        draw_orb(renderer, &gs->orbs[i], map_rect, gs->orb_texture);
    }

    // Portal
    draw_portal(renderer, &gs->portal, map_rect, gs->portal_texture);

    // Active environmental effect overlay
    draw_environment_effect(renderer, gs, map_rect);

    // Camp markers (faint circles)
    for (u32 i = 0; i < gs->num_camps; i++) {
        draw_camp_marker(renderer, &gs->camps[i], map_rect);
    }

    // Enemies
    for (u32 i = 0; i < gs->num_enemies; i++) {
        Unit *e = &gs->enemies[i];
        SDL_Texture *tex = (e->role < ROLE_COUNT) ? role_textures[e->role] : NULL;
        draw_unit_composite(renderer, e, map_rect, tex);
    }

    // Projectiles
    for (u32 i = 0; i < gs->num_projectiles; i++) {
        draw_projectile(renderer, &gs->projectiles[i], map_rect);
    }

    // Particles (after projectiles, before units on top)
    g_particles_render(&gs->particles, renderer, map_rect);

    // Squad companions
    for (u32 i = 0; i < gs->num_squad; i++) {
        Unit *u = &gs->squad[i];
        SDL_Texture *tex = (u->role < ROLE_COUNT) ? role_textures[u->role] : NULL;
        draw_unit_composite(renderer, u, map_rect, tex);
    }

    // Player (drawn last, on top)
    {
        Unit *p = &gs->player;
        SDL_Texture *tex = (p->role < ROLE_COUNT) ? role_textures[p->role] : NULL;
        draw_unit_composite(renderer, p, map_rect, tex);
    }

    // Health bars on top of everything
    for (u32 i = 0; i < gs->num_enemies; i++) {
        if (gs->enemies[i].hp < gs->enemies[i].max_hp)
            draw_health_bar(renderer, &gs->enemies[i], map_rect);
    }
    for (u32 i = 0; i < gs->num_squad; i++) {
        draw_health_bar(renderer, &gs->squad[i], map_rect);
    }
    draw_health_bar(renderer, &gs->player, map_rect);
}
