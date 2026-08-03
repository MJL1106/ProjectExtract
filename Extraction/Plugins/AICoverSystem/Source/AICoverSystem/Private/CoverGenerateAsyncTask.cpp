//// Copyright, (c) Sami Kangasmaa 2022


#include "CoverGenerateAsyncTask.h"
#include "CoverSystemProxy.h"
#include "CoverBuilder.h"
#include "CoverMemory.h"
#include "StaticCoverArchive.h"

FCoverGenerateTask::FCoverGenerateTask(UWorld* World, FCoverSystemProxy* InProxy, ARecastNavMesh* NavMesh, const FCoverBuildParams& Params, const FVector& InOrigin, double InExtent, const TArray<FCoverData>* AdditionalData)
{
	check(World);
	check(InProxy);
	check(NavMesh);

	Proxy = InProxy;
	WorldPtr = World;
	NavMeshPtr = NavMesh;
	BuildParams = Params;
	Origin = InOrigin;
	Extent = InExtent;

	if (AdditionalData)
	{
		AdditionalCoverData = *AdditionalData;
	}
}

FCoverGenerateTask::~FCoverGenerateTask() {}

void FCoverGenerateTask::Run(TFunctionRef<void()> EvalTaskBudget)
{
	if (!WorldPtr.IsValid(false, true))
	{
		return;
	}
	if (!NavMeshPtr.IsValid(false, true))
	{
		return;
	}

	check(Proxy);

	/*
	* Create a new memory block to build results without locking the current memory in the proxy
	* Once the results are ready, the memory will be swapped, so game thread can access it
	*/
	FCoverMemory* Memory = new FCoverMemory(Extent, Origin);

	FCoverBuilder Builder;
	if (Builder.Build(Memory, WorldPtr.Get(), NavMeshPtr.Get(), BuildParams, AdditionalCoverData, EvalTaskBudget))
	{
		// Swap the memory in proxy by syncing the pointer between this and game thread
		check(Memory);
		Proxy->SwapMemory(Memory);
	}
	else // Build failed, discard the memory
	{
		delete Memory;
	}
}

FCoverLoadTask::FCoverLoadTask(FCoverSystemProxy* InProxy, FCoverSerializedArchive* InArchive, const FVector& InOrigin, double InExtent, bool bInDeserialize)
{
	check(InProxy);
	check(InArchive);

	Proxy = InProxy;
	Archive = InArchive;
	Extent = InExtent;
	Origin = InOrigin;
	bDeserialize = bInDeserialize;
}

FCoverLoadTask::~FCoverLoadTask() {}

void FCoverLoadTask::Run(TFunctionRef<void()> EvalTaskBudget)
{
	check(Proxy);
	check(Archive);

	// Create a new memory where to load the covers to
	FCoverMemory* Memory = new FCoverMemory(Extent, Origin);

	// Load the archive into memory
	Archive->Load(Memory, bDeserialize);

	// ---> Loading runs independently and is not accessing physics scene. We don't need to evaluate time budget by calling EvalTaskBudget()

	// Swap memory in proxy, so loaded covers come available in game thread
	check(Memory);
	Proxy->SwapMemory(Memory);
}