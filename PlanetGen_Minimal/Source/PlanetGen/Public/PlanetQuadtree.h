// PlanetQuadtree.h
// Per-face quadtree LOD: each of the 6 cube faces starts as a single root node and splits
// based on viewer distance. Only leaf nodes become actual streamed chunks. Includes
// cross-face depth balancing to prevent T-junction cracks at cube edges.
#pragma once

#include "CoreMinimal.h"
#include "PlanetMath.h"
#include "PlanetQuadtree.generated.h"

// Identifies a chunk uniquely: face + quadtree node position/size/depth.
USTRUCT(BlueprintType)
struct FPlanetChunkCoord
{
	GENERATED_BODY()

	UPROPERTY() EPlanetCubeFace Face = EPlanetCubeFace::PosZ;
	UPROPERTY() double CenterU = 0.0;    // node center, in face-local [-1, 1] space
	UPROPERTY() double CenterV = 0.0;
	UPROPERTY() double HalfExtent = 1.0; // node half-width, in same space
	UPROPERTY() int32 Depth = 0;         // 0 = whole face, increases per split

	// Quantization shared by operator== and GetTypeHash. They MUST use the identical
	// function: a tolerance-based == combined with a bucket-based hash lets two "equal"
	// coords land in different hash buckets, silently breaking TMap/TSet lookups
	// (symptom: chunks that never unload because Contains() can't find them).
	static FORCEINLINE int64 Quantize(double V) { return (int64)FMath::RoundToDouble(V * 1e9); }

	bool operator==(const FPlanetChunkCoord& Other) const
	{
		return Face == Other.Face && Depth == Other.Depth
			&& Quantize(CenterU) == Quantize(Other.CenterU)
			&& Quantize(CenterV) == Quantize(Other.CenterV);
	}
};

FORCEINLINE uint32 GetTypeHash(const FPlanetChunkCoord& C)
{
	// Quantize to 1e9 (not 1e6) -- at deeper LOD depths (10+) on larger planets,
	// CenterU/V values can differ by as little as 1e-7, which 1e6 quantization
	// maps to the same integer, causing hash collisions and incorrect TMap/TSet
	// lookups that leave parent chunks visible alongside their children.
	const int64 QU = FPlanetChunkCoord::Quantize(C.CenterU);
	const int64 QV = FPlanetChunkCoord::Quantize(C.CenterV);
	return HashCombine(HashCombine((uint32)C.Face, GetTypeHash(C.Depth)),
	                    HashCombine(GetTypeHash(QU), GetTypeHash(QV)));
}

// Plain data tree node -- no UObjects, cheap to walk every tick. Game-thread only;
// only leaf coords get dispatched to the async chunk build pipeline.
class PLANETGEN_API FPlanetQuadNode
{
public:
	FPlanetQuadNode(EPlanetCubeFace InFace, double InCenterU, double InCenterV, double InHalfExtent, int32 InDepth)
		: Face(InFace), CenterU(InCenterU), CenterV(InCenterV), HalfExtent(InHalfExtent), Depth(InDepth)
	{}

	EPlanetCubeFace Face;
	double CenterU, CenterV, HalfExtent;
	int32 Depth;

	// How many consecutive frames this non-leaf node has wanted to merge.
	// Merge is only allowed once this reaches MergeFrameThreshold, preventing
	// rapid split/merge thrashing when the viewer moves fast near chunk boundaries.
	int32 FramesWantingMerge = 0;
	static constexpr int32 MergeFrameThreshold = 10; // ~0.17s at 60fps

	TUniquePtr<FPlanetQuadNode> Children[4]; // null if leaf
	bool IsLeaf() const { return Children[0] == nullptr; }

	FPlanetChunkCoord ToCoord() const
	{
		FPlanetChunkCoord C;
		C.Face = Face; C.CenterU = CenterU; C.CenterV = CenterV;
		C.HalfExtent = HalfExtent; C.Depth = Depth;
		return C;
	}

	// Approximate world-space center position post sphere-projection. Used only for
	// LOD distance heuristics -- cheap, no height sampling.
	FVector GetApproxWorldPos(const FVector& PlanetCenter, double PlanetRadius) const
	{
		return PlanetCenter + PlanetMath::FaceUVToSpherePoint(Face, CenterU, CenterV, PlanetRadius);
	}

	// Rough world-space size of this node, for distance/size LOD ratio tests.
	double GetApproxWorldSize(double PlanetRadius) const
	{
		return HalfExtent * 2.0 * PlanetRadius;
	}

	void GetTouchingEdges(TArray<EFaceEdge>& OutEdges) const
	{
		PlanetMath::GetTouchingEdges(CenterU, CenterV, HalfExtent, OutEdges);
	}

	void Split()
	{
		if (!IsLeaf()) return;
		const double ChildHalf = HalfExtent * 0.5;
		const double Offsets[4][2] = { {-1,-1}, {1,-1}, {-1,1}, {1,1} };
		for (int32 i = 0; i < 4; ++i)
		{
			Children[i] = MakeUnique<FPlanetQuadNode>(
				Face,
				CenterU + Offsets[i][0] * ChildHalf,
				CenterV + Offsets[i][1] * ChildHalf,
				ChildHalf,
				Depth + 1);
		}
	}

	void Merge()
	{
		for (int32 i = 0; i < 4; ++i) Children[i].Reset();
	}
};

// Owns the 6 face roots and runs the per-frame LOD split/merge + cross-face balancing.
class PLANETGEN_API FPlanetQuadtree
{
public:
	// bEnableCrossFaceBalancing: see Docs/SeamValidation.md before enabling in production --
	// it depends on PlanetMath::GetCubeNeighbor, which needs visual validation first.
	// Skirts (handled in chunk mesh generation) work regardless of this setting and should
	// always stay on; this flag only controls the depth-balancing pass on top of skirts.
	void Initialize(double InPlanetRadius, int32 InMaxDepth, double InSplitFactor,
	                 bool bInEnableCrossFaceBalancing = false);

	// Walks all 6 trees, splitting/merging nodes based on viewer distance, then balances
	// cross-face/cross-tree depth mismatches. Returns the current leaf set as the load list.
	void UpdateLOD(const FVector& ViewerLocation, const FVector& PlanetCenter,
	               TArray<FPlanetChunkCoord>& OutLeaves);

private:
	TUniquePtr<FPlanetQuadNode> Roots[6];
	double PlanetRadius = 100000.0;
	int32 MaxDepth = 6;
	double SplitFactor = 2.0; // node splits when Distance < NodeSize * SplitFactor
	bool bEnableCrossFaceBalancing = false;

	void UpdateNode(FPlanetQuadNode* Node, const FVector& ViewerLocation, const FVector& PlanetCenter);
	void CollectLeaves(FPlanetQuadNode* Node, TArray<FPlanetChunkCoord>& OutLeaves) const;

	// Cross-face T-junction prevention (depth-clamped neighbors).
	void BalanceNode(FPlanetQuadNode* Node, bool& bOutAnySplit);
	int32 FindNeighborLeafDepth(FPlanetQuadNode* Node, EFaceEdge Edge);
	int32 DescendToLeafDepth(FPlanetQuadNode* Node, double U, double V);
	FPlanetQuadNode* GetRootForFace(EPlanetCubeFace Face);
	void ResolveNeighborUV(EPlanetCubeFace SourceFace, EFaceEdge SourceEdge, double SharedCoord,
	                        const FCubeNeighbor& Neighbor, double& OutU, double& OutV);
};
