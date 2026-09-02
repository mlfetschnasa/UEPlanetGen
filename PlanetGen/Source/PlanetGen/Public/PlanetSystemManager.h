// PlanetSystemManager.h
// Sits above individual APlanetManager instances (one per solar system / level). Picks the
// nearest planet (by radius-relative distance), reconfigures the shared atmosphere
// controller for it, and crossfades every planet's glow shell against the real atmosphere.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PlanetSystemManager.generated.h"

class APlanetManager;
class APlanetAtmosphereController;

UCLASS()
class PLANETGEN_API APlanetSystemManager : public AActor
{
	GENERATED_BODY()

public:
	APlanetSystemManager();

	UPROPERTY(EditAnywhere) TArray<APlanetManager*> Planets;
	UPROPERTY(EditAnywhere) APlanetAtmosphereController* AtmosphereController;

	// Distance (in multiples of planet radius) at which the real atmosphere starts fading in.
	UPROPERTY(EditAnywhere) double AtmosphereFadeStartRadiusMultiplier = 5.0;
	// Distance (in multiples of planet radius) at which it's fully visible.
	UPROPERTY(EditAnywhere) double AtmosphereFadeEndRadiusMultiplier = 2.0;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

private:
	TWeakObjectPtr<APlanetManager> ActivePlanet;
};
