// PlanetChunk.cpp
#include "PlanetChunk.h"
#include "NoiseGenerator.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"

APlanetChunk::APlanetChunk()
{
	PrimaryActorTick.bCanEverTick = false;
	MeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("MeshComponent"));
	RootComponent = MeshComponent;
	MeshComponent->bUseAsyncCooking = true;
	// Explicit profile -- do not rely on UProceduralMeshComponent's engine default,
	// which is ambiguous and, if it doesn't block the ship's collision channel, would
	// silently reproduce a "flies straight through" symptom independent of the CCD/
	// complex-collision issue documented in SetCollisionEnabled() below.
	MeshComponent->SetCollisionProfileName(TEXT("BlockAll"));
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Mark mesh component transient so its vertex/triangle data is NEVER serialized
	// to the level asset. Without this, editor-generated meshes bloat the level file
	// and make saves/PIE launches extremely slow. The planet regenerates from parameters
	// on load instead -- much faster and keeps the level file small.
	MeshComponent->SetFlags(RF_Transient);

	// Foliage: HISM gives free per-instance frustum/occlusion culling and per-instance
	// distance culling via InstanceStart/EndCullDistance -- the standard UE approach for
	// large instance counts (this is exactly how Landscape's own grass system works,
	// per-tile). NoCollision for now: foliage shouldn't interfere with the terrain
	// collision / ship-safety-sweep setup. RF_Transient for the same reason as the
	// terrain mesh -- instance data must never serialize into the level.
	GrassHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("GrassHISM"));
	GrassHISM->SetupAttachment(RootComponent);
	GrassHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GrassHISM->SetCastShadow(false); // grass: skip shadow casting, cheap and rarely noticeable
	GrassHISM->SetFlags(RF_Transient);

	RockHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("RockHISM"));
	RockHISM->SetupAttachment(RootComponent);
	RockHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RockHISM->SetCastShadow(true); // rocks: larger/sparser, shadow contribution is worth it
	RockHISM->SetFlags(RF_Transient);

	SetActorHiddenInGame(true);
}

void APlanetChunk::Activate(const FPlanetChunkCoord& InCoord, const FVector& PlanetCenter, double PlanetRadius)
{
	Coord = InCoord;
	bActive = true;
	bReady = false;

	// Cheap approximation (no noise/height sampling) of this chunk's actual world-space
	// center -- same projection the quadtree itself uses for LOD distance decisions.
	// Used by collision-LOD and any other proximity check, since GetActorLocation() is
	// always ZeroVector (mesh vertices are baked absolute-world-space, not actor-relative).
	ApproxWorldCenter = PlanetCenter + PlanetMath::FaceUVToSpherePoint(
		Coord.Face, Coord.CenterU, Coord.CenterV, PlanetRadius);

	// Undo Deactivate()'s parking position. BuildMeshData bakes absolute world-space
	// positions directly into the vertex array (FinalPos = PlanetCenter + Dir * (Radius +
	// Height)), so this actor's own transform must be identity -- any non-zero actor
	// location here would double-offset the already-world-space mesh geometry.
	SetActorLocation(FVector::ZeroVector);
	SetActorHiddenInGame(false);
}

void APlanetChunk::Deactivate()
{
	bActive = false;
	bReady = false;
	bBuildInFlight = false;
	bCollisionEnabled = false;
	bFoliageEnabled = false;
	ApproxWorldCenter = FVector::ZeroVector;
	CachedMeshData = FChunkMeshData(); // drop cached arrays, free memory
	CachedFoliageData = FFoliageInstanceData(); // this chunk may be recycled for a
	                                             // different coord -- never carry over
	MeshComponent->ClearAllMeshSections(); // also drops cooked collision
	GrassHISM->ClearInstances();
	RockHISM->ClearInstances();
	SetActorHiddenInGame(true);
	SetActorLocation(FVector(0, 0, -1000000.0)); // park far away
}

// ============================================================
// PHASE 1 -- pure math, no UObject/UE API touches. Safe on any thread.
// ============================================================
FChunkMeshData APlanetChunk::BuildMeshData(
	const FPlanetChunkCoord& Coord,
	FVector PlanetCenter,
	double PlanetRadius,
	int32 VertsPerEdge,
	double SeaLevel,
	double RockStartHeight,
	double SnowStartHeight,
	double MaxHeight,
	TSharedPtr<FNoiseGenerator, ESPMode::ThreadSafe> Noise,
	const TArray<FStaticPOI>& POIs,
	ENoiseDebugView DebugView)
{
	FChunkMeshData Out;
	if (!Noise.IsValid() || VertsPerEdge < 2) return Out;

	// Coarse per-chunk POI filter, done ONCE before the vertex loop (not per-vertex).
	// With POI counts in the tens-to-low-hundreds range, a chunk typically has 0-1
	// relevant POIs -- this keeps the common (zero-POI) case essentially free, and
	// avoids an O(vertices * POIs) cost for planets with any buildings at all.
	TArray<const FStaticPOI*> RelevantPOIs;
	if (POIs.Num() > 0)
	{
		const FVector ChunkApproxCenter = PlanetCenter + PlanetMath::FaceUVToSpherePoint(
			Coord.Face, Coord.CenterU, Coord.CenterV, PlanetRadius);
		// Generous overestimate of this chunk's bounding radius -- false positives here
		// only cost a few extra (cheap) per-vertex distance checks, false negatives would
		// incorrectly skip flattening near a chunk's edge, so err large.
		const double ChunkBoundRadius = Coord.HalfExtent * PlanetRadius * 1.5;
		for (const FStaticPOI& POI : POIs)
		{
			const FVector POIApproxPos = PlanetCenter + POI.Direction * PlanetRadius;
			const double Influence = POI.FlattenRadius + POI.FlattenBlendDistance + ChunkBoundRadius;
			if (FVector::DistSquared(ChunkApproxCenter, POIApproxPos) <= Influence * Influence)
			{
				RelevantPOIs.Add(&POI);
			}
		}
	}

	const int32 N = VertsPerEdge;
	const int32 VertCount = N * N;
	Out.Vertices.SetNumUninitialized(VertCount);
	Out.Normals.SetNumUninitialized(VertCount);
	Out.UVs.SetNumUninitialized(VertCount);
	Out.VertexColors.SetNumUninitialized(VertCount);

	// Biome thresholds come from PlanetManager UPROPERTYs -- tune live in PIE
	// via the BP_PlanetManager instance in the World Outliner without recompiling.
	const double RockStart = RockStartHeight;
	const double SnowStart = SnowStartHeight;
	const double HeightRange = MaxHeight;

	// This chunk owns a sub-rectangle of its face's [-1,1] UV space.
	const double ChunkExtent = Coord.HalfExtent * 2.0;
	const double FaceU0 = Coord.CenterU - Coord.HalfExtent;
	const double FaceV0 = Coord.CenterV - Coord.HalfExtent;

	for (int32 y = 0; y < N; ++y)
	{
		for (int32 x = 0; x < N; ++x)
		{
			const int32 Index = y * N + x;

			double U = FaceU0 + (x / double(N - 1)) * ChunkExtent;
			double V = FaceV0 + (y / double(N - 1)) * ChunkExtent;

			// Snap cube-face edges to exact +-1.0 for cross-face seam prevention.
			U = PlanetMath::SnapToCubeEdge(U);
			V = PlanetMath::SnapToCubeEdge(V);

			// --- Cube-sphere projection ---
			const FVector CubePoint = PlanetMath::FaceUVToCubePoint(Coord.Face, U, V);
			const FVector Dir = PlanetMath::GetSphereNormal(CubePoint); // normalize

			// --- Height variation ---
			double Height = Noise->SampleHeight(Dir);                          // NoiseGen->SampleHeight(pos)
			Height = FMath::Clamp(Height, -HeightRange, HeightRange);          // Clamp(-500, +500)

			// --- POI flatten pass: force height toward each nearby POI's TargetHeight. ---
			// Applied here (before FinalPos/debug-view/biome all consume Height) so every
			// downstream consumer -- render mesh, collision, biome coloring, foliage
			// exclusion -- sees one single, consistent flattened height. Distance uses the
			// pre-height vertex position (approximation: ignores height's own contribution
			// to the exact 3D distance, which is negligible relative to typical flatten
			// radii and avoids a circular dependency on the height being computed).
			if (RelevantPOIs.Num() > 0)
			{
				const FVector VertexApproxPos = PlanetCenter + Dir * PlanetRadius;
				const FStaticPOI* BestPOI = nullptr;
				double BestDist = 0.0;
				for (const FStaticPOI* POI : RelevantPOIs)
				{
					const FVector POIApproxPos = PlanetCenter + POI->Direction * PlanetRadius;
					const double Dist = FVector::Dist(VertexApproxPos, POIApproxPos);
					if (Dist <= POI->FlattenRadius + POI->FlattenBlendDistance && (!BestPOI || Dist < BestDist))
					{
						BestPOI = POI;
						BestDist = Dist;
					}
				}
				if (BestPOI)
				{
					if (BestDist <= BestPOI->FlattenRadius)
					{
						Height = BestPOI->TargetHeight; // fully flattened
					}
					else
					{
						// Alpha: 0 at inner (flatten) edge -> 1 at outer edge (fully natural).
						const double Alpha = FMath::SmoothStep(BestPOI->FlattenRadius,
							BestPOI->FlattenRadius + BestPOI->FlattenBlendDistance, BestDist);
						Height = FMath::Lerp(BestPOI->TargetHeight, Height, Alpha);
					}
				}
			}

			const FVector FinalPos = PlanetCenter + Dir * (PlanetRadius + Height); // Center + Dir*(R+Height)

			Out.Vertices[Index] = FinalPos;
			Out.Normals[Index] = Dir; // placeholder; replaced with smooth triangle normals below
			Out.UVs[Index] = FVector2D((U + 1.0) * 0.5, (V + 1.0) * 0.5);

			// --- Vertex color biome blending ---
			// R=Water, G=Grass, B=Rock. Snow is NOT stored in Alpha -- UProceduralMeshComponent
			// doesn't reliably populate the Vertex Color node's A output in UE5. Instead, Snow
			// weight is derived entirely in the material as (1.0 - R - G - B), which also
			// guarantees the four weights always sum to 1 by construction.
			// --- Debug view: write the selected noise signal as grayscale instead of
			// biome weights. Wire Vertex Color -> Base Color to inspect. Slope still
			// lands in Alpha via the later pass, which is harmless here.
			if (DebugView != ENoiseDebugView::Off)
			{
				float DV = 0.f;
				switch (DebugView)
				{
				case ENoiseDebugView::ContinentHeight:
					DV = (float)((Noise->SampleContinentHeight(Dir) / HeightRange) * 0.5 + 0.5); break;
				case ENoiseDebugView::MountainMask:
					DV = (float)Noise->SampleMountainMask(Dir); break;
				case ENoiseDebugView::CanyonMask:
					DV = (float)Noise->SampleCanyonMask(Dir); break;
				case ENoiseDebugView::PlateauMask:
					DV = (float)Noise->SamplePlateauMask(Dir); break;
				case ENoiseDebugView::FinalHeight:
					DV = (float)((Height / HeightRange) * 0.5 + 0.5); break;
				default: break;
				}
				Out.VertexColors[Index] = FLinearColor(DV, DV, DV, 0.f);
				continue;
			}

			float R = 0.f, G = 0.f, B = 0.f;

			// SeaLevel is in meters (PlanetPreset.h); Height is cm-domain like PlanetRadius,
			// so it needs the same *100 conversion PlanetOceanShell/IsPointUnderwater apply --
			// without it this never matches in practice and land renders dry under the ocean shell.
			if (Height <= SeaLevel * 100.0)
			{
				R = 1.0f; // Water -- G and B stay 0, so Snow derived = 0 too. Correct.
			}
			else
			{
				float RockBlend = (float)FMath::GetMappedRangeValueClamped(
					FVector2D(RockStart, SnowStart), FVector2D(0.0, 1.0), Height);
				float SnowBlend = (float)FMath::GetMappedRangeValueClamped(
					FVector2D(SnowStart, HeightRange), FVector2D(0.0, 1.0), Height);

				RockBlend = FMath::Clamp(RockBlend, 0.f, 1.f);
				SnowBlend = FMath::Clamp(SnowBlend, 0.f, 1.f);

				B = RockBlend * (1.f - SnowBlend);
				// Snow = SnowBlend, but stored implicitly -- material derives it as (1-R-G-B)
				G = FMath::Clamp(1.f - B - SnowBlend, 0.f, 1.f); // Grass = remainder after Rock+Snow
			}

			Out.VertexColors[Index] = FLinearColor(R, G, B, 0.f);
		}
	}

	// Triangles (two tris per quad, consistent winding)
	Out.Triangles.Reserve((N - 1) * (N - 1) * 6);
	for (int32 y = 0; y < N - 1; ++y)
	{
		for (int32 x = 0; x < N - 1; ++x)
		{
			const int32 I0 = y * N + x;
			const int32 I1 = I0 + 1;
			const int32 I2 = I0 + N;
			const int32 I3 = I2 + 1;

			Out.Triangles.Add(I0); Out.Triangles.Add(I2); Out.Triangles.Add(I1);
			Out.Triangles.Add(I1); Out.Triangles.Add(I2); Out.Triangles.Add(I3);
		}
	}

	// Compute triangle-averaged normals first -- needed for slope computation below.
	Out.Normals.Init(FVector::ZeroVector, VertCount);
	for (int32 i = 0; i < Out.Triangles.Num(); i += 3)
	{
		const int32 IA = Out.Triangles[i], IB = Out.Triangles[i + 1], IC = Out.Triangles[i + 2];
		const FVector FaceNormal = FVector::CrossProduct(
			Out.Vertices[IC] - Out.Vertices[IA],
			Out.Vertices[IB] - Out.Vertices[IA]).GetSafeNormal();
		Out.Normals[IA] += FaceNormal;
		Out.Normals[IB] += FaceNormal;
		Out.Normals[IC] += FaceNormal;
	}
	for (FVector& Nrm : Out.Normals) Nrm = Nrm.GetSafeNormal();

	// Compute slope from triangle-averaged normals.
	// Slope = deviation of the triangle normal from the radial direction:
	//   0.0 = flat, 1.0 = vertical cliff.
	// Stored in vertex color Alpha (Snow is derived in material as 1-R-G-B,
	// leaving Alpha free for this slope signal).
	for (int32 i = 0; i < VertCount; ++i)
	{
		const FVector RadialDir = (Out.Vertices[i] - PlanetCenter).GetSafeNormal();
		const float NdotR = (float)FVector::DotProduct(Out.Normals[i], RadialDir);
		Out.VertexColors[i].A = FMath::Clamp(1.0f - NdotR, 0.f, 1.f);
	}

	// NOTE: triangle-averaged normals kept in Out.Normals -- NOT replaced with analytical
	// normals. The material blends VertexNormalWS (surface detail) and analytical normal
	// (seamless) based on camera distance. See M_PlanetTerrain Normal pin setup.

	Out.Tangents.SetNum(VertCount); // placeholder tangents; compute properly if normal maps need it

	// --- Edge skirts: hide LOD cracks by extruding boundary vertices inward toward planet center ---
	// Cheap insurance against T-junction seams regardless of whether cross-face balancing
	// (FPlanetQuadtree's bEnableCrossFaceBalancing) is enabled or validated yet.
	// Skirt depth scales with the chunk's world size. The old fixed 50.0 was 50 UE
	// units (0.5m -- the comment claimed meters, but vertices are in cm), which is
	// invisible against multi-km height variation and left visible gaps at LOD
	// boundaries. 5% of chunk width comfortably covers the geometric error between
	// adjacent LOD levels at any planet scale.
	const double ChunkWorldSize = Coord.HalfExtent * 2.0 * PlanetRadius;
	const double SkirtDepth = ChunkWorldSize * 0.05;

	auto AddSkirtForEdge = [&Out, PlanetCenter, SkirtDepth](TFunctionRef<int32(int32)> EdgeVertexIndex, int32 Count)
	{
		const int32 SkirtStart = Out.Vertices.Num();
		for (int32 i = 0; i < Count; ++i)
		{
			const int32 SrcIdx = EdgeVertexIndex(i);
			const FVector SrcPos = Out.Vertices[SrcIdx];
			const FVector InwardDir = (SrcPos - PlanetCenter).GetSafeNormal();

			// Copy each value to a local BEFORE calling Add() -- Out.Normals[SrcIdx] etc.
			// return references into the array's own buffer, and Add() can trigger a
			// reallocation that invalidates that reference before it's read, causing a
			// dangling-pointer crash (the array tries to copy from memory it just freed).
			const FVector SrcNormal = Out.Normals[SrcIdx];
			const FVector2D SrcUV = Out.UVs[SrcIdx];
			const FLinearColor SrcColor = Out.VertexColors[SrcIdx];
			const FProcMeshTangent SrcTangent = Out.Tangents.IsValidIndex(SrcIdx) ? Out.Tangents[SrcIdx] : FProcMeshTangent();

			Out.Vertices.Add(SrcPos - InwardDir * SkirtDepth);
			Out.Normals.Add(SrcNormal);
			Out.UVs.Add(SrcUV);
			Out.VertexColors.Add(SrcColor);
			Out.Tangents.Add(SrcTangent);
		}
		for (int32 i = 0; i < Count - 1; ++i)
		{
			const int32 A = EdgeVertexIndex(i);
			const int32 B = EdgeVertexIndex(i + 1);
			const int32 SA = SkirtStart + i;
			const int32 SB = SkirtStart + i + 1;
			Out.Triangles.Append({ A, SA, B,  B, SA, SB });
		}
	};

	AddSkirtForEdge([N](int32 i) { return i; },                 N); // V_Neg edge (y=0 row)
	AddSkirtForEdge([N](int32 i) { return (N - 1) * N + i; },    N); // V_Pos edge (y=N-1 row)
	AddSkirtForEdge([N](int32 i) { return i * N; },              N); // U_Neg edge (x=0 col)
	AddSkirtForEdge([N](int32 i) { return i * N + (N - 1); },    N); // U_Pos edge (x=N-1 col)

	Out.bValid = true;
	return Out;
}

// ============================================================
// PHASE 2 -- game thread only. Uploads to UE objects.
// ============================================================
void APlanetChunk::ApplyMeshData(FChunkMeshData Data, UMaterialInterface* MaterialInstance)
{
	check(IsInGameThread());
	if (!Data.bValid) return;

	CachedMeshData = MoveTemp(Data); // retain for later collision toggling (Collision LOD)

	MeshComponent->CreateMeshSection_LinearColor(
		0,
		CachedMeshData.Vertices,
		CachedMeshData.Triangles,
		CachedMeshData.Normals,
		CachedMeshData.UVs,
		CachedMeshData.VertexColors,
		CachedMeshData.Tangents,
		/*bCreateCollision=*/ false); // collision deferred -- see SetCollisionEnabled

	if (MaterialInstance)
	{
		MeshComponent->SetMaterial(0, MaterialInstance);
	}

	bCollisionEnabled = false;
	bReady = true; // mark chunk as ready
	bBuildInFlight = false;
}

// IMPORTANT PHYSICS LIMITATION (read before debugging tunneling further):
// This mesh uses bUseComplexAsSimpleCollision -- the full render triangle mesh IS the
// collision. Neither PhysX nor Chaos support CCD (continuous collision detection)
// against complex/trimesh collision, only against simple shapes (sphere/box/convex).
// A physics-simulated actor (SetSimulatePhysics + AddForce) moving faster than one
// triangle-width per physics substep WILL tunnel through this terrain, and enabling
// CCD on the moving actor does not help -- the engine ignores CCD against complex
// geometry entirely. The supported mitigation is physics substepping (Project
// Settings -> Physics -> Substepping: enable, lower Max Substep Delta Time, raise
// Max Substeps), which integrates the fast body in several smaller steps per frame
// so each individual step's travel distance shrinks below the tunneling threshold.
// It reduces the problem, it does not categorically solve it at unbounded speed --
// there is no free way to get true CCD against a per-chunk heightfield mesh.
void APlanetChunk::SetCollisionEnabled(bool bEnabled)
{
	if (bEnabled == bCollisionEnabled) return; // no-op, avoid redundant re-cooks
	if (!bReady || !CachedMeshData.bValid) return;

	if (bEnabled)
	{
		// UProceduralMeshComponent has no public "re-cook collision on an existing
		// section" entry point -- CreateMeshSection_LinearColor(bCreateCollision=true)
		// is the only supported way to generate collision for a section. Re-calling it
		// on the same section index replaces that section's geometry + collision
		// together; since CachedMeshData already holds the Phase-1 arrays, this is a
		// re-upload (not a Phase-1 rebuild) but does cost more than a true "just flip
		// collision on" would have. bUseAsyncCooking (set in the constructor) still
		// keeps the actual PhysX/Chaos cook off the game thread.
		MeshComponent->bUseComplexAsSimpleCollision = true; // terrain: render mesh IS collision mesh
		MeshComponent->CreateMeshSection_LinearColor(
			0,
			CachedMeshData.Vertices,
			CachedMeshData.Triangles,
			CachedMeshData.Normals,
			CachedMeshData.UVs,
			CachedMeshData.VertexColors,
			CachedMeshData.Tangents,
			/*bCreateCollision=*/ true);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
	else
	{
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		// Cooked collision data can linger until ClearAllMeshSections() on Deactivate();
		// that's fine, it means re-enabling later near a boundary just re-runs the
		// CreateMeshSection_LinearColor call above rather than needing extra cleanup.
	}

	bCollisionEnabled = bEnabled;
}

// ============================================================
// FOLIAGE -- Phase 1 (pure math) + Phase 2 (game thread HISM population)
// ============================================================

FFoliageInstanceData APlanetChunk::BuildFoliageData(
	const FChunkMeshData& MeshData,
	const FVector& PlanetCenter,
	double PlanetRadius,
	const FFoliageSettings& Settings,
	const FPlanetChunkCoord& Coord,
	const TArray<FStaticPOI>& POIs)
{
	FFoliageInstanceData Out;
	if (!MeshData.bValid) return Out;
	if (!Settings.Grass.bEnabled && !Settings.Rocks.bEnabled)
	{
		Out.bValid = true;
		return Out;
	}

	// Cheap deterministic hash -> [0,1). Seeded from the chunk coord's own hash (already
	// deterministic/quantized -- see GetTypeHash(FPlanetChunkCoord) in PlanetQuadtree.h)
	// plus per-point salts. Same coord always produces the same foliage layout, so
	// nothing needs to be saved -- it regenerates identically after pool recycling,
	// exactly like terrain height.
	auto HashToUnit = [](uint32 Seed) -> double
	{
		Seed ^= Seed << 13; Seed ^= Seed >> 17; Seed ^= Seed << 5; // xorshift32
		return (double)(Seed & 0x00FFFFFFu) / (double)0x01000000u; // [0,1)
	};
	const uint32 CoordSeed = GetTypeHash(Coord);

	auto ProcessType = [&](const FFoliageTypeSettings& T, uint32 TypeSalt, bool bIsGrassChannel, TArray<FTransform>& OutTransforms)
	{
		if (!T.bEnabled || T.DensityPerSqm <= 0.0) return;

		uint32 InstanceCounter = 0;

		const int32 TriCount = MeshData.Triangles.Num() / 3;
		for (int32 T_i = 0; T_i < TriCount; ++T_i)
		{
			const int32 IA = MeshData.Triangles[T_i * 3 + 0];
			const int32 IB = MeshData.Triangles[T_i * 3 + 1];
			const int32 IC = MeshData.Triangles[T_i * 3 + 2];
			const FVector& VA = MeshData.Vertices[IA];
			const FVector& VB = MeshData.Vertices[IB];
			const FVector& VC = MeshData.Vertices[IC];

			const double AreaCm2 = 0.5 * FVector::CrossProduct(VB - VA, VC - VA).Size();
			const double AreaM2 = AreaCm2 / 10000.0; // 100 UE units = 1m, so cm^2 / 100^2

			// Expected instance count for this triangle, split into a guaranteed integer
			// part and a fractional remainder resolved by an INDEPENDENT per-triangle coin
			// flip (not a shared running accumulator). A shared accumulator crosses its
			// threshold at near-constant triangle intervals when density is locally
			// uniform -- since triangles are always iterated in the same fixed row-major
			// grid order, that regularity reads as visible rows in the final scatter.
			// An independent hash per triangle has no cross-triangle state, so it can't
			// inherit the grid's own periodicity.
			const double Expected = AreaM2 * T.DensityPerSqm;
			const int32 BaseInstances = (int32)FMath::FloorToDouble(Expected);
			const double FracRemainder = Expected - BaseInstances;
			const uint32 ExtraSeed = CoordSeed ^ (uint32)(T_i * 2246822519u) ^ TypeSalt ^ 0xB5297A4Du;
			const int32 ExtraInstance = (HashToUnit(ExtraSeed) < FracRemainder) ? 1 : 0;
			const int32 InstancesThisTri = BaseInstances + ExtraInstance;

			for (int32 i = 0; i < InstancesThisTri; ++i)
			{
				InstanceCounter++;
				const uint32 PointSeed = CoordSeed ^ (InstanceCounter * 2654435761u) ^ (uint32)(T_i * 40503) ^ TypeSalt;

				// Random barycentric point within the triangle.
				double r1 = HashToUnit(PointSeed);
				double r2 = HashToUnit(PointSeed ^ 0x9E3779B9u);
				if (r1 + r2 > 1.0) { r1 = 1.0 - r1; r2 = 1.0 - r2; }
				const double r3 = 1.0 - r1 - r2;

				const FVector Pos = VA * r1 + VB * r2 + VC * r3;
				const FVector Nrm = (MeshData.Normals[IA] * r1 + MeshData.Normals[IB] * r2 + MeshData.Normals[IC] * r3).GetSafeNormal();
				const FLinearColor Col = MeshData.VertexColors[IA] * r1 + MeshData.VertexColors[IB] * r2 + MeshData.VertexColors[IC] * r3;

				// Gate by the terrain's OWN biome weight and slope data -- no separate
				// noise sampling needed, this is exactly what the terrain already used
				// to decide grass/rock coloring, so foliage placement stays consistent
				// with what the ground actually looks like at that point.
				const double BiomeWeight = bIsGrassChannel ? Col.G : Col.B;
				const double Slope = Col.A;

				if (BiomeWeight < T.MinBiomeWeight) continue;
				if (Slope < T.MinSlope || Slope > T.MaxSlope) continue;

				// Skip candidates inside any POI's flatten influence -- keeps building
				// footprints clear of grass/rocks. Direct check against all POIs (not
				// chunk-filtered) since counts are modest at this phase; a coarse filter
				// like BuildMeshData's would only matter with far more POIs.
				bool bInsidePOI = false;
				for (const FStaticPOI& POI : POIs)
				{
					const FVector POIApproxPos = PlanetCenter + POI.Direction * PlanetRadius;
					if (FVector::Dist(Pos, POIApproxPos) <= POI.FlattenRadius + POI.FlattenBlendDistance)
					{
						bInsidePOI = true;
						break;
					}
				}
				if (bInsidePOI) continue;

				const FVector RadialDir = (Pos - PlanetCenter).GetSafeNormal();
				const FVector UpDir = FMath::Lerp(RadialDir, Nrm, T.NormalAlignment).GetSafeNormal();

				const FQuat AlignQuat = FQuat::FindBetweenNormals(FVector::UpVector, UpDir);
				FQuat FinalQuat = AlignQuat;
				if (T.bRandomYaw)
				{
					const double YawDeg = HashToUnit(PointSeed ^ 0x85EBCA6Bu) * 360.0;
					FinalQuat = FQuat(UpDir, FMath::DegreesToRadians(YawDeg)) * AlignQuat;
				}

				const double ScaleT = HashToUnit(PointSeed ^ 0xC2B2AE35u);
				const double Scale = FMath::Lerp(T.MinScale, T.MaxScale, ScaleT);

				OutTransforms.Add(FTransform(FinalQuat, Pos, FVector(Scale)));
			}
		}
	};

	ProcessType(Settings.Grass, 0x1000u, /*bIsGrassChannel=*/ true, Out.GrassTransforms);
	ProcessType(Settings.Rocks, 0x2000u, /*bIsGrassChannel=*/ false, Out.RockTransforms);

	Out.bValid = true;
	return Out;
}

void APlanetChunk::SetFoliageMeshes(UStaticMesh* GrassMesh, UStaticMesh* RockMesh, const FFoliageSettings& Settings)
{
	if (GrassHISM && GrassMesh && GrassHISM->GetStaticMesh() != GrassMesh)
	{
		GrassHISM->SetStaticMesh(GrassMesh);
	}
	if (RockHISM && RockMesh && RockHISM->GetStaticMesh() != RockMesh)
	{
		RockHISM->SetStaticMesh(RockMesh);
	}
	if (GrassHISM)
	{
		GrassHISM->InstanceStartCullDistance = (float)Settings.Grass.CullStartDistance;
		GrassHISM->InstanceEndCullDistance = (float)Settings.Grass.CullEndDistance;
	}
	if (RockHISM)
	{
		RockHISM->InstanceStartCullDistance = (float)Settings.Rocks.CullStartDistance;
		RockHISM->InstanceEndCullDistance = (float)Settings.Rocks.CullEndDistance;
	}
}

void APlanetChunk::SetFoliageEnabled(bool bEnabled)
{
	if (bEnabled == bFoliageEnabled) return; // no-op, avoid redundant HISM churn
	if (!CachedFoliageData.bValid) return;

	if (bEnabled)
	{
		// bWorldSpace=true: our transforms are absolute world-space (matching the
		// chunk's own vertex convention), and this actor's transform is always
		// identity/ZeroVector, same reasoning as terrain mesh vertices.
		if (GrassHISM && CachedFoliageData.GrassTransforms.Num() > 0)
		{
			GrassHISM->AddInstances(CachedFoliageData.GrassTransforms, /*bShouldReturnIndices=*/ false, /*bWorldSpace=*/ true);
		}
		if (RockHISM && CachedFoliageData.RockTransforms.Num() > 0)
		{
			RockHISM->AddInstances(CachedFoliageData.RockTransforms, /*bShouldReturnIndices=*/ false, /*bWorldSpace=*/ true);
		}
	}
	else
	{
		GrassHISM->ClearInstances();
		RockHISM->ClearInstances();
	}

	bFoliageEnabled = bEnabled;
}
