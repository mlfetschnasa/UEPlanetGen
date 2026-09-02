// PlanetAtmosphereController.cpp
#include "PlanetAtmosphereController.h"
#include "PlanetManager.h"

APlanetAtmosphereController::APlanetAtmosphereController()
{
	PrimaryActorTick.bCanEverTick = false;

	SkyAtmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("SkyAtmosphere"));
	RootComponent = SkyAtmosphere;

	// CRITICAL: the component's default TransformMode is PlanetTopAtAbsoluteWorldOrigin,
	// which pins the atmosphere's ground surface to world origin and IGNORES the actor's
	// location entirely -- ConfigureForPlanet()'s SetActorLocation() would do nothing,
	// which is why this system originally appeared completely broken. This mode makes
	// the atmosphere's planet CENTER follow the component transform, so repositioning
	// the actor to a planet's center gives that planet the atmosphere.
	SkyAtmosphere->TransformMode = ESkyAtmosphereTransformMode::PlanetCenterAtComponentTransform;

	VolumetricCloud = CreateDefaultSubobject<UVolumetricCloudComponent>(TEXT("VolumetricCloud"));
	VolumetricCloud->SetupAttachment(RootComponent);
	VolumetricCloud->SetVisibility(false); // ConfigureForPlanet enables it when a cloud material exists

	HeightFog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("HeightFog"));
	HeightFog->SetupAttachment(RootComponent);
	// Exponential height fog is a PLANAR, world-Z-based effect -- on a spherical planet it
	// produces a fog slab through the planet's middle (as observed). Off by default;
	// opt-in for localized/flat-ish play areas where its assumptions roughly hold.
	HeightFog->SetVisibility(false);
}

void APlanetAtmosphereController::ConfigureForPlanet(const APlanetManager* Planet)
{
	if (!Planet) return;
	CurrentPlanet = Planet;

	const FPlanetAtmosphereSettings& S = Planet->AtmosphereSettings;
	const double PlanetRadiusKm = Planet->GetPlanetRadius() / 100000.0; // UU(cm) -> km
	const double AtmosphereHeightKm = PlanetRadiusKm * S.AtmosphereHeightRatio;

	// --- Reposition the atmosphere actor to this planet's center ---
	SetActorLocation(Planet->GetActorLocation());

	// --- Scale-critical parameters: rederived per-planet, NOT copy-pasted Earth defaults ---
	SkyAtmosphere->BottomRadius = PlanetRadiusKm;
	SkyAtmosphere->AtmosphereHeight = AtmosphereHeightKm;

	// Rayleigh/Mie exponential falloff distributions are defined relative to atmosphere
	// height -- rescale them or a tiny planet gets a washed-out, gradient-less sky.
	const double HeightScaleFactor = AtmosphereHeightKm / 100.0; // 100km = Earth reference atmosphere height
	SkyAtmosphere->RayleighExponentialDistribution = S.RayleighExponentialDistribution * HeightScaleFactor;
	SkyAtmosphere->MieExponentialDistribution = S.MieExponentialDistribution * HeightScaleFactor;

	// --- Direct copy-through parameters (not scale-dependent) ---
	SkyAtmosphere->RayleighScattering = S.RayleighScattering;
	SkyAtmosphere->MieScatteringScale = S.MieScatteringScale;
	SkyAtmosphere->MieAbsorptionScale = S.MieAbsorptionScale;
	SkyAtmosphere->MieAnisotropy = S.MieAnisotropy;
	SkyAtmosphere->MultiScatteringFactor = S.MultiScatteringFactor;

	SkyAtmosphere->MarkRenderStateDirty();

	// --- Clouds ---
	if (S.bHasClouds && S.CloudMaterial)
	{
		CloudMID = UMaterialInstanceDynamic::Create(S.CloudMaterial, this);
		CloudMID->SetScalarParameterValue(TEXT("Coverage"), S.CloudCoverage);
		CloudMID->SetScalarParameterValue(TEXT("DensityScale"), S.CloudDensityScale);

		// Material's height-gradient shaping needs the ACTUAL world-space Z range the
		// cloud layer occupies (cm, world-origin-relative) -- not the component's
		// altitude-above-surface convention (km, sea-level-relative). Must match
		// VolumetricCloud->LayerBottomAltitude/LayerHeight below, see Docs/MaterialSetup.md.
		const double LayerBottomWorld = (PlanetRadiusKm + S.CloudLayerBottomKm * HeightScaleFactor) * 100000.0
			+ Planet->GetActorLocation().Z;
		const double LayerHeightWorld = S.CloudLayerHeightKm * HeightScaleFactor * 100000.0;

		CloudMID->SetScalarParameterValue(TEXT("LayerBottomWorldZ"), (float)LayerBottomWorld);
		CloudMID->SetScalarParameterValue(TEXT("LayerHeightWorld"), (float)LayerHeightWorld);
		CloudMID->SetVectorParameterValue(TEXT("CloudAlbedo"), FLinearColor(0.9f, 0.92f, 0.95f, 1.0f));

		VolumetricCloud->Material = CloudMID;
		VolumetricCloud->SetVisibility(true);
		VolumetricCloud->LayerBottomAltitude = S.CloudLayerBottomKm * HeightScaleFactor;
		VolumetricCloud->LayerHeight = S.CloudLayerHeightKm * HeightScaleFactor;
		VolumetricCloud->MarkRenderStateDirty();
	}
	else
	{
		VolumetricCloud->SetVisibility(false);
	}

	// Height fog ground-level tint roughly matches the atmosphere's horizon color so
	// there's no visible seam between "sky color" and "ground haze color".
	// NOTE: this property was renamed FogInscatteringColor -> FogInscatteringLuminance
	// in modern UE (part of the engine's broader shift to physically-based luminance
	// units) and is a flat member directly on the component, not nested in a sub-struct.
	HeightFog->FogInscatteringLuminance = S.HorizonTintOverride;
	HeightFog->MarkRenderStateDirty();
}

void APlanetAtmosphereController::SetTransitionAlpha(float Alpha)
{
	Alpha = FMath::Clamp(Alpha, 0.f, 1.f);

	// USkyAtmosphereComponent has no built-in opacity/fade control -- there is no
	// SetOpacity()/Opacity property on this component. We approximate a fade by:
	//  (a) toggling visibility at the extremes (fully cull when invisible, for GPU cost), and
	//  (b) scaling the actual scattering intensity properties so the sky visibly
	//      thins out rather than popping instantly when crossing the threshold.
	// This intentionally mutates the values ConfigureForPlanet() set; CurrentPlanet's
	// settings remain the source of truth and are re-applied in full on every
	// ConfigureForPlanet() call, so this fade never permanently drifts the base config.
	if (const APlanetManager* Planet = CurrentPlanet.Get())
	{
		const FPlanetAtmosphereSettings& S = Planet->AtmosphereSettings;
		SkyAtmosphere->MultiScatteringFactor = (float)(S.MultiScatteringFactor * Alpha);
		SkyAtmosphere->MieScatteringScale = (float)(S.MieScatteringScale * Alpha);
		SkyAtmosphere->MarkRenderStateDirty();
	}
	SkyAtmosphere->SetVisibility(Alpha > 0.01f);

	if (CloudMID)
	{
		CloudMID->SetScalarParameterValue(TEXT("DensityFadeAlpha"), Alpha);
	}
	VolumetricCloud->SetVisibility(Alpha > 0.01f); // fully cull when invisible to save GPU cost
}
