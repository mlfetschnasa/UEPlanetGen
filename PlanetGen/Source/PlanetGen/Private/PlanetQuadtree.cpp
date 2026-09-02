// PlanetQuadtree.cpp
#include "PlanetQuadtree.h"

void FPlanetQuadtree::Initialize(double InPlanetRadius, int32 InMaxDepth, double InSplitFactor,
                                  bool bInEnableCrossFaceBalancing)
{
	PlanetRadius = InPlanetRadius;
	MaxDepth = InMaxDepth;
	SplitFactor = InSplitFactor;
	bEnableCrossFaceBalancing = bInEnableCrossFaceBalancing;

	constexpr EPlanetCubeFace Faces[6] = {
		EPlanetCubeFace::PosX, EPlanetCubeFace::NegX, EPlanetCubeFace::PosY,
		EPlanetCubeFace::NegY, EPlanetCubeFace::PosZ, EPlanetCubeFace::NegZ };

	for (int32 i = 0; i < 6; ++i)
	{
		Roots[i] = MakeUnique<FPlanetQuadNode>(Faces[i], 0.0, 0.0, 1.0, 0);
	}
}

void FPlanetQuadtree::UpdateLOD(const FVector& ViewerLocation, const FVector& PlanetCenter,
                                 TArray<FPlanetChunkCoord>& OutLeaves)
{
	for (int32 i = 0; i < 6; ++i)
	{
		UpdateNode(Roots[i].Get(), ViewerLocation, PlanetCenter);
	}

	if (bEnableCrossFaceBalancing)
	{
		// Iterate a few passes since one split can cascade (a newly-split node's
		// neighbor may now also need splitting).
		for (int32 Pass = 0; Pass < 4; ++Pass)
		{
			bool bAnySplit = false;
			for (int32 i = 0; i < 6; ++i)
			{
				BalanceNode(Roots[i].Get(), bAnySplit);
			}
			if (!bAnySplit) break;
		}
	}

	OutLeaves.Reset();
	for (int32 i = 0; i < 6; ++i)
	{
		CollectLeaves(Roots[i].Get(), OutLeaves);
	}
}

void FPlanetQuadtree::UpdateNode(FPlanetQuadNode* Node, const FVector& ViewerLocation, const FVector& PlanetCenter)
{
	const FVector NodePos = Node->GetApproxWorldPos(PlanetCenter, PlanetRadius);
	const double NodeSize = Node->GetApproxWorldSize(PlanetRadius);
	const double Dist = FVector::Dist(NodePos, ViewerLocation);

	// Core LOD heuristic: split when the viewer is closer than (node size * factor).
	const bool bWantSplit = (Dist < NodeSize * SplitFactor) && (Node->Depth < MaxDepth);

	if (bWantSplit && Node->IsLeaf())
	{
		Node->FramesWantingMerge = 0;
		Node->Split();
	}
	else if (!bWantSplit && !Node->IsLeaf())
	{
		bool bAllChildrenAreLeaves = true;
		for (int32 i = 0; i < 4; ++i)
		{
			if (!Node->Children[i]->IsLeaf()) { bAllChildrenAreLeaves = false; break; }
		}
		const bool bComfortablyFar = Dist > NodeSize * SplitFactor * 1.6;

		if (bAllChildrenAreLeaves && bComfortablyFar)
		{
			Node->FramesWantingMerge++;
			if (Node->FramesWantingMerge >= FPlanetQuadNode::MergeFrameThreshold)
			{
				Node->FramesWantingMerge = 0;
				Node->Merge();
			}
		}
	}
	else if (bWantSplit && !Node->IsLeaf())
	{
		Node->FramesWantingMerge = 0;
	}

	if (!Node->IsLeaf())
	{
		for (int32 i = 0; i < 4; ++i)
		{
			UpdateNode(Node->Children[i].Get(), ViewerLocation, PlanetCenter);
		}
	}
}

void FPlanetQuadtree::CollectLeaves(FPlanetQuadNode* Node, TArray<FPlanetChunkCoord>& OutLeaves) const
{
	if (Node->IsLeaf())
	{
		OutLeaves.Add(Node->ToCoord());
	}
	else
	{
		for (int32 i = 0; i < 4; ++i)
		{
			CollectLeaves(Node->Children[i].Get(), OutLeaves);
		}
	}
}

void FPlanetQuadtree::BalanceNode(FPlanetQuadNode* Node, bool& bOutAnySplit)
{
	if (Node->IsLeaf())
	{
		TArray<EFaceEdge> Edges;
		Node->GetTouchingEdges(Edges);
		for (EFaceEdge Edge : Edges)
		{
			const int32 NeighborDepth = FindNeighborLeafDepth(Node, Edge);
			if (NeighborDepth > Node->Depth + 1)
			{
				Node->Split();
				bOutAnySplit = true;
				break;
			}
		}
	}
	else
	{
		for (int32 i = 0; i < 4; ++i)
		{
			BalanceNode(Node->Children[i].Get(), bOutAnySplit);
		}
	}
}

int32 FPlanetQuadtree::FindNeighborLeafDepth(FPlanetQuadNode* Node, EFaceEdge Edge)
{
	const FCubeNeighbor Neighbor = PlanetMath::GetCubeNeighbor(Node->Face, Edge);
	FPlanetQuadNode* NeighborRoot = GetRootForFace(Neighbor.NeighborFace);
	if (!NeighborRoot) return -1;

	const double SharedCoord = (Edge == EFaceEdge::U_Pos || Edge == EFaceEdge::U_Neg)
		? Node->CenterV : Node->CenterU;

	double NeighborU, NeighborV;
	ResolveNeighborUV(Node->Face, Edge, SharedCoord, Neighbor, NeighborU, NeighborV);

	return DescendToLeafDepth(NeighborRoot, NeighborU, NeighborV);
}

int32 FPlanetQuadtree::DescendToLeafDepth(FPlanetQuadNode* Node, double U, double V)
{
	if (Node->IsLeaf()) return Node->Depth;
	for (int32 i = 0; i < 4; ++i)
	{
		FPlanetQuadNode* Child = Node->Children[i].Get();
		if (FMath::Abs(U - Child->CenterU) <= Child->HalfExtent + 1e-9 &&
		    FMath::Abs(V - Child->CenterV) <= Child->HalfExtent + 1e-9)
		{
			return DescendToLeafDepth(Child, U, V);
		}
	}
	return Node->Depth; // fallback, shouldn't hit
}

FPlanetQuadNode* FPlanetQuadtree::GetRootForFace(EPlanetCubeFace Face)
{
	for (int32 i = 0; i < 6; ++i)
	{
		if (Roots[i]->Face == Face) return Roots[i].Get();
	}
	return nullptr;
}

void FPlanetQuadtree::ResolveNeighborUV(EPlanetCubeFace SourceFace, EFaceEdge SourceEdge, double SharedCoord,
                                         const FCubeNeighbor& Neighbor, double& OutU, double& OutV)
{
	double MatchingAxisValue; // the neighbor's fixed-edge coordinate (+-1.0)
	switch (SourceEdge)
	{
		case EFaceEdge::U_Pos: MatchingAxisValue = Neighbor.bFlipU ? 1.0 : -1.0; break;
		case EFaceEdge::U_Neg: MatchingAxisValue = Neighbor.bFlipU ? -1.0 : 1.0; break;
		case EFaceEdge::V_Pos: MatchingAxisValue = Neighbor.bFlipV ? 1.0 : -1.0; break;
		default:               MatchingAxisValue = Neighbor.bFlipV ? -1.0 : 1.0; break;
	}
	const double VaryingAxisValue = (Neighbor.bFlipU || Neighbor.bFlipV) ? -SharedCoord : SharedCoord;

	const bool bSourceWasUEdge = (SourceEdge == EFaceEdge::U_Pos || SourceEdge == EFaceEdge::U_Neg);
	const bool bNeighborUIsFixed = Neighbor.bSwapUV ? !bSourceWasUEdge : bSourceWasUEdge;

	if (bNeighborUIsFixed) { OutU = MatchingAxisValue; OutV = VaryingAxisValue; }
	else                   { OutU = VaryingAxisValue;  OutV = MatchingAxisValue; }
}
