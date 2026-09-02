// PlanetMath.h
// Core cube-sphere parameterization: face UV -> cube point -> sphere point.
// Every other system (terrain chunks, ocean shell, glow shell, quadtree) builds on this.
#pragma once

#include "CoreMinimal.h"
#include "PlanetMath.generated.h"

UENUM(BlueprintType)
enum class EPlanetCubeFace : uint8
{
	PosX, NegX, PosY, NegY, PosZ, NegZ
};

// Which edge(s) of a face a quadtree node touches. Used for cross-face LOD balancing.
enum class EFaceEdge : uint8 { None, U_Pos, U_Neg, V_Pos, V_Neg };

// Describes how a face's shared edge maps onto its neighbor's UV space.
// Needs 3 independent bits to fully specify: which of the neighbor's two axes (U or V) is
// the one pinned to the shared edge, which end of that pinned axis (+1 or -1) the edge sits
// at, and which direction the neighbor's free axis runs relative to our own edge-sweep
// coordinate. bFixedAxisPositive and bReverseVarying are kept as two SEPARATE bits — an
// earlier version reused a single bFlipU/bFlipV bit for both roles, which conflated two
// independent choices and made several of the 24 face/edge pairs geometrically
// unrepresentable (confirmed by brute-force point-matching against FaceUVToCubePoint: every
// one of the 24 old table entries produced a mismatched point, several with no valid
// bFlipU/bFlipV combination existing at all). The table below was regenerated with this
// 3-bit encoding and verified to reproduce identical 3D points to FaceUVToCubePoint at
// densely-sampled points along all 24 edges. Still worth an in-PIE flight-test per
// Docs/03_SeamValidation.md before trusting it for the actual split-cascade behavior end to
// end — this only proves the coordinate transform itself is now correct.
struct FCubeNeighbor
{
	EPlanetCubeFace NeighborFace = EPlanetCubeFace::PosZ;
	bool bSwapUV = false;            // true: neighbor's fixed (edge) axis is V when our edge ran along U, or vice versa.
	bool bFixedAxisPositive = false; // true: the neighbor's fixed edge axis sits at +1.0 (else -1.0).
	bool bReverseVarying = false;    // true: neighbor's free axis runs opposite our shared edge coordinate.
};

namespace PlanetMath
{
	// Maps a 2D face-local UV in [-1, 1] to a 3D point ON THE CUBE (not yet normalized).
	PLANETGEN_API FVector FaceUVToCubePoint(EPlanetCubeFace Face, double U, double V);

	// Core sphere projection: normalize the cube point, scale by radius.
	// This is THE single function that turns "flat cube grid" into "sphere".
	FORCEINLINE FVector CubeToSphere(const FVector& CubePoint, double PlanetRadius)
	{
		return CubePoint.GetSafeNormal() * PlanetRadius;
	}

	// Convenience: face UV directly to world-space sphere point (pre-height, center-relative).
	FORCEINLINE FVector FaceUVToSpherePoint(EPlanetCubeFace Face, double U, double V, double PlanetRadius)
	{
		const FVector CubePoint = FaceUVToCubePoint(Face, U, V);
		return CubeToSphere(CubePoint, PlanetRadius);
	}

	// The "Dir" used everywhere downstream: surface normal AND height-offset direction.
	FORCEINLINE FVector GetSphereNormal(const FVector& CubePoint)
	{
		return CubePoint.GetSafeNormal();
	}

	// Snaps a UV coordinate to exactly -1.0 or 1.0 if within epsilon of the cube edge.
	// Call this on every vertex U/V before FaceUVToCubePoint (not just at chunk boundaries) —
	// guarantees bit-identical edge points across faces/depths/paths, eliminating
	// floating-point-drift seam cracks.
	FORCEINLINE double SnapToCubeEdge(double Value, double Epsilon = 1e-7)
	{
		if (FMath::IsNearlyEqual(Value, 1.0, Epsilon))  return 1.0;
		if (FMath::IsNearlyEqual(Value, -1.0, Epsilon)) return -1.0;
		return Value;
	}

	// Returns which face-edge(s) a quadtree node touches, given its UV center + half-extent.
	PLANETGEN_API void GetTouchingEdges(double CenterU, double CenterV, double HalfExtent,
	                                     TArray<EFaceEdge>& OutEdges);

	// Static topology table: which face/axis-mapping lies across a given face+edge.
	// See the note on FCubeNeighbor above for how this was derived/verified.
	PLANETGEN_API FCubeNeighbor GetCubeNeighbor(EPlanetCubeFace Face, EFaceEdge Edge);

	// Inverse of FaceUVToCubePoint: given a unit-length direction (e.g. from
	// (WorldPos - PlanetCenter).GetSafeNormal()), returns which cube face it falls on
	// and its local UV. Used by anything going from an arbitrary world position back
	// to grid coordinates (e.g. FPlanetNavGrid's nearest-node lookup).
	PLANETGEN_API void SphereDirectionToFaceUV(const FVector& Dir, EPlanetCubeFace& OutFace, double& OutU, double& OutV);

	// Simple analytic "is this point underwater" check — use this instead of raycasting
	// against the ocean shell mesh for buoyancy/swim-state, it's exactly as accurate
	// (the ocean mesh IS this sphere) and far cheaper.
	// SeaLevel is in meters (matching noise height output); PlanetRadius is in cm.
	FORCEINLINE bool IsPointUnderwater(const FVector& WorldPos, const FVector& PlanetCenter,
	                                    double PlanetRadius, double SeaLevel)
	{
		return FVector::Dist(WorldPos, PlanetCenter) < (PlanetRadius + SeaLevel * 100.0);
	}
}
