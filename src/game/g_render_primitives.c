#include "g_render_primitives.h"
#include <math.h>

#define CIRCLE_SEGMENTS 24

void gr_fill_circle(SDL_Renderer *renderer, f32 cx, f32 cy, f32 radius,
                    u8 r, u8 g, u8 b, u8 a) {
    SDL_Vertex verts[CIRCLE_SEGMENTS + 2];
    int indices[CIRCLE_SEGMENTS * 3];

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

void gr_draw_ring(SDL_Renderer *renderer, f32 cx, f32 cy,
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

void gr_fill_ellipse(SDL_Renderer *renderer, f32 cx, f32 cy,
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

void gr_draw_ellipse_ring(SDL_Renderer *renderer, f32 cx, f32 cy,
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
