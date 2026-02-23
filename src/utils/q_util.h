#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- Integer types ---- */
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

/* ---- Floating point ---- */
typedef float f32;
typedef double f64;

/* ---- Array helpers ---- */
#define ARRAY_LEN(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* ---- Min / Max ---- */
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(x, l, h) (MAX((l), MIN((x), (h))))

/* ---- Bit helpers ---- */
#define BIT(n) (1u << (n))
#define HAS_BIT(x, n) (((x) & BIT(n)) != 0)
