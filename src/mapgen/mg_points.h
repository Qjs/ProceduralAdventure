#ifndef MG_POINTS_H
#define MG_POINTS_H

#include "mg_types.h"

// Generate jittered grid points in [0,1]². Returns interleaved f32 pairs.
// Caller must free() the returned pointer. *out_count receives actual point count.
f32 *mg_generate_points(u32 site_count, u32 seed, u32 *out_count);

#endif
