// PlanetOceanShell.cpp
#include "PlanetOceanShell.h"

APlanetOceanShell::APlanetOceanShell()
{
	PrimaryActorTick.bCanEverTick = false;
	MeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("OceanMesh"));
	RootComponent = MeshComponent;
	MeshComponent->bUseAsyncCooking = true;
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComponent->SetCastShadow(false);
	// RF_Transient: don't serialize mesh data to level asset (same as terrain chunks)
	MeshComponent->SetFlags(RF_Transient);
}

void APlanetOceanShell::GenerateOceanMesh(const FVector& PlanetCenter, double PlanetRadius,
	double SeaLevel, int32 SubdivisionsPerFace, UMaterialInterface* WaterMaterial)
{
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FProcMeshTangent> Tangents;

	// PlanetRadius is in UE units (cm). SeaLevel is in meters (matching NoiseGenerator's
	// height output) -- multiply by 100 to convert to cm before adding to PlanetRadius.
	const double OceanRadius = PlanetRadius + (SeaLevel * 100.0);
	const int32 N = SubdivisionsPerFace + 1; // verts per edge

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
				double U = -1.0 + (x / double(N - 1)) * 2.0;
				double V = -1.0 + (y / double(N - 1)) * 2.0;
				U = PlanetMath::SnapToCubeEdge(U);
				V = PlanetMath::SnapToCubeEdge(V);

				const FVector CubePoint = PlanetMath::FaceUVToCubePoint(Face, U, V);
				const FVector Dir = PlanetMath::GetSphereNormal(CubePoint);
				const FVector Pos = PlanetCenter + Dir * OceanRadius;

				Vertices.Add(Pos);
				Normals.Add(Dir); // perfectly smooth sphere -- Dir IS the correct normal
				UVs.Add(FVector2D((U + 1.0) * 0.5, (V + 1.0) * 0.5));
			}
		}

		for (int32 y = 0; y < N - 1; ++y)
		{
			for (int32 x = 0; x < N - 1; ++x)
			{
				const int32 I0 = BaseIndex + y * N + x;
				const int32 I1 = I0 + 1;
				const int32 I2 = I0 + N;
				const int32 I3 = I2 + 1;
				Triangles.Append({ I0, I2, I1,  I1, I2, I3 });
			}
		}
	}

	Tangents.SetNum(Vertices.Num());

	MeshComponent->CreateMeshSection(
		0, Vertices, Triangles, Normals, UVs,
		TArray<FColor>(), Tangents,
		/*bCreateCollision=*/ false);

	if (WaterMaterial)
	{
		MeshComponent->SetMaterial(0, WaterMaterial);
	}
}
