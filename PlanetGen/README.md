# PlanetGen — Procedural Planet Generation for UE 5.7 (C++)

Runtime procedural spherical planets: cube-sphere terrain with LOD streaming, layered
noise (mountains/canyons/plateaus), biome-based material blending, ocean, atmosphere,
foliage, hand-placed static buildings with terrain flattening, and a custom navigation
system for AI. Two package variants ship from the same source:

- **Full package** (`PlanetGen/`, this one) — everything, including ocean, glow shell,
  atmosphere.
- **Minimal package** (`PlanetGen_Minimal/`) — terrain + streaming + noise layers +
  foliage + POIs + navigation only. No ocean/glow/atmosphere.

Both are kept in sync file-for-file wherever they share functionality — if you fix a
bug in one, check whether the same fix applies to the other.

## About the `Docs/` folder in this package

`Docs/01_ProjectSetup.md`, `02_BlueprintWiring.md`, `03_SeamValidation.md`, and
`04_MaterialSetup.md` were written earlier in this project's development and are
**partially stale** — several systems below were built or substantially reworked
after those docs were last touched, and one specific area (the terrain material's
Normal/slope wiring) changed multiple times.

- **`01_ProjectSetup.md`** — module/folder setup. Should still be accurate; nothing
  about basic module registration changed later.
- **`03_SeamValidation.md`** — cross-face neighbor table caveats. Should still be
  accurate; `bEnableCrossFaceLODBalancing` is still off-by-default and still
  unvalidated, unchanged from when this doc was written.
- **`02_BlueprintWiring.md`** — likely still correct for the *original* chunk
  streaming setup, but **predates** the Preset/Override system, foliage, POIs, and
  navigation. Treat it as covering only the base terrain/ocean/atmosphere actors; see
  this README for everything added since.
- **`04_MaterialSetup.md`** — **known stale on the Normal pin and slope detection.**
  The material's Normal/slope wiring was reworked several times over the course of
  this project (world-space vs. tangent-space normals, `VertexColor.A` slope →
  analytical-normal `DDX/DDY` slope, chunk-boundary seam fixes). **Trust the Material
  Setup section of this README over that doc's Normal/slope sections specifically.**
  The rest of that doc (triplanar sampling structure, water material, glow shell base
  setup) is more likely still accurate, but verify against this README's callouts
  before relying on any of it blind.

If you're picking this project up fresh, read this README first, then use the `Docs/`
files only for the setup mechanics they still get right — the architecture and current
correct behavior described below supersedes them wherever they conflict.

---

## Quick Start

1. Extract into your project's `Source/PlanetGen/` (or add as a plugin module).
2. **Delete `Intermediate/`, `Binaries/`, `Saved/`, `.vs/`, regenerate project files**,
   then build. (Any header change in this project's history has required a full clean
   wipe — UHT reflection data goes stale otherwise. Just always do it.)
3. Place a `BP_PlanetManager` (Blueprint subclass of `APlanetManager`) in a level.
4. Assign `ChunkClass` (subclass of `APlanetChunk`), `ChunkMaterial`
   (`M_PlanetTerrain`, see Material Setup below), and either a `Preset`
   (`UPlanetPreset` data asset) or fill in the `Override_*` properties directly.
5. Click **Regenerate Planet** in the Details panel. Terrain (and ocean, in the full
   package) should appear in the editor viewport immediately.
6. Press Play — runtime streaming takes over as you approach the surface.

For a player character that walks on the sphere, see **Character Setup** below.

---

## Architecture Overview

### Cube-sphere parameterization (`PlanetMath.h/.cpp`)
The planet is 6 cube faces (`EPlanetCubeFace`), each parameterized by UV in `[-1,1]`.
`FaceUVToCubePoint` -> `CubeToSphere` (normalize + scale by radius) is the single
function that turns "flat cube grid" into "sphere." `SphereDirectionToFaceUV` is the
algebraic inverse (world direction -> face + UV), used by the nav grid's nearest-node
lookup. `SnapToCubeEdge` prevents floating-point drift cracks at face boundaries — call
it on every U/V before projecting, not just at chunk edges.

### Chunk streaming (`PlanetChunk.h/.cpp`, `PlanetChunkPool.h/.cpp`, `PlanetQuadtree.h/.cpp`, `PlanetManager.h/.cpp`)
- Per-face quadtree drives LOD: chunks split when `Dist < NodeSize * SplitFactor`,
  merge with 60% hysteresis + a 10-frame `FramesWantingMerge` counter to prevent
  thrashing at high speed. **Do not reset the merge counter on temporary distance
  fluctuation** — only on an actual split — or merges never fire.
- Chunks are pooled actors (`FPlanetChunkPool`), pre-allocated, recycled via
  `Activate()`/`Deactivate()`. Pool and spawned actors are `RF_Transient` — nothing
  about streamed content is ever saved to the level.
- Mesh build (`BuildMeshData`) is **static, pure, thread-safe** — same
  `(Coord, PlanetCenter, PlanetRadius, ...)` in, same mesh out, always. This purity is
  load-bearing: chunk-boundary seamlessness, pool-recycle safety, and foliage/POI
  determinism all depend on it. Runs off-thread via `UE::Tasks::Launch`; the
  game-thread callback **must** verify `WeakChunk->GetCoord() == Coord` before
  applying — pooled chunks get recycled mid-build, and without this check a stale
  low-LOD build can stamp itself onto a chunk now assigned to a different, higher-LOD
  coord (this was a real, hard-to-diagnose bug this session).
- Vertex positions are **absolute world-space**; chunk actor transforms are always
  `FVector::ZeroVector`. Anything that spawns at `GetActorLocation()` instead of
  `ZeroVector` will double-offset for planets not at world origin (bit us with the
  ocean shell — fixed, but watch for this pattern in new systems).
- `MaxLoadsPerTick = 16` throttles chunk builds dispatched per frame to avoid hitches
  when crossing LOD boundaries at speed.
- Collision uses an explicit `SetCollisionProfileName(TEXT("BlockAll"))` (never rely
  on the engine default) and `bUseComplexAsSimpleCollision`. **Important limitation**:
  neither Chaos nor PhysX support CCD against complex/trimesh collision, and dynamic
  bodies resting/pushing against concave complex geometry have unreliable
  depenetration. A physics-simulated actor (ship, etc.) *will* tunnel through terrain
  under sustained thrust even with CCD enabled on it. Fix: `UPlanetTerrainSafetyComponent`
  (below) — add it to any physics-simulated actor that touches terrain.

### Noise & terrain shape (`NoiseGenerator.h/.cpp`)
`FNoiseGenerator::SampleHeight(UnitSphereDir)` composes, in order:
1. **Continent base** — low-freq FBM, clamped `+-HeightScale`.
2. **Mountains** (`FMountainLayerSettings`) — masked ridged multifractal, additive.
   Mask = low-freq noise + domain warp + `SmoothStep` threshold from `Coverage`.
3. **Canyons** (`FCanyonLayerSettings`) — same ridged-network technique as mountains,
   subtractive, `WallSharpness` exponent narrows/steepens the carve.
4. **Plateaus** (`FPlateauLayerSettings`) — broad noise quantized into
   `TerraceCount` flat steps, masked, with `CanyonAffinity` biasing the mask toward
   wherever the canyon mask is already active (mesas cluster near canyons, matching
   real erosion geology and keeping "desert" features visually grouped).

Every layer: pure function, decorrelated lattice offset (its own irrational `FVector`
added before sampling — prevents features spookily aligning with each other or with
the base), early-outs when its mask is ~0 so cost only lands where the feature is
visible. **Never make sampling depend on LOD** — a coarse and fine chunk sampling the
same boundary point must get bit-identical results, or seams reappear.

`ENoiseDebugView` (Off/ContinentHeight/MountainMask/CanyonMask/PlateauMask/FinalHeight)
paints the selected signal as grayscale into vertex color instead of biome weights —
set on `BP_PlanetManager` under **Planet | Debug**, wire `VertexColor -> BaseColor` in
the material to see it, Regenerate. This has been the primary tuning workflow all
session — use it before touching numbers blind.

**Amplitude budget**: `BuildMeshData` clamps final height to `+-MaxHeight`
symmetrically. `RegeneratePlanet` logs a warning if `HeightScale + Mountains.Height`
(positive direction) or `HeightScale + Canyons.Depth` (negative direction) exceeds
`MaxHeight` — respect that warning or peaks/canyons silently flat-top.

### Material (`M_PlanetTerrain`, not included in this package — built in-editor)
- **Tangent Space Normal must be UNCHECKED** on the material. Everything wired into
  the Normal pin is world-space (the analytical `normalize(WorldPos - PlanetCenter)`
  blended with `VertexNormalWS`) — leaving tangent-space checked silently breaks all
  local lighting (point lights, etc.) even though the sun looks fine (a single global
  direction "works" against systematically wrong normals; local lights don't).
- Normal pin: `lerp(VertexNormalWS, AnalyticalNormal, saturate(CameraDist/BlendDistance))`
  — triangle-detail up close, seamless analytical normal at distance (chunk-boundary
  lighting seams are otherwise visible between separately-rendered chunk actors, since
  GPU normal interpolation doesn't cross draw calls).
- Slope detection (`MF_SlopeRockBlend`) uses `DDX/DDY` of the **analytical** normal
  (not `VertexColor.A`, which has per-chunk-boundary discontinuities that caused
  visible material seams before this fix) — `SlopeMagnitude = saturate((|DDX|+|DDY|) / SlopeScale)`.
- Triplanar sampling (`MF_TriplanarSample`) should use the analytical normal for blend
  weights, not `VertexNormalWS`, for the same cross-boundary-consistency reason.
- Vertex color channels: `R` = water weight, `G` = grass, `B` = rock, `A` = slope
  (0=flat, 1=vertical) — **not** snow; snow is derived as `saturate(1-R-G-B)`.
  Both foliage gating and POI-adjacent logic read these same channels — if you ever
  redesign the channel encoding, foliage/POI code needs updating too.
- `PlanetCenterWorldPosition` (Vector3 MID parameter, set from C++ per-manager) drives
  the analytical normal — every planet needs its own MID for this to be correct with
  multiple planets in a scene (already handled: each `APlanetManager` owns its own MID).

### Ocean / Glow shell / Atmosphere (full package only)
- Both shells spawn at `FVector::ZeroVector` (world-space vertices, same convention as
  chunks) and are `RF_Transient`. The **editor preview** ocean is additionally
  `bIsEditorOnlyActor = true` — `RF_Transient` alone does **not** exclude an actor from
  PIE world duplication (only from saving), a lesson learned the hard way (invisible
  duplicate ocean blocking the real runtime spawn). Any future "editor preview, not
  meant for PIE" actor needs `bIsEditorOnlyActor`, not just `RF_Transient`.
- `BeginPlay` unconditionally clears + respawns transient-spawned content (ocean, glow,
  POI buildings) rather than checking "do I already have one" — pointer-validity checks
  are unreliable across the editor/PIE boundary since inherited cross-world references
  read as valid but point at the wrong world's object. This pattern should be followed
  for anything new that spawns transient content at `BeginPlay`.
- `M_PlanetGlowShell`: **Additive + Unlit** (not Translucent — UE 5.7 changed
  Translucent blend-mode behavior; Additive is also the more physically appropriate
  choice for a rim-glow effect anyway). Modulated by `SkyAtmosphereLightDirection` dot
  product so it fades on the night side rather than glowing uniformly.
- `PlanetAtmosphereController`: `TransformMode = PlanetCenterAtComponentTransform` is
  **required** — the component's default mode pins the atmosphere to world origin
  regardless of actor position. `PlanetSystemManager` auto-discovers all
  `APlanetManager` + the atmosphere controller in the level if its arrays are left
  empty, and owns the Sky Atmosphere visibility/fade every tick — don't manually
  toggle it elsewhere or you'll fight the controller.

### Foliage (`FoliageSettings.h`, `BuildFoliageData` in `PlanetChunk.cpp`)
Per-chunk HISM (Hierarchical Instanced Static Mesh) components, populated/cleared by
distance exactly like collision toggling. Candidate points scattered per-triangle
(pure, deterministic — hashed from chunk coord + triangle index + instance index, same
philosophy as terrain height), gated by the terrain's own biome weight + slope (no
separate noise sampling). Only ever generated for chunks at the quadtree's **finest**
LOD depth. **Known fixed bug**: an early version used a shared running accumulator for
fractional instance counts, which crossed its threshold at regular intervals matching
the mesh's row-major triangulation order — visible as rows. Fixed by making the
fractional "extra instance" decision an independent per-triangle hash instead of a
shared accumulator. If any future scatter system reappears with visible
rows/grid-alignment, check for this exact class of bug first.

### Static POIs / buildings — **Phase 1 only** (`PlanetPOI.h`)
Hand-placed only (no procedural scatter yet — deferred). Workflow: place any actor with
a `UPlanetPOIMarkerComponent` attached roughly where you want a building, tune
`FlattenRadius`/`FlattenBlendDistance`/`BuildingClass` (exposed `BlueprintReadWrite` —
settable from Construction Script or BeginPlay, not just the Details panel), click
**Bake POIs From Markers** on the manager, then **Regenerate Planet**.

- Terrain height is force-flattened toward each POI's `TargetHeight` within
  `FlattenRadius`, smooth blend through `FlattenBlendDistance`, applied in
  `BuildMeshData` **after** raw noise height, **before** biome coloring — a coarse
  per-chunk POI filter runs once before the vertex loop so the common zero-POI case is
  free.
- Foliage is excluded within the same flatten+blend radius (checked in
  `BuildFoliageData`).
- Spawned building actors are `RF_Transient`, always cleared+respawned unconditionally
  in `BeginPlay`/`RegeneratePlanet` (see the cross-world-pointer note above — this bug
  hit POI buildings too, same fix pattern).
- **Deferred to a later phase**: procedural/algorithmic scatter, distance-based
  building streaming (currently always-spawned — fine for modest counts, not yet
  tested at scale), and POI-flatten-mask awareness in the nav grid (below) — currently
  the nav grid samples raw unflattened noise, so it may think a building's pad is
  still steep natural terrain.

### Navigation — **Phase 1 only** (`PlanetNavGrid.h/.cpp`)
Custom grid, **not** Unreal's built-in Recast navmesh — Recast assumes a single fixed
global up-axis when classifying walkable surfaces, which breaks catastrophically on a
sphere (only a small patch near wherever it thinks "up" points would classify
correctly). `FPlanetNavGrid` samples `SampleHeight` directly (independent of streamed
chunks — a path can be found even where no chunk mesh is currently loaded), builds
walkability from local per-node slope (finite-difference from neighbor world
positions, compared against **that node's own** radial direction, never a global
axis), and runs standard A* with real 3D Euclidean distance as both edge cost and
heuristic (curvature only mattered for building the graph, not searching it).

**Phase 1 limitation, by design**: single cube face only. `FindPath` on a Start/End
pair spanning two faces logs a warning and returns `false`. Cross-face stitching is
deferred — same open-problem category as this project's already-documented cross-face
LOD balancing (`Docs/03_SeamValidation.md`).

Exposed on `APlanetManager`: `NavGridResolution`, `MaxWalkableSlopeDegrees`,
`BakeNavigation()` (also runs automatically at the end of `RegeneratePlanet`), and
`FindPath(Start, End, OutPath) -> bool` (`BlueprintCallable`).

### `UPlanetTerrainSafetyComponent`
Drop-in `UActorComponent` for any physics-simulated actor that touches terrain (ships,
debris). Independently sphere-sweeps from last-known-safe to current position every
tick (`TG_PostPhysics`), **after** physics has already moved the body — corrects
position/velocity regardless of whether Chaos's own depenetration succeeded that frame.
Exists specifically because of the complex-collision CCD limitation noted above; not
optional for anything physics-simulated that flies/drives into terrain at speed.

---

## Character Setup (player or AI, sphere-surface movement)

`UCharacterMovementComponent::SetGravityDirection(FVector)` is real,
`BlueprintCallable`, confirmed present since UE 5.4 (verified against Epic's 5.7 docs
directly). Every tick: `GravityDir = Normalize(PlanetCenter - GetActorLocation())`,
call `SetGravityDirection(GravityDir)`. Handles floor detection/falling/walking
correctly relative to the custom direction.

**Camera/control rotation does NOT automatically follow** — this needs manual handling
(build a local `Forward`/`Right`/`Up` basis each tick via `Vector Plane Project` +
`Cross Product` + `Make Rotation From Axes`, apply look input relative to that basis
instead of raw world yaw/pitch). This part needs in-editor testing/tuning — treat it
as the part of character setup requiring iteration, not a solved recipe. Epic's own
official tutorial covers the same combination: "Custom Gravity in UE 5.4" by Ari
Arnbjornsson, `dev.epicgames.com/community/learning/tutorials/w6l7/unreal-engine-custom-gravity-in-ue-5-4`.

No C++ changes are needed for basic character setup — `GetActorLocation()` on the
manager (already Blueprint-accessible) is all a character needs for `PlanetCenter`.

For enemy AI: same gravity-redirection tick logic, steering toward
`PlanetManager->FindPath()` waypoints instead of raw input.

---

## Working Conventions (read before making changes)

- **Full clean wipe (`Intermediate`/`Binaries`/`Saved`/`.vs`, regenerate project files)
  after ANY header change.** This has been the rule all session, no exceptions found.
  `.cpp`-only changes are safe for incremental rebuilds.
- **Every noise/height function must be a pure function of its inputs.** No LOD
  dependence, no hidden state, no randomness that isn't seeded/deterministic from
  position. This is the single most load-bearing invariant in the project — nearly
  every seam/consistency bug this session traced back to something breaking this.
- **Chunk/ocean/glow/building actors are `RF_Transient`, spawned at `ZeroVector`,
  never saved.** New spawnable content should follow this pattern unless there's a
  specific reason not to (e.g., hand-placed POI markers, which ARE meant to be saved —
  they're designer-authored data, not derived/regenerable content).
- **`RF_Transient` does not stop PIE world duplication, only saving.** Anything
  spawned in the editor that must not exist in PIE needs `bIsEditorOnlyActor = true`.
  Anything that must exist fresh in PIE regardless of inherited state should
  unconditionally clear+respawn in `BeginPlay` rather than checking pointer validity.
- **The Preset/Override_* pattern**: `UPlanetPreset` (data asset) holds reusable named
  configs; `Override_*` properties on the manager are the fallback when `Preset` is
  null (`EditConditionHides` collapses them in the Details panel when a preset is
  assigned). `Get*()` accessors on `APlanetManager` are the only thing that should ever
  be read internally — never read `Preset->X` or `Override_X` directly from new code.
- **Primary tuning/debugging loop**: click **Regenerate Planet**, look at the result
  (using `ENoiseDebugView` for noise layers, raw `VertexColor -> BaseColor` wiring for
  material issues, Unlit viewmode to isolate lighting vs. geometry vs. material
  causes). This loop was used to diagnose essentially every bug this session — prefer
  it over reasoning from code alone when something looks wrong.

---

## Known Open Items / Deferred Work

- Cross-face LOD balancing (`bEnableCrossFaceLODBalancing`) — implemented but flagged
  as needing visual validation before trusting it; off by default.
- Cross-face navigation pathfinding — Phase 1 explicitly single-face only.
- POI procedural/algorithmic scatter — Phase 1 is hand-placed only.
- POI-flatten-mask awareness in the nav grid — nav grid currently samples raw noise.
- Distance-based streaming for POI buildings — currently always-spawned; untested at
  large building counts.
- `SeaLevel` unit consistency — worth a pass if ocean-adjacent bugs reappear (terrain
  and ocean shell historically used slightly different unit conventions for this
  value; may already be resolved, verify before assuming).
- LOD split/merge tuning (`LODSplitFactor`, `MergeFrameThreshold`) is scale-dependent
  — re-tune after any significant `PlanetRadius` change.

---

## Scale Reference (values that have tested well together)

At `PlanetRadius = 6,500,000` (65km, in cm — UE units):
- `HeightScale = 650,000` (continent base, ~10% of radius)
- `MaxHeight = 6,500,000` (generous headroom for combined layers — see Amplitude
  Budget above; doesn't mean terrain uses all of it)
- `Mountains.Height ~ 200,000`, `RangeFrequency 1.5-3`, `Coverage 0.3`
- `Canyons.Depth ~ 200,000`, `RegionFrequency 2-3`, `Coverage 0.2`
- `Plateaus.Height ~ 80,000`, `Coverage 0.1`, `CanyonAffinity 0.7-0.9`
- `MaxQuadtreeDepth = 10`, `LODSplitFactor = 2.5`, `PoolSize >= 2000`
- `CollisionRadius = 500,000`, `EditorLODDepth = 3`
