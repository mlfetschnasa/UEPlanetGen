// PlanetTerrainSafetyComponent.h
//
// WHY THIS EXISTS: chunk collision uses bUseComplexAsSimpleCollision (the render mesh
// IS the collision mesh). Chaos/PhysX do not support CCD against complex/trimesh
// collision, and dynamic bodies resting/pushing against arbitrary concave triangle
// soup have unreliable depenetration -- a physics-simulated actor thrusting into
// terrain can register the initial hit (an abrupt stop) and then, as continued force
// overwhelms the solver's ability to resolve penetration against the concave mesh,
// tunnel straight through. This is a documented Chaos/PhysX limitation, not fixable
// via CCD flags or substep tuning alone.
//
// This component is a safety net that runs ALONGSIDE physics simulation (it does not
// replace it): every tick, after physics has moved the body, it independently sweeps
// from the actor's last known-safe position to its new position. If that sweep would
// hit terrain, the actor is snapped back to the last safe point along the sweep and
// the inward velocity component is removed -- regardless of what Chaos's own
// resolution did or failed to do that frame.
//
// USAGE: Add Component -> "Planet Terrain Safety" on your physics-simulated pawn
// (e.g. the spaceship). Leave TrackedPrimitive unset to auto-use the owner's root
// component. Set SweepRadius to roughly your ship's smallest collision dimension.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PlanetTerrainSafetyComponent.generated.h"

UCLASS(ClassGroup = (PlanetGen), meta = (BlueprintSpawnableComponent))
class PLANETGEN_API UPlanetTerrainSafetyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPlanetTerrainSafetyComponent();

	// Primitive to protect. Leave null to auto-use the owner actor's root component
	// (typical for a single-capsule/single-body ship).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet Terrain Safety")
	UPrimitiveComponent* TrackedPrimitive = nullptr;

	// Radius of the safety sweep sphere. Should be roughly the tracked primitive's
	// smallest collision dimension (e.g. a capsule's radius) -- too large clips
	// through tight terrain gaps at the wrong points, too small lets thin/fast
	// penetration slip past the sweep itself.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet Terrain Safety", meta = (ClampMin = "1.0"))
	double SweepRadius = 100.0;

	// Collision channel the sweep tests against. Matches the terrain's explicit
	// "BlockAll" profile (default Object Type WorldStatic) set on chunk mesh components.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet Terrain Safety")
	TEnumAsByte<ECollisionChannel> SweepChannel = ECC_WorldStatic;

	// true: remove only the velocity component driving into the surface (ship slides
	//       along terrain -- usually feels better for arcade flight).
	// false: zero all linear velocity on correction (hard stop).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet Terrain Safety")
	bool bSlideAlongSurface = true;

	// Small pull-back from the exact sweep hit point so the actor doesn't rest
	// exactly ON the surface (which can immediately re-trigger a hit next tick).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet Terrain Safety", meta = (ClampMin = "0.0"))
	double SkinWidth = 10.0;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	FVector LastSafeLocation = FVector::ZeroVector;
	bool bHasLastSafeLocation = false;

	UPrimitiveComponent* ResolveTrackedPrimitive() const;
};
