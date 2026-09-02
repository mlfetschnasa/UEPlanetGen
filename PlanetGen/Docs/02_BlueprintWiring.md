# Blueprint Wiring & Editor Setup

This is the step-by-step for going from "code compiles" to "a planet renders in PIE."
None of these steps require writing Blueprint graph logic — every class is fully driven
from C++ (BeginPlay/Tick), so "wiring" here means: subclassing in the editor, assigning
asset references, and placing actors in a level. Where a Blueprint graph genuinely helps
(e.g. a simple bootstrap), it's called out explicitly.

---

## Step 0 — Create your asset folder structure

In the Content Browser, create:

```
Content/Planet/
  Blueprints/      <- BP subclasses of the C++ actors
  Materials/        <- M_PlanetTerrain, M_PlanetWater, M_PlanetGlowShell, M_VolumetricCloud
  Textures/         <- biome textures (grass/rock/snow albedo+normal+roughness), water normals
```

---

## Step 1 — Create Blueprint subclasses

You generally don't place the C++ classes directly in a level — create a thin BP subclass
of each so you can tweak defaults per-project without touching code, and so the Content
Browser shows them with proper thumbnails.

| C++ Class | Create BP Subclass | Suggested Name |
|---|---|---|
| `APlanetChunk` | Yes | `BP_PlanetChunk` |
| `APlanetManager` | Yes | `BP_PlanetManager` |
| `APlanetOceanShell` | Yes | `BP_PlanetOceanShell` |
| `APlanetGlowShell` | Yes | `BP_PlanetGlowShell` |
| `APlanetAtmosphereController` | Yes | `BP_PlanetAtmosphereController` |
| `APlanetSystemManager` | Yes | `BP_PlanetSystemManager` |

To create each: Content Browser -> Add -> Blueprint Class -> "All Classes" search box ->
type the C++ class name (e.g. `PlanetChunk`) -> select it -> name it per the table above.
None of these need any graph edits for a basic working planet — leave Event Graph empty.

---

## Step 2 — Build the terrain material (`M_PlanetTerrain`)

Follow the node graph from the texture-biome design (triplanar sampling, vertex-color
blend, slope-based rock blending). Summary of required **Material Parameters** you must
create as actual parameter nodes (right-click any constant -> "Convert to Parameter") so
C++ can drive them via `UMaterialInstanceDynamic`:

| Parameter Name | Type | Used by |
|---|---|---|
| `GrassTiling` | Scalar | `MF_TriplanarSample` call for grass layer |
| `RockTiling` | Scalar | `MF_TriplanarSample` call for rock layer |
| `SnowTiling` | Scalar | `MF_TriplanarSample` call for snow layer |
| `BlendSharpness` | Scalar | triplanar blend power |
| `PlanetCenterWorldPosition` | Vector3 | radial "up" computation for slope blend |
| `SlopeStartDegrees` | Scalar | `MF_SlopeRockBlend` input |
| `SlopeEndDegrees` | Scalar | `MF_SlopeRockBlend` input |

These exact names matter — `PlanetManager.cpp`'s `BeginPlay()` calls
`SetScalarParameterValue(TEXT("GrassTiling"), ...)` etc. with these literal strings. If you
rename a parameter in the material, update the matching string in `PlanetManager.cpp`.

Build `MF_TriplanarSample` and `MF_SlopeRockBlend` as separate Material Functions per the
node graphs already specified, then wire them into `M_PlanetTerrain`'s main graph. Assign
your grass/rock/snow Albedo+Normal+Roughness textures as texture parameters inside the
triplanar function calls.

**Material Domain**: Surface. **Blend Mode**: Opaque. **Shading Model**: Default Lit.

Save as `Content/Planet/Materials/M_PlanetTerrain`.

---

## Step 3 — Build the water material (`M_PlanetWater`)

Per the ocean shader design: panning normal maps, scene-depth-based shoreline fade,
Fresnel. **Material Domain**: Surface. **Blend Mode**: Translucent. **Shading Model**:
Default Lit. **Two Sided**: off.

No parameters here are driven from C++ in the current wiring — `WaterMaterial` is assigned
directly (not via MID) in `PlanetManager`. If you want runtime-tunable water (e.g. per-planet
color), convert your constants to parameters and create a `UMaterialInstanceDynamic` the
same way `ChunkMID` is created, then extend `APlanetManager::BeginPlay()`.

Save as `Content/Planet/Materials/M_PlanetWater`.

---

## Step 4 — Build the glow shell material (`M_PlanetGlowShell`)

Per the Fresnel-rim design. **Material Domain**: Surface. **Blend Mode**: Translucent.
**Shading Model**: Unlit.

Required parameters:

| Parameter Name | Type |
|---|---|
| `GlowColor` | Vector3 |
| `GlowIntensity` | Scalar |
| `RimPower` | Scalar |
| `GlowAlpha` | Scalar |

`GlowColor` and `GlowAlpha` are set from C++ (`PlanetGlowShell.cpp` / `PlanetManager.cpp`)
using these exact names — keep them in sync if renamed.

Save as `Content/Planet/Materials/M_PlanetGlowShell`.

---

## Step 5 — Build the volumetric cloud material (`M_VolumetricCloud`)

Per the layered-noise cloud design (shape noise, height gradient, detail erosion, wind
drift). **Material Domain**: Volume.

Required parameters (all driven from `PlanetAtmosphereController.cpp`):

| Parameter Name | Type |
|---|---|
| `Coverage` | Scalar |
| `DensityScale` | Scalar |
| `DensityFadeAlpha` | Scalar |
| `LayerBottomWorldZ` | Scalar |
| `LayerHeightWorld` | Scalar |
| `CloudAlbedo` | Vector3 |

Also expose (not currently driven from C++, tune as material defaults or add more MID
parameter calls if you want them runtime-tunable): `ShapeScale`, `DetailScale`,
`ErosionStrength`, `WindDirection`, `WindSpeed`, `Phase G`.

Save as `Content/Planet/Materials/M_VolumetricCloud`.

---

## Step 6 — Configure `BP_PlanetChunk`

Open `BP_PlanetChunk`. In the Class Defaults panel, there's nothing required here — the
mesh component and collision settings are fully configured in `APlanetChunk`'s C++
constructor. Leave as-is unless you want to override something like cast-shadow behavior.

---

## Step 7 — Configure `BP_PlanetOceanShell` / `BP_PlanetGlowShell`

Same as Step 6 — no Class Defaults needed, these are fully driven by the `GenerateOceanMesh`
/ `GenerateGlowShell` calls made from `PlanetManager::BeginPlay()`. Nothing to assign here.

---

## Step 8 — Configure `BP_PlanetAtmosphereController`

No Class Defaults needed. This actor's three components (`SkyAtmosphere`,
`VolumetricCloud`, `HeightFog`) get fully reconfigured at runtime by
`ConfigureForPlanet()`. You only need ONE instance of this in your level (see Step 10).

---

## Step 9 — Configure `BP_PlanetManager` (the main per-planet setup)

This is the Blueprint with the most assignments. Open `BP_PlanetManager` -> Class Defaults,
and fill in every category:

### Planet
- `Chunk Class` -> `BP_PlanetChunk`
- `Chunk Material` -> `M_PlanetTerrain`
- `Planet Radius` -> e.g. `100000` for a 1km test planet, scale up for real use
- `Sea Level` -> `0` (meters, matches the hardcoded biome threshold in `BuildMeshData`)
- `Noise Settings` -> expand and tune `Seed`/`Octaves`/`Frequency`/`Lacunarity`/`Persistence`/`HeightScale`

### Planet | Streaming
- `Pool Size` -> `500` (or lower for initial testing, e.g. `100`, to iterate faster)
- `Verts Per Chunk Edge` -> `33` (good default; higher = more detail per chunk, costs more)

### Planet | LOD
- `Max Quadtree Depth` -> start at `5`-`6` for testing, raise once stable
- `LOD Split Factor` -> `2.0` default
- `Enable Cross Face LOD Balancing` -> **leave false** until you've run the seam
  validation pass (see `Docs/03_SeamValidation.md`) — edge skirts handle cracks
  adequately in the meantime

### Planet | Collision
- `Collision Radius` -> `5000` default; raise if your character moves fast (see tuning
  note in the collision LOD design — pad for max move speed × streaming tick interval)

### Planet | Material
- `Grass Tiling Scale` / `Rock Tiling Scale` / `Snow Tiling Scale` -> start at `0.01`/
  `0.006`/`0.008`, tune visually
- `Triplanar Blend Sharpness` -> `4.0`
- `Slope Start Degrees` / `Slope End Degrees` -> `30` / `60`

### Planet | Ocean
- `Ocean Subdivisions Per Face` -> `12`
- `Water Material` -> `M_PlanetWater`
- `Ocean Shell Class` -> `BP_PlanetOceanShell`

### Planet | Atmosphere
- `Atmosphere Settings` -> expand and configure (Rayleigh/Mie scattering, horizon tint,
  cloud settings) — see table below for Earth-like starting values
- `Glow Shell Scale` -> `1.08`
- `Glow Shell Material` -> `M_PlanetGlowShell`
- `Glow Shell Class` -> `BP_PlanetGlowShell`
- Inside `Atmosphere Settings -> Cloud Material` -> `M_VolumetricCloud`

#### Earth-like `AtmosphereSettings` starting values

| Field | Value |
|---|---|
| Atmosphere Height Ratio | 0.015 |
| Rayleigh Scattering | (0.0058, 0.0135, 0.0331) |
| Rayleigh Exponential Distribution | 8.0 |
| Mie Scattering Scale | 0.004 |
| Mie Absorption Scale | 0.0044 |
| Mie Exponential Distribution | 1.2 |
| Mie Anisotropy | 0.8 |
| Horizon Tint Override | light blue, ~(0.7, 0.85, 1.0) |
| Has Clouds | true |
| Cloud Layer Bottom Km | 2.0 |
| Cloud Layer Height Km | 6.0 |
| Cloud Coverage | 0.5 |
| Cloud Density Scale | 1.0 |

For a Mars-like planet: lower `AtmosphereHeightRatio` (thinner), shift `RayleighScattering`
toward dusty red/orange, set `HorizonTintOverride` orange, set `bHasClouds = false`.

---

## Step 10 — Set up the level

1. Drag one `BP_PlanetAtmosphereController` into the level. There should be **exactly one**
   in the level regardless of how many planets you have (see the multi-planet design notes —
   UE supports only one active Sky Atmosphere component at a time).
2. Drag one `BP_PlanetManager` into the level per planet. Position each at its intended
   world location (the manager's actor transform location IS the planet center — `BuildMeshData`,
   the ocean shell, and the glow shell all read `GetActorLocation()` as `PlanetCenter`).
3. Drag one `BP_PlanetSystemManager` into the level.
4. Select `BP_PlanetSystemManager` in the World Outliner -> Details panel:
   - `Planets` -> add an array entry per `BP_PlanetManager` instance you placed
   - `Atmosphere Controller` -> assign the one `BP_PlanetAtmosphereController` instance
   - `Atmosphere Fade Start/End Radius Multiplier` -> defaults `5.0` / `2.0` are reasonable
     starting points; tune based on how dramatic you want the space-to-surface transition
     to look

---

## Step 11 — Player pawn / character requirements

Nothing in this system requires a custom pawn to *render* — `PlanetManager::Tick()` and
`PlanetSystemManager::Tick()` both call
`UGameplayStatics::GetPlayerController(GetWorld(), 0)->GetPawn()->GetActorLocation()` to
find the viewer. Any possessed pawn (including the default `DefaultPawn` /
`SpectatorPawn`) works for testing streaming and atmosphere — just place a Player Start
near one of your planets.

**Caveat**: standing/walking on the surface with normal UE gravity won't work correctly —
default `CharacterMovementComponent` gravity points along world -Z, not toward the planet
center, so a character will fall through the surface at any point that isn't directly
"north." This is expected and out of scope for this wiring pass — flagged in the original
design discussion as the next system to build (custom gravity/movement component) once
you're ready for it.

---

## Step 12 — First test checklist

1. PIE near a planet's surface (spawn the Player Start within `LoadRadius`-equivalent
   distance, i.e. roughly within a few times `PlanetRadius`).
2. Confirm chunks appear and the pool doesn't immediately exhaust — check
   `Pool Size` vs. how many leaves your `Max Quadtree Depth` produces near the camera; if
   chunks are missing/flickering, the pool is likely too small for your current LOD settings.
3. Fly away in a Spectator Pawn until the planet is small in the view — confirm the glow
   shell appears and the real atmosphere/clouds fade out.
4. Fly back toward the surface — confirm the real atmosphere/clouds fade in and the glow
   shell fades out, without an obvious double-glow seam (see `Docs/03_SeamValidation.md`
   if it looks wrong).
5. Walk into the ocean shell area — confirm water renders with shoreline depth fade, not
   a flat-colored hard edge.
6. Check `stat unit` / `stat gpu` for early performance sanity, especially if running
   `Pool Size = 500` with high `Max Quadtree Depth` and triplanar materials together for
   the first time.
