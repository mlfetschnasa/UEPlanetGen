// PlanetGlowShell.h
// Cheap Fresnel-rim "fake atmosphere" for any planet that ISN'T the currently-active one.
// UE only supports one real USkyAtmosphereComponent at a time, so every other planet shows
// this stand-in: invisible over the visible disc, glowing at the silhouette edge.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "PlanetMath.h"
#include "PlanetGlowShell.generated.h"

UCLASS()
class PLANETGEN_API APlanetGlowShell : public AActor
{
	GENERATED_BODY()

public:
	APlanetGlowShell();

	UPROPERTY(VisibleAnywhere)
	UProceduralMeshComponent* MeshComponent;

	// GlowShellScale: multiplier on PlanetRadius (e.g. 1.08 = 8% larger than the planet
	// surface), giving the Fresnel rim room to live without clipping into terrain.
	void GenerateGlowShell(const FVector& PlanetCenter, double PlanetRadius,
	                       double GlowShellScale, UMaterialInterface* GlowMaterial,
	                       int32 SubdivisionsPerFace = 48);

	// Drives this shell's visibility. Inverse of the real atmosphere's fade-in: full glow
	// in deep space, fades to 0 as the real Sky Atmosphere takes over up close.
	void SetGlowAlpha(float Alpha);

private:
	UPROPERTY()
	UMaterialInstanceDynamic* GlowMID = nullptr;
};
