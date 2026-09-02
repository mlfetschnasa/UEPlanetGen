// FoliageSettings.h
// Tunable parameters for per-chunk instanced foliage. Placement itself is computed in
// APlanetChunk::BuildFoliageData (PlanetChunk.h/.cpp) -- this file only holds the knobs.
//
// ARCHITECTURE: foliage rides the exact same lifecycle as terrain collision (see
// SetCollisionEnabled). Candidate instance transforms are computed once, off-thread,
// alongside BuildMeshData (pure function of the chunk's own mesh data -- no extra noise
// sampling needed), then cached on the chunk. A HISM (Hierarchical Instanced Static Mesh)
// component per foliage type is populated/cleared based on distance to the viewer, same
// as collision toggling. Foliage is only ever generated for chunks at the quadtree's
// finest LOD depth -- coarse, distant chunks never carry foliage data at all.
#pragma once

#include "CoreMinimal.h"
#include "FoliageSettings.generated.h"

USTRUCT(BlueprintType)
struct FFoliageTypeSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bEnabled = false;

	// Target instance count per square meter at full suitability. Chunk triangle area
	// (converted from UE units to m^2) times this value determines how many candidate
	// points are scattered on that triangle.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnabled", ClampMin = "0.0"))
	double DensityPerSqm = 0.5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnabled", ClampMin = "0.01"))
	double MinScale = 0.8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnabled", ClampMin = "0.01"))
	double MaxScale = 1.3;

	// Minimum biome suitability weight (0-1) required for placement. Grass reads the
	// terrain's Grass vertex-color weight, Rocks reads the Rock weight -- wired in
	// BuildFoliageData, not configured here.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnabled", ClampMin = "0.0", ClampMax = "1.0"))
	double MinBiomeWeight = 0.5;

	// Allowed slope range (0=flat, 1=vertical -- matches the terrain's existing slope
	// encoding in vertex color Alpha).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnabled", ClampMin = "0.0", ClampMax = "1.0"))
	double MinSlope = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnabled", ClampMin = "0.0", ClampMax = "1.0"))
	double MaxSlope = 1.0;

	// 0 = instance "up" is purely radial (planet-center-outward, ignores local mesh
	// bumps -- smoothest, most upright look). 1 = fully follows the local (possibly
	// bumpy) interpolated triangle normal. Same blend concept as the material's
	// VertexNormalWS/analytical-normal lerp.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnabled", ClampMin = "0.0", ClampMax = "1.0"))
	double NormalAlignment = 0.5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnabled"))
	bool bRandomYaw = true;

	// HISM built-in per-instance distance culling (UE units).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnabled", ClampMin = "0.0"))
	double CullStartDistance = 3000.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnabled", ClampMin = "0.0"))
	double CullEndDistance = 6000.0;
};

USTRUCT(BlueprintType)
struct FFoliageSettings
{
	GENERATED_BODY()

	// Chunks only populate their foliage HISMs within this world-space distance of the
	// viewer. Independent of (and typically much smaller than) terrain streaming/collision
	// radii -- foliage is a close-range detail layer.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	double FoliageRadius = 8000.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FFoliageTypeSettings Grass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FFoliageTypeSettings Rocks;
};
