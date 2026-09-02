# Seam Validation — Cube-Edge Neighbor Table

`PlanetMath::GetCubeNeighbor()` (in `PlanetMath.cpp`) is a hand-derived table mapping each
cube face's 4 edges to the neighboring face and the axis swap/flip needed to translate a
coordinate across that boundary. It is used ONLY by `FPlanetQuadtree`'s cross-face depth
balancing pass (`bEnableCrossFaceLODBalancing`), which is **disabled by default** in
`APlanetManager` for exactly this reason.

## Why this needs validation before enabling

Cube-face adjacency tables are notoriously easy to get subtly wrong — one flipped axis out
of 24 possible face/edge pairings produces a seam that looks "almost right but rotated,"
which is hard to spot from code review alone. The table was derived analytically from
`FaceUVToCubePoint()`'s axis conventions, but has not been runtime-verified against actual
rendered output.

## What happens if you enable it without validating

Worst case: a wrong table entry causes the balancer to compare a node against the *wrong*
neighbor node, producing either unnecessary over-splitting (harmless, just wastes some pool
budget) or a balancing pass that doesn't actually catch a real T-junction at that specific
edge (meaning you rely on edge skirts alone there, which is the same as leaving balancing
off). **It does not produce visible mesh corruption** — `BuildMeshData` itself doesn't use
this table, only the LOD decision does. So enabling it with a bug is low-risk, just
potentially ineffective at the specific mismatched edges.

## Recommended validation procedure

1. Keep `bEnableCrossFaceLODBalancing = false`. Edge skirts (always on, in
   `APlanetChunk::BuildMeshData`) already hide most LOD-mismatch cracks — ship with this
   first.
2. To validate the table before enabling: temporarily set `M_PlanetTerrain`'s Base Color
   to output a distinct flat color per cube face (e.g. wire `ECubeFace`-equivalent info — in
   practice, easiest to do this by temporarily assigning 6 different colored materials to
   chunks based on `Coord.Face`, which requires a small temporary debug branch in
   `RequestLoad()` selecting a material per face).
3. In PIE, set `Max Quadtree Depth` low (e.g. 2-3) so face boundaries are easy to find and
   approach.
4. Fly to each of the 12 cube edges and 8 corners at a slow speed. With cross-face
   balancing OFF, you're checking that `SnapToCubeEdge` + edge skirts alone produce no
   visible gap (this validates the *projection* math, independent of the neighbor table).
5. Enable `bEnableCrossFaceLODBalancing = true`. Force an asymmetric LOD situation — stand
   very close to one face near an edge so it splits deep, while the camera is far enough
   from the neighboring face that it stays shallow — and confirm the balancer forces the
   neighbor to split too (you can verify this indirectly by watching chunk pool usage near
   that edge, or by adding a temporary on-screen debug print of
   `FPlanetQuadtree::FindNeighborLeafDepth` results).
6. If any edge shows a rotated/mismatched seam with balancing enabled, that specific
   `Face`/`Edge` entry in `GetCubeNeighbor()` likely has an incorrect `bSwapUV`/`bFlipU`/
   `bFlipV` combination — cross-reference against `FaceUVToCubePoint()`'s axis definitions
   for those two specific faces and correct just that entry.

## Bottom line

Ship with edge skirts only (default state) unless you've specifically observed T-junction
artifacts that skirts aren't adequately hiding (most visible as a thin but persistent dark
line at a LOD boundary, especially noticeable on water-adjacent or high-contrast biome
edges). Only invest in validating/fixing the neighbor table if that becomes a real visual
problem in your specific scenes.
