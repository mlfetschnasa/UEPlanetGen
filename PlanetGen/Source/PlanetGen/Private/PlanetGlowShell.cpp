// PlanetGlowShell.cpp
#include "PlanetGlowShell.h"

APlanetGlowShell::APlanetGlowShell()
{
	PrimaryActorTick.bCanEverTick = false;
	MeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("GlowMesh"));
	RootComponent = MeshComponent;
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComponent->SetCastShadow(false);
	MeshComponent->bUseAsyncCooking = false;
	MeshComponent->SetFlags(RF_Transient);
}

void APlanetGlowShell::GenerateGlowShell(const FVector& PlanetCenter, double PlanetRadius,
	double GlowShellScale, UMaterialInterface* GlowMaterial, int32 SubdivisionsPerFace)
{
	const double ShellRadius = PlanetRadius * GlowShellScale;
	// The silhouette rim IS the effect -- a coarse sphere reads as a polygon at the
	// horizon line. 48+ subdivisions per face keeps the rim smooth even fullscreen.
	SubdivisionsPerFace = FMath::Max(SubdivisionsPerFace, 4);

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FProcMeshTangent> Tangents;

	const int32 N = SubdivisionsPerFace + 1;
	constexpr EPlanetCubeFace Faces[6] = {
		EPlanetCubeFace::PosX, EPlanetCubeFace::NegX, EPlanetCubeFace::PosY,
		EPlanetCubeFace::NegY, EPlanetCubeFace::PosZ, EPlanetCubeFace::NegZ };

	for (EPlanetCubeFace Face : Faces)
	{
		const int32 BaseIndex = Vertices.Num();
		for (int32 y = 0; y < N; ++y)
		{
			for (int32 x = 0; x < N; ++x)
			{
				double U = PlanetMath::SnapToCubeEdge(-1.0 + (x / double(N - 1)) * 2.0);
				double V = PlanetMath::SnapToCubeEdge(-1.0 + (y / double(N - 1)) * 2.0);

				const FVector CubePoint = PlanetMath::FaceUVToCubePoint(Face, U, V);
				const FVector Dir = PlanetMath::GetSphereNormal(CubePoint);

				Vertices.Add(PlanetCenter + Dir * ShellRadius);
				Normals.Add(Dir);
				UVs.Add(FVector2D((U + 1.0) * 0.5, (V + 1.0) * 0.5));
			}
		}
		for (int32 y = 0; y < N - 1; ++y)
		{
			for (int32 x = 0; x < N - 1; ++x)
			{
				const int32 I0 = BaseIndex + y * N + x;
				const int32 I1 = I0 + 1, I2 = I0 + N, I3 = I2 + 1;
				Triangles.Append({ I0, I2, I1,  I1, I2, I3 });
			}
		}
	}

	Tangents.SetNum(Vertices.Num());

	MeshComponent->CreateMeshSection(0, Vertices, Triangles, Normals, UVs,
		TArray<FColor>(), Tangents, /*bCreateCollision=*/ false);

	if (GlowMaterial)
	{
		GlowMID = UMaterialInstanceDynamic::Create(GlowMaterial, this);
		MeshComponent->SetMaterial(0, GlowMID);
	}
}

void APlanetGlowShell::SetGlowAlpha(float Alpha)
{
	if (GlowMID)
	{
		GlowMID->SetScalarParameterValue(TEXT("GlowAlpha"), FMath::Clamp(Alpha, 0.f, 1.f));
	}
}
