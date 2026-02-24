#include "g_render.h"
#include "g_render_entities.h"
#include "g_render_effects.h"
#include "g_particles.h"

void g_render_game(GameState *gs, SDL_Renderer *renderer, SDL_FRect map_rect,
                   SDL_Texture *role_textures[ROLE_COUNT]) {
    for (u32 i = 0; i < gs->num_orbs; i++) {
        gr_draw_orb(renderer, &gs->orbs[i], map_rect, gs->orb_texture);
    }

    gr_draw_portal(renderer, &gs->portal, map_rect, gs->portal_texture);
    gr_draw_environment_effect(renderer, gs, map_rect);

    for (u32 i = 0; i < gs->num_camps; i++) {
        gr_draw_camp_marker(renderer, &gs->camps[i], map_rect);
    }

    for (u32 i = 0; i < gs->num_enemies; i++) {
        Unit *e = &gs->enemies[i];
        SDL_Texture *tex = (e->role < ROLE_COUNT) ? role_textures[e->role] : NULL;
        gr_draw_unit_composite(renderer, e, map_rect, tex);
    }

    for (u32 i = 0; i < gs->num_projectiles; i++) {
        gr_draw_projectile(renderer, &gs->projectiles[i], map_rect);
    }

    g_particles_render(&gs->particles, renderer, map_rect);

    for (u32 i = 0; i < gs->num_squad; i++) {
        Unit *u = &gs->squad[i];
        SDL_Texture *tex = (u->role < ROLE_COUNT) ? role_textures[u->role] : NULL;
        gr_draw_squad_layered(renderer, gs, u, map_rect, tex);
    }

    {
        Unit *p = &gs->player;
        SDL_Texture *tex = (p->role < ROLE_COUNT) ? role_textures[p->role] : NULL;
        gr_draw_squad_layered(renderer, gs, p, map_rect, tex);
    }

    for (u32 i = 0; i < gs->num_enemies; i++) {
        if (gs->enemies[i].hp < gs->enemies[i].max_hp)
            gr_draw_health_bar(renderer, &gs->enemies[i], map_rect);
    }
    for (u32 i = 0; i < gs->num_squad; i++) {
        gr_draw_health_bar(renderer, &gs->squad[i], map_rect);
    }
    gr_draw_health_bar(renderer, &gs->player, map_rect);
}
