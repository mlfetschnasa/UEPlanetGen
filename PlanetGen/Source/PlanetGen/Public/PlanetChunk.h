// PlanetChunk.h
// A single streamed terrain chunk. Phase 1 (BuildMeshData) is pure math, safe to run on a
// background task. Phase 2 (ApplyMeshData) touches UE objects and must run on the game thread.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "PlanetMath.h"
#include "PlanetQuadtree.h"
#include "NoiseGenerator.h" // FNoiseGenerator + ENoiseDebugView (BuildMeshData param)
#include "FoliageSettings.h"
#include "PlanetPOI.h"
#include "PlanetChunk.generated.h"

class UHierarchicalInstancedStaticMeshComponent;

// Plain-data result of Phase 1 -- safe to build off-thread, no UObject access inside.
struct FChunkMeshData
{
	TArray<FVector>   Vertices;
	TArray<int32>     Triangles;
	TArray<FVector>   Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> VertexColors; // R=water, G=grass, B=rock weight; A=slope (0=flat,1=vertical)
	TArray<FProcMeshTangent> Tangents;
	bool bValid = false;
};

// Plain-data result of foliage placement -- also safe to build off-thread. World-space
// transforms (matching the chunk's absolute-world-space vertex convention), ready to
// hand a HISM component via AddInstances(..., bWorldSpace=true).
struct FFoliageInstanceData
{
	TArray<FTransform> GrassTransforms;
	TArray<FTransform> RockTransforms;
	bool bValid = false;
};

UCLASS()
class PLANETGEN_API APlanetChunk : public AActor
{
	GENERATED_BODY()

public:
	APlanetChunk();

	UPROPERTY(VisibleAnywhere)
	UProceduralMeshComponent* MeshComponent;

	UPROPERTY(VisibleAnywhere)
	UHierarchicalInstancedStaticMeshComponent* GrassHISM;

	UPROPERTY(VisibleAnywhere)
	UHierarchicalInstancedStaticMeshComponent* RockHISM;

	// --- Pool lifecycle ---
	void Activate(const FPlanetChunkCoord& InCoord, const FVector& PlanetCenter, double PlanetRadius);
	void Deactivate(); // returns to pool, hides, clears mesh sections + cached data

	// Phase 1: pure math, intended to be called from a background task. No UObject access.
	static FChunkMeshData BuildMeshData(
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
		ENoiseDebugView DebugView = ENoiseDebugView::Off);

	// Phase 2: must run on the game thread -- uploads to UProceduralMeshComponent,
	// assigns material, marks chunk as ready. Collision is NOT created here; see
	// SetCollisionEnabled (Collision LOD is a separate, tighter-radius concern).
	// Takes Data BY VALUE so callers can MoveTemp() their buffers in -- avoids copying
	// ~100KB+ of vertex arrays per chunk (previously copied twice: into the lambda
	// capture and again into CachedMeshData).
	void ApplyMeshData(FChunkMeshData Data, UMaterialInterface* MaterialInstance);

	// Pure math -- scatters candidate foliage instances across the chunk's ALREADY-BUILT
	// mesh (reuses vertex positions/normals/vertex-color biome weights rather than
	// resampling noise), gated by each type's MinBiomeWeight/slope range. Deterministic:
	// seeded from the chunk coord's hash, so re-running on the same coord (e.g. after
	// pool recycling) always produces identical placement -- no data needs saving.
	// Safe to call from a background task immediately after BuildMeshData.
	static FFoliageInstanceData BuildFoliageData(
		const FChunkMeshData& MeshData,
		const FVector& PlanetCenter,
		double PlanetRadius,
		const FFoliageSettings& Settings,
		const FPlanetChunkCoord& Coord,
		const TArray<FStaticPOI>& POIs);

	// Game thread. Assigns the source meshes + HISM cull distances. Cheap to call
	// repeatedly -- no-ops if the mesh is already assigned.
	void SetFoliageMeshes(UStaticMesh* GrassMesh, UStaticMesh* RockMesh, const FFoliageSettings& Settings);

	// Stores Phase-1 foliage data for later enable/disable without recomputation
	// (mirrors CachedMeshData's role for collision toggling).
	void CacheFoliageData(FFoliageInstanceData Data) { CachedFoliageData = MoveTemp(Data); }

	// Populates (true) or clears (false) the HISM components from CachedFoliageData.
	// No-op if already in the requested state, or if no foliage data has been cached yet.
	void SetFoliageEnabled(bool bEnabled);

	// Toggles collision on/off on the already-built mesh. Re-enabling re-cooks using the
	// cached Phase-1 arrays (no rebuild needed); disabling just flips the collision flag.
	void SetCollisionEnabled(bool bEnabled);
	bool HasCollisionEnabled() const { return bCollisionEnabled; }

	bool IsActive() const { return bActive; }
	const FPlanetChunkCoord& GetCoord() const { return Coord; }

	// Actual world-space center of this chunk's surface patch. Use this for distance
	// checks (e.g. Collision LOD) instead of GetActorLocation() -- the actor's own
	// transform is always FVector::ZeroVector, since chunk mesh vertices are baked as
	// absolute world-space positions directly (see BuildMeshData), not relative to an
	// actor transform.
	FVector GetApproxWorldCenter() const { return ApproxWorldCenter; }

	bool bBuildInFlight = false;
	bool bReady = false;

private:
	FPlanetChunkCoord Coord;
	bool bActive = false;
	bool bCollisionEnabled = false;
	bool bFoliageEnabled = false;
	FVector ApproxWorldCenter = FVector::ZeroVector;
	FChunkMeshData CachedMeshData; // retained so collision can be toggled without a rebuild
	FFoliageInstanceData CachedFoliageData; // retained so foliage can be toggled without a rebuild
};
