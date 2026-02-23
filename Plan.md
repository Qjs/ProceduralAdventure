Goals and constraints

Decide target output: a raster image (SDL texture) plus optional debug overlays (cell edges, rivers, elevation, moisture).

Choose map size (e.g., 1024×1024 raster) and polygon resolution (e.g., 2k–20k sites).

Decide determinism: seedable RNG, reproducible generation.

Decide performance: generate once on “Regenerate”; render every frame from cached texture.

Data model (graph you will generate)

Define core elements:

Site/Center (one per Voronoi cell): position, water/land, elevation, moisture, biome, neighbor list, border/coast flags.

Corner (Voronoi vertices / Delaunay circumcenters): position, elevation, moisture, downslope pointer, river flux.

Edge: endpoints (corners), adjacent centers (left/right), river amount, coast flag.

Build adjacency lists:

Center.neighbors (centers sharing an edge)

Center.borders (edges around the cell)

Corner.adjacent (corners connected by edges)

Corner.touches (centers that meet at corner)

Sampling points (sites)

Generate N points in [0,1]×[0,1] using Poisson disk sampling (Bridson) or jittered grid.

Store sites in an array; keep the boundary box to clip Voronoi cells.

Build the mesh (Delaunay + Voronoi)

Compute Delaunay triangulation from the sites (use a library or robust implementation).

Construct Voronoi diagram as the dual:

Voronoi corners are triangle circumcenters.

Voronoi edges connect circumcenters of adjacent triangles.

Voronoi cells correspond to each site, formed by the polygon of circumcenters around that site.

Clip the Voronoi edges/cells to the bounding box.

Populate the graph connectivity

For each Delaunay triangle:

Create/get its circumcenter as a Corner.

For each Delaunay edge between two sites:

Identify the two triangles that share it; their circumcenters define a Voronoi Edge segment.

Create Edge: corners=(c0,c1), centers=(s0,s1).

Add to adjacency:

s0.neighbors add s1; s1.neighbors add s0

s0.borders add edge; s1.borders add edge

c0.adjacent add c1; c1.adjacent add c0

c0.touches add s0 and s1; c1.touches add s0 and s1

Mark border elements:

Any center with polygon clipped by the bounding box is “border”.

Any edge that lies on bounding box is “border edge”.

Corners on the boundary are “border corners”.

Define an island shape function (land mask)

Implement a deterministic scalar function f(p) returning “landness”:

radial term: r = distance(p, center)/maxRadius

noise term: n = fbm(p * frequency) (Perlin/Simplex fractal)

optional warping: p’ = p + warpStrength *noise2(p* warpFreq)

landness = n - k * r^power

For each Center:

compute landness at its position

set water = (landness < threshold) or forced water if border

Coast detection:

Center.coast = land && any neighbor is water

Elevation assignment (coast-distance method)

Initialize:

water centers elevation = 0

coastal land centers: elevation = small value

Compute “distance from coast”:

BFS from all coastal land centers outward over land neighbors

dist[center] = steps or weighted distance

Convert to elevation:

elevation = normalize(dist) then apply curve (e.g., elevation = (distNorm)^gamma)

add small noise to break ties

Corner elevation:

corner elevation = average of touching centers’ elevation, with water corners near 0

Optional mountain shaping:

choose a few inland peaks; add gaussian bumps; re-normalize

keep coast elevation low to preserve island look

Determine downhill direction for corners

For each Corner:

evaluate neighboring corners via adjacent list

set corner.downslope = neighbor with minimum elevation (steepest descent)

Mark “ocean corners”:

corner is ocean if any touching center is water and corner is near boundary or connected to water region.

Water flow and rivers (flux accumulation)

Select spring corners:

pick corners above an elevation threshold and not near coast

sample by probability proportional to elevation (or use a fixed count)

For each spring:

walk: c = spring; while not ocean and not stuck:

next = c.downslope

increment river flux along edge (c,next) (store on Edge)

c = next

stop when reaching ocean/water

Flux accumulation improvement:

compute flow order by sorting corners by elevation descending

initialize corner.flow = rainfall (constant or based on moisture seeds)

for corners descending: add corner.flow to downslope.flow

set river amount on edge proportional to flow crossing that edge

Define river edges:

Edge.river = flow > riverThreshold

widen based on flow (for rendering)

Lakes (optional but common)

If downslope walk hits a local minimum not in ocean:

treat as lake basin

flood-fill basin until it spills to a lower outlet

mark enclosed water region as lake; update elevations (or treat water level)

If skipping lakes initially:

accept “stuck” springs as no-river or force a spill via neighbor with slightly lower effective elevation.

Moisture model

Initialize moisture sources:

all water centers high moisture

centers adjacent to river edges get boosted moisture

Propagate moisture inland:

BFS/Dijkstra on centers:

moisture[neighbor] = max(moisture[neighbor], moisture[current] * decay)

use separate decay for uphill vs downhill if desired

Optionally add rainfall shadow:

pick prevailing wind direction

reduce moisture on leeward side of high elevations

Biome classification

Choose biome rules using elevation and moisture:

water: ocean/lake

coast: beach if low elevation

high elevation: snow/tundra

medium elevation:

low moisture: desert/steppe

mid: grassland

high: forest/rainforest

Store biome per center.

Rasterization for SDL rendering

Decide raster resolution (e.g., W×H).

For each pixel:

map pixel to world position p in [0,1]²

find containing Voronoi cell:

simplest: nearest site search (kd-tree or uniform grid acceleration)

or precompute a Voronoi “cell id” buffer by nearest-site

color = palette[center.biome]

optional shading:

darken by elevation slope (approx from elevation gradients)

add coastline outline by detecting neighbor water boundaries

Rivers overlay:

draw river edges into raster with line rasterization in pixel space

widen based on Edge.river amount

Upload pixel buffer to an SDL streaming texture once per generation.

Debug overlays and toggles

Draw polygon edges (thin lines) for mesh visualization.

Draw coast edges, river edges, springs.

Draw elevation grayscale and moisture grayscale modes.

Provide hotkeys to toggle overlays without regenerating.

Controls, determinism, and parameters

Expose parameters:

N sites, poisson radius

noise frequency, octaves, warp strength

island threshold, radial falloff k/power

elevation gamma, peak count/strength

river spring count, river threshold, moisture decay

Seed handling:

one seed generates all noise and random choices

store seed with map for replay.

Testing and validation checks

Connectivity sanity:

each edge has 0–2 adjacent centers; corners valid

each center neighbor relation is symmetric

Geography sanity:

border forced water prevents “land touching edge” artifacts

ensure at least one large landmass; if not, adjust threshold and regenerate

ensure elevation increases inland on average

rivers should terminate in ocean; log stuck cases

Packaging into code modules

mesh:

poisson sampling, triangulation wrapper, voronoi build, adjacency build

mapgen:

island mask, elevation BFS, downslope, rivers, moisture, biomes

raster:

nearest-cell acceleration, pixel fill, overlays

app integration:

“Regenerate(seed)” path off main render loop

SDL texture update and render
