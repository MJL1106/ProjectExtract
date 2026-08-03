//// Copyright, (c) Sami Kangasmaa 2022

#pragma once

#include "CoreMinimal.h"
#include "CoverMemory.h"
#include "CoverWorkerThread.h"
#include "CoverGenerateAsyncTask.h"
#include "CoverSystemPublicData.h"

#include "Engine/World.h"

/**
 * Proxy class to act between game thread and async generation thread
 */
class FCoverSystemProxy
{
private:

	bool bInitialized = false;
	bool bGenerated = false;

public:

	FCoverSystemProxy();
	~FCoverSystemProxy();

	void Initialize(FCoverWorkerTaskManager* InTaskManager);

	// Releases the proxy, ensures that all async tasks are completed before returning
	void Release();

	// Called per frame update and observe worker task
	void UpdateTaskProgress(float DeltaTime);

	// Time since this was last generated, FLT_MAX if never generated
	inline float GetTimeSinceLastGenerate() const { return TimeSinceLastGenerate; }

private:

	/*
	* Shared cover memory between game and worker thread
	* Memory can be locked during swap by worker task
	*/
	FCoverMemory* CoverMemory = nullptr;

	// Mutex to control access into cover memory
	FCriticalSection MemoryCriticalSection;

	// Task manager to use for async processing
	FCoverWorkerTaskManager* WorkerTaskManager;

	// Time since this was last generated (FLT_MAX) by default (never generated)
	float TimeSinceLastGenerate = FLT_MAX;

public:

	// Safe access to memory from multiple threads via lambda.
	void AccessMemory(TFunctionRef< void(FCoverMemory*)> Func);

	/*
	* Swaps the memory accessible by game thread to a newly built one
	* This is called from work task once the build has been completed to minimize lock time
	*/
	void SwapMemory(FCoverMemory* NewMemory);

public:

	// Generates entities of covers and stores them into a entity container
	bool GenerateCovers(bool bAsync, bool bRegenerate, UWorld* World, const FCoverBuildParams& Params, const FVector& Origin, double Extent, const TArray<FCoverData>* AdditionalData = nullptr);

	// Load covers from given archive and insert them into a cover memory
	bool LoadCoversFromArchive(FCoverSerializedArchive& ArchiveRef, bool bAsync, const FVector& Origin, double Extent, bool bDeserializeBytes = true);

	// Checks if worker task is currently dispatched to task manager
	bool IsWorkerTaskDispatched() const;

	// Checks if worker task is dispatched but has not yet been started to be processed
	bool IsWorkerTaskInQueue() const;

protected: // Async and internal generation

	friend class FCoverGenerateTask;

	// Task currently running or in queue to generate covers
	TSharedPtr<TCoverWorkerTask<FCoverGenerateTask>> RunningGenerateTask;

	// Task currently running or in queue to load covers from archive
	TSharedPtr<TCoverWorkerTask<FCoverLoadTask>> RunningLoadTask;

	// Fetches a main nav mesh from the given world
	ARecastNavMesh* GetNavMesh(UWorld* World) const;
};
