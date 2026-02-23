#ifndef DELAUNATOR_H
#define DELAUNATOR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DELAUNATOR_PRECISION
#define DELAUNATOR_PRECISION 64
#endif

#if DELAUNATOR_PRECISION == 64
typedef double d_fp;
typedef size_t d_size;
#else
typedef float d_fp;
typedef uint32_t d_size;
#endif

/* Invalid index sentinel value */
#define DELAUNATOR_INVALID_INDEX ((d_size)-1)

/* Error codes */
typedef enum {
    DELAUNATOR_SUCCESS = 0,
    DELAUNATOR_ERROR_INVALID_INPUT = -1,
    DELAUNATOR_ERROR_NO_TRIANGULATION = -2,
    DELAUNATOR_ERROR_NO_SEED_POINT = -3,
    DELAUNATOR_ERROR_INSUFFICIENT_SCRATCH = -4
} delaunator_error_t;

/* Main structure holding triangulation results */
typedef struct {
    /* Input coordinates (interleaved x,y pairs) - not owned */
    const d_fp* coords;
    size_t num_points;

    /* Output arrays - allocated in scratch space */
    d_size* triangles;
    size_t triangles_len;

    d_size* halfedges;
    size_t halfedges_len;

    d_size* hull_prev;
    d_size* hull_next;
    d_size* hull_tri;

    d_size hull_start;

    /* Internal scratch space tracking */
    void* scratch_space;
    size_t scratch_size;
} delaunator_t;

/**
 * Calculate required output for JUST the triangulation
 *
 * @param num_points Number of points (coords array length / 2)
 * @return Required scratch space size in bytes
 */
size_t delaunator_calculate_output_size(size_t num_points);

/**
 * Calculate required scratch space size for triangulation
 *
 * @param num_points Number of points (coords array length / 2)
 * @return Required scratch space size in bytes
 */
size_t delaunator_calculate_scratch_size(size_t num_points);

/**
 * Perform Delaunay triangulation
 *
 * @param del Output structure (will be populated)
 * @param coords Input coordinates as interleaved x,y pairs [x0,y0,x1,y1,...]
 * @param num_points Number of points (coords array length / 2)
 * @param scratch_space User-provided scratch memory
 * @param scratch_size Size of scratch space in bytes
 * @return Error code (0 on success)
 */
delaunator_error_t delaunator_triangulate(
    delaunator_t* del,
    const d_fp* coords,
    size_t num_points,
    d_size * triangulation,
    size_t triangulation_size,
    void* scratch_space,
    size_t scratch_size);

/**
 * Calculate hull area
 *
 * @param del Triangulation result
 * @return Hull area
 */
d_fp delaunator_get_hull_area(const delaunator_t* del);

#ifdef __cplusplus
}
#endif

#endif /* DELAUNATOR_H */
