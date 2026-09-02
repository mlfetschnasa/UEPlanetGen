// PlanetPreset.h
// A named, reusable planet configuration asset. Create these in the Content Browser
// (right-click -> Miscellaneous -> Data Asset -> UPlanetPreset) and assign them to
// APlanetManager instances via the "Preset" dropdown. Changing a preset automatically
// updates all planets using it when RegeneratePlanet() is called.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "NoiseGenerator.h"
#include "FoliageSettings.h"
#include "PlanetPreset.generated.h"

UCLASS(BlueprintType)
class PLANETGEN_API UPlanetPreset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// --- Planet scale ---
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Planet")
	double PlanetRadius = 100000.0; // UE units (cm); 100000 = 1km

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Planet")
	double SeaLevel = 1.0; // meters relative to PlanetRadius

	// --- Noise ---
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Noise")
	FNoiseSettings NoiseSettings;

	// --- Biome thresholds (meters) ---
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biomes")
	double RockStartHeight = 150.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biomes")
	double SnowStartHeight = 380.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biomes")
	double MaxHeight = 500.0; // must match NoiseSettings.HeightScale

	// --- Material tiling ---
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Material")
	double GrassTilingScale = 0.01;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Material")
	double RockTilingScale = 0.006;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Material")
	double SnowTilingScale = 0.008;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Material")
	double TriplanarBlendSharpness = 4.0;

	// --- Slope detection ---
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Material")
	double SlopeStartThreshold = 0.1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Material")
	double SlopeEndThreshold = 0.9;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Material")
	double SlopeScale = 500000.0;

	// --- Ocean ---
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean")
	int32 OceanSubdivisionsPerFace = 64;

	// --- LOD ---
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LOD")
	int32 MaxQuadtreeDepth = 7;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LOD")
	double LODSplitFactor = 2.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LOD")
	int32 EditorLODDepth = 3;

	// --- Collision ---
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision")
	double CollisionRadius = 500000.0;

	// --- Foliage ---
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foliage")
	FFoliageSettings Foliage;
};
