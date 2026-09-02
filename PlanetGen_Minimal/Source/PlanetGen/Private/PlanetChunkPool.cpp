// PlanetChunkPool.cpp
#include "PlanetChunkPool.h"

void FPlanetChunkPool::Initialize(UWorld* World, int32 PoolSize, TSubclassOf<APlanetChunk> ChunkClass,
                                   const FString& OutlinerFolder)
{
	AllChunks.Reserve(PoolSize);
	FreeList.Reserve(PoolSize);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// RF_Transient on the ACTORS (not just their mesh components): without this, every
	// pooled chunk actor spawned in the editor is serialized into the level asset
	// (slow saves) and duplicated into PIE as an orphaned, meshless actor.
	Params.ObjectFlags = RF_Transient;

	// Pre-allocate ~500 actors on startup -- pay the cost once, never again during play.
	for (int32 i = 0; i < PoolSize; ++i)
	{
		APlanetChunk* Chunk = World->SpawnActor<APlanetChunk>(ChunkClass, FVector::ZeroVector, FRotator::ZeroRotator, Params);
		if (Chunk)
		{
#if WITH_EDITOR
			// File under "<ManagerLabel>_Chunks" in the World Outliner so hundreds of
			// pooled actors don't flood the root, and multi-planet setups stay separated.
			if (!OutlinerFolder.IsEmpty())
			{
				Chunk->SetFolderPath(FName(*OutlinerFolder));
			}
#endif
			Chunk->Deactivate(); // start hidden/parked
			AllChunks.Add(Chunk);
			FreeList.Add(Chunk);
		}
	}
}

APlanetChunk* FPlanetChunkPool::Acquire()
{
	if (FreeList.Num() == 0) return nullptr; // pool exhausted
	APlanetChunk* Chunk = FreeList.Pop();
	InUse.Add(Chunk);
	return Chunk;
}

void FPlanetChunkPool::Release(APlanetChunk* Chunk)
{
	if (!Chunk || !InUse.Contains(Chunk)) return;
	InUse.Remove(Chunk);
	Chunk->Deactivate();
	FreeList.Add(Chunk);
}
