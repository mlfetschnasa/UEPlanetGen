// PlanetChunkPool.h
// Pre-allocates N chunk actors at startup and recycles them, eliminating per-chunk
// spawn/destroy overhead during streaming.
#pragma once

#include "CoreMinimal.h"
#include "PlanetChunk.h"

class PLANETGEN_API FPlanetChunkPool
{
public:
	// OutlinerFolder: editor-only World Outliner folder the chunk actors are filed
	// under (e.g. "PM1_Chunks"). Ignored in packaged builds -- folders are an
	// editor concept and the calls compile out under !WITH_EDITOR.
	void Initialize(UWorld* World, int32 PoolSize, TSubclassOf<APlanetChunk> ChunkClass,
	                const FString& OutlinerFolder = FString());

	APlanetChunk* Acquire(); // returns nullptr if pool exhausted -- caller should wait, not spawn new
	void Release(APlanetChunk* Chunk);

	int32 NumFree() const { return FreeList.Num(); }
	int32 NumInUse() const { return InUse.Num(); }
	int32 NumTotal() const { return AllChunks.Num(); }

private:
	TArray<APlanetChunk*> AllChunks;
	TArray<APlanetChunk*> FreeList;
	TSet<APlanetChunk*> InUse;
};
