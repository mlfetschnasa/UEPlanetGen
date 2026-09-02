// PlanetOceanShell.h
// Static, non-streamed sphere at PlanetRadius + SeaLevel. No chunk LOD, no seams to manage --
// water visual complexity lives in the shader (depth fade, Fresnel, panning normals), not mesh detail.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "PlanetMath.h"
#include "PlanetOceanShell.generated.h"

UCLASS()
class PLANETGEN_API APlanetOceanShell : public AActor
{
	GENERATED_BODY()

public:
	APlanetOceanShell();

	UPROPERTY(VisibleAnywhere)
	UProceduralMeshComponent* MeshComponent;

	// Builds the static ocean sphere once. Cheap enough (coarse subdivision) to run
	// synchronously on the game thread at BeginPlay -- unlike the 500+ streamed terrain
	// chunks, this is a handful of low-poly faces, no async needed.
	void GenerateOceanMesh(const FVector& PlanetCenter, double PlanetRadius, double SeaLevel,
	                       int32 SubdivisionsPerFace, UMaterialInterface* WaterMaterial);
};
