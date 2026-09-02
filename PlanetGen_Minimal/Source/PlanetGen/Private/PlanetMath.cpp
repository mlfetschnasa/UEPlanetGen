// PlanetMath.cpp
#include "PlanetMath.h"

FVector PlanetMath::FaceUVToCubePoint(EPlanetCubeFace Face, double U, double V)
{
	switch (Face)
	{
	case EPlanetCubeFace::PosX: return FVector(1.0, U, V);
	case EPlanetCubeFace::NegX: return FVector(-1.0, -U, V);
	case EPlanetCubeFace::PosY: return FVector(-U, 1.0, V);
	case EPlanetCubeFace::NegY: return FVector(U, -1.0, V);
	case EPlanetCubeFace::PosZ: return FVector(-V, U, 1.0);
	case EPlanetCubeFace::NegZ: return FVector(V, U, -1.0);
	default:              return FVector(1.0, U, V);
	}
}

void PlanetMath::GetTouchingEdges(double CenterU, double CenterV, double HalfExtent, TArray<EFaceEdge>& OutEdges)
{
	OutEdges.Reset();
	constexpr double Eps = 1e-7;
	if (FMath::IsNearlyEqual(CenterU + HalfExtent, 1.0, Eps))  OutEdges.Add(EFaceEdge::U_Pos);
	if (FMath::IsNearlyEqual(CenterU - HalfExtent, -1.0, Eps)) OutEdges.Add(EFaceEdge::U_Neg);
	if (FMath::IsNearlyEqual(CenterV + HalfExtent, 1.0, Eps))  OutEdges.Add(EFaceEdge::V_Pos);
	if (FMath::IsNearlyEqual(CenterV - HalfExtent, -1.0, Eps)) OutEdges.Add(EFaceEdge::V_Neg);
}

void PlanetMath::SphereDirectionToFaceUV(const FVector& Dir, EPlanetCubeFace& OutFace, double& OutU, double& OutV)
{
	// Dominant axis (largest magnitude component) selects the face; the other two
	// components divided by the dominant one give local UV -- the exact algebraic
	// inverse of FaceUVToCubePoint's per-face formulas below.
	const double AX = FMath::Abs(Dir.X), AY = FMath::Abs(Dir.Y), AZ = FMath::Abs(Dir.Z);

	if (AX >= AY && AX >= AZ)
	{
		if (Dir.X > 0.0) { OutFace = EPlanetCubeFace::PosX; OutU = Dir.Y / Dir.X; OutV =  Dir.Z / Dir.X; }
		else             { OutFace = EPlanetCubeFace::NegX; OutU = Dir.Y / Dir.X; OutV = -Dir.Z / Dir.X; }
	}
	else if (AY >= AX && AY >= AZ)
	{
		if (Dir.Y > 0.0) { OutFace = EPlanetCubeFace::PosY; OutU = -Dir.X / Dir.Y; OutV =  Dir.Z / Dir.Y; }
		else             { OutFace = EPlanetCubeFace::NegY; OutU = -Dir.X / Dir.Y; OutV = -Dir.Z / Dir.Y; }
	}
	else
	{
		if (Dir.Z > 0.0) { OutFace = EPlanetCubeFace::PosZ; OutU =  Dir.Y / Dir.Z; OutV = -Dir.X / Dir.Z; }
		else             { OutFace = EPlanetCubeFace::NegZ; OutU = -Dir.Y / Dir.Z; OutV = -Dir.X / Dir.Z; }
	}

	OutU = FMath::Clamp(OutU, -1.0, 1.0);
	OutV = FMath::Clamp(OutV, -1.0, 1.0);
}

// Derived from FaceUVToCubePoint above. See validation note in PlanetMath.h / Docs/SeamValidation.md
// before relying on this for production cross-face LOD balancing — verify visually first.
FCubeNeighbor PlanetMath::GetCubeNeighbor(EPlanetCubeFace Face, EFaceEdge Edge)
{
	switch (Face)
	{
	case EPlanetCubeFace::PosX:
		switch (Edge) {
			case EFaceEdge::U_Pos: return { EPlanetCubeFace::NegZ, false, false, false };
			case EFaceEdge::U_Neg: return { EPlanetCubeFace::PosZ, false, false, false };
			case EFaceEdge::V_Pos: return { EPlanetCubeFace::PosY, true,  false, false };
			case EFaceEdge::V_Neg: return { EPlanetCubeFace::NegY, true,  false, true  };
			default: break;
		}
		break;
	case EPlanetCubeFace::NegX:
		switch (Edge) {
			case EFaceEdge::U_Pos: return { EPlanetCubeFace::PosZ, false, false, false };
			case EFaceEdge::U_Neg: return { EPlanetCubeFace::NegZ, false, false, false };
			case EFaceEdge::V_Pos: return { EPlanetCubeFace::PosY, true,  true,  false };
			case EFaceEdge::V_Neg: return { EPlanetCubeFace::NegY, true,  true,  true  };
			default: break;
		}
		break;
	case EPlanetCubeFace::PosY:
		switch (Edge) {
			case EFaceEdge::U_Pos: return { EPlanetCubeFace::NegX, true,  false, true  };
			case EFaceEdge::U_Neg: return { EPlanetCubeFace::PosX, true,  false, false };
			case EFaceEdge::V_Pos: return { EPlanetCubeFace::PosZ, false, false, true  };
			case EFaceEdge::V_Neg: return { EPlanetCubeFace::NegZ, false, false, false };
			default: break;
		}
		break;
	case EPlanetCubeFace::NegY:
		switch (Edge) {
			case EFaceEdge::U_Pos: return { EPlanetCubeFace::PosX, true,  false, true  };
			case EFaceEdge::U_Neg: return { EPlanetCubeFace::NegX, true,  false, false };
			case EFaceEdge::V_Pos: return { EPlanetCubeFace::NegZ, false, true,  false };
			case EFaceEdge::V_Neg: return { EPlanetCubeFace::PosZ, false, true,  true  };
			default: break;
		}
		break;
	case EPlanetCubeFace::PosZ:
		switch (Edge) {
			case EFaceEdge::U_Pos: return { EPlanetCubeFace::PosY, false, false, false };
			case EFaceEdge::U_Neg: return { EPlanetCubeFace::NegY, false, true,  false };
			case EFaceEdge::V_Pos: return { EPlanetCubeFace::NegX, true,  false, false };
			case EFaceEdge::V_Neg: return { EPlanetCubeFace::PosX, true,  true,  false };
			default: break;
		}
		break;
	case EPlanetCubeFace::NegZ:
		switch (Edge) {
			case EFaceEdge::U_Pos: return { EPlanetCubeFace::NegY, false, false, true  };
			case EFaceEdge::U_Neg: return { EPlanetCubeFace::PosY, false, true,  true  };
			case EFaceEdge::V_Pos: return { EPlanetCubeFace::PosX, true,  false, false };
			case EFaceEdge::V_Neg: return { EPlanetCubeFace::NegX, true,  true,  false };
			default: break;
		}
		break;
	}
	return { Face, false, false, false }; // unreachable
}
