// PlanetPOI.h
// Phase 1 static-object support: hand-placed building sites that FLATTEN the terrain
// around themselves (rather than just being oriented to sit on whatever height the
// noise happens to produce there). This is deliberately separate from FNoiseSettings/
// FNoiseGenerator -- POIs are authored/generated content layered ON TOP of procedural
// noise, not noise themselves.
//
// WORKFLOW: place any actor in the level (e.g. the building Blueprint itself) roughly
// where you want it on the planet's surface, add a UPlanetPOIMarkerComponent to it,
// tune FlattenRadius/BlendDistance/BuildingClass, then click "Bake POIs From Markers"
// on the PlanetManager. This scans the level for markers, converts each one's world
// position to a direction relative to the planet's center, and writes FStaticPOI
// entries. The actual gameplay building is spawned FRESH at the exact, correctly
// flattened position/orientation when the planet regenerates -- not the (possibly
// imprecisely placed) marker actor itself.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PlanetPOI.generated.h"

USTRUCT(BlueprintType)
struct FStaticPOI
{
	GENERATED_BODY()

	// Unit-sphere direction from the planet's center -- the "where" on the planet.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector Direction = FVector::UpVector;

	// Terrain height is force-flattened to TargetHeight within this radius (world units).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	double FlattenRadius = 3000.0;

	// Width of the smooth transition band OUTSIDE FlattenRadius, blending back to
	// natural procedural height. Total influence radius is FlattenRadius + this.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	double FlattenBlendDistance = 1500.0;

	// The elevation (same units as HeightScale) the ground is flattened to. Typically
	// auto-computed at bake time from the CONTINENT height only (ignoring mountains/
	// canyons/plateaus), so buildings land on a plausible base elevation rather than
	// wherever a feature layer happened to be at that exact point.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	double TargetHeight = 0.0;

	// Building Blueprint to spawn here. Left null = flatten the ground but spawn nothing
	// (useful for a landing pad or clearing with no fixed structure).
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSubclassOf<AActor> BuildingClass;

	// Bookkeeping only (which marker this came from) -- not used by placement logic.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName Name;
};

// Attach to any actor placed in the level to mark it as a building site. Tune the
// properties below, then click "Bake POIs From Markers" on the PlanetManager -- the
// marker's WORLD POSITION is converted to a planet-relative direction; nothing else
// about the marker actor matters (it is not spawned, moved, or referenced further).
UCLASS(ClassGroup = (PlanetGen), meta = (BlueprintSpawnableComponent))
class PLANETGEN_API UPlanetPOIMarkerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet POI", meta = (ClampMin = "0.0"))
	double FlattenRadius = 3000.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet POI", meta = (ClampMin = "0.0"))
	double FlattenBlendDistance = 1500.0;

	// If false (default), TargetHeight is auto-sampled from the continent base height
	// at bake time (ignoring mountains/canyons/plateaus). If true, use ManualTargetHeight
	// instead -- useful for intentionally elevated/sunken sites.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet POI")
	bool bOverrideTargetHeight = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet POI", meta = (EditCondition = "bOverrideTargetHeight"))
	double ManualTargetHeight = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet POI")
	TSubclassOf<AActor> BuildingClass;
};
