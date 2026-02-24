#include "g_unit_gen.h"
#include "stb_perlin.h"
#include <math.h>
#include <string.h>

#define TEX_SIZE 32

typedef struct {
    u8 r, g, b;
} Color;

typedef struct {
    Color stops[4];
    Color rim;
    f32   noise_freq;
    f32   noise_gain;
    s32   noise_octaves;
    f32   seed_z;        // unique per role
} RolePalette;

static Color color_lerp(Color a, Color b, f32 t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return (Color){
        (u8)(a.r + (b.r - a.r) * t),
        (u8)(a.g + (b.g - a.g) * t),
        (u8)(a.b + (b.b - a.b) * t),
    };
}

// Sample a 4-stop palette at t in [0,1]
static Color palette_sample(const Color stops[4], f32 t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    f32 scaled = t * 3.0f; // 3 segments between 4 stops
    s32 idx = (s32)scaled;
    if (idx >= 3) idx = 2;
    f32 frac = scaled - (f32)idx;
    return color_lerp(stops[idx], stops[idx + 1], frac);
}

static RolePalette role_palettes[ROLE_COUNT] = {
    // ROLE_PLAYER — White/silver/steel (heroic armor)
    [ROLE_PLAYER] = {
        .stops = {{240, 240, 250}, {210, 215, 230}, {180, 190, 210}, {150, 160, 185}},
        .rim = {255, 220, 120},
        .noise_freq = 3.0f, .noise_gain = 0.4f, .noise_octaves = 3, .seed_z = 0.5f,
    },
    // ROLE_MELEE — Red/crimson/brown (heavy armor, rocky turbulence)
    [ROLE_MELEE] = {
        .stops = {{230, 90, 70}, {200, 70, 55}, {170, 80, 50}, {140, 65, 45}},
        .rim = {255, 220, 120},
        .noise_freq = 5.0f, .noise_gain = 0.5f, .noise_octaves = 4, .seed_z = 1.5f,
    },
    // ROLE_ARCHER — Green/olive/tan (leather, directional streaks)
    [ROLE_ARCHER] = {
        .stops = {{140, 190, 80}, {120, 170, 70}, {150, 145, 85}, {130, 120, 75}},
        .rim = {255, 220, 120},
        .noise_freq = 4.0f, .noise_gain = 0.45f, .noise_octaves = 3, .seed_z = 2.5f,
    },
    // ROLE_HEALER — Gold/amber/cream (cloth robes, smooth radial)
    [ROLE_HEALER] = {
        .stops = {{255, 235, 160}, {245, 210, 120}, {230, 190, 100}, {215, 170, 85}},
        .rim = {255, 230, 130},
        .noise_freq = 2.5f, .noise_gain = 0.35f, .noise_octaves = 3, .seed_z = 3.5f,
    },
    // ROLE_MAGE — Blue/purple/indigo (magical robes, swirling turbulence)
    [ROLE_MAGE] = {
        .stops = {{120, 110, 240}, {140, 90, 220}, {110, 80, 200}, {90, 65, 175}},
        .rim = {255, 220, 120},
        .noise_freq = 4.5f, .noise_gain = 0.5f, .noise_octaves = 4, .seed_z = 4.5f,
    },
    // ROLE_ENEMY_MELEE — Dark red/black/rust (crude armor, rough high-freq)
    [ROLE_ENEMY_MELEE] = {
        .stops = {{170, 50, 35}, {130, 40, 30}, {100, 35, 25}, {75, 25, 20}},
        .rim = {210, 60, 40},
        .noise_freq = 7.0f, .noise_gain = 0.55f, .noise_octaves = 4, .seed_z = 5.5f,
    },
    // ROLE_ENEMY_RANGED — Purple/dark violet/gray (dark cloth, wispy turbulence)
    [ROLE_ENEMY_RANGED] = {
        .stops = {{145, 80, 175}, {120, 60, 150}, {95, 55, 120}, {80, 60, 95}},
        .rim = {210, 60, 40},
        .noise_freq = 5.0f, .noise_gain = 0.5f, .noise_octaves = 4, .seed_z = 6.5f,
    },
};

static void generate_role_texture(SDL_Renderer *renderer, UnitRole role,
                                   SDL_Texture **out) {
    const RolePalette *pal = &role_palettes[role];

    u8 pixels[TEX_SIZE * TEX_SIZE * 4];
    memset(pixels, 0, sizeof(pixels));

    f32 cx = (f32)TEX_SIZE * 0.5f;
    f32 cy = (f32)TEX_SIZE * 0.5f;
    f32 rad = cx - 1.0f; // leave 1px border for antialiasing

    // Light direction (top-left, normalized)
    f32 light_x = -0.5f, light_y = -0.7f, light_z = 0.5f;
    f32 light_len = sqrtf(light_x * light_x + light_y * light_y + light_z * light_z);
    light_x /= light_len; light_y /= light_len; light_z /= light_len;

    for (s32 py = 0; py < TEX_SIZE; py++) {
        for (s32 px = 0; px < TEX_SIZE; px++) {
            f32 dx = ((f32)px + 0.5f - cx) / rad;
            f32 dy = ((f32)py + 0.5f - cy) / rad;
            f32 dist2 = dx * dx + dy * dy;

            u8 *pixel = &pixels[(py * TEX_SIZE + px) * 4];

            if (dist2 > 1.0f) {
                // Outside circle — transparent
                pixel[0] = pixel[1] = pixel[2] = pixel[3] = 0;
                continue;
            }

            f32 dist = sqrtf(dist2);

            // Spherical projection: map 2D disk to 3D sphere surface
            f32 sz = sqrtf(1.0f - dist2); // z on unit sphere
            f32 sx = dx;
            f32 sy = dy;

            // fBm noise using spherical coordinates
            f32 nx = sx * pal->noise_freq;
            f32 ny = sy * pal->noise_freq;
            f32 nz = sz * pal->noise_freq + pal->seed_z;

            f32 noise = stb_perlin_fbm_noise3(nx, ny, nz,
                                               2.0f, pal->noise_gain,
                                               pal->noise_octaves);

            // Role-specific noise modifiers
            switch (role) {
                case ROLE_ARCHER: {
                    // Directional streaks along x-axis
                    f32 streak = stb_perlin_fbm_noise3(
                        nx * 2.0f, ny * 0.3f, nz, 2.0f, 0.4f, 2);
                    noise = noise * 0.6f + streak * 0.4f;
                    break;
                }
                case ROLE_MAGE: {
                    // Swirling: rotate noise coords by angle from center
                    f32 angle = atan2f(dy, dx);
                    f32 swirl = stb_perlin_fbm_noise3(
                        nx + sinf(angle * 3.0f) * 0.5f,
                        ny + cosf(angle * 3.0f) * 0.5f,
                        nz, 2.0f, 0.5f, 3);
                    noise = noise * 0.5f + swirl * 0.5f;
                    break;
                }
                case ROLE_HEALER: {
                    // Smooth radial gradient blend
                    noise = noise * 0.7f + (1.0f - dist) * 0.3f;
                    break;
                }
                default:
                    break;
            }

            // Map noise to [0,1] range for palette sampling
            f32 t = (noise + 0.5f); // fbm roughly in [-0.5, 0.5]
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;

            Color col = palette_sample(pal->stops, t);

            // Directional lighting (dot product with light direction)
            f32 ndotl = sx * light_x + sy * light_y + sz * light_z;
            if (ndotl < 0.0f) ndotl = 0.0f;
            f32 lighting = 0.55f + 0.45f * ndotl; // ambient 0.55 + diffuse 0.45

            // Limb darkening (subtle darken at edges)
            f32 limb = 0.75f + 0.25f * sz;
            lighting *= limb;

            col.r = (u8)(col.r * lighting);
            col.g = (u8)(col.g * lighting);
            col.b = (u8)(col.b * lighting);

            // Rim glow — strongest at edges, in team color
            f32 rim_strength = 1.0f - sz; // 0 at center, 1 at edge
            rim_strength = rim_strength * rim_strength * rim_strength; // cubic falloff
            rim_strength *= 0.6f; // max intensity

            col.r = (u8)(col.r * (1.0f - rim_strength) + pal->rim.r * rim_strength);
            col.g = (u8)(col.g * (1.0f - rim_strength) + pal->rim.g * rim_strength);
            col.b = (u8)(col.b * (1.0f - rim_strength) + pal->rim.b * rim_strength);

            // Soft edge antialiasing
            f32 alpha = 1.0f;
            if (dist > 0.9f) {
                alpha = (1.0f - dist) / 0.1f;
                if (alpha < 0.0f) alpha = 0.0f;
            }

            pixel[0] = col.r;
            pixel[1] = col.g;
            pixel[2] = col.b;
            pixel[3] = (u8)(alpha * 255.0f);
        }
    }

    // Create SDL texture from pixel data
    SDL_Surface *surface = SDL_CreateSurfaceFrom(
        TEX_SIZE, TEX_SIZE, SDL_PIXELFORMAT_RGBA32, pixels, TEX_SIZE * 4);
    if (surface) {
        *out = SDL_CreateTextureFromSurface(renderer, surface);
        if (*out) {
            SDL_SetTextureBlendMode(*out, SDL_BLENDMODE_BLEND);
        }
        SDL_DestroySurface(surface);
    }
}

void g_unit_gen_textures(SDL_Renderer *renderer, SDL_Texture *out[ROLE_COUNT]) {
    for (s32 i = 0; i < ROLE_COUNT; i++) {
        out[i] = NULL;
        generate_role_texture(renderer, (UnitRole)i, &out[i]);
    }
}

// ---------------------------------------------------------------------------
// Procedural swirly orb / portal texture generation
// ---------------------------------------------------------------------------

static SDL_Texture *generate_swirl_texture(SDL_Renderer *renderer,
                                            const Color stops[4], const Color rim,
                                            f32 freq, f32 seed_z, f32 swirl_strength,
                                            s32 size) {
    u8 *pixels = (u8 *)SDL_calloc(size * size * 4, 1);
    if (!pixels) return NULL;

    f32 cx = (f32)size * 0.5f;
    f32 cy = (f32)size * 0.5f;
    f32 rad = cx - 1.0f;

    f32 light_x = -0.4f, light_y = -0.6f, light_z = 0.65f;
    f32 light_len = sqrtf(light_x * light_x + light_y * light_y + light_z * light_z);
    light_x /= light_len; light_y /= light_len; light_z /= light_len;

    for (s32 py = 0; py < size; py++) {
        for (s32 px = 0; px < size; px++) {
            f32 dx = ((f32)px + 0.5f - cx) / rad;
            f32 dy = ((f32)py + 0.5f - cy) / rad;
            f32 dist2 = dx * dx + dy * dy;
            u8 *pixel = &pixels[(py * size + px) * 4];

            if (dist2 > 1.0f) {
                pixel[0] = pixel[1] = pixel[2] = pixel[3] = 0;
                continue;
            }

            f32 dist = sqrtf(dist2);
            f32 sz = sqrtf(1.0f - dist2);
            f32 sx = dx, sy = dy;

            // Spherical noise coordinates
            f32 nx = sx * freq;
            f32 ny = sy * freq;
            f32 nz = sz * freq + seed_z;

            // Base fBm noise
            f32 noise = stb_perlin_fbm_noise3(nx, ny, nz, 2.0f, 0.5f, 4);

            // Swirl: perturb coords by polar angle, scaled by distance from center
            f32 angle = atan2f(dy, dx);
            f32 swirl_amt = swirl_strength * dist;
            f32 swirl = stb_perlin_fbm_noise3(
                nx + sinf(angle * 4.0f + dist * 6.0f) * swirl_amt,
                ny + cosf(angle * 4.0f + dist * 6.0f) * swirl_amt,
                nz + sinf(angle * 2.0f) * 0.3f,
                2.0f, 0.5f, 3);

            // Blend base noise with swirl — heavy swirl bias for the swirly look
            noise = noise * 0.3f + swirl * 0.7f;

            // Map to palette
            f32 t = noise + 0.5f;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            Color col = palette_sample(stops, t);

            // Lighting
            f32 ndotl = sx * light_x + sy * light_y + sz * light_z;
            if (ndotl < 0.0f) ndotl = 0.0f;
            f32 lighting = 0.5f + 0.5f * ndotl;
            f32 limb = 0.7f + 0.3f * sz;
            lighting *= limb;

            col.r = (u8)(col.r * lighting);
            col.g = (u8)(col.g * lighting);
            col.b = (u8)(col.b * lighting);

            // Rim glow — colored edge highlight
            f32 rim_strength = 1.0f - sz;
            rim_strength = rim_strength * rim_strength * rim_strength * rim_strength;
            rim_strength *= 0.7f;
            col.r = (u8)(col.r * (1.0f - rim_strength) + rim.r * rim_strength);
            col.g = (u8)(col.g * (1.0f - rim_strength) + rim.g * rim_strength);
            col.b = (u8)(col.b * (1.0f - rim_strength) + rim.b * rim_strength);

            // Soft edge
            f32 alpha = 1.0f;
            if (dist > 0.85f) {
                alpha = (1.0f - dist) / 0.15f;
                if (alpha < 0.0f) alpha = 0.0f;
            }

            pixel[0] = col.r;
            pixel[1] = col.g;
            pixel[2] = col.b;
            pixel[3] = (u8)(alpha * 255.0f);
        }
    }

    SDL_Texture *tex = NULL;
    SDL_Surface *surface = SDL_CreateSurfaceFrom(
        size, size, SDL_PIXELFORMAT_RGBA32, pixels, size * 4);
    if (surface) {
        tex = SDL_CreateTextureFromSurface(renderer, surface);
        if (tex) SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_DestroySurface(surface);
    }
    SDL_free(pixels);
    return tex;
}

SDL_Texture *g_gen_orb_texture(SDL_Renderer *renderer) {
    // Light blue swirly orb
    Color stops[4] = {{180, 230, 255}, {120, 200, 250}, {80, 170, 240}, {50, 140, 220}};
    Color rim_col = {200, 240, 255};
    return generate_swirl_texture(renderer, stops, rim_col,
                                   4.0f, 7.5f, 0.8f, 48);
}

SDL_Texture *g_gen_portal_texture(SDL_Renderer *renderer) {
    // Purple swirly portal
    Color stops[4] = {{180, 120, 255}, {140, 80, 240}, {110, 60, 210}, {80, 40, 180}};
    Color rim_col = {220, 160, 255};
    return generate_swirl_texture(renderer, stops, rim_col,
                                   5.0f, 9.0f, 1.0f, 48);
}

void g_unit_gen_destroy(SDL_Texture *textures[ROLE_COUNT]) {
    for (s32 i = 0; i < ROLE_COUNT; i++) {
        if (textures[i]) {
            SDL_DestroyTexture(textures[i]);
            textures[i] = NULL;
        }
    }
}
