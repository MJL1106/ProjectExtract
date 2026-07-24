//// Copyright, (c) Sami Kangasmaa 2022


#include "CoverSystemProxy.h"
#include "CoverSolver.h"
#include "CoverSystemModule.h"

FCoverSystemProxy::FCoverSystemProxy()
{
	CoverMemory = nullptr;
	WorkerTaskManager = nullptr;
	bInitialized = false;
	bGenerated = false;
}

FCoverSystemProxy::~FCoverSystemProxy()
{
	check(!bInitialized);
}

void FCoverSystemProxy::Initialize(FCoverWorkerTaskManager* InTaskManager)
{
	check(IsInGameThread());

	check(CoverMemory == nullptr);
	CoverMemory = new FCoverMemory(1.0f, FVector::ZeroVector);
	WorkerTaskManager = InTaskManager;
	bInitialized = true;
	bGenerated = false;
}

void FCoverSystemProxy::Release()
{
	check(IsInGameThread());

	// Ensure that async task completes before releasing the proxy
	if (RunningGenerateTask.IsValid())
	{
		check(WorkerTaskManager);
		WorkerTaskManager->AbandonTask<TCoverWorkerTask<FCoverGenerateTask>>(RunningGenerateTask);
		RunningGenerateTask.Reset();
	}

	// Ensure that async task completes before releasing the proxy
	if (RunningLoadTask.IsValid())
	{
		check(WorkerTaskManager);
		WorkerTaskManager->AbandonTask<TCoverWorkerTask<FCoverLoadTask>>(RunningLoadTask);
		RunningLoadTask.Reset();
	}

	check(CoverMemory);
	delete CoverMemory;
	CoverMemory = nullptr;
	WorkerTaskManager = nullptr;
	bGenerated = false;
	bInitialized = false;
}

void FCoverSystemProxy::UpdateTaskProgress(float DeltaTime)
{
	check(IsInGameThread());

	// Check if the running async task is completed and delete it in that case
	if (RunningGenerateTask.IsValid())
	{
		if (RunningGenerateTask->IsDone())
		{
			RunningGenerateTask.Reset();
			bGenerated = true;
		}
	}

	if (RunningLoadTask.IsValid())
	{
		if (RunningLoadTask->IsDone())
		{
			RunningLoadTask.Reset();
			bGenerated = true;
		}
	}

	if (TimeSinceLastGenerate < FLT_MAX)
	{
		TimeSinceLastGenerate += DeltaTime;
	}
}

void FCoverSystemProxy::SwapMemory(FCoverMemory* NewMemory)
{
	// Access the memory block and swap it to the new one
	this->AccessMemory([=, this](FCoverMemory* Memory)
		{
			if (CoverMemory)
			{
				delete CoverMemory;
			}
			CoverMemory = NewMemory;
		});
}

void FCoverSystemProxy::AccessMemory(TFunctionRef< void(FCoverMemory*)> Func)
{
	// Lock before accessing the memory
	FScopeLock ScopeLock(&MemoryCriticalSection);

	// Call function and pass locked memory
	Func(CoverMemory);
}


bool FCoverSystemProxy::GenerateCovers(bool bAsync, bool bRegenerate, UWorld* World, const FCoverBuildParams& Params, const FVector& Origin, double Extent, const TArray<FCoverData>* AdditionalData)
{
	check(IsInGameThread());

	check(bInitialized);
	check(World);

	if (bGenerated && !bRegenerate)
	{
		return true;
	}

	// task already running, don't start a generation yet
	if (IsWorkerTaskDispatched())
	{
		if (IsWorkerTaskInQueue())
		{
			return true; // If the task was dispatched but was still in queue, we can say that the covers are going to be updated, no need to launch new task
		}
		return false; // Task was already dispatched and in progress, so return false to notify that we need possibly to regenerate later
	}

	ARecastNavMesh* NavMesh = GetNavMesh(World);
	if (!NavMesh)
	{
		UE_LOG(LogCoverSystem, Warning, TEXT("Cover generation can't be done because navigation is missing!"));
		return true;
	}

	// Run build task
	TCoverWorkerTask<FCoverGenerateTask>* Task = new TCoverWorkerTask<FCoverGenerateTask>(World, this, NavMesh, Params, Origin, Extent, AdditionalData);
	check(Task);

	if (bAsync && WorkerTaskManager) // Async
	{
		// Make the task shareable and dispatch it via task manager
		RunningGenerateTask = MakeShareable(Task);
		WorkerTaskManager->DispatchTask<TCoverWorkerTask<FCoverGenerateTask>>(RunningGenerateTask);
	}
	else // Sync
	{
		// Run the task syncronously in game thread
		Task->RunSynchronously();
		delete Task;
		bGenerated = true;
	}

	TimeSinceLastGenerate = 0;
	return true;
}

bool FCoverSystemProxy::LoadCoversFromArchive(FCoverSerializedArchive& ArchiveRef, bool bAsync, const FVector& Origin, double Extent, bool bDeserializeBytes)
{
	check(IsInGameThread());
	check(bInitialized);
	
	// task already running, don't start a yet
	if (IsWorkerTaskDispatched())
	{
		return false;
	}

	// Load static covers in a separate task
	TCoverWorkerTask<FCoverLoadTask>* Task = new TCoverWorkerTask<FCoverLoadTask>(this, &ArchiveRef, Origin, Extent, bDeserializeBytes);
	check(Task);

	if (bAsync && WorkerTaskManager) // Async
	{
		RunningLoadTask = MakeShareable(Task);
		WorkerTaskManager->DispatchTask<TCoverWorkerTask<FCoverLoadTask>>(RunningLoadTask);
	}
	else // Sync
	{
		// Load in game thread
		Task->RunSynchronously();
		delete Task;
		bGenerated = true;
	}

	return true;
}

bool FCoverSystemProxy::IsWorkerTaskDispatched() const
{
	if (RunningGenerateTask.IsValid() && !RunningGenerateTask->IsDone())
	{
		return true;
	}
	if (RunningLoadTask.IsValid() && !RunningLoadTask->IsDone())
	{
		return true;
	}
	return false;
}

bool FCoverSystemProxy::IsWorkerTaskInQueue() const
{
	if (RunningGenerateTask.IsValid() && RunningGenerateTask->IsInQueue())
	{
		return true;
	}
	if (RunningLoadTask.IsValid() && RunningGenerateTask->IsInQueue())
	{
		return true;
	}
	return false;
}

ARecastNavMesh* FCoverSystemProxy::GetNavMesh(UWorld* World) const
{
	const UNavigationSystemBase* NavSystem = World->GetNavigationSystem();
	if (!NavSystem )
	{
		return nullptr;
	}
	return const_cast<ARecastNavMesh*>(static_cast<const ARecastNavMesh*>(NavSystem->GetMainNavData()));
}