// PlanetAtmosphereController.h
// Owns the ONE active USkyAtmosphereComponent/UVolumetricCloudComponent in the world and
// gets reconfigured to match whichever planet is currently nearest the player. Also drives
// the space-to-surface fade. Every other planet falls back to its own APlanetGlowShell.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "PlanetAtmosphereSettings.h"
#include "PlanetAtmosphereController.generated.h"

class APlanetManager;

UCLASS()
class PLANETGEN_API APlanetAtmosphereController : public AActor
{
	GENERATED_BODY()

public:
	APlanetAtmosphereController();

	UPROPERTY(VisibleAnywhere) USkyAtmosphereComponent* SkyAtmosphere;
	UPROPERTY(VisibleAnywhere) UVolumetricCloudComponent* VolumetricCloud;
	UPROPERTY(VisibleAnywhere) UExponentialHeightFogComponent* HeightFog;

	// Reconfigures every atmosphere/cloud parameter to match the given planet's radius
	// and FPlanetAtmosphereSettings. Also repositions this actor to the planet's center.
	void ConfigureForPlanet(const APlanetManager* Planet);

	// Smoothly fades the real atmosphere/clouds in/out during the space<->surface transition.
	// 0 = fully faded out (deep space), 1 = fully visible (near/on the surface).
	void SetTransitionAlpha(float Alpha);

private:
	TWeakObjectPtr<const APlanetManager> CurrentPlanet;

	UPROPERTY()
	UMaterialInstanceDynamic* CloudMID = nullptr;
};
