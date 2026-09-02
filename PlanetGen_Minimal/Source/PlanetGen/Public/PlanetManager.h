// PlanetManager.h
// Per-planet controller. Configuration is driven by a UPlanetPreset data asset --
// create presets in the Content Browser (right-click -> Miscellaneous -> Data Asset
// -> UPlanetPreset) and assign via the Preset dropdown. Per-instance properties
// (materials, chunk class, pool size) remain here since they're not part of the
// planet's mathematical configuration.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PlanetChunkPool.h"
#include "PlanetQuadtree.h"
#include "NoiseGenerator.h"
#include "FoliageSettings.h"
#include "PlanetPOI.h"
#include "PlanetNavGrid.h"
#include "PlanetPreset.h"
#include "Tasks/Task.h"
#include "PlanetManager.generated.h"

class APlanetChunk;

UCLASS()
class PLANETGEN_API APlanetManager : public AActor
{
	GENERATED_BODY()

public:
	APlanetManager();

	// --- Preset (holds all planet config: noise, biomes, LOD, material params, etc.) ---
	// Assign a UPlanetPreset asset here. Leave null to use the override properties below.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	UPlanetPreset* Preset = nullptr;

	// --- Per-instance properties (not in preset -- specific to this manager instance) ---
	UPROPERTY(EditAnywhere, Category = "Planet")
	TSubclassOf<APlanetChunk> ChunkClass;

	UPROPERTY(EditAnywhere, Category = "Planet")
	UMaterialInterface* ChunkMaterial;

	// --- Foliage (asset references -- per-instance, like ChunkMaterial; density/gating
	// tuning lives in FFoliageSettings via Preset/Override_FoliageSettings) ---
	UPROPERTY(EditAnywhere, Category = "Planet|Foliage") UStaticMesh* GrassMesh;
	UPROPERTY(EditAnywhere, Category = "Planet|Foliage") UStaticMesh* RockMesh;

	// Pool size: how many chunk actors to pre-allocate. Must be >= 6 * 4^EditorLODDepth
	// for editor generation to work without exhausting the pool.
	UPROPERTY(EditAnywhere, Category = "Planet|Streaming")
	int32 PoolSize = 500;

	// Vertex density per chunk edge. Higher = more surface detail, more memory/CPU.
	UPROPERTY(EditAnywhere, Category = "Planet|Streaming")
	int32 VertsPerChunkEdge = 33;

	// Cross-face LOD balancing -- see Docs/SeamValidation.md before enabling.
	UPROPERTY(EditAnywhere, Category = "Planet|LOD",
		meta = (ToolTip = "See Docs/SeamValidation.md before enabling."))
	bool bEnableCrossFaceLODBalancing = false;

	// --- Navigation (Phase 1: custom grid built directly from noise -- see
	// PlanetNavGrid.h for why standard Recast navmesh doesn't work on a sphere).
	// Single-face pathfinding only; cross-face paths not yet supported.
	UPROPERTY(EditAnywhere, Category = "Planet|Navigation")
	int32 NavGridResolution = 64;

	UPROPERTY(EditAnywhere, Category = "Planet|Navigation")
	double MaxWalkableSlopeDegrees = 45.0;

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Planet|Navigation")
	void BakeNavigation();

	// Finds a path between two world-space points. Returns false (logged) if they're
	// on different cube faces (Phase 1 limitation) or no walkable route exists.
	UFUNCTION(BlueprintCallable, Category = "Planet|Navigation")
	bool FindPath(FVector Start, FVector End, TArray<FVector>& OutPath);

	// --- Static POIs (Phase 1: flatten mask + hand-placed markers + always-spawned
	// buildings, no distance-based streaming yet). Baked from UPlanetPOIMarkerComponent
	// actors placed in the level via the button below -- see PlanetPOI.h for workflow.
	UPROPERTY(EditAnywhere, Category = "Planet|POI")
	TArray<FStaticPOI> POIs;

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Planet|POI")
	void BakePOIsFromMarkers();

	// --- Debug ---
	// Paints the selected noise signal into vertex color as grayscale (instead of
	// biome weights). Per-instance dev tool, deliberately NOT part of the preset.
	// Change value -> Regenerate Planet to see it.
	UPROPERTY(EditAnywhere, Category = "Planet|Debug")
	ENoiseDebugView NoiseDebugView = ENoiseDebugView::Off;

	// --- Editor Generation ---
	UPROPERTY(EditAnywhere, Category = "Planet|Editor")
	bool bGenerateOnConstruction = false;

	// --- Callable functions ---
	// Regenerates the planet mesh from current Preset (or override properties).
	// In the editor: generates synchronously at EditorLODDepth.
	// In PIE: clears chunks so the streaming loop rebuilds on next tick.
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Planet")
	void RegeneratePlanet();

	// Reinitializes the noise generator with current settings, then call RegeneratePlanet.
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Planet")
	void ReinitializeNoise();

	// Convenience accessors -- read from Preset if set, else fall back to override values.
	// These are what all internal code should use rather than reading Preset directly.
	double GetPlanetRadius() const;
	double GetSeaLevel() const;
	const FNoiseSettings& GetNoiseSettings() const;
	double GetRockStartHeight() const;
	double GetSnowStartHeight() const;
	double GetMaxHeight() const;
	double GetGrassTilingScale() const;
	double GetRockTilingScale() const;
	double GetSnowTilingScale() const;
	double GetTriplanarBlendSharpness() const;
	double GetSlopeStartThreshold() const;
	double GetSlopeEndThreshold() const;
	double GetSlopeScale() const;
	double GetCollisionRadius() const;
	int32 GetMaxQuadtreeDepth() const;
	double GetLODSplitFactor() const;
	int32 GetEditorLODDepth() const;
	int32 GetOceanSubdivisionsPerFace() const;
	const FFoliageSettings& GetFoliageSettings() const;

	// --- Override properties (used when Preset is null) ---
	// These mirror UPlanetPreset fields exactly. When a Preset is assigned,
	// these are ignored -- the Preset values take precedence.
	UPROPERTY(EditAnywhere, Category = "Planet|Overrides",
		meta = (EditCondition = "Preset == nullptr", EditConditionHides))
	double Override_PlanetRadius = 100000.0;

	UPROPERTY(EditAnywhere, Category = "Planet|Overrides",
		meta = (EditCondition = "Preset == nullptr", EditConditionHides))
	double Override_SeaLevel = 1.0;

	UPROPERTY(EditAnywhere, Category = "Planet|Overrides",
		meta = (EditCondition = "Preset == nullptr", EditConditionHides))
	FNoiseSettings Override_NoiseSettings;

	UPROPERTY(EditAnywhere, Category = "Planet|Overrides",
		meta = (EditCondition = "Preset == nullptr", EditConditionHides))
	double Override_RockStartHeight = 150.0;

	UPROPERTY(EditAnywhere, Category = "Planet|Overrides",
		meta = (EditCondition = "Preset == nullptr", EditConditionHides))
	double Override_SnowStartHeight = 380.0;

	UPROPERTY(EditAnywhere, Category = "Planet|Overrides",
		meta = (EditCondition = "Preset == nullptr", EditConditionHides))
	double Override_MaxHeight = 500.0;

	UPROPERTY(EditAnywhere, Category = "Planet|Overrides",
		meta = (EditCondition = "Preset == nullptr", EditConditionHides))
	double Override_GrassTilingScale = 0.01;

	UPROPERTY(EditAnywhere, Category = "Planet|Overrides",
		meta = (EditCondition = "Preset == nullptr", EditConditionHides))
	double Override_RockTilingScale = 0.006;

	UPROPERTY(EditAnywhere, Category = "Planet|Overrides",
		meta = (EditCondition = "Preset == nullptr", EditConditionHides))
	double Override_SnowTilingScale = 0.008;

	UPROPERTY(EditAnywhere, Category = "Planet|Overrides",
		meta = (EditCondition = "Preset == nullptr", EditConditionHides))
	double Override_TriplanarBlendSharpness = 4.0;

	UPROPERTY(EditAnywhere, Category = "Planet|Overrides",
		meta = (EditCondition = "Preset == nullptr", EditConditionHides))
	double Override_SlopeStartThreshold = 0.1;

	UPROPERTY(EditAnywhere, Category = "Planet|Overrides",
		meta = (EditCondition = "Preset == nullptr", EditConditionHides))
	double Override_SlopeEndThreshold = 0.9;

	UPROPERTY(EditAnywhere, Category = "Planet|Overrides",
		meta = (EditCondition = "Preset == nullptr", EditConditionHides))
	double Override_SlopeScale = 500000.0;

	UPROPERTY(EditAnywhere, Category = "Planet|Overrides",
		meta = (EditCondition = "Preset == nullptr", EditConditionHides))
	double Override_CollisionRadius = 500000.0;

	UPROPERTY(EditAnywhere, Category = "Planet|Overrides",
		meta = (EditCondition = "Preset == nullptr", EditConditionHides))
	int32 Override_MaxQuadtreeDepth = 7;

	UPROPERTY(EditAnywhere, Category = "Planet|Overrides",
		meta = (EditCondition = "Preset == nullptr", EditConditionHides))
	double Override_LODSplitFactor = 2.0;

	UPROPERTY(EditAnywhere, Category = "Planet|Overrides",
		meta = (EditCondition = "Preset == nullptr", EditConditionHides))
	int32 Override_EditorLODDepth = 3;

	UPROPERTY(EditAnywhere, Category = "Planet|Overrides",
		meta = (EditCondition = "Preset == nullptr", EditConditionHides))
	int32 Override_OceanSubdivisionsPerFace = 64;

	UPROPERTY(EditAnywhere, Category = "Planet|Overrides",
		meta = (EditCondition = "Preset == nullptr", EditConditionHides))
	FFoliageSettings Override_FoliageSettings;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void OnConstruction(const FTransform& Transform) override;

private:
	FPlanetNavGrid NavGrid;

	UPROPERTY()
	TArray<AActor*> SpawnedBuildings;

	void SpawnPOIBuildings();
	void ClearSpawnedBuildings();

	FPlanetChunkPool ChunkPool;
	FPlanetQuadtree Quadtree;
	TSharedPtr<FNoiseGenerator, ESPMode::ThreadSafe> Noise;

	UPROPERTY()
	UMaterialInstanceDynamic* ChunkMID = nullptr;

	TMap<FPlanetChunkCoord, APlanetChunk*> ActiveChunks;
	TSet<FPlanetChunkCoord> PendingCoords;
	TArray<FPlanetChunkCoord> CurrentLeaves;

	FString GetChunkFolderName() const;
	void SetupMaterialInstance();
	void GenerateEditorMesh();
	void UpdateStreaming(const FVector& ViewerLocation);
	void RequestLoad(const FPlanetChunkCoord& Coord);
	void RequestUnload(const FPlanetChunkCoord& Coord);
};
