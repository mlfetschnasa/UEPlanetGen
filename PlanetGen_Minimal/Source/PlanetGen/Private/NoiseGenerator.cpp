// NoiseGenerator.cpp
#include "NoiseGenerator.h"
#include "Math/UnrealMathUtility.h"

FNoiseGenerator::FNoiseGenerator(const FNoiseSettings& InSettings)
	: Settings(InSettings)
{
	// Build a deterministic, seeded permutation table (classic improved-Perlin style).
	TArray<int32> P;
	P.SetNum(256);
	for (int32 i = 0; i < 256; ++i) P[i] = i;

	FRandomStream Stream(Settings.Seed);
	for (int32 i = 255; i > 0; --i)
	{
		const int32 j = Stream.RandRange(0, i);
		P.Swap(i, j);
	}

	Permutation.SetNum(512);
	for (int32 i = 0; i < 512; ++i) Permutation[i] = P[i & 255];
}

double FNoiseGenerator::Grad(int32 Hash, double X, double Y, double Z) const
{
	const int32 H = Hash & 15;
	const double U = H < 8 ? X : Y;
	const double V = H < 4 ? Y : (H == 12 || H == 14 ? X : Z);
	return ((H & 1) == 0 ? U : -U) + ((H & 2) == 0 ? V : -V);
}

double FNoiseGenerator::Simplex3D(double X, double Y, double Z) const
{
	// Classic improved-Perlin 3D noise (deterministic, single-octave, range ~[-1,1]).
	const int32 XI = FMath::FloorToInt(X) & 255;
	const int32 YI = FMath::FloorToInt(Y) & 255;
	const int32 ZI = FMath::FloorToInt(Z) & 255;

	X -= FMath::FloorToDouble(X);
	Y -= FMath::FloorToDouble(Y);
	Z -= FMath::FloorToDouble(Z);

	const double U = Fade(X), V = Fade(Y), W = Fade(Z);

	const auto& P = Permutation;
	const int32 A  = P[XI] + YI,     AA = P[A] + ZI,   AB = P[A + 1] + ZI;
	const int32 B  = P[XI + 1] + YI, BA = P[B] + ZI,   BB = P[B + 1] + ZI;

	return FMath::Lerp(
		FMath::Lerp(
			FMath::Lerp(Grad(P[AA], X, Y, Z),         Grad(P[BA], X - 1, Y, Z),         U),
			FMath::Lerp(Grad(P[AB], X, Y - 1, Z),     Grad(P[BB], X - 1, Y - 1, Z),     U), V),
		FMath::Lerp(
			FMath::Lerp(Grad(P[AA + 1], X, Y, Z - 1), Grad(P[BA + 1], X - 1, Y, Z - 1), U),
			FMath::Lerp(Grad(P[AB + 1], X, Y - 1, Z - 1), Grad(P[BB + 1], X - 1, Y - 1, Z - 1), U), V),
		W);
}

double FNoiseGenerator::SampleContinentHeight(const FVector& UnitSpherePoint) const
{
	double Amplitude = 1.0;
	double Frequency = Settings.Frequency;
	double Sum = 0.0;
	double MaxAmplitude = 0.0;

	// Offset the sample point by an irrational vector to break Perlin noise's lattice
	// bias -- without this, low frequencies produce visible circular rings at latitudes
	// where the unit sphere direction vectors align with the integer noise lattice.
	FVector P = (UnitSpherePoint + FVector(1.7f, 3.1f, 2.3f)) * Frequency;

	for (int32 i = 0; i < Settings.Octaves; ++i)
	{
		Sum += Simplex3D(P.X, P.Y, P.Z) * Amplitude;
		MaxAmplitude += Amplitude;

		Amplitude *= Settings.Persistence;
		P *= Settings.Lacunarity;
	}

	const double Normalized = (MaxAmplitude > 0.0) ? (Sum / MaxAmplitude) : 0.0; // [-1, 1]
	const double Height = Normalized * Settings.HeightScale;
	return FMath::Clamp(Height, -Settings.HeightScale, Settings.HeightScale);
}

double FNoiseGenerator::RidgedFBM(const FVector& InP, double Frequency, int32 Octaves) const
{
	double Amplitude = 1.0;
	double Sum = 0.0;
	double MaxAmplitude = 0.0;
	FVector P = InP * Frequency;

	for (int32 i = 0; i < Octaves; ++i)
	{
		const double N = Simplex3D(P.X, P.Y, P.Z);   // ~[-1, 1]
		double Ridge = 1.0 - FMath::Abs(N);           // [0, 1], crest where N crosses 0
		Ridge *= Ridge;                                // sharpen crests, deepen valleys
		Sum += Ridge * Amplitude;
		MaxAmplitude += Amplitude;

		Amplitude *= 0.5;
		P *= 2.0;
	}
	return (MaxAmplitude > 0.0) ? (Sum / MaxAmplitude) : 0.0; // [0, 1]
}

double FNoiseGenerator::SampleMountainMask(const FVector& UnitSpherePoint) const
{
	const FMountainLayerSettings& M = Settings.Mountains;
	if (!M.bEnabled || M.Coverage <= 0.0) return 0.0;

	// Every layer samples a DECORRELATED domain (distinct irrational offsets) so its
	// features don't spookily align with the continent base or other layers.
	FVector P = UnitSpherePoint + FVector(7.3, 11.1, 5.7);

	// Domain warp: displace the mask's sample position by low-frequency noise so
	// range belts curve and meander instead of forming round Perlin blobs.
	if (M.RangeWarp > 0.0)
	{
		const double WarpFreq = M.RangeFrequency * 0.5;
		const FVector W1 = (UnitSpherePoint + FVector(13.7, 2.9, 9.1)) * WarpFreq;
		const FVector W2 = (UnitSpherePoint + FVector(5.3, 17.1, 3.7)) * WarpFreq;
		const FVector W3 = (UnitSpherePoint + FVector(9.9, 6.1, 15.3)) * WarpFreq;
		P += FVector(
			Simplex3D(W1.X, W1.Y, W1.Z),
			Simplex3D(W2.X, W2.Y, W2.Z),
			Simplex3D(W3.X, W3.Y, W3.Z)) * M.RangeWarp;
	}

	P *= M.RangeFrequency;
	const double N01 = Simplex3D(P.X, P.Y, P.Z) * 0.5 + 0.5; // [0, 1]

	// Coverage -> threshold, with a fixed smooth transition band so range edges
	// blend into the surrounding terrain instead of forming a cliff at the mask edge.
	const double Threshold = 1.0 - M.Coverage;
	return FMath::SmoothStep(Threshold, Threshold + 0.15, N01);
}

double FNoiseGenerator::SampleCanyonMask(const FVector& UnitSpherePoint) const
{
	const FCanyonLayerSettings& C = Settings.Canyons;
	if (!C.bEnabled || C.Coverage <= 0.0) return 0.0;

	// Decorrelated domain (distinct from continents/mountains) so canyon regions
	// don't spookily align with mountain ranges or the continent base.
	FVector P = UnitSpherePoint + FVector(2.1, 14.7, 8.9);

	if (C.RegionWarp > 0.0)
	{
		const double WarpFreq = C.RegionFrequency * 0.5;
		const FVector W1 = (UnitSpherePoint + FVector(6.7, 19.3, 4.1)) * WarpFreq;
		const FVector W2 = (UnitSpherePoint + FVector(11.9, 3.3, 16.7)) * WarpFreq;
		const FVector W3 = (UnitSpherePoint + FVector(1.3, 9.9, 21.1)) * WarpFreq;
		P += FVector(
			Simplex3D(W1.X, W1.Y, W1.Z),
			Simplex3D(W2.X, W2.Y, W2.Z),
			Simplex3D(W3.X, W3.Y, W3.Z)) * C.RegionWarp;
	}

	P *= C.RegionFrequency;
	const double N01 = Simplex3D(P.X, P.Y, P.Z) * 0.5 + 0.5; // [0, 1]

	const double Threshold = 1.0 - C.Coverage;
	return FMath::SmoothStep(Threshold, Threshold + 0.15, N01);
}

double FNoiseGenerator::SamplePlateauMask(const FVector& UnitSpherePoint) const
{
	const FPlateauLayerSettings& Pl = Settings.Plateaus;
	if (!Pl.bEnabled || (Pl.Coverage <= 0.0 && Pl.CanyonAffinity <= 0.0)) return 0.0;

	// Own independent region mask -- decorrelated domain, same pattern as mountains/canyons.
	FVector P = UnitSpherePoint + FVector(18.1, 4.3, 9.7);

	if (Pl.MaskWarp > 0.0)
	{
		const double WarpFreq = Pl.MaskFrequency * 0.5;
		const FVector W1 = (UnitSpherePoint + FVector(3.1, 22.7, 6.3)) * WarpFreq;
		const FVector W2 = (UnitSpherePoint + FVector(14.9, 1.1, 19.3)) * WarpFreq;
		const FVector W3 = (UnitSpherePoint + FVector(7.7, 16.3, 2.9)) * WarpFreq;
		P += FVector(
			Simplex3D(W1.X, W1.Y, W1.Z),
			Simplex3D(W2.X, W2.Y, W2.Z),
			Simplex3D(W3.X, W3.Y, W3.Z)) * Pl.MaskWarp;
	}

	P *= Pl.MaskFrequency;
	const double OwnNoise01 = Simplex3D(P.X, P.Y, P.Z) * 0.5 + 0.5; // [0, 1]

	// Bias toward wherever canyons are already active. CanyonAffinity=0 -> pure
	// independent mask (unbiased); higher values push the biased value further
	// above threshold specifically inside canyon regions, clustering the two
	// features. Reads 0 automatically if the Canyons layer itself is disabled.
	const double CanyonBias = SampleCanyonMask(UnitSpherePoint) * Pl.CanyonAffinity;
	const double Biased = OwnNoise01 + CanyonBias;

	const double Threshold = 1.0 - Pl.Coverage;
	return FMath::SmoothStep(Threshold, Threshold + 0.15, Biased);
}

double FNoiseGenerator::SampleHeight(const FVector& UnitSpherePoint) const
{
	double Height = SampleContinentHeight(UnitSpherePoint);

	// --- Mountain layer: masked ridged multifractal, additive ---
	const FMountainLayerSettings& M = Settings.Mountains;
	if (M.bEnabled)
	{
		const double Mask = SampleMountainMask(UnitSpherePoint);
		if (Mask > 0.001) // early-out: skip the ridge octaves outside the ranges
		{
			const FVector RidgeP = UnitSpherePoint + FVector(4.9, 8.3, 6.1);
			const double Ridge = RidgedFBM(RidgeP, M.RidgeFrequency, M.RidgeOctaves);
			Height += Mask * Ridge * M.Height;
		}
	}

	// --- Canyon layer: masked ridged-network carve, subtractive ---
	// Reuses the exact same ridged-noise technique as mountains (the zero-crossing
	// network is a branching line pattern either way) but SUBTRACTS instead of
	// adding, and raises the network signal to WallSharpness to narrow/steepen the
	// carve profile instead of leaving it as broad ridges.
	const FCanyonLayerSettings& C = Settings.Canyons;
	if (C.bEnabled)
	{
		const double Mask = SampleCanyonMask(UnitSpherePoint);
		if (Mask > 0.001) // early-out: skip the channel octaves outside canyon regions
		{
			const FVector ChannelP = UnitSpherePoint + FVector(16.3, 1.9, 12.7);
			const double Network = RidgedFBM(ChannelP, C.ChannelFrequency, C.ChannelOctaves);
			const double Carve = FMath::Pow(FMath::Clamp(Network, 0.0, 1.0), C.WallSharpness);
			Height -= Mask * Carve * C.Depth;
		}
	}

	// --- Plateau layer: masked terraced mesa elevation, additive, biased near canyons ---
	const FPlateauLayerSettings& Pl = Settings.Plateaus;
	if (Pl.bEnabled)
	{
		const double Mask = SamplePlateauMask(UnitSpherePoint);
		if (Mask > 0.001) // early-out: skip elevation+terracing outside plateau regions
		{
			// Broad noise field defines each mesa's tabletop shape before terracing.
			const FVector ElevP = (UnitSpherePoint + FVector(21.3, 8.1, 13.9)) * Pl.ElevationFrequency;
			const double Elevation01 = Simplex3D(ElevP.X, ElevP.Y, ElevP.Z) * 0.5 + 0.5; // [0,1]

			// Quantize into TerraceCount discrete flat steps, with EdgeSharpness
			// controlling how crisp the walls between steps are (low = smooth ramp,
			// high = near-vertical cliff at each step boundary).
			const double RawSteps = Elevation01 * Pl.TerraceCount;
			const double StepIndex = FMath::Floor(RawSteps);
			const double Frac = RawSteps - StepIndex;
			const double HalfBand = FMath::Clamp(0.5 / FMath::Max(Pl.EdgeSharpness, 0.01), 0.0, 0.5);
			const double Band = FMath::SmoothStep(0.5 - HalfBand, 0.5 + HalfBand, Frac);
			const double Terraced = FMath::Clamp((StepIndex + Band) / (double)Pl.TerraceCount, 0.0, 1.0);

			Height += Mask * Terraced * Pl.Height;
		}
	}

	return Height;
}
