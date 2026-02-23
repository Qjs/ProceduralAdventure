#include "delaunator.h"
#include <math.h>
#include <float.h>
#include <string.h>

/* Helper macros */
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

/* Scratch space layout structure */
typedef struct {
    d_size* ids;
    d_size* hull_prev;
    d_size* hull_next;
    d_size* hull_tri;
    d_size* m_hash;
    d_size* m_edge_stack;
    d_size* triangles;
    d_size* halfedges;
    d_fp* hull_area_temp;
} scratch_layout_t;

/* Internal state structure */
typedef struct {
    const d_fp* coords;
    size_t n;
    d_fp m_center_x;
    d_fp m_center_y;
    d_size m_hash_size;

    /* Scratch space arrays */
    d_size* ids;
    d_size* hull_prev;
    d_size* hull_next;
    d_size* hull_tri;
    d_size* m_hash;
    d_size* m_edge_stack;
    size_t m_edge_stack_len;

    d_size* triangles;
    size_t triangles_len;
    size_t triangles_cap;

    d_size* halfedges;
    size_t halfedges_len;
    size_t halfedges_cap;

    d_size hull_start;
} delaunator_state_t;

static inline size_t fast_mod(size_t i, size_t c) {
    return i >= c ? i % c : i;
}

static inline d_fp dist(d_fp ax, d_fp ay, d_fp bx, d_fp by) {
    d_fp dx = ax - bx;
    d_fp dy = ay - by;
    return dx * dx + dy * dy;
}

static inline d_fp circumradius(d_fp ax, d_fp ay, d_fp bx, d_fp by, d_fp cx, d_fp cy) {
    d_fp dx = bx - ax;
    d_fp dy = by - ay;
    d_fp ex = cx - ax;
    d_fp ey = cy - ay;

    d_fp bl = dx * dx + dy * dy;
    d_fp cl = ex * ex + ey * ey;
    d_fp d = dx * ey - dy * ex;

    d_fp x = (ey * bl - dy * cl) * 0.5 / d;
    d_fp y = (dx * cl - ex * bl) * 0.5 / d;

    if ((bl > 0.0 || bl < 0.0) && (cl > 0.0 || cl < 0.0) && (d > 0.0 || d < 0.0)) {
        return x * x + y * y;
    } else {
#if DELAUNATOR_PRECISION == 64
        return DBL_MAX;
#else
        return FLT_MAX;
#endif
    }
}

static inline int orient(d_fp px, d_fp py, d_fp qx, d_fp qy, d_fp rx, d_fp ry) {
    return (qy - py) * (rx - qx) - (qx - px) * (ry - qy) < 0.0;
}

static inline void circumcenter(d_fp ax, d_fp ay, d_fp bx, d_fp by, d_fp cx, d_fp cy,
                                d_fp* out_x, d_fp* out_y) {
    d_fp dx = bx - ax;
    d_fp dy = by - ay;
    d_fp ex = cx - ax;
    d_fp ey = cy - ay;

    d_fp bl = dx * dx + dy * dy;
    d_fp cl = ex * ex + ey * ey;
    d_fp d = dx * ey - dy * ex;

    *out_x = ax + (ey * bl - dy * cl) * 0.5 / d;
    *out_y = ay + (dx * cl - ex * bl) * 0.5 / d;
}

static inline int check_pts_equal(d_fp x1, d_fp y1, d_fp x2, d_fp y2) {
#if DELAUNATOR_PRECISION == 64
    return fabs(x1 - x2) <= DBL_EPSILON && fabs(y1 - y2) <= DBL_EPSILON;
#else
    return fabsf(x1 - x2) <= FLT_EPSILON && fabsf(y1 - y2) <= FLT_EPSILON;
#endif
}

static inline d_fp pseudo_angle(d_fp dx, d_fp dy) {
#if DELAUNATOR_PRECISION == 64
    d_fp p = dx / (fabs(dx) + fabs(dy));
#else
    d_fp p = dx / (fabsf(dx) + fabsf(dy));
#endif
    return (dy > 0.0 ? 3.0 - p : 1.0 + p) / 4.0;
}

static inline int in_circle(d_fp ax, d_fp ay, d_fp bx, d_fp by,
                           d_fp cx, d_fp cy, d_fp px, d_fp py) {
    d_fp dx = ax - px;
    d_fp dy = ay - py;
    d_fp ex = bx - px;
    d_fp ey = by - py;
    d_fp fx = cx - px;
    d_fp fy = cy - py;

    d_fp ap = dx * dx + dy * dy;
    d_fp bp = ex * ex + ey * ey;
    d_fp cp = fx * fx + fy * fy;

    return (dx * (ey * cp - bp * fy) - dy * (ex * cp - bp * fx) + ap * (ex * fy - ey * fx)) < 0.0;
}

static d_size hash_key(const delaunator_state_t* state, d_fp x, d_fp y) {
    d_fp dx = x - state->m_center_x;
    d_fp dy = y - state->m_center_y;
#if DELAUNATOR_PRECISION == 64
    return fast_mod((d_size)llround(floor(pseudo_angle(dx, dy) * (d_fp)state->m_hash_size)),
                   state->m_hash_size);
#else
    return fast_mod((d_size)lroundf(floorf(pseudo_angle(dx, dy) * (d_fp)state->m_hash_size)),
                   state->m_hash_size);
#endif
}

static void link_edge(delaunator_state_t* state, d_size a, d_size b) {
    if (a < state->halfedges_len) {
        state->halfedges[a] = b;
    } else if (a == state->halfedges_len) {
        state->halfedges[state->halfedges_len++] = b;
    }

    if (b != DELAUNATOR_INVALID_INDEX) {
        if (b < state->halfedges_len) {
            state->halfedges[b] = a;
        } else if (b == state->halfedges_len) {
            state->halfedges[state->halfedges_len++] = a;
        }
    }
}

static d_size add_triangle(delaunator_state_t* state,
                          d_size i0, d_size i1, d_size i2,
                          d_size a, d_size b, d_size c) {
    d_size t = state->triangles_len;
    state->triangles[state->triangles_len++] = i0;
    state->triangles[state->triangles_len++] = i1;
    state->triangles[state->triangles_len++] = i2;

    link_edge(state, t, a);
    link_edge(state, t + 1, b);
    link_edge(state, t + 2, c);

    return t;
}

static d_size legalize(delaunator_state_t* state, d_size a) {
    size_t i = 0;
    d_size ar = 0;
    state->m_edge_stack_len = 0;

    while (1) {
        const size_t b = state->halfedges[a];
        const size_t a0 = 3 * (a / 3);
        ar = a0 + (a + 2) % 3;

        if (b == DELAUNATOR_INVALID_INDEX) {
            if (i > 0) {
                i--;
                a = state->m_edge_stack[i];
                continue;
            } else {
                break;
            }
        }

        const size_t b0 = 3 * (b / 3);
        const size_t al = a0 + (a + 1) % 3;
        const size_t bl = b0 + (b + 2) % 3;

        const d_size p0 = state->triangles[ar];
        const d_size pr = state->triangles[a];
        const d_size pl = state->triangles[al];
        const d_size p1 = state->triangles[bl];

        const int illegal = in_circle(
            state->coords[2 * p0], state->coords[2 * p0 + 1],
            state->coords[2 * pr], state->coords[2 * pr + 1],
            state->coords[2 * pl], state->coords[2 * pl + 1],
            state->coords[2 * p1], state->coords[2 * p1 + 1]);

        if (illegal) {
            state->triangles[a] = p1;
            state->triangles[b] = p0;

            d_size hbl = state->halfedges[bl];

            if (hbl == DELAUNATOR_INVALID_INDEX) {
                d_size e = state->hull_start;
                do {
                    if (state->hull_tri[e] == bl) {
                        state->hull_tri[e] = a;
                        break;
                    }
                    e = state->hull_next[e];
                } while (e != state->hull_start);
            }

            link_edge(state, a, hbl);
            link_edge(state, b, state->halfedges[ar]);
            link_edge(state, ar, bl);

            d_size br = b0 + (b + 1) % 3;
            state->m_edge_stack[i++] = br;
            if (i > state->m_edge_stack_len) {
                state->m_edge_stack_len = i;
            }
        } else {
            if (i > 0) {
                i--;
                a = state->m_edge_stack[i];
                continue;
            } else {
                break;
            }
        }
    }

    return ar;
}

/* Comparison function for sorting */
static int compare_dist(const void* va, const void* vb, void* arg) {
    delaunator_state_t* state = (delaunator_state_t*)arg;
    d_size i = *(d_size*)va;
    d_size j = *(d_size*)vb;

    d_fp d1 = dist(state->coords[2 * i], state->coords[2 * i + 1],
                   state->m_center_x, state->m_center_y);
    d_fp d2 = dist(state->coords[2 * j], state->coords[2 * j + 1],
                   state->m_center_x, state->m_center_y);
    d_fp diff1 = d1 - d2;
    d_fp diff2 = state->coords[2 * i] - state->coords[2 * j];
    d_fp diff3 = state->coords[2 * i + 1] - state->coords[2 * j + 1];

    if (diff1 > 0.0 || diff1 < 0.0) {
        return diff1 < 0 ? -1 : 1;
    } else if (diff2 > 0.0 || diff2 < 0.0) {
        return diff2 < 0 ? -1 : 1;
    } else {
        return diff3 < 0 ? -1 : 1;
    }
}

static void sort_ids(d_size* ids, size_t n, delaunator_state_t* state) {
    if (n <= 1) return;

    /* Build max heap */
    for (size_t i = n / 2; i-- > 0; ) {
        size_t curr = i;
        while (curr < n) {
            size_t largest = curr;
            size_t left = 2 * curr + 1;
            size_t right = 2 * curr + 2;

            if (left < n && compare_dist(&ids[left], &ids[largest], state) > 0)
                largest = left;
            if (right < n && compare_dist(&ids[right], &ids[largest], state) > 0)
                largest = right;

            if (largest == curr) break;

            d_size temp = ids[curr];
            ids[curr] = ids[largest];
            ids[largest] = temp;
            curr = largest;
        }
    }

    /* Extract from heap */
    for (size_t i = n - 1; i > 0; i--) {
        d_size temp = ids[0];
        ids[0] = ids[i];
        ids[i] = temp;

        /* Heapify root */
        size_t curr = 0;
        while (curr < i) {
            size_t largest = curr;
            size_t left = 2 * curr + 1;
            size_t right = 2 * curr + 2;

            if (left < i && compare_dist(&ids[left], &ids[largest], state) > 0)
                largest = left;
            if (right < i && compare_dist(&ids[right], &ids[largest], state) > 0)
                largest = right;

            if (largest == curr) break;

            d_size temp = ids[curr];
            ids[curr] = ids[largest];
            ids[largest] = temp;
            curr = largest;
        }
    }
}

size_t delaunator_calculate_output_size(size_t num_points) {
    if (num_points < 3) {
        return 0;
    }

    size_t n = num_points;
    d_size max_triangles = 2 * n - 5;
    return 64 + sizeof(d_size) * max_triangles * 3;    /* triangles */
}


size_t delaunator_calculate_scratch_size(size_t num_points) {
    if (num_points < 3) {
        return 0;
    }

    size_t n = num_points;
    d_size max_triangles = 2 * n - 5;
    d_size hash_size = (d_size)(ceil(sqrt((double)n)));

    size_t size = 0;
    size += sizeof(d_size) * n;                    /* ids */
    size += sizeof(d_size) * n;                    /* hull_prev */
    size += sizeof(d_size) * n;                    /* hull_next */
    size += sizeof(d_size) * n;                    /* hull_tri */
    size += sizeof(d_size) * hash_size;            /* m_hash */
    size += sizeof(d_size) * max_triangles;        /* m_edge_stack */
//  size += sizeof(d_size) * max_triangles * 3;    /* triangles */
    size += sizeof(d_size) * max_triangles * 3;    /* halfedges */
    size += sizeof(d_fp) * n;                      /* hull_area_temp */

    /* Add padding for alignment */
    size += 64;

    return size;
}

static void* align_ptr(void* ptr, size_t alignment) {
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t aligned = (addr + alignment - 1) & ~(alignment - 1);
    return (void*)aligned;
}

static void setup_scratch_space(void* scratch_space, size_t num_points,
                                scratch_layout_t* layout) {
    d_size max_triangles = 2 * num_points - 5;
    d_size hash_size = (d_size)(ceil(sqrt((double)num_points)));

    char* ptr = (char*)align_ptr(scratch_space, sizeof(d_size));

    layout->ids = (d_size*)ptr;
    ptr += sizeof(d_size) * num_points;

    layout->hull_prev = (d_size*)ptr;
    ptr += sizeof(d_size) * num_points;

    layout->hull_next = (d_size*)ptr;
    ptr += sizeof(d_size) * num_points;

    layout->hull_tri = (d_size*)ptr;
    ptr += sizeof(d_size) * num_points;

    layout->m_hash = (d_size*)ptr;
    ptr += sizeof(d_size) * hash_size;

    layout->m_edge_stack = (d_size*)ptr;
    ptr += sizeof(d_size) * max_triangles;

	if(layout->triangles == 0L)
	{
		layout->triangles = (d_size*)ptr;
		ptr += sizeof(d_size) * max_triangles * 3;
    }

    layout->halfedges = (d_size*)ptr;
    ptr += sizeof(d_size) * max_triangles * 3;

    layout->hull_area_temp = (d_fp*)ptr;
}

/* Kahan summation */
static d_fp sum_array(const d_fp* x, size_t len) {
    if (len == 0) return 0.0;

    double sum = x[0];
    double err = 0.0;

    for (size_t i = 1; i < len; i++) {
        const d_fp k = x[i];
        const d_fp m = sum + k;
#if DELAUNATOR_PRECISION == 64
        err += fabs(sum) >= fabs(k) ? sum - m + k : k - m + sum;
#else
        err += fabsf(sum) >= fabsf(k) ? sum - m + k : k - m + sum;
#endif
        sum = m;
    }
    return sum + err;
}

delaunator_error_t delaunator_triangulate(
    delaunator_t* del,
    const d_fp* coords,
    size_t num_points,
    d_size * triangulation,
    size_t triangulation_size,
    void* scratch_space,
    size_t scratch_size)
    {

    if (!del || !coords || !scratch_space || num_points < 3) {
        return DELAUNATOR_ERROR_INVALID_INPUT;
    }

    size_t required_size = delaunator_calculate_scratch_size(num_points);
    size_t output_size   =  delaunator_calculate_output_size(num_points);
    if (scratch_size < required_size
    ||  triangulation_size < output_size) {

		if(scratch_size < output_size+scratch_size)
			return DELAUNATOR_ERROR_INSUFFICIENT_SCRATCH;
    }

    memset(del, 0, sizeof(delaunator_t));
    del->coords = coords;
    del->num_points = num_points;
    del->scratch_space = scratch_space;
    del->scratch_size = scratch_size;


    scratch_layout_t layout;

	if(triangulation_size >= output_size)
		layout.triangles = triangulation;
	else
		layout.triangles = 0L;

    setup_scratch_space(scratch_space, num_points, &layout);

    delaunator_state_t state;
    memset(&state, 0, sizeof(state));
    state.coords = coords;
    state.n = num_points;
    state.ids = layout.ids;
    state.hull_prev = layout.hull_prev;
    state.hull_next = layout.hull_next;
    state.hull_tri = layout.hull_tri;
    state.m_hash = layout.m_hash;
    state.m_edge_stack = layout.m_edge_stack;
    state.triangles = layout.triangles;
    state.halfedges = layout.halfedges;

    d_size max_triangles = 2 * num_points - 5;
    state.triangles_cap = max_triangles * 3;
    state.halfedges_cap = max_triangles * 3;

#if DELAUNATOR_PRECISION == 64
    d_fp max_x = -DBL_MAX;
    d_fp max_y = -DBL_MAX;
    d_fp min_x = DBL_MAX;
    d_fp min_y = DBL_MAX;
#else
    d_fp max_x = -FLT_MAX;
    d_fp max_y = -FLT_MAX;
    d_fp min_x = FLT_MAX;
    d_fp min_y = FLT_MAX;
#endif

    for (d_size i = 0; i < num_points; i++) {
        d_fp x = coords[2 * i];
        d_fp y = coords[2 * i + 1];

        if (x < min_x) min_x = x;
        if (y < min_y) min_y = y;
        if (x > max_x) max_x = x;
        if (y > max_y) max_y = y;

        state.ids[i] = i;
    }

    d_fp cx = (min_x + max_x) / 2;
    d_fp cy = (min_y + max_y) / 2;

#if DELAUNATOR_PRECISION == 64
    d_fp min_dist = DBL_MAX;
#else
    d_fp min_dist = FLT_MAX;
#endif

    d_size i0 = DELAUNATOR_INVALID_INDEX;
    d_size i1 = DELAUNATOR_INVALID_INDEX;
    d_size i2 = DELAUNATOR_INVALID_INDEX;

    /* Pick seed point close to centroid */
    for (d_size i = 0; i < num_points; i++) {
        d_fp d = dist(cx, cy, coords[2 * i], coords[2 * i + 1]);
        if (d < min_dist) {
            i0 = i;
            min_dist = d;
        }
    }

    if (i0 == DELAUNATOR_INVALID_INDEX) {
        return DELAUNATOR_ERROR_NO_SEED_POINT;
    }

    d_fp i0x = coords[2 * i0];
    d_fp i0y = coords[2 * i0 + 1];

#if DELAUNATOR_PRECISION == 64
    min_dist = DBL_MAX;
#else
    min_dist = FLT_MAX;
#endif

    /* Find closest point to seed */
    for (d_size i = 0; i < num_points; i++) {
        if (i == i0) continue;
        d_fp d = dist(i0x, i0y, coords[2 * i], coords[2 * i + 1]);
        if (d < min_dist && d > 0.0) {
            i1 = i;
            min_dist = d;
        }
    }

    d_fp i1x = coords[2 * i1];
    d_fp i1y = coords[2 * i1 + 1];

#if DELAUNATOR_PRECISION == 64
    d_fp min_radius = DBL_MAX;
#else
    d_fp min_radius = FLT_MAX;
#endif

    /* Find third point forming smallest circumcircle */
    for (d_size i = 0; i < num_points; i++) {
        if (i == i0 || i == i1) continue;

        d_fp r = circumradius(i0x, i0y, i1x, i1y, coords[2 * i], coords[2 * i + 1]);

        if (r < min_radius) {
            i2 = i;
            min_radius = r;
        }
    }

#if DELAUNATOR_PRECISION == 64
    if (!(min_radius < DBL_MAX)) {
#else
    if (!(min_radius < FLT_MAX)) {
#endif
        return DELAUNATOR_ERROR_NO_TRIANGULATION;
    }

    d_fp i2x = coords[2 * i2];
    d_fp i2y = coords[2 * i2 + 1];

    if (orient(i0x, i0y, i1x, i1y, i2x, i2y)) {
        d_size tmp = i1; i1 = i2; i2 = tmp;
        d_fp tmpx = i1x; i1x = i2x; i2x = tmpx;
        d_fp tmpy = i1y; i1y = i2y; i2y = tmpy;
    }

    circumcenter(i0x, i0y, i1x, i1y, i2x, i2y, &state.m_center_x, &state.m_center_y);

    /* Sort points by distance from circumcenter */
    sort_ids(state.ids, num_points, &state);

    /* Initialize hash table */
#if DELAUNATOR_PRECISION == 64
    state.m_hash_size = (d_size)llround(ceil(sqrt((double)num_points)));
#else
    state.m_hash_size = (d_size)lroundf(ceilf(sqrtf((float)num_points)));
#endif

    for (d_size i = 0; i < state.m_hash_size; i++) {
        state.m_hash[i] = DELAUNATOR_INVALID_INDEX;
    }

    state.hull_start = i0;

    state.hull_next[i0] = state.hull_prev[i2] = i1;
    state.hull_next[i1] = state.hull_prev[i0] = i2;
    state.hull_next[i2] = state.hull_prev[i1] = i0;

    state.hull_tri[i0] = 0;
    state.hull_tri[i1] = 1;
    state.hull_tri[i2] = 2;

    state.m_hash[hash_key(&state, i0x, i0y)] = i0;
    state.m_hash[hash_key(&state, i1x, i1y)] = i1;
    state.m_hash[hash_key(&state, i2x, i2y)] = i2;

    add_triangle(&state, i0, i1, i2, DELAUNATOR_INVALID_INDEX,
                DELAUNATOR_INVALID_INDEX, DELAUNATOR_INVALID_INDEX);

    d_fp xp = NAN;
    d_fp yp = NAN;

    for (d_size k = 0; k < num_points; k++) {
        const d_size i = state.ids[k];
        const d_fp x = coords[2 * i];
        const d_fp y = coords[2 * i + 1];

        /* Skip near-duplicate points */
        if (k > 0 && check_pts_equal(x, y, xp, yp)) continue;
        xp = x;
        yp = y;

        /* Skip seed triangle points */
        if (check_pts_equal(x, y, i0x, i0y) ||
            check_pts_equal(x, y, i1x, i1y) ||
            check_pts_equal(x, y, i2x, i2y)) continue;

        /* Find visible edge on convex hull */
        d_size start = 0;
        size_t key = hash_key(&state, x, y);
        for (size_t j = 0; j < state.m_hash_size; j++) {
            start = state.m_hash[fast_mod(key + j, state.m_hash_size)];
            if (start != DELAUNATOR_INVALID_INDEX && start != state.hull_next[start]) break;
        }

        start = state.hull_prev[start];
        size_t e = start;
        size_t q;

        while (q = state.hull_next[e],
               !orient(x, y, coords[2 * e], coords[2 * e + 1],
                      coords[2 * q], coords[2 * q + 1])) {
            e = q;
            if (e == start) {
                e = DELAUNATOR_INVALID_INDEX;
                break;
            }
        }

        if (e == DELAUNATOR_INVALID_INDEX) continue;

        /* Add first triangle from point */
        d_size t = add_triangle(&state, e, i, state.hull_next[e],
                               DELAUNATOR_INVALID_INDEX, DELAUNATOR_INVALID_INDEX,
                               state.hull_tri[e]);

        state.hull_tri[i] = legalize(&state, t + 2);
        state.hull_tri[e] = t;

        /* Walk forward through hull */
        d_size next = state.hull_next[e];
        while (q = state.hull_next[next],
               orient(x, y, coords[2 * next], coords[2 * next + 1],
                     coords[2 * q], coords[2 * q + 1])) {
            t = add_triangle(&state, next, i, q, state.hull_tri[i],
                           DELAUNATOR_INVALID_INDEX, state.hull_tri[next]);
            state.hull_tri[i] = legalize(&state, t + 2);
            state.hull_next[next] = next;
            next = q;
        }

        /* Walk backward from other side */
        if (e == start) {
            while (q = state.hull_prev[e],
                   orient(x, y, coords[2 * q], coords[2 * q + 1],
                         coords[2 * e], coords[2 * e + 1])) {
                t = add_triangle(&state, q, i, e, DELAUNATOR_INVALID_INDEX,
                               state.hull_tri[e], state.hull_tri[q]);
                legalize(&state, t + 2);
                state.hull_tri[q] = t;
                state.hull_next[e] = e;
                e = q;
            }
        }

        /* Update hull indices */
        state.hull_prev[i] = e;
        state.hull_start = e;
        state.hull_prev[next] = i;
        state.hull_next[e] = i;
        state.hull_next[i] = next;

        state.m_hash[hash_key(&state, x, y)] = i;
        state.m_hash[hash_key(&state, coords[2 * e], coords[2 * e + 1])] = e;
    }

    /* Copy results to output structure */
    del->triangles = state.triangles;
    del->triangles_len = state.triangles_len;
    del->halfedges = state.halfedges;
    del->halfedges_len = state.halfedges_len;
    del->hull_prev = state.hull_prev;
    del->hull_next = state.hull_next;
    del->hull_tri = state.hull_tri;
    del->hull_start = state.hull_start;

    return DELAUNATOR_SUCCESS;
}

d_fp delaunator_get_hull_area(const delaunator_t* del) {
    if (!del || !del->coords) {
        return 0.0;
    }

    scratch_layout_t layout;
    setup_scratch_space(del->scratch_space, del->num_points, &layout);

    size_t hull_area_len = 0;
    size_t e = del->hull_start;
    do {
        layout.hull_area_temp[hull_area_len++] =
            (del->coords[2 * e] - del->coords[2 * del->hull_prev[e]]) *
            (del->coords[2 * e + 1] + del->coords[2 * del->hull_prev[e] + 1]);
        e = del->hull_next[e];
    } while (e != del->hull_start);

    return sum_array(layout.hull_area_temp, hull_area_len);
}
