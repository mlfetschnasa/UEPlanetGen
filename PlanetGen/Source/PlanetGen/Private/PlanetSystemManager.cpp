// PlanetSystemManager.cpp
#include "PlanetSystemManager.h"
#include "PlanetManager.h"
#include "PlanetGlowShell.h"
#include "PlanetAtmosphereController.h"
#include "Kismet/GameplayStatics.h"

APlanetSystemManager::APlanetSystemManager()
{
	PrimaryActorTick.bCanEverTick = true;
}

void APlanetSystemManager::BeginPlay()
{
	Super::BeginPlay();

	// QoL: if not wired up in the Details panel, discover the pieces automatically.
	// Explicit assignment still wins (lets you scope the system to a subset of planets).
	if (Planets.Num() == 0)
	{
		TArray<AActor*> Found;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), APlanetManager::StaticClass(), Found);
		for (AActor* A : Found)
		{
			Planets.Add(Cast<APlanetManager>(A));
		}
		if (Planets.Num() > 0)
		{
			UE_LOG(LogTemp, Log, TEXT("PlanetSystemManager: auto-discovered %d planet(s)"), Planets.Num());
		}
	}
	if (!AtmosphereController)
	{
		TArray<AActor*> Found;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), APlanetAtmosphereController::StaticClass(), Found);
		if (Found.Num() > 0)
		{
			AtmosphereController = Cast<APlanetAtmosphereController>(Found[0]);
			UE_LOG(LogTemp, Log, TEXT("PlanetSystemManager: auto-discovered atmosphere controller"));
		}
	}
}

void APlanetSystemManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!AtmosphereController || Planets.Num() == 0) return;

	APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	if (!PC || !PC->GetPawn()) return;
	const FVector ViewerLocation = PC->GetPawn()->GetActorLocation();

	// Find the nearest planet by RADIUS-RELATIVE distance, not raw distance -- a small
	// planet 10km away and a huge planet 10km away are not equally "close" in terms of
	// when their atmosphere should matter.
	APlanetManager* NearestPlanet = nullptr;
	double NearestRelativeDist = TNumericLimits<double>::Max();

	for (APlanetManager* Planet : Planets)
	{
		if (!Planet || Planet->GetPlanetRadius() <= 0.0) continue;
		const double RelativeDist = FVector::Dist(Planet->GetActorLocation(), ViewerLocation) / Planet->GetPlanetRadius();
		if (RelativeDist < NearestRelativeDist)
		{
			NearestRelativeDist = RelativeDist;
			NearestPlanet = Planet;
		}
	}
	if (!NearestPlanet) return;

	// Reconfigure the shared atmosphere controller only when the nearest planet CHANGES.
	if (ActivePlanet.Get() != NearestPlanet)
	{
		AtmosphereController->ConfigureForPlanet(NearestPlanet);
		ActivePlanet = NearestPlanet;
	}

	const double RealAtmosphereFade = 1.0 - FMath::Clamp(
		(NearestRelativeDist - AtmosphereFadeEndRadiusMultiplier) /
		(AtmosphereFadeStartRadiusMultiplier - AtmosphereFadeEndRadiusMultiplier),
		0.0, 1.0);
	AtmosphereController->SetTransitionAlpha((float)RealAtmosphereFade);

	// --- Glow shell crossfade pass, every planet ---
	for (APlanetManager* Planet : Planets)
	{
		if (!Planet || !Planet->GetGlowShell()) continue;

		if (Planet == NearestPlanet)
		{
			// Active planet: fake glow fades OUT as the real atmosphere fades IN.
			Planet->GetGlowShell()->SetGlowAlpha((float)(1.0 - RealAtmosphereFade));
		}
		else
		{
			// Every other planet has no real atmosphere active -- fake glow stays full.
			Planet->GetGlowShell()->SetGlowAlpha(1.0f);
		}
	}
}
