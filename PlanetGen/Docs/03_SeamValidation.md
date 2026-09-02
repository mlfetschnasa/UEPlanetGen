# Seam Validation — Cube-Edge Neighbor Table

`PlanetMath::GetCubeNeighbor()` (in `PlanetMath.cpp`) is a table mapping each cube face's 4
edges to the neighboring face and the axis mapping needed to translate a coordinate across
that boundary. It is used ONLY by `FPlanetQuadtree`'s cross-face depth balancing pass
(`bEnableCrossFaceLODBalancing`), which is **disabled by default** in `APlanetManager`.

## Update: the original table was found to be comprehensively wrong, and has been rewritten

A prior version of this doc described the table as "hand-derived, not yet runtime-verified"
and suggested any error would be an isolated "one flipped axis" mistake. Brute-force
point-matching every `(Face, Edge)` pair's mapped neighbor UV against `FaceUVToCubePoint()`'s
own output (at several sampled points along each of the 24 edges) found that **all 24**
entries in the old table produced mismatched points, not just one or two. The root cause
was structural, not a typo: `FCubeNeighbor` used a single `bFlipU`/`bFlipV` bit to encode two
independent choices at once (which sign the neighbor's fixed edge-axis uses, AND which
direction the neighbor's free axis runs), and for several face pairs those two needs
conflict — no value of that bit could satisfy both, so a fully correct 24-entry table was
not achievable with that encoding regardless of how carefully it was hand-derived.

`FCubeNeighbor` now has three independent bits (`bSwapUV`, `bFixedAxisPositive`,
`bReverseVarying` — see the comment on the struct in `PlanetMath.h`), and the table was
regenerated and reverified to reproduce identical 3D points to `FaceUVToCubePoint()` at
densely-sampled points along all 24 edges. This proves the *coordinate transform* is now
correct; it does not by itself prove the split-cascade behavior is bug-free end to end in a
running level, so the in-PIE validation procedure below is still worth doing before flipping
`bEnableCrossFaceLODBalancing` on by default.

## What happens if you enable it

With the corrected table, a `FindNeighborLeafDepth` lookup now queries the actual
geometrically-adjacent node, so the balancing pass should catch real cross-face T-junctions
as intended. **It does not produce visible mesh corruption either way** — `BuildMeshData`
itself doesn't use this table, only the LOD split/merge decision does — so even an
undiscovered remaining bug here would only mean relying on edge skirts alone at that
specific edge, same as leaving balancing off.

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
6. If any edge still shows a rotated/mismatched seam with balancing enabled, don't hand-tweak
   that entry's bits by trial and error — re-run the brute-force point-matching check (sample
   `FaceUVToCubePoint()` along the edge, feed each sample through `ResolveNeighborUV` and the
   neighbor face's `FaceUVToCubePoint()`, compare) for that specific `Face`/`Edge` pair. That
   approach is what caught the original table being wrong on all 24 entries; eyeballing it
   is exactly how the original table went wrong in the first place.

## Bottom line

Ship with edge skirts only (default state) unless you've specifically observed T-junction
artifacts that skirts aren't adequately hiding (most visible as a thin but persistent dark
line at a LOD boundary, especially noticeable on water-adjacent or high-contrast biome
edges). The neighbor table itself is now verified correct against the coordinate math: what
remains untested is the in-engine split-cascade behavior, which is what the procedure above
actually validates.
