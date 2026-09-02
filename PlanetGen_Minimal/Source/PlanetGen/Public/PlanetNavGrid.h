// PlanetNavGrid.h
// Phase 1 custom navigation: a per-cube-face grid whose walkability is derived directly
// from FNoiseGenerator::SampleHeight -- NOT from rendered/streamed chunk meshes. This
// keeps navigation completely decoupled from render LOD/streaming state (a path can be
// queried and built correctly even for terrain whose chunks aren't currently loaded).
//
// WHY NOT UNREAL'S BUILT-IN RECAST NAVMESH: Recast assumes a single fixed global up-axis
// when voxelizing/classifying walkable surfaces. On a sphere, "up" is different at every
// point -- a standard navmesh would only correctly classify a small patch near wherever
// it thinks up points, and misclassify the rest of the sphere as unwalkable. Building a
// custom grid directly from noise sidesteps this by never assuming a fixed up-axis --
// each node's own local slope is tested against ITS OWN radial direction.
//
// PHASE 1 LIMITATION: pathfinding is confined to a single cube face. Start/End points on
// different faces return no path (logged). Cross-face stitching is deferred -- the exact
// same category of problem this project's cross-face LOD balancing already documents as
// unsolved (see Docs/SeamValidation.md).
//
// ALSO NOTE: this grid samples RAW noise height, independent of the POI flatten-mask
// (which currently only exists inside BuildMeshData's vertex loop). A building's flattened
// pad may not yet be reflected in navigation data. Fine for Phase 1; worth revisiting if
// enemies need to navigate cleanly around/into flattened building sites.
#pragma once

#include "CoreMinimal.h"
#include "PlanetMath.h"
#include "NoiseGenerator.h"

struct FNavGridNode
{
	FVector WorldPosition = FVector::ZeroVector;
	bool bWalkable = false;
};

class PLANETGEN_API FPlanetNavGrid
{
public:
	// Synchronous, game-thread bake. Cost is trivial (Resolution^2 * 6 faces noise
	// samples, a handful of samples each) -- sub-second even at high resolution, no
	// need for the async pipeline terrain streaming uses.
	void Build(const FVector& InPlanetCenter, double InPlanetRadius,
	           TSharedPtr<FNoiseGenerator, ESPMode::ThreadSafe> Noise,
	           int32 InResolution, double MaxWalkableSlopeDegrees);

	// A* over the grid. Returns false (and logs) if Start/End resolve to different
	// cube faces (Phase 1 limitation) or if no walkable path exists between them.
	// OutPath includes both endpoints' snapped grid-node world positions, in order.
	bool FindPath(const FVector& Start, const FVector& End, TArray<FVector>& OutPath) const;

	bool IsBuilt() const { return Nodes.Num() > 0; }

private:
	FVector PlanetCenter = FVector::ZeroVector;
	double PlanetRadius = 0.0;
	int32 Resolution = 64;

	// Flat array: index = Face*Resolution*Resolution + Y*Resolution + X.
	TArray<FNavGridNode> Nodes;

	int32 NodeIndex(EPlanetCubeFace Face, int32 X, int32 Y) const;
	bool WorldPosToGridCoord(const FVector& WorldPos, EPlanetCubeFace& OutFace, int32& OutX, int32& OutY) const;
	void GetNeighbors(EPlanetCubeFace Face, int32 X, int32 Y, TArray<int32>& OutNeighborIndices) const;
};
