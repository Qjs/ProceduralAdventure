#ifndef G_RENDER_PRIMITIVES_H
#define G_RENDER_PRIMITIVES_H

#include "../utils/q_util.h"
#include <SDL3/SDL.h>

void gr_fill_circle(SDL_Renderer *renderer, f32 cx, f32 cy, f32 radius,
                    u8 r, u8 g, u8 b, u8 a);

void gr_draw_ring(SDL_Renderer *renderer, f32 cx, f32 cy,
                  f32 inner_r, f32 outer_r,
                  u8 r, u8 g, u8 b, u8 a);

void gr_fill_ellipse(SDL_Renderer *renderer, f32 cx, f32 cy,
                     f32 rx, f32 ry, u8 r, u8 g, u8 b, u8 a);

void gr_draw_ellipse_ring(SDL_Renderer *renderer, f32 cx, f32 cy,
                          f32 inner_rx, f32 inner_ry,
                          f32 outer_rx, f32 outer_ry,
                          u8 r, u8 g, u8 b, u8 a);

#endif
