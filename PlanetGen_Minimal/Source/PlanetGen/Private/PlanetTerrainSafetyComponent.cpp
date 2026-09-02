// PlanetTerrainSafetyComponent.cpp
#include "PlanetTerrainSafetyComponent.h"
#include "PhysicsEngine/BodyInstance.h"

UPlanetTerrainSafetyComponent::UPlanetTerrainSafetyComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Run AFTER physics has integrated this frame's motion, so we're correcting the
	// actual post-physics position rather than racing ahead of / behind the solver.
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

UPrimitiveComponent* UPlanetTerrainSafetyComponent::ResolveTrackedPrimitive() const
{
	if (TrackedPrimitive) return TrackedPrimitive;
	if (AActor* Owner = GetOwner())
	{
		return Cast<UPrimitiveComponent>(Owner->GetRootComponent());
	}
	return nullptr;
}

void UPlanetTerrainSafetyComponent::BeginPlay()
{
	Super::BeginPlay();

	if (UPrimitiveComponent* Prim = ResolveTrackedPrimitive())
	{
		LastSafeLocation = Prim->GetComponentLocation();
		bHasLastSafeLocation = true;
	}
}

void UPlanetTerrainSafetyComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UPrimitiveComponent* Prim = ResolveTrackedPrimitive();
	if (!Prim || !GetWorld()) return;

	const FVector CurrentLocation = Prim->GetComponentLocation();

	if (!bHasLastSafeLocation)
	{
		LastSafeLocation = CurrentLocation;
		bHasLastSafeLocation = true;
		return;
	}

	// Sweep is a no-op if physics didn't move the body this tick (Start == End),
	// which is fine -- it just confirms nothing to correct.
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PlanetTerrainSafetySweep), /*bTraceComplex=*/ true, GetOwner());
	// bTraceComplex = true is REQUIRED here: chunk collision is complex-as-simple, so
	// a sweep that only tests simple collision would silently miss the terrain entirely.

	FHitResult Hit;
	const bool bHit = GetWorld()->SweepSingleByChannel(
		Hit,
		LastSafeLocation,
		CurrentLocation,
		FQuat::Identity,
		SweepChannel,
		FCollisionShape::MakeSphere(SweepRadius),
		QueryParams);

	if (bHit)
	{
		// Snap back to the hit point, pulled back slightly along the surface normal
		// so the actor doesn't rest exactly on the boundary (which can immediately
		// re-trigger a hit next tick from floating point jitter alone).
		const FVector SafeLocation = Hit.Location + Hit.Normal * SkinWidth;

		Prim->GetOwner()->SetActorLocation(SafeLocation, /*bSweep=*/ false, nullptr, ETeleportType::TeleportPhysics);
		// TeleportPhysics: moves the simulated body's transform without the physics
		// engine treating this as a sudden implicit velocity change / triggering odd
		// collision resolution artifacts on the correction itself.

		if (Prim->IsSimulatingPhysics())
		{
			const FVector Velocity = Prim->GetPhysicsLinearVelocity();
			if (bSlideAlongSurface)
			{
				// Remove only the component of velocity driving INTO the surface;
				// preserve tangential velocity so the ship slides along terrain
				// rather than stopping dead -- generally feels better for flight.
				const double InwardSpeed = FVector::DotProduct(Velocity, Hit.Normal);
				if (InwardSpeed < 0.0) // moving into the surface
				{
					Prim->SetPhysicsLinearVelocity(Velocity - Hit.Normal * InwardSpeed);
				}
			}
			else
			{
				Prim->SetPhysicsLinearVelocity(FVector::ZeroVector);
			}
		}

		LastSafeLocation = SafeLocation;
	}
	else
	{
		LastSafeLocation = CurrentLocation;
	}
}
