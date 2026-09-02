// PlanetNavGrid.cpp
#include "PlanetNavGrid.h"
#include "Algo/Reverse.h"

int32 FPlanetNavGrid::NodeIndex(EPlanetCubeFace Face, int32 X, int32 Y) const
{
	return (int32)Face * Resolution * Resolution + Y * Resolution + X;
}

void FPlanetNavGrid::Build(const FVector& InPlanetCenter, double InPlanetRadius,
	TSharedPtr<FNoiseGenerator, ESPMode::ThreadSafe> Noise, int32 InResolution, double MaxWalkableSlopeDegrees)
{
	Nodes.Reset();
	if (!Noise.IsValid() || InResolution < 2) return;

	PlanetCenter = InPlanetCenter;
	PlanetRadius = InPlanetRadius;
	Resolution = InResolution;

	constexpr EPlanetCubeFace Faces[6] = {
		EPlanetCubeFace::PosX, EPlanetCubeFace::NegX, EPlanetCubeFace::PosY,
		EPlanetCubeFace::NegY, EPlanetCubeFace::PosZ, EPlanetCubeFace::NegZ };

	Nodes.SetNum(6 * Resolution * Resolution);

	const double CellUV = 2.0 / (Resolution - 1); // grid spans [-1, 1] in Resolution steps

	// Pass 1: world positions (height-sampled). Needed before Pass 2 so slope can be
	// derived from ALREADY-COMPUTED neighbor positions rather than re-sampling noise.
	for (EPlanetCubeFace Face : Faces)
	{
		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				const double U = -1.0 + X * CellUV;
				const double V = -1.0 + Y * CellUV;
				const FVector Dir = PlanetMath::FaceUVToSpherePoint(Face, U, V, 1.0).GetSafeNormal();
				const double Height = Noise->SampleHeight(Dir);
				const FVector WorldPos = PlanetCenter + Dir * (PlanetRadius + Height);

				FNavGridNode& Node = Nodes[NodeIndex(Face, X, Y)];
				Node.WorldPosition = WorldPos;
				Node.bWalkable = false; // set in Pass 2
			}
		}
	}

	// Pass 2: slope from neighbor position differences -> walkability. Interior nodes
	// use central differences; edge nodes fall back to a one-sided difference (still a
	// perfectly reasonable local slope estimate, just using only the neighbor that exists).
	const double MaxSlopeCos = FMath::Cos(FMath::DegreesToRadians(MaxWalkableSlopeDegrees));

	for (EPlanetCubeFace Face : Faces)
	{
		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				FNavGridNode& Node = Nodes[NodeIndex(Face, X, Y)];

				const int32 XN = FMath::Clamp(X - 1, 0, Resolution - 1);
				const int32 XP = FMath::Clamp(X + 1, 0, Resolution - 1);
				const int32 YN = FMath::Clamp(Y - 1, 0, Resolution - 1);
				const int32 YP = FMath::Clamp(Y + 1, 0, Resolution - 1);

				const FVector& PosXN = Nodes[NodeIndex(Face, XN, Y)].WorldPosition;
				const FVector& PosXP = Nodes[NodeIndex(Face, XP, Y)].WorldPosition;
				const FVector& PosYN = Nodes[NodeIndex(Face, X, YN)].WorldPosition;
				const FVector& PosYP = Nodes[NodeIndex(Face, X, YP)].WorldPosition;

				const FVector TangentU = PosXP - PosXN;
				const FVector TangentV = PosYP - PosYN;
				const FVector EstimatedNormal = FVector::CrossProduct(TangentU, TangentV).GetSafeNormal();

				const FVector RadialDir = (Node.WorldPosition - PlanetCenter).GetSafeNormal();
				// Cross product winding can point the estimated normal either way depending
				// on grid orientation per face -- compare against the ABSOLUTE dot product,
				// since we only care about the ANGLE between estimated-normal and radial,
				// not which side it points to.
				const double NdotR = FMath::Abs(FVector::DotProduct(EstimatedNormal, RadialDir));

				Node.bWalkable = (NdotR >= MaxSlopeCos);
			}
		}
	}
}

bool FPlanetNavGrid::WorldPosToGridCoord(const FVector& WorldPos, EPlanetCubeFace& OutFace, int32& OutX, int32& OutY) const
{
	const FVector Dir = (WorldPos - PlanetCenter).GetSafeNormal();
	double U, V;
	PlanetMath::SphereDirectionToFaceUV(Dir, OutFace, U, V);

	const double CellUV = 2.0 / (Resolution - 1);
	OutX = FMath::Clamp(FMath::RoundToInt32((U + 1.0) / CellUV), 0, Resolution - 1);
	OutY = FMath::Clamp(FMath::RoundToInt32((V + 1.0) / CellUV), 0, Resolution - 1);
	return true;
}

void FPlanetNavGrid::GetNeighbors(EPlanetCubeFace Face, int32 X, int32 Y, TArray<int32>& OutNeighborIndices) const
{
	OutNeighborIndices.Reset();
	// 8-connected within this face only (Phase 1: no cross-face edges).
	for (int32 DY = -1; DY <= 1; ++DY)
	{
		for (int32 DX = -1; DX <= 1; ++DX)
		{
			if (DX == 0 && DY == 0) continue;
			const int32 NX = X + DX;
			const int32 NY = Y + DY;
			if (NX < 0 || NX >= Resolution || NY < 0 || NY >= Resolution) continue;
			OutNeighborIndices.Add(NodeIndex(Face, NX, NY));
		}
	}
}

bool FPlanetNavGrid::FindPath(const FVector& Start, const FVector& End, TArray<FVector>& OutPath) const
{
	OutPath.Reset();
	if (!IsBuilt()) return false;

	EPlanetCubeFace StartFace, EndFace;
	int32 StartX, StartY, EndX, EndY;
	WorldPosToGridCoord(Start, StartFace, StartX, StartY);
	WorldPosToGridCoord(End, EndFace, EndX, EndY);

	if (StartFace != EndFace)
	{
		UE_LOG(LogTemp, Warning, TEXT("PlanetGen NavGrid: Start/End are on different cube faces -- cross-face pathfinding is a Phase 1 limitation, not yet supported."));
		return false;
	}

	const int32 StartIdx = NodeIndex(StartFace, StartX, StartY);
	const int32 EndIdx = NodeIndex(StartFace, EndX, EndY);

	if (!Nodes[StartIdx].bWalkable || !Nodes[EndIdx].bWalkable)
	{
		return false; // start or end itself isn't walkable terrain
	}

	// Straightforward A*. Open/closed sets as TSet, scores as TMap -- a proper binary
	// heap would be faster for very frequent path requests, but at Resolution^2 nodes
	// per face (a few thousand), a linear scan for the lowest FScore is a small fraction
	// of a millisecond per request. Revisit if AI path-request frequency becomes a
	// bottleneck.
	TSet<int32> OpenSet;
	TSet<int32> ClosedSet;
	TMap<int32, double> GScore;
	TMap<int32, double> FScore;
	TMap<int32, int32> CameFrom;

	OpenSet.Add(StartIdx);
	GScore.Add(StartIdx, 0.0);
	FScore.Add(StartIdx, FVector::Dist(Nodes[StartIdx].WorldPosition, Nodes[EndIdx].WorldPosition));

	TArray<int32> Neighbors;

	while (OpenSet.Num() > 0)
	{
		// Find lowest-FScore node in the open set.
		int32 Current = -1;
		double BestF = TNumericLimits<double>::Max();
		for (int32 Idx : OpenSet)
		{
			const double F = FScore.Contains(Idx) ? FScore[Idx] : TNumericLimits<double>::Max();
			if (F < BestF) { BestF = F; Current = Idx; }
		}

		if (Current == EndIdx)
		{
			// Reconstruct path by walking CameFrom back to Start, then reverse.
			TArray<int32> IndexPath;
			int32 Walk = Current;
			IndexPath.Add(Walk);
			while (CameFrom.Contains(Walk))
			{
				Walk = CameFrom[Walk];
				IndexPath.Add(Walk);
			}
			Algo::Reverse(IndexPath);

			for (int32 Idx : IndexPath)
			{
				OutPath.Add(Nodes[Idx].WorldPosition);
			}
			return true;
		}

		OpenSet.Remove(Current);
		ClosedSet.Add(Current);

		// Recover (X,Y) from the flat index for this face (Face component is constant
		// since Phase 1 never crosses faces): Index = Face*Res*Res + Y*Res + X.
		const int32 WithinFace = Current - (int32)StartFace * Resolution * Resolution;
		const int32 CurrentX = WithinFace % Resolution;
		const int32 CurrentY = WithinFace / Resolution;

		GetNeighbors(StartFace, CurrentX, CurrentY, Neighbors);
		for (int32 NeighborIdx : Neighbors)
		{
			if (ClosedSet.Contains(NeighborIdx) || !Nodes[NeighborIdx].bWalkable) continue;

			const double TentativeG = GScore[Current] + FVector::Dist(Nodes[Current].WorldPosition, Nodes[NeighborIdx].WorldPosition);

			const bool bInOpenSet = OpenSet.Contains(NeighborIdx);
			const double ExistingG = GScore.Contains(NeighborIdx) ? GScore[NeighborIdx] : TNumericLimits<double>::Max();

			if (!bInOpenSet || TentativeG < ExistingG)
			{
				CameFrom.Add(NeighborIdx, Current);
				GScore.Add(NeighborIdx, TentativeG);
				FScore.Add(NeighborIdx, TentativeG + FVector::Dist(Nodes[NeighborIdx].WorldPosition, Nodes[EndIdx].WorldPosition));
				if (!bInOpenSet)
				{
					OpenSet.Add(NeighborIdx);
				}
			}
		}
	}

	return false; // open set exhausted, no path found (e.g. blocked by unwalkable terrain)
}
