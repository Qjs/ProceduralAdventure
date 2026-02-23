#include "mg_mesh.h"
#include "delaunator.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

// We use 64-bit delaunator (doubles) for precision, then store results as f32.

static void circumcenter_f64(double ax, double ay, double bx, double by,
                             double cx, double cy, double *ox, double *oy) {
    double dx = bx - ax, dy = by - ay;
    double ex = cx - ax, ey = cy - ay;
    double bl = dx*dx + dy*dy;
    double cl = ex*ex + ey*ey;
    double d  = dx*ey - dy*ex;
    if (fabs(d) < 1e-12) { *ox = (ax+bx+cx)/3.0; *oy = (ay+by+cy)/3.0; return; }
    *ox = ax + (ey*bl - dy*cl) * 0.5 / d;
    *oy = ay + (dx*cl - ex*bl) * 0.5 / d;
}

bool mg_build_mesh(MapGraph *graph, const f32 *coords_f32, u32 num_points) {
    memset(graph, 0, sizeof(*graph));

    // Convert coords to double for delaunator
    double *coords = malloc(sizeof(double) * 2 * num_points);
    if (!coords) return false;
    for (u32 i = 0; i < num_points * 2; i++)
        coords[i] = (double)coords_f32[i];

    // Allocate scratch + output
    size_t out_size     = delaunator_calculate_output_size(num_points);
    size_t scratch_size = delaunator_calculate_scratch_size(num_points);
    void *out_buf    = malloc(out_size);
    void *scratch    = malloc(scratch_size);
    if (!out_buf || !scratch) { free(coords); free(out_buf); free(scratch); return false; }

    delaunator_t del;
    delaunator_error_t err = delaunator_triangulate(
        &del, coords, num_points,
        (d_size*)out_buf, out_size,
        scratch, scratch_size);

    if (err != DELAUNATOR_SUCCESS) {
        free(coords); free(out_buf); free(scratch);
        return false;
    }

    u32 num_triangles = (u32)(del.triangles_len / 3);
    u32 num_halfedges = (u32)del.halfedges_len;

    // --- Mark hull sites ---
    bool *on_hull = calloc(num_points, sizeof(bool));
    {
        d_size e = del.hull_start;
        do {
            on_hull[e] = true;
            e = del.hull_next[e];
        } while (e != del.hull_start);
    }

    // --- Compute circumcenters (one Corner per triangle) ---
    graph->num_corners = num_triangles;
    graph->corners = calloc(num_triangles, sizeof(Corner));

    for (u32 t = 0; t < num_triangles; t++) {
        u32 i0 = (u32)del.triangles[3*t+0];
        u32 i1 = (u32)del.triangles[3*t+1];
        u32 i2 = (u32)del.triangles[3*t+2];
        double cx, cy;
        circumcenter_f64(coords[2*i0], coords[2*i0+1],
                         coords[2*i1], coords[2*i1+1],
                         coords[2*i2], coords[2*i2+1], &cx, &cy);
        // Clamp to [0,1] for rendering
        if (cx < 0.0) cx = 0.0; if (cx > 1.0) cx = 1.0;
        if (cy < 0.0) cy = 0.0; if (cy > 1.0) cy = 1.0;
        graph->corners[t].pos = (Vec2){(f32)cx, (f32)cy};
    }

    // --- Build Centers ---
    graph->num_centers = num_points;
    graph->centers = calloc(num_points, sizeof(Center));
    for (u32 i = 0; i < num_points; i++) {
        graph->centers[i].pos = (Vec2){coords_f32[2*i], coords_f32[2*i+1]};
        graph->centers[i].border = on_hull[i];
    }

    // --- Build point_to_edge: for each site, one incoming half-edge ---
    // We need this to walk around each site's Voronoi cell.
    u32 *point_to_edge = malloc(sizeof(u32) * num_points);
    memset(point_to_edge, 0xFF, sizeof(u32) * num_points);
    for (u32 e = 0; e < num_halfedges; e++) {
        u32 p = (u32)del.triangles[e];
        // prefer non-hull edges, but any will do
        if (point_to_edge[p] == UINT32_MAX || del.halfedges[e] == DELAUNATOR_INVALID_INDEX)
            point_to_edge[p] = e;
    }

    // --- Count edges (deduplicate: only process e where e < halfedge[e] or halfedge is sentinel) ---
    u32 edge_count = 0;
    for (u32 e = 0; e < num_halfedges; e++) {
        d_size opp = del.halfedges[e];
        if (opp == DELAUNATOR_INVALID_INDEX || e < (u32)opp)
            edge_count++;
    }

    graph->num_edges = edge_count;
    graph->edges = calloc(edge_count, sizeof(MapEdge));

    // Map from half-edge index to MapEdge index
    u32 *he_to_edge = malloc(sizeof(u32) * num_halfedges);
    memset(he_to_edge, 0xFF, sizeof(u32) * num_halfedges);

    {
        u32 ei = 0;
        for (u32 e = 0; e < num_halfedges; e++) {
            d_size opp = del.halfedges[e];
            if (opp == DELAUNATOR_INVALID_INDEX || e < (u32)opp) {
                MapEdge *me = &graph->edges[ei];
                // Delaunay edge connects two sites
                u32 tri_e = e / 3;           // triangle containing half-edge e
                u32 next_e = (e % 3 == 2) ? e - 2 : e + 1;
                me->d0 = (u32)del.triangles[e];
                me->d1 = (u32)del.triangles[next_e];
                // Voronoi edge connects circumcenters of adjacent triangles
                me->v0 = tri_e;
                me->v1 = (opp != DELAUNATOR_INVALID_INDEX) ? (u32)(opp / 3) : UINT32_MAX;
                me->border = (opp == DELAUNATOR_INVALID_INDEX);

                he_to_edge[e] = ei;
                if (opp != DELAUNATOR_INVALID_INDEX)
                    he_to_edge[(u32)opp] = ei;
                ei++;
            }
        }
    }

    // --- Two-pass adjacency: count then fill ---
    // For Centers: neighbors, borders (edges), corners
    // For Corners: adjacent (corners), touches (centers), protrudes (edges)

    // Pass 1: count
    // Each MapEdge contributes to both d0 and d1 Centers (neighbors, borders)
    for (u32 ei = 0; ei < edge_count; ei++) {
        MapEdge *me = &graph->edges[ei];
        graph->centers[me->d0].num_neighbors++;
        graph->centers[me->d0].num_borders++;
        graph->centers[me->d1].num_neighbors++;
        graph->centers[me->d1].num_borders++;
    }

    // Each triangle contributes its 3 vertices as site corners
    for (u32 t = 0; t < num_triangles; t++) {
        for (int j = 0; j < 3; j++) {
            u32 site = (u32)del.triangles[3*t+j];
            graph->centers[site].num_corners++;
        }
    }

    // For corners: each MapEdge contributes to v0 and v1
    for (u32 ei = 0; ei < edge_count; ei++) {
        MapEdge *me = &graph->edges[ei];
        if (me->v0 != UINT32_MAX) {
            graph->corners[me->v0].num_adjacent++;
            graph->corners[me->v0].num_protrudes++;
        }
        if (me->v1 != UINT32_MAX) {
            graph->corners[me->v1].num_adjacent++;
            graph->corners[me->v1].num_protrudes++;
        }
    }

    // Each triangle's corner touches its 3 sites
    for (u32 t = 0; t < num_triangles; t++) {
        graph->corners[t].num_touches = 3;
    }

    // Pass 1.5: compute total adjacency buffer size and allocate
    u32 total = 0;
    for (u32 i = 0; i < num_points; i++)
        total += graph->centers[i].num_neighbors + graph->centers[i].num_borders + graph->centers[i].num_corners;
    for (u32 i = 0; i < num_triangles; i++)
        total += graph->corners[i].num_adjacent + graph->corners[i].num_touches + graph->corners[i].num_protrudes;

    graph->adj_buf = malloc(sizeof(u32) * total);
    if (!graph->adj_buf) {
        free(coords); free(out_buf); free(scratch); free(on_hull);
        free(point_to_edge); free(he_to_edge);
        return false;
    }

    // Assign pointers from adj_buf and reset counts
    u32 *ptr = graph->adj_buf;
    for (u32 i = 0; i < num_points; i++) {
        Center *c = &graph->centers[i];
        c->neighbors = ptr; ptr += c->num_neighbors; c->num_neighbors = 0;
        c->borders   = ptr; ptr += c->num_borders;   c->num_borders = 0;
        c->corners   = ptr; ptr += c->num_corners;   c->num_corners = 0;
    }
    for (u32 i = 0; i < num_triangles; i++) {
        Corner *c = &graph->corners[i];
        c->adjacent  = ptr; ptr += c->num_adjacent;  c->num_adjacent = 0;
        c->touches   = ptr; ptr += c->num_touches;   c->num_touches = 0;
        c->protrudes = ptr; ptr += c->num_protrudes;  c->num_protrudes = 0;
    }

    // Pass 2: fill
    for (u32 ei = 0; ei < edge_count; ei++) {
        MapEdge *me = &graph->edges[ei];
        Center *c0 = &graph->centers[me->d0];
        Center *c1 = &graph->centers[me->d1];
        c0->neighbors[c0->num_neighbors++] = me->d1;
        c0->borders[c0->num_borders++]     = ei;
        c1->neighbors[c1->num_neighbors++] = me->d0;
        c1->borders[c1->num_borders++]     = ei;
    }

    for (u32 t = 0; t < num_triangles; t++) {
        for (int j = 0; j < 3; j++) {
            u32 site = (u32)del.triangles[3*t+j];
            graph->centers[site].corners[graph->centers[site].num_corners++] = t;
        }
    }

    for (u32 ei = 0; ei < edge_count; ei++) {
        MapEdge *me = &graph->edges[ei];
        if (me->v0 != UINT32_MAX) {
            Corner *c = &graph->corners[me->v0];
            c->adjacent[c->num_adjacent++] = (me->v1 != UINT32_MAX) ? me->v1 : me->v0;
            c->protrudes[c->num_protrudes++] = ei;
        }
        if (me->v1 != UINT32_MAX) {
            Corner *c = &graph->corners[me->v1];
            c->adjacent[c->num_adjacent++] = me->v0;
            c->protrudes[c->num_protrudes++] = ei;
        }
    }

    for (u32 t = 0; t < num_triangles; t++) {
        Corner *c = &graph->corners[t];
        c->touches[0] = (u32)del.triangles[3*t+0];
        c->touches[1] = (u32)del.triangles[3*t+1];
        c->touches[2] = (u32)del.triangles[3*t+2];
        c->num_touches = 3;
    }

    // Mark border corners (any corner touching a hull site)
    for (u32 t = 0; t < num_triangles; t++) {
        for (int j = 0; j < 3; j++) {
            u32 site = (u32)del.triangles[3*t+j];
            if (on_hull[site]) {
                graph->corners[t].border = true;
                break;
            }
        }
    }

    free(coords);
    free(out_buf);
    free(scratch);
    free(on_hull);
    free(point_to_edge);
    free(he_to_edge);
    return true;
}

void mg_free_mesh(MapGraph *graph) {
    free(graph->centers);
    free(graph->corners);
    free(graph->edges);
    free(graph->adj_buf);
    memset(graph, 0, sizeof(*graph));
}
