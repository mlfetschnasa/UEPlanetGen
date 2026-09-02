# Material Setup Reference

Consolidated node-graph specs for all 4 materials + 2 material functions this system
needs. Pairs with `Docs/02_BlueprintWiring.md` Steps 2-5 (which list the required
Material Parameter names C++ depends on) — this doc is the "how to build the graph,"
that doc is the "what to name things and assign."

---

## MF_TriplanarSample (Material Function)

**Purpose**: sample a tileable texture without UV distortion, by projecting from 3
world-space axes and blending based on surface normal. Required because cube-sphere
terrain has no consistent UV unwrap direction (your `BuildMeshData`-generated UVs stretch
near cube edges and would tile visibly wrong with standard UV sampling).

**Function Inputs**:
- `Texture` (Texture2D)
- `WorldPosition` (Vector3) — feed `Absolute World Position`
- `WorldNormal` (Vector3) — feed `Pixel Normal WS`
- `TilingScale` (Scalar, default 0.01)
- `BlendSharpness` (Scalar, default 4.0)

**Graph**:
```
ScaledPos = WorldPosition * TilingScale

SampleXY = Texture Sample (Texture, UV = ScaledPos.XY)
SampleXZ = Texture Sample (Texture, UV = ScaledPos.XZ)
SampleYZ = Texture Sample (Texture, UV = ScaledPos.YZ)

BlendWeights = pow(abs(WorldNormal), BlendSharpness)
BlendWeights = BlendWeights / (BlendWeights.x + BlendWeights.y + BlendWeights.z)

Result = SampleYZ * BlendWeights.x + SampleXZ * BlendWeights.y + SampleXY * BlendWeights.z
```

**Function Output**: `Result` (the blended sample — call once per texture map: albedo,
normal, roughness, each as a separate `MF_TriplanarSample` call).

---

## MF_SlopeRockBlend (Material Function)

**Purpose**: boosts rock blend weight on steep slopes regardless of elevation, so cliffs
don't show grass texture. Operates on the **per-pixel radial "up" direction** (not fixed
world-up), since "up" differs at every point on a sphere.

**Function Inputs**:
- `WorldNormal` (Vector3) — feed `Pixel Normal WS`
- `AbsoluteWorldPosition` (Vector3)
- `PlanetCenterWorldPosition` (Vector3) — parameter, set from C++
- `SlopeStartDegrees` (Scalar, default 30.0)
- `SlopeEndDegrees` (Scalar, default 60.0)
- `LandWeights` (Vector3) — (Grass, Rock, Snow), already renormalized from vertex color

**Graph**:
```
PlanetUpVector = normalize(AbsoluteWorldPosition - PlanetCenterWorldPosition)

NdotUp = dot(WorldNormal, PlanetUpVector)
SlopeAngleFactor = saturate(NdotUp)

CosStart = cos(radians(SlopeStartDegrees))
CosEnd   = cos(radians(SlopeEndDegrees))

SlopeRockBoost = 1.0 - smoothstep(CosEnd, CosStart, SlopeAngleFactor)

AdjustedRock  = max(LandWeights.b, SlopeRockBoost)
RemainingRoom = 1.0 - AdjustedRock
GrassSnowSum  = LandWeights.g + LandWeights.a
SafeDivisor   = max(GrassSnowSum, 0.0001)

AdjustedGrass = LandWeights.g * (RemainingRoom / SafeDivisor)
AdjustedSnow  = LandWeights.a * (RemainingRoom / SafeDivisor)
```

**Function Output**: `FinalLandWeights` = (AdjustedGrass, AdjustedRock, AdjustedSnow) as Vector3.

---

## M_PlanetTerrain (Material)

**Domain**: Surface · **Blend Mode**: Opaque · **Shading Model**: Default Lit

**Parameters required** (see wiring doc Step 2 for the exact names C++ targets):
`GrassTiling`, `RockTiling`, `SnowTiling`, `BlendSharpness`, `PlanetCenterWorldPosition`,
`SlopeStartDegrees`, `SlopeEndDegrees`, plus 9 Texture parameters (Grass/Rock/Snow ×
Albedo/Normal/Roughness).

**Graph**:
```
// Per layer (×3: Grass, Rock, Snow), each calling MF_TriplanarSample 3× (albedo/normal/roughness):
GrassAlbedo/Normal/Rough = MF_TriplanarSample(T_Grass_*, WorldPos, WorldNormal, GrassTiling, BlendSharpness)
RockAlbedo/Normal/Rough  = MF_TriplanarSample(T_Rock_*,  WorldPos, WorldNormal, RockTiling,  BlendSharpness)
SnowAlbedo/Normal/Rough  = MF_TriplanarSample(T_Snow_*,  WorldPos, WorldNormal, SnowTiling,  BlendSharpness)

VC = Vertex Color                      // R=Water, G=Grass, B=Rock, A=Snow
LandSum = max(VC.g + VC.b + VC.a, 0.0001)
LandWeights = float3(VC.g, VC.b, VC.a) / LandSum

FinalLandWeights = MF_SlopeRockBlend(WorldNormal, AbsoluteWorldPosition,
                                      PlanetCenterWorldPosition,
                                      SlopeStartDegrees, SlopeEndDegrees, LandWeights)

FinalAlbedo    = GrassAlbedo * FinalLandWeights.x + RockAlbedo * FinalLandWeights.y + SnowAlbedo * FinalLandWeights.z
FinalNormal    = normalize(GrassNormal * FinalLandWeights.x + RockNormal * FinalLandWeights.y + SnowNormal * FinalLandWeights.z)
FinalRoughness = GrassRough * FinalLandWeights.x + RockRough * FinalLandWeights.y + SnowRough * FinalLandWeights.z

-> Base Color: FinalAlbedo
-> Normal: FinalNormal
-> Roughness: FinalRoughness
```

Water (`VC.r`) is intentionally NOT blended here — handled by the separate ocean shell
mesh + `M_PlanetWater`, see below.

---

## M_PlanetWater (Material)

**Domain**: Surface · **Blend Mode**: Translucent · **Shading Model**: Default Lit ·
**Two Sided**: Off

**Graph**:
```
// Panning normals (two samples, different scale/speed avoids visible tiling sync)
NormalA = Texture Sample (T_Water_Normal, UV + (Time*0.02, Time*0.015))
NormalB = Texture Sample (T_Water_Normal, UV*2.3 + (Time*-0.013, Time*0.021))
BlendedNormal = normalize(NormalA + NormalB)

// Depth-based shoreline fade
PixelWorldDepth = SceneTexture(SceneDepth)
SurfaceDepth = Pixel Depth (Camera-space depth of this pixel)
DepthDifference = PixelWorldDepth - SurfaceDepth
DepthFadeFactor = saturate(DepthDifference / DepthFadeDistance)   // DepthFadeDistance: Scalar param, ~500

FinalWaterColor = lerp(ShallowColor, DeepColor, DepthFadeFactor)   // both Vector3 params
BaseOpacity = lerp(0.3, 0.9, DepthFadeFactor)

Fresnel = Fresnel(Exponent=5.0, BaseReflectFraction=0.04)
FinalOpacity = saturate(BaseOpacity + Fresnel * 0.5)

-> Normal: BlendedNormal
-> Base Color: FinalWaterColor
-> Opacity: FinalOpacity
-> Roughness: ~0.05-0.1 (constant)
-> Specular: ~0.5 (constant)
```

---

## M_PlanetGlowShell (Material)

**Domain**: Surface · **Blend Mode**: Translucent · **Shading Model**: Unlit

**Parameters**: `GlowColor` (Vector3), `GlowIntensity` (Scalar, default 2.0), `RimPower`
(Scalar, default 3.0), `GlowAlpha` (Scalar, driven from C++).

**Graph**:
```
CameraVector = Camera Vector (built-in node)
NdotV = saturate(dot(WorldNormal, CameraVector))
Fresnel = pow(1.0 - NdotV, RimPower)

FinalEmissive = GlowColor * GlowIntensity * Fresnel
FinalOpacity  = Fresnel * GlowAlpha

-> Emissive Color: FinalEmissive
-> Opacity: FinalOpacity
```

---

## M_VolumetricCloud (Material)

**Domain**: Volume. Start from Unreal's built-in volumetric cloud material template
(New Material -> set Material Domain to Volume -> exposes `Volumetric Advanced Output`).

**Parameters**: `Coverage`, `DensityScale`, `DensityFadeAlpha`, `LayerBottomWorldZ`,
`LayerHeightWorld` (all Scalar), `CloudAlbedo` (Vector3). Also recommended as
material-internal constants (not currently C++-driven, tune as defaults or add MID calls
if you want runtime control): `ShapeScale` (~0.0008), `DetailScale` (~0.004),
`ErosionStrength` (~0.3), `WindDirection` (Vector3, e.g. (1,0,0)), `WindSpeed` (~200),
`PhaseG` (~0.7).

**Graph**:
```
WorldPos = Absolute World Position

// Step 1: base shape (low-frequency Perlin)
ShapeNoise = Noise(Position = WorldPos * ShapeScale, Function = Perlin, Levels = 3,
                    OutputMin = 0, OutputMax = 1)

// Step 2: height gradient
NormalizedHeight = (WorldPos.z - LayerBottomWorldZ) / LayerHeightWorld
HeightGradient = saturate(smoothstep(0.0, 0.2, NormalizedHeight))
               * saturate(1.0 - smoothstep(0.7, 1.0, NormalizedHeight))

// Step 3: coverage gating
CoverageThreshold = 1.0 - Coverage
BaseShape = saturate(ShapeNoise - CoverageThreshold) * HeightGradient

// Step 4: detail erosion (high-frequency, wind-panned)
DetailUVW = WorldPos * DetailScale + (WindDirection * Time * WindSpeed)
DetailNoise = Noise(Position = DetailUVW, Function = Perlin, Levels = 2,
                     OutputMin = 0, OutputMax = 1)
ErosionAmount = DetailNoise * ErosionStrength
FinalDensity = saturate(BaseShape - ErosionAmount * (1.0 - BaseShape))

// Step 5: apply external scale/fade controls
OutputDensity = FinalDensity * DensityScale * DensityFadeAlpha

-> Volumetric Advanced Output:
     Density: OutputDensity
     Albedo: CloudAlbedo
     Phase G: PhaseG (or constant ~0.7)
     Extinction Scale: 1.0 (component default unless tuning)
```

**Unit note**: `LayerBottomWorldZ`/`LayerHeightWorld` are in **world-space centimeters**
(matching `Absolute World Position`), not the kilometers-above-sea-level convention
`UVolumetricCloudComponent`'s own `LayerBottomAltitude`/`LayerHeight` properties use. The
C++ side (`PlanetAtmosphereController::ConfigureForPlanet`) does this unit conversion for
you — don't convert again in the material graph.

---

## Texture asset checklist

You need to source or author (not covered by this system — these are content assets):

- `T_Grass_Albedo`, `T_Grass_Normal`, `T_Grass_Roughness`
- `T_Rock_Albedo`, `T_Rock_Normal`, `T_Rock_Roughness`
- `T_Snow_Albedo`, `T_Snow_Normal`, `T_Snow_Roughness`
- `T_Water_Normal` (panning ripple normal map)

Any tileable PBR texture set works (Quixel Megascans, free CC0 sources, or your own).
Megascans integrates directly into UE if you have access to it via Fab/Quixel Bridge.
