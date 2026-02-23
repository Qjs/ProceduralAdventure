#include "g_render.h"
#include <math.h>

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

#define CIRCLE_SEGMENTS 24

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

static void draw_orb(SDL_Renderer *renderer, const Orb *orb, SDL_FRect mr) {
    if (!orb->active) return;
    f32 pulse = (sinf(orb->pulse_timer * 3.0f) + 1.0f) * 0.5f; // 0..1

    f32 cx = mr.x + orb->pos.x * mr.w;
    f32 cy = mr.y + orb->pos.y * mr.h;
    f32 core_r = orb->radius * mr.w;
    f32 ring_inner = core_r * 1.8f;
    f32 ring_outer = core_r * 2.8f + pulse * core_r * 0.5f;

    // Outer light ring
    u8 ring_alpha = (u8)(50 + pulse * 50);
    draw_ring(renderer, cx, cy, ring_inner, ring_outer, 140, 210, 255, ring_alpha);

    // Core filled circle
    u8 core_alpha = (u8)(180 + pulse * 75);
    fill_circle(renderer, cx, cy, core_r, 140, 210, 255, core_alpha);
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

static void draw_portal(SDL_Renderer *renderer, const Portal *portal, SDL_FRect mr) {
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

    // Core filled ellipse
    u8 core_alpha = (u8)(200 + pulse * 55);
    fill_ellipse(renderer, cx, cy, core_rx, core_ry, 120, 80, 240, core_alpha);
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

void g_render_game(GameState *gs, SDL_Renderer *renderer, SDL_FRect map_rect) {
    // Orbs
    for (u32 i = 0; i < gs->num_orbs; i++) {
        draw_orb(renderer, &gs->orbs[i], map_rect);
    }

    // Portal
    draw_portal(renderer, &gs->portal, map_rect);

    // Camp markers (faint circles)
    for (u32 i = 0; i < gs->num_camps; i++) {
        draw_camp_marker(renderer, &gs->camps[i], map_rect);
    }

    // Enemies
    for (u32 i = 0; i < gs->num_enemies; i++) {
        draw_unit(renderer, &gs->enemies[i], map_rect);
    }

    // Projectiles
    for (u32 i = 0; i < gs->num_projectiles; i++) {
        draw_projectile(renderer, &gs->projectiles[i], map_rect);
    }

    // Squad companions
    for (u32 i = 0; i < gs->num_squad; i++) {
        draw_unit(renderer, &gs->squad[i], map_rect);
    }

    // Player (drawn last, on top)
    draw_unit(renderer, &gs->player, map_rect);

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
