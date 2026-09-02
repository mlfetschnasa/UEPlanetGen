// NoiseGenerator.h
// Self-contained, seeded, thread-safe 3D Perlin FBM noise. Sampling with a 3D unit-sphere
// direction (rather than 2D per-face UV) makes the noise seamless across all 6 cube faces
// for free -- there is no "UV" discontinuity in 3D space.
//
// LAYER SYSTEM: terrain is composed from a continent base plus optional, individually
// toggleable feature layers (mountains, and later canyons/plateaus following the same
// pattern). Every layer is a PURE function of the unit-sphere direction -- same input,
// same output, on any thread -- which is what keeps chunk boundaries seamless. Each
// layer is masked by its own cheap low-frequency noise, and the expensive detail noise
// is only evaluated inside the mask (early-out elsewhere).
#pragma once

#include "CoreMinimal.h"
#include "NoiseGenerator.generated.h"

// Debug visualization written into vertex color by BuildMeshData instead of biome
// weights. View by wiring Vertex Color -> Base Color in M_PlanetTerrain (grayscale
// also reads fine through the normal material). Change + Regenerate Planet to update.
UENUM(BlueprintType)
enum class ENoiseDebugView : uint8
{
	Off             UMETA(DisplayName = "Off (biome colors)"),
	ContinentHeight UMETA(DisplayName = "Continent Height"),
	MountainMask    UMETA(DisplayName = "Mountain Mask"),
	CanyonMask      UMETA(DisplayName = "Canyon Mask"),
	PlateauMask     UMETA(DisplayName = "Plateau Mask"),
	FinalHeight     UMETA(DisplayName = "Final Height"),
};

USTRUCT(BlueprintType)
struct FMountainLayerSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bEnabled = false;

	// Peak amplitude ADDED on top of the continent base, in the same units as
	// HeightScale. Budget: HeightScale + Height should stay <= the planet's MaxHeight
	// or the clamp in BuildMeshData flat-tops the peaks.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnabled"))
	double Height = 4000.0;

	// How many mountain-belt cells fit around the sphere. Lower = fewer, larger
	// contiguous ranges; higher = many small isolated ranges.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnabled"))
	double RangeFrequency = 1.5;

	// Fraction of the planet covered by ranges (0 = none, 1 = everywhere).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnabled", ClampMin = "0.0", ClampMax = "1.0"))
	double Coverage = 0.35;

	// Scale of individual ridges/peaks inside a range (higher = smaller, denser peaks).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnabled"))
	double RidgeFrequency = 8.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnabled", ClampMin = "1", ClampMax = "8"))
	int32 RidgeOctaves = 4;

	// Domain-warps the range mask so belts curve organically instead of forming blobs.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnabled", ClampMin = "0.0"))
	double RangeWarp = 0.4;
};

// Mirrors FMountainLayerSettings exactly, but SUBTRACTIVE: carves a branching canyon
// network down into the terrain instead of building ridges up. Uses the same
// ridged-noise technique (the zero-crossing network that forms mountain ridges is
// exactly the line pattern a canyon network should follow) -- just carved downward
// and shaped with a sharper wall profile. Canyons carved below SeaLevel flood
// automatically (the ocean shell and water vertex-color weight both key off height),
// turning deep networks into fjords/lake chains for free.
USTRUCT(BlueprintType)
struct FCanyonLayerSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bEnabled = false;

	// Carve depth SUBTRACTED from height, same units as HeightScale. Budget: the
	// deepest point (continent trough + full Depth) should stay within -MaxHeight,
	// or the clamp in BuildMeshData flat-bottoms the canyons.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnabled"))
	double Depth = 3000.0;

	// How many canyon-region cells fit around the sphere -- the "areas where canyon
	// noise gets added" knob. Lower = a few large canyon regions, higher = many
	// smaller ones scattered around the planet.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnabled"))
	double RegionFrequency = 2.0;

	// Fraction of the planet covered by canyon regions (0 = none, 1 = everywhere).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnabled", ClampMin = "0.0", ClampMax = "1.0"))
	double Coverage = 0.2;

	// Scale of individual channels within a canyon region (higher = smaller, denser
	// branching network).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnabled"))
	double ChannelFrequency = 10.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnabled", ClampMin = "1", ClampMax = "8"))
	int32 ChannelOctaves = 4;

	// Shapes the carve profile: higher = narrower, steeper-walled canyons (a tight
	// slot); lower = wider, gentler valleys. Applied as an exponent on the channel
	// network signal before it scales Depth.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnabled", ClampMin = "0.5", ClampMax = "8.0"))
	double WallSharpness = 3.0;

	// Domain-warps the region mask so canyon areas meander organically.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnabled", ClampMin = "0.0"))
	double RegionWarp = 0.4;
};

// Flat-topped mesa/butte terracing. Composed as its own broad elevation noise field,
// quantized into discrete flat steps, masked to specific regions -- same layer
// pattern as mountains/canyons. CanyonAffinity biases the region mask toward wherever
// SampleCanyonMask is already active, so plateaus cluster around canyon systems
// (matches real-world erosion geology: mesas/buttes are erosion-resistant remnants
// that commonly stand alongside the canyon networks that carved their surroundings
// away -- and incidentally keeps "desert" features visually grouped together).
USTRUCT(BlueprintType)
struct FPlateauLayerSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bEnabled = false;

	// Mesa/butte height ADDED on top of existing terrain, same units as HeightScale.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnabled"))
	double Height = 80000.0;

	// How many plateau-region cells fit around the sphere, independent of canyon
	// proximity -- this is the layer's OWN base likelihood before the canyon bias.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnabled"))
	double MaskFrequency = 5.0;

	// Fraction of the planet covered by plateaus from the independent mask alone
	// (0 = none, 1 = everywhere). Keep this low -- plateaus should read as an accent
	// feature, not a base terrain type; CanyonAffinity does the heavy lifting for
	// making them common specifically near canyons.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnabled", ClampMin = "0.0", ClampMax = "1.0"))
	double Coverage = 0.1;

	// How strongly canyon presence pulls plateaus into existing: 0 = fully
	// independent of canyons; higher values make plateaus overwhelmingly appear
	// wherever SampleCanyonMask is already active, clustering the two features
	// together. Has no effect if the Canyons layer is disabled (mask reads 0).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnabled", ClampMin = "0.0", ClampMax = "2.0"))
	double CanyonAffinity = 0.8;

	// Scale of the broad noise field defining each mesa's "tabletop" shape/extent
	// before terracing (higher = smaller, more numerous individual mesas).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnabled"))
	double ElevationFrequency = 7.0;

	// Number of discrete flat steps the elevation is quantized into (a classic
	// stair-stepped mesa profile). Higher = more, thinner terraces.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnabled", ClampMin = "1", ClampMax = "12"))
	int32 TerraceCount = 4;

	// How crisp the walls between terrace steps are: low = smooth ramps (terracing
	// barely visible), high = near-vertical cliff walls between flat tops.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnabled", ClampMin = "0.5", ClampMax = "16.0"))
	double EdgeSharpness = 5.0;

	// Domain-warps the plateau's own independent mask (not the canyon bias term).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnabled", ClampMin = "0.0"))
	double MaskWarp = 0.4;
};

USTRUCT(BlueprintType)
struct FNoiseSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Seed = 1337;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Octaves = 6;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) double Frequency = 0.8;     // applied to unit sphere point
	UPROPERTY(EditAnywhere, BlueprintReadWrite) double Lacunarity = 2.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) double Persistence = 0.5;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) double HeightScale = 500.0; // continent base amplitude

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FMountainLayerSettings Mountains;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FCanyonLayerSettings Canyons;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FPlateauLayerSettings Plateaus;
};

// Pure, stateless (after construction), thread-safe noise sampler.
// Safe to call any Sample*() from any worker thread once constructed.
class PLANETGEN_API FNoiseGenerator
{
public:
	explicit FNoiseGenerator(const FNoiseSettings& InSettings);

	// Final composed height (continent + enabled feature layers), in HeightScale units.
	// NOT clamped to a total budget here -- BuildMeshData clamps to +-MaxHeight.
	double SampleHeight(const FVector& UnitSpherePoint) const;

	// Individual layers, exposed for the vertex-color debug views.
	double SampleContinentHeight(const FVector& UnitSpherePoint) const; // clamped +-HeightScale
	double SampleMountainMask(const FVector& UnitSpherePoint) const;    // [0,1]
	double SampleCanyonMask(const FVector& UnitSpherePoint) const;      // [0,1]
	double SamplePlateauMask(const FVector& UnitSpherePoint) const;     // [0,1], biased toward canyons

private:
	double Simplex3D(double X, double Y, double Z) const;
	double Fade(double T) const { return T * T * T * (T * (T * 6.0 - 15.0) + 10.0); }
	double Grad(int32 Hash, double X, double Y, double Z) const;

	// Ridged multifractal in [0,1]: per octave 1-|perlin|, squared to sharpen crests.
	double RidgedFBM(const FVector& P, double Frequency, int32 Octaves) const;

	FNoiseSettings Settings;
	TArray<int32> Permutation; // 512-entry permutation table, built from Seed
};
