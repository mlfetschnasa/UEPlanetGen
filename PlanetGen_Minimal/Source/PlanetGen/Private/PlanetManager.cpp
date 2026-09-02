// PlanetManager.cpp
#include "PlanetManager.h"
#include "PlanetChunk.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UObjectIterator.h"

APlanetManager::APlanetManager()
{
	PrimaryActorTick.bCanEverTick = true;
}

// ============================================================
// PRESET ACCESSORS -- read from Preset if set, else Override_*
// ============================================================

double APlanetManager::GetPlanetRadius() const           { return Preset ? Preset->PlanetRadius : Override_PlanetRadius; }
double APlanetManager::GetSeaLevel() const               { return Preset ? Preset->SeaLevel : Override_SeaLevel; }
const FNoiseSettings& APlanetManager::GetNoiseSettings() const { return Preset ? Preset->NoiseSettings : Override_NoiseSettings; }
double APlanetManager::GetRockStartHeight() const        { return Preset ? Preset->RockStartHeight : Override_RockStartHeight; }
double APlanetManager::GetSnowStartHeight() const        { return Preset ? Preset->SnowStartHeight : Override_SnowStartHeight; }
double APlanetManager::GetMaxHeight() const              { return Preset ? Preset->MaxHeight : Override_MaxHeight; }
double APlanetManager::GetGrassTilingScale() const       { return Preset ? Preset->GrassTilingScale : Override_GrassTilingScale; }
double APlanetManager::GetRockTilingScale() const        { return Preset ? Preset->RockTilingScale : Override_RockTilingScale; }
double APlanetManager::GetSnowTilingScale() const        { return Preset ? Preset->SnowTilingScale : Override_SnowTilingScale; }
double APlanetManager::GetTriplanarBlendSharpness() const{ return Preset ? Preset->TriplanarBlendSharpness : Override_TriplanarBlendSharpness; }
double APlanetManager::GetSlopeStartThreshold() const    { return Preset ? Preset->SlopeStartThreshold : Override_SlopeStartThreshold; }
double APlanetManager::GetSlopeEndThreshold() const      { return Preset ? Preset->SlopeEndThreshold : Override_SlopeEndThreshold; }
double APlanetManager::GetSlopeScale() const             { return Preset ? Preset->SlopeScale : Override_SlopeScale; }
double APlanetManager::GetCollisionRadius() const        { return Preset ? Preset->CollisionRadius : Override_CollisionRadius; }
int32 APlanetManager::GetMaxQuadtreeDepth() const        { return Preset ? Preset->MaxQuadtreeDepth : Override_MaxQuadtreeDepth; }
double APlanetManager::GetLODSplitFactor() const         { return Preset ? Preset->LODSplitFactor : Override_LODSplitFactor; }
int32 APlanetManager::GetEditorLODDepth() const          { return Preset ? Preset->EditorLODDepth : Override_EditorLODDepth; }
int32 APlanetManager::GetOceanSubdivisionsPerFace() const{ return Preset ? Preset->OceanSubdivisionsPerFace : Override_OceanSubdivisionsPerFace; }
const FFoliageSettings& APlanetManager::GetFoliageSettings() const { return Preset ? Preset->Foliage : Override_FoliageSettings; }

// ============================================================
// SHARED HELPERS
// ============================================================

// Per-instance Outliner folder for this planet's pooled chunk actors, derived from
// the actor's display name: manager labeled "PM1" -> folder "PM1_Chunks".
// GetActorLabel() is the name shown (and renamable) in the Outliner; editor-only.
FString APlanetManager::GetChunkFolderName() const
{
#if WITH_EDITOR
	return GetActorLabel() + TEXT("_Chunks");
#else
	return FString();
#endif
}

void APlanetManager::SetupMaterialInstance()
{
	if (!ChunkMaterial) return;
	if (!ChunkMID)
	{
		ChunkMID = UMaterialInstanceDynamic::Create(ChunkMaterial, this);
	}
	ChunkMID->SetScalarParameterValue(TEXT("GrassTiling"), GetGrassTilingScale());
	ChunkMID->SetScalarParameterValue(TEXT("RockTiling"), GetRockTilingScale());
	ChunkMID->SetScalarParameterValue(TEXT("SnowTiling"), GetSnowTilingScale());
	ChunkMID->SetScalarParameterValue(TEXT("BlendSharpness"), GetTriplanarBlendSharpness());
	ChunkMID->SetScalarParameterValue(TEXT("SlopeStartThreshold"), GetSlopeStartThreshold());
	ChunkMID->SetScalarParameterValue(TEXT("SlopeEndThreshold"), GetSlopeEndThreshold());
	ChunkMID->SetScalarParameterValue(TEXT("SlopeScale"), GetSlopeScale());
	ChunkMID->SetVectorParameterValue(TEXT("PlanetCenterWorldPosition"), GetActorLocation());
}

void APlanetManager::GenerateEditorMesh()
{
	if (!Noise.IsValid() || !ChunkClass || !GetWorld()) return;

	SetupMaterialInstance();
	UMaterialInterface* Material = ChunkMID ? static_cast<UMaterialInterface*>(ChunkMID) : ChunkMaterial;

	const FVector PlanetCenter = GetActorLocation();
	const double PlanetRadius = GetPlanetRadius();
	const int32 EditorDepth = GetEditorLODDepth();
	const int32 CellsPerFace = (1 << EditorDepth);
	const double CellSize = 2.0 / CellsPerFace;

	constexpr EPlanetCubeFace Faces[6] = {
		EPlanetCubeFace::PosX, EPlanetCubeFace::NegX, EPlanetCubeFace::PosY,
		EPlanetCubeFace::NegY, EPlanetCubeFace::PosZ, EPlanetCubeFace::NegZ };

	for (EPlanetCubeFace Face : Faces)
	{
		for (int32 y = 0; y < CellsPerFace; ++y)
		{
			for (int32 x = 0; x < CellsPerFace; ++x)
			{
				FPlanetChunkCoord Coord;
				Coord.Face = Face;
				Coord.Depth = EditorDepth;
				Coord.HalfExtent = CellSize * 0.5;
				Coord.CenterU = -1.0 + (x + 0.5) * CellSize;
				Coord.CenterV = -1.0 + (y + 0.5) * CellSize;

				if (ActiveChunks.Contains(Coord)) continue;

				APlanetChunk* Chunk = ChunkPool.Acquire();
				if (!Chunk)
				{
					UE_LOG(LogTemp, Warning, TEXT("PlanetGen: Pool exhausted during editor generation -- raise PoolSize"));
					return;
				}

				Chunk->Activate(Coord, PlanetCenter, PlanetRadius);

				FChunkMeshData MeshData = APlanetChunk::BuildMeshData(
					Coord, PlanetCenter, PlanetRadius, VertsPerChunkEdge,
					GetSeaLevel(), GetRockStartHeight(), GetSnowStartHeight(), GetMaxHeight(), Noise, POIs, NoiseDebugView);

				Chunk->ApplyMeshData(MoveTemp(MeshData), Material);
				ActiveChunks.Add(Coord, Chunk);
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("PlanetGen: Editor mesh generated (%d chunks at depth %d, preset: %s)"),
		ActiveChunks.Num(), EditorDepth,
		Preset ? *Preset->GetName() : TEXT("Override"));
}

// ============================================================
// EDITOR LIFECYCLE
// ============================================================

void APlanetManager::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (!bGenerateOnConstruction) return;
	if (!ChunkClass || !ChunkMaterial) return;
	if (GetWorld() && GetWorld()->IsGameWorld()) return;

	RegeneratePlanet();
}

// ============================================================
// RUNTIME LIFECYCLE
// ============================================================

void APlanetManager::BeginPlay()
{
	Super::BeginPlay();

	Noise = MakeShared<FNoiseGenerator, ESPMode::ThreadSafe>(GetNoiseSettings());

	if (ChunkPool.NumTotal() == 0)
	{
		ChunkPool.Initialize(GetWorld(), PoolSize, ChunkClass, GetChunkFolderName());
	}

	Quadtree.Initialize(GetPlanetRadius(), GetMaxQuadtreeDepth(), GetLODSplitFactor(), bEnableCrossFaceLODBalancing);

	// Recreate MID at runtime -- editor MID doesn't carry over to PIE
	ChunkMID = nullptr;
	SetupMaterialInstance();

	// Apply runtime MID to any already-active editor-generated chunks
	if (ChunkMID)
	{
		UMaterialInterface* Material = static_cast<UMaterialInterface*>(ChunkMID);
		for (auto& Pair : ActiveChunks)
		{
			if (Pair.Value && Pair.Value->bReady)
			{
				Pair.Value->MeshComponent->SetMaterial(0, Material);
			}
		}
	}

	// Always respawn fresh for THIS world -- see full-package comment for why the
	// old "Num() == 0" conditional was unreliable (cross-world pointer inheritance).
	SpawnPOIBuildings();
}

void APlanetManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	if (!PC || !PC->GetPawn()) return;

	UpdateStreaming(PC->GetPawn()->GetActorLocation());
}

// ============================================================
// CALLABLE FUNCTIONS
// ============================================================

void APlanetManager::ReinitializeNoise()
{
	Noise = MakeShared<FNoiseGenerator, ESPMode::ThreadSafe>(GetNoiseSettings());
	const FNoiseSettings& S = GetNoiseSettings();
	UE_LOG(LogTemp, Log, TEXT("PlanetGen: Noise reinitialized (Seed=%d, Freq=%.2f, Octaves=%d)"),
		S.Seed, S.Frequency, S.Octaves);
}

// ============================================================
// NAVIGATION (Phase 1)
// ============================================================

void APlanetManager::BakeNavigation()
{
	if (!Noise.IsValid())
	{
		Noise = MakeShared<FNoiseGenerator, ESPMode::ThreadSafe>(GetNoiseSettings());
	}

	NavGrid.Build(GetActorLocation(), GetPlanetRadius(), Noise, NavGridResolution, MaxWalkableSlopeDegrees);
	UE_LOG(LogTemp, Log, TEXT("PlanetGen: Navigation grid baked (%dx%d per face, MaxSlope=%.1f deg)"),
		NavGridResolution, NavGridResolution, MaxWalkableSlopeDegrees);
}

bool APlanetManager::FindPath(FVector Start, FVector End, TArray<FVector>& OutPath)
{
	if (!NavGrid.IsBuilt())
	{
		UE_LOG(LogTemp, Warning, TEXT("PlanetGen: FindPath called before navigation was baked -- call BakeNavigation first."));
		return false;
	}
	return NavGrid.FindPath(Start, End, OutPath);
}

// ============================================================
// STATIC POIs (Phase 1)
// ============================================================

void APlanetManager::BakePOIsFromMarkers()
{
#if WITH_EDITOR
	if (!GetWorld()) return;

	if (!Noise.IsValid())
	{
		Noise = MakeShared<FNoiseGenerator, ESPMode::ThreadSafe>(GetNoiseSettings());
	}

	const FVector PlanetCenter = GetActorLocation();
	TArray<FStaticPOI> NewPOIs;

	for (TObjectIterator<UPlanetPOIMarkerComponent> It; It; ++It)
	{
		UPlanetPOIMarkerComponent* Marker = *It;
		if (!IsValid(Marker) || Marker->GetWorld() != GetWorld()) continue;

		AActor* MarkerOwner = Marker->GetOwner();
		if (!MarkerOwner) continue;

		FStaticPOI POI;
		POI.Direction = (MarkerOwner->GetActorLocation() - PlanetCenter).GetSafeNormal();
		POI.FlattenRadius = Marker->FlattenRadius;
		POI.FlattenBlendDistance = Marker->FlattenBlendDistance;
		POI.BuildingClass = Marker->BuildingClass;
		POI.TargetHeight = Marker->bOverrideTargetHeight
			? Marker->ManualTargetHeight
			: Noise->SampleContinentHeight(POI.Direction);
		POI.Name = MarkerOwner->GetFName();
		NewPOIs.Add(POI);
	}

	POIs = MoveTemp(NewPOIs);
	UE_LOG(LogTemp, Log, TEXT("PlanetGen: Baked %d POI(s) from markers"), POIs.Num());
#endif
}

void APlanetManager::SpawnPOIBuildings()
{
	ClearSpawnedBuildings();
	if (!GetWorld()) return;

	const FVector PlanetCenter = GetActorLocation();
	const double Radius = GetPlanetRadius();

	for (const FStaticPOI& POI : POIs)
	{
		if (!POI.BuildingClass) continue;

		const FVector WorldPos = PlanetCenter + POI.Direction * (Radius + POI.TargetHeight);
		const FQuat AlignQuat = FQuat::FindBetweenNormals(FVector::UpVector, POI.Direction);

		FActorSpawnParameters SP;
		SP.ObjectFlags = RF_Transient;
		if (AActor* Building = GetWorld()->SpawnActor<AActor>(POI.BuildingClass, WorldPos, AlignQuat.Rotator(), SP))
		{
			SpawnedBuildings.Add(Building);
		}
	}
}

void APlanetManager::ClearSpawnedBuildings()
{
	for (AActor* A : SpawnedBuildings)
	{
		// Only destroy actors belonging to OUR world. A reference inherited via PIE
		// world duplication can point at the EDITOR world's building actor -- calling
		// Destroy() on that would delete it from the editor level. Just drop it.
		if (IsValid(A) && A->GetWorld() == GetWorld())
		{
			A->Destroy();
		}
	}
	SpawnedBuildings.Reset();
}

void APlanetManager::RegeneratePlanet()
{
	// Always rebuild the noise generator: it snapshots settings at construction, so
	// layer toggles / noise edits would otherwise need a separate ReinitializeNoise
	// click first. One button now does the whole loop; the separate button remains
	// for reinitializing without a rebuild.
	Noise = MakeShared<FNoiseGenerator, ESPMode::ThreadSafe>(GetNoiseSettings());

	// Amplitude budget check: layers are ADDITIVE, and BuildMeshData clamps the total
	// to +-MaxHeight -- exceeding the budget silently flat-tops the peaks.
	{
		// BuildMeshData clamps the final combined height to +-MaxHeight symmetrically,
		// so both directions need checking: mountains push the positive budget up,
		// canyons push the negative budget down (from an already-negative continent
		// trough, worst case).
		const FNoiseSettings& NS = GetNoiseSettings();
		// Plateaus are additive like mountains -- include in the same positive-direction
		// worst-case sum (their masks aren't mutually exclusive, so a point could in
		// principle sit inside both a mountain range and a plateau region at once).
		double PositiveBudget = NS.HeightScale;
		if (NS.Mountains.bEnabled) PositiveBudget += NS.Mountains.Height;
		if (NS.Plateaus.bEnabled) PositiveBudget += NS.Plateaus.Height;
		double NegativeBudget = NS.HeightScale;
		if (NS.Canyons.bEnabled) NegativeBudget += NS.Canyons.Depth;

		if (PositiveBudget > GetMaxHeight())
		{
			UE_LOG(LogTemp, Warning, TEXT("PlanetGen: peak amplitude budget (%.0f) exceeds MaxHeight (%.0f) -- mountain peaks will be clamped flat. Raise MaxHeight or lower Mountains.Height."),
				PositiveBudget, GetMaxHeight());
		}
		if (NegativeBudget > GetMaxHeight())
		{
			UE_LOG(LogTemp, Warning, TEXT("PlanetGen: carve amplitude budget (%.0f) exceeds MaxHeight (%.0f) -- deepest canyons will be clamped flat. Raise MaxHeight or lower Canyons.Depth."),
				NegativeBudget, GetMaxHeight());
		}
	}

	// Diagnostic log -- check output log to verify these match your intended settings.
	// If HeightScale=0 or Octaves=0, terrain will be a perfect sphere.
	const FNoiseSettings& NS = GetNoiseSettings();
	UE_LOG(LogTemp, Warning, TEXT("PlanetGen: Regenerating -- Preset=%s, HeightScale=%.0f, Frequency=%.2f, Octaves=%d, PlanetRadius=%.0f, SeaLevel=%.1f"),
		Preset ? *Preset->GetName() : TEXT("None(Override)"),
		NS.HeightScale, NS.Frequency, NS.Octaves,
		GetPlanetRadius(), GetSeaLevel());

	if (ChunkPool.NumTotal() == 0 && ChunkClass && GetWorld())
	{
		ChunkPool.Initialize(GetWorld(), PoolSize, ChunkClass, GetChunkFolderName());
	}

	PendingCoords.Reset();
	for (auto& Pair : ActiveChunks)
	{
		ChunkPool.Release(Pair.Value);
	}
	ActiveChunks.Reset();

	Quadtree.Initialize(GetPlanetRadius(), GetMaxQuadtreeDepth(), GetLODSplitFactor(), bEnableCrossFaceLODBalancing);

	const bool bIsEditor = GetWorld() && !GetWorld()->IsGameWorld();
	if (bIsEditor)
	{
		GenerateEditorMesh();
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("PlanetGen: Planet regeneration triggered"));
	}

	// Phase 1: always-spawned (no distance streaming yet).
	SpawnPOIBuildings();

	// Navigation is independent of chunks/mesh -- always rebake so it reflects current
	// noise settings (and, in the runtime branch, is ready before any AI needs it).
	BakeNavigation();
}

// ============================================================
// STREAMING
// ============================================================

void APlanetManager::UpdateStreaming(const FVector& ViewerLocation)
{
	const FVector PlanetCenter = GetActorLocation();
	const double PlanetRadius = GetPlanetRadius();
	const double CollisionRadius = GetCollisionRadius();

	Quadtree.UpdateLOD(ViewerLocation, PlanetCenter, CurrentLeaves);
	TSet<FPlanetChunkCoord> DesiredSet(CurrentLeaves);

	for (auto It = ActiveChunks.CreateIterator(); It; ++It)
	{
		if (!DesiredSet.Contains(It.Key()))
		{
			RequestUnload(It.Key());
			It.RemoveCurrent();
		}
	}

	// Throttle: crossing an LOD boundary at speed can otherwise dispatch hundreds of
	// async builds in one frame (game-thread hitch from actor activation + task spam).
	// Un-dispatched leaves simply load on the following ticks.
	constexpr int32 MaxLoadsPerTick = 16;
	int32 LoadsThisTick = 0;
	for (const FPlanetChunkCoord& Coord : CurrentLeaves)
	{
		if (ActiveChunks.Contains(Coord) || PendingCoords.Contains(Coord)) continue;
		RequestLoad(Coord);
		if (++LoadsThisTick >= MaxLoadsPerTick) break;
	}

	const int32 FinestDepth = GetMaxQuadtreeDepth();
	const double FoliageRadius = GetFoliageSettings().FoliageRadius;

	for (auto& Pair : ActiveChunks)
	{
		APlanetChunk* Chunk = Pair.Value;
		if (!IsValid(Chunk) || !Chunk->bReady) continue;
		const double Dist = FVector::Dist(Chunk->GetApproxWorldCenter(), ViewerLocation);
		Chunk->SetCollisionEnabled(Dist <= CollisionRadius);

		if (Chunk->GetCoord().Depth >= FinestDepth)
		{
			Chunk->SetFoliageEnabled(Dist <= FoliageRadius);
		}
	}
}

void APlanetManager::RequestLoad(const FPlanetChunkCoord& Coord)
{
	APlanetChunk* Chunk = ChunkPool.Acquire();
	if (!Chunk) return;

	const FVector PlanetCenter = GetActorLocation();
	const double Radius = GetPlanetRadius();

	Chunk->Activate(Coord, PlanetCenter, Radius);
	Chunk->bBuildInFlight = true;
	PendingCoords.Add(Coord);
	ActiveChunks.Add(Coord, Chunk);

	const double LocalSeaLevel = GetSeaLevel();
	const double LocalRockStart = GetRockStartHeight();
	const double LocalSnowStart = GetSnowStartHeight();
	const double LocalMaxHeight = GetMaxHeight();
	const int32 N = VertsPerChunkEdge;
	const ENoiseDebugView LocalDebugView = NoiseDebugView;
	const int32 LocalMaxQuadtreeDepth = GetMaxQuadtreeDepth();
	const FFoliageSettings LocalFoliageSettings = GetFoliageSettings();
	const TArray<FStaticPOI> LocalPOIs = POIs;
	TSharedPtr<FNoiseGenerator, ESPMode::ThreadSafe> NoiseRef = Noise;
	TWeakObjectPtr<APlanetChunk> WeakChunk = Chunk;
	TWeakObjectPtr<APlanetManager> WeakSelf = this;
	UMaterialInterface* Material = ChunkMID ? static_cast<UMaterialInterface*>(ChunkMID) : ChunkMaterial;

	UE::Tasks::Launch(UE_SOURCE_LOCATION, [Coord, PlanetCenter, Radius, N, LocalSeaLevel, LocalRockStart, LocalSnowStart, LocalMaxHeight, LocalDebugView, LocalMaxQuadtreeDepth, LocalFoliageSettings, LocalPOIs, NoiseRef, WeakChunk, WeakSelf, Material]()
	{
		FChunkMeshData MeshData = APlanetChunk::BuildMeshData(Coord, PlanetCenter, Radius, N,
			LocalSeaLevel, LocalRockStart, LocalSnowStart, LocalMaxHeight, NoiseRef, LocalPOIs, LocalDebugView);

		FFoliageInstanceData FoliageData;
		if (Coord.Depth >= LocalMaxQuadtreeDepth &&
		    (LocalFoliageSettings.Grass.bEnabled || LocalFoliageSettings.Rocks.bEnabled))
		{
			FoliageData = APlanetChunk::BuildFoliageData(MeshData, PlanetCenter, Radius, LocalFoliageSettings, Coord, LocalPOIs);
		}

		AsyncTask(ENamedThreads::GameThread, [WeakChunk, WeakSelf, Coord, MeshData = MoveTemp(MeshData), FoliageData = MoveTemp(FoliageData), Material, LocalFoliageSettings]() mutable
		{
			if (!WeakSelf.IsValid()) return;
			// CRITICAL: also verify the chunk is still assigned to THIS coord. Pooled
			// chunks get recycled -- if this chunk was unloaded and re-acquired for a
			// different coord while our build was in flight, IsValid+IsActive both pass
			// but applying would stamp a stale (often lower-LOD) mesh onto the new
			// coord's chunk. Symptom: low-LOD chunks stuck among high-LOD ones.
			if (!WeakChunk.IsValid() || !WeakChunk->IsActive() || !(WeakChunk->GetCoord() == Coord))
			{
				WeakSelf->PendingCoords.Remove(Coord);
				return;
			}
			WeakChunk->ApplyMeshData(MoveTemp(MeshData), Material);
			WeakChunk->SetFoliageMeshes(WeakSelf->GrassMesh, WeakSelf->RockMesh, LocalFoliageSettings);
			WeakChunk->CacheFoliageData(MoveTemp(FoliageData));
			WeakSelf->PendingCoords.Remove(Coord);
		});
	});
}

void APlanetManager::RequestUnload(const FPlanetChunkCoord& Coord)
{
	if (APlanetChunk** Found = ActiveChunks.Find(Coord))
	{
		ChunkPool.Release(*Found);
	}
	PendingCoords.Remove(Coord);
}
