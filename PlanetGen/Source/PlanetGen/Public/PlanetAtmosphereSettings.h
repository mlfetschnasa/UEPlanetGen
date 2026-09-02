// PlanetAtmosphereSettings.h
// Per-planet atmosphere/cloud configuration data. Consumed by APlanetAtmosphereController
// when a planet becomes the "active" one (see APlanetSystemManager), and by each
// APlanetManager's own glow shell while inactive.
#pragma once

#include "CoreMinimal.h"
#include "PlanetAtmosphereSettings.generated.h"

USTRUCT(BlueprintType)
struct FPlanetAtmosphereSettings
{
	GENERATED_BODY()

	// --- Scale (derived from PlanetRadius at runtime; exposed for non-Earth-like overrides) ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	double AtmosphereHeightRatio = 0.015; // atmosphere height as a fraction of planet radius
	                                       // (Earth: ~100km / 6360km ~= 0.0157)

	// --- Scattering (map directly to USkyAtmosphereComponent properties) ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FLinearColor RayleighScattering = FLinearColor(0.0058f, 0.0135f, 0.0331f, 1.0f); // Earth-blue default
	UPROPERTY(EditAnywhere, BlueprintReadWrite) double RayleighExponentialDistribution = 8.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) double MieScatteringScale = 0.004;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) double MieAbsorptionScale = 0.0044;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) double MieExponentialDistribution = 1.2;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) double MieAnisotropy = 0.8; // forward-scatter sun-glow tightness
	// MultiScatteringFactor on USkyAtmosphereComponent is a scalar (0=off, 1=full dual-scattering
	// bounce contribution), NOT a tint color -- it controls intensity, not hue.
	UPROPERTY(EditAnywhere, BlueprintReadWrite) double MultiScatteringFactor = 1.0;

	// --- Look/feel ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FLinearColor HorizonTintOverride = FLinearColor(0.7f, 0.85f, 1.0f, 1.0f); // also seeds the fake glow shell color

	// --- Clouds ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bHasClouds = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) double CloudLayerBottomKm = 2.0; // pre-scale; rescaled by AtmosphereHeightRatio
	UPROPERTY(EditAnywhere, BlueprintReadWrite) double CloudLayerHeightKm = 6.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) double CloudCoverage = 0.5;      // 0=clear, 1=overcast
	UPROPERTY(EditAnywhere, BlueprintReadWrite) double CloudDensityScale = 1.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	class UMaterialInterface* CloudMaterial = nullptr; // Volume-domain material, see Docs/MaterialSetup.md
};
