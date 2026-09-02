// PlanetManager.h
// Top-level orchestrator for a single planet. Configuration is driven by a UPlanetPreset
// data asset -- assign one via the Preset dropdown. Per-instance properties (materials,
// chunk class, pool size, ocean/glow assets) remain here.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PlanetChunkPool.h"
#include "PlanetQuadtree.h"
#include "NoiseGenerator.h"
#include "PlanetAtmosphereSettings.h"
#include "FoliageSettings.h"
#include "PlanetPOI.h"
#include "PlanetNavGrid.h"
#include "PlanetPreset.h"
#include "Tasks/Task.h"
#include "PlanetManager.generated.h"

class APlanetChunk;
class APlanetOceanShell;
class APlanetGlowShell;

UCLASS()
class PLANETGEN_API APlanetManager : public AActor
{
	GENERATED_BODY()

public:
	APlanetManager();

	// --- Preset ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	UPlanetPreset* Preset = nullptr;

	// --- Per-instance properties ---
	UPROPERTY(EditAnywhere, Category = "Planet") TSubclassOf<APlanetChunk> ChunkClass;
	UPROPERTY(EditAnywhere, Category = "Planet") UMaterialInterface* ChunkMaterial;
	UPROPERTY(EditAnywhere, Category = "Planet|Streaming") int32 PoolSize = 500;
	UPROPERTY(EditAnywhere, Category = "Planet|Streaming") int32 VertsPerChunkEdge = 33;
	UPROPERTY(EditAnywhere, Category = "Planet|LOD",
		meta = (ToolTip = "See Docs/SeamValidation.md before enabling."))
	bool bEnableCrossFaceLODBalancing = false;

	// --- Ocean ---
	UPROPERTY(EditAnywhere, Category = "Planet|Ocean") UMaterialInterface* WaterMaterial;
	UPROPERTY(EditAnywhere, Category = "Planet|Ocean") TSubclassOf<APlanetOceanShell> OceanShellClass;

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

	// --- Foliage (asset references -- per-instance, like ChunkMaterial/WaterMaterial;
	// density/gating tuning lives in FFoliageSettings via Preset/Override_FoliageSettings) ---
	UPROPERTY(EditAnywhere, Category = "Planet|Foliage") UStaticMesh* GrassMesh;
	UPROPERTY(EditAnywhere, Category = "Planet|Foliage") UStaticMesh* RockMesh;

	// --- Atmosphere / glow shell ---
	UPROPERTY(EditAnywhere, Category = "Planet|Atmosphere") FPlanetAtmosphereSettings AtmosphereSettings;
	UPROPERTY(EditAnywhere, Category = "Planet|Atmosphere") double GlowShellScale = 1.08;
	UPROPERTY(EditAnywhere, Category = "Planet|Atmosphere") int32 GlowShellSubdivisions = 48;
	UPROPERTY(EditAnywhere, Category = "Planet|Atmosphere") UMaterialInterface* GlowShellMaterial;
	UPROPERTY(EditAnywhere, Category = "Planet|Atmosphere") TSubclassOf<APlanetGlowShell> GlowShellClass;

	APlanetGlowShell* GetGlowShell() const { return GlowShell; }

	// --- Debug ---
	// Paints the selected noise signal into vertex color as grayscale (instead of
	// biome weights). Per-instance dev tool, deliberately NOT part of the preset.
	// Change value -> Regenerate Planet to see it.
	UPROPERTY(EditAnywhere, Category = "Planet|Debug")
	ENoiseDebugView NoiseDebugView = ENoiseDebugView::Off;

	// --- Editor Generation ---
	UPROPERTY(EditAnywhere, Category = "Planet|Editor") bool bGenerateOnConstruction = false;

	// --- Override properties (used when Preset is null) ---
	UPROPERTY(EditAnywhere, Category = "Planet|Overrides", meta = (EditCondition = "Preset == nullptr", EditConditionHides))
	double Override_PlanetRadius = 100000.0;
	UPROPERTY(EditAnywhere, Category = "Planet|Overrides", meta = (EditCondition = "Preset == nullptr", EditConditionHides))
	double Override_SeaLevel = 1.0;
	UPROPERTY(EditAnywhere, Category = "Planet|Overrides", meta = (EditCondition = "Preset == nullptr", EditConditionHides))
	FNoiseSettings Override_NoiseSettings;
	UPROPERTY(EditAnywhere, Category = "Planet|Overrides", meta = (EditCondition = "Preset == nullptr", EditConditionHides))
	double Override_RockStartHeight = 150.0;
	UPROPERTY(EditAnywhere, Category = "Planet|Overrides", meta = (EditCondition = "Preset == nullptr", EditConditionHides))
	double Override_SnowStartHeight = 380.0;
	UPROPERTY(EditAnywhere, Category = "Planet|Overrides", meta = (EditCondition = "Preset == nullptr", EditConditionHides))
	double Override_MaxHeight = 500.0;
	UPROPERTY(EditAnywhere, Category = "Planet|Overrides", meta = (EditCondition = "Preset == nullptr", EditConditionHides))
	double Override_GrassTilingScale = 0.01;
	UPROPERTY(EditAnywhere, Category = "Planet|Overrides", meta = (EditCondition = "Preset == nullptr", EditConditionHides))
	double Override_RockTilingScale = 0.006;
	UPROPERTY(EditAnywhere, Category = "Planet|Overrides", meta = (EditCondition = "Preset == nullptr", EditConditionHides))
	double Override_SnowTilingScale = 0.008;
	UPROPERTY(EditAnywhere, Category = "Planet|Overrides", meta = (EditCondition = "Preset == nullptr", EditConditionHides))
	double Override_TriplanarBlendSharpness = 4.0;
	UPROPERTY(EditAnywhere, Category = "Planet|Overrides", meta = (EditCondition = "Preset == nullptr", EditConditionHides))
	double Override_SlopeStartThreshold = 0.1;
	UPROPERTY(EditAnywhere, Category = "Planet|Overrides", meta = (EditCondition = "Preset == nullptr", EditConditionHides))
	double Override_SlopeEndThreshold = 0.9;
	UPROPERTY(EditAnywhere, Category = "Planet|Overrides", meta = (EditCondition = "Preset == nullptr", EditConditionHides))
	double Override_SlopeScale = 500000.0;
	UPROPERTY(EditAnywhere, Category = "Planet|Overrides", meta = (EditCondition = "Preset == nullptr", EditConditionHides))
	double Override_CollisionRadius = 500000.0;
	UPROPERTY(EditAnywhere, Category = "Planet|Overrides", meta = (EditCondition = "Preset == nullptr", EditConditionHides))
	int32 Override_MaxQuadtreeDepth = 7;
	UPROPERTY(EditAnywhere, Category = "Planet|Overrides", meta = (EditCondition = "Preset == nullptr", EditConditionHides))
	double Override_LODSplitFactor = 2.0;
	UPROPERTY(EditAnywhere, Category = "Planet|Overrides", meta = (EditCondition = "Preset == nullptr", EditConditionHides))
	int32 Override_EditorLODDepth = 3;
	UPROPERTY(EditAnywhere, Category = "Planet|Overrides", meta = (EditCondition = "Preset == nullptr", EditConditionHides))
	int32 Override_OceanSubdivisionsPerFace = 64;
	UPROPERTY(EditAnywhere, Category = "Planet|Overrides", meta = (EditCondition = "Preset == nullptr", EditConditionHides))
	FFoliageSettings Override_FoliageSettings;

	// --- Callable ---
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Planet") void RegeneratePlanet();
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Planet") void ReinitializeNoise();

	// --- Accessors ---
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

	UPROPERTY() UMaterialInstanceDynamic* ChunkMID = nullptr;
	UPROPERTY() APlanetOceanShell* OceanShell = nullptr;
	UPROPERTY() APlanetGlowShell* GlowShell = nullptr;

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
