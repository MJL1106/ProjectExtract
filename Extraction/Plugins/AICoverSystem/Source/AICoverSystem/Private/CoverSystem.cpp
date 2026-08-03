//// Copyright, (c) Sami Kangasmaa 2022


#include "CoverSystem.h"
#include "CoverSystemModule.h"
#include "CoverSystemProxy.h"
#include "CoverWorkerThread.h"
#include "CoverMemory.h"
#include "CoverSolver.h"
#include "ManualCoverPoint.h"
#include "CoverPartitionInvokerComponent.h"

#include "EngineUtils.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"

#include "Misc/ScopedSlowTask.h"

#include "Deams51Algorithms.h"

#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

#include "UObject/ConstructorHelpers.h"
#include "Components/BillboardComponent.h"
#include "Engine/Texture2D.h"

#if WITH_EDITOR
#include "Editor.h"
#include "EditorViewportClient.h"
#include "LevelEditorViewport.h"
#include "Subsystems/UnrealEditorSubsystem.h"
#include "Camera/PlayerCameraManager.h"
#endif

DECLARE_STATS_GROUP(TEXT("AICoverSystem"), STATGROUP_CoverSystem, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("CoverSystemTick"), STAT_TickCoverSystem, STATGROUP_CoverSystem);
DECLARE_CYCLE_STAT(TEXT("ObserveAsyncTasks"), STAT_ObserveAsyncTasks, STATGROUP_CoverSystem);
DECLARE_CYCLE_STAT(TEXT("UpdatePartitions"), STAT_UpdatePartitions, STATGROUP_CoverSystem);
DECLARE_CYCLE_STAT(TEXT("RegeneratePartitions"), STAT_RegeneratePartitions, STATGROUP_CoverSystem);
DECLARE_CYCLE_STAT(TEXT("DebugDrawCovers"), STAT_DebugDrawCovers, STATGROUP_CoverSystem);

DECLARE_CYCLE_STAT(TEXT("CollectInvokers"), STAT_CollectInvokers, STATGROUP_CoverSystem);
DECLARE_CYCLE_STAT(TEXT("CollectActivePartitions"), STAT_CollectActivePartitions, STATGROUP_CoverSystem);
DECLARE_CYCLE_STAT(TEXT("CreatePartitionProxies"), STAT_CreatePartitionProxies, STATGROUP_CoverSystem);
DECLARE_CYCLE_STAT(TEXT("DestroyDeactivePartitions"), STAT_DestroyDeactivePartitions, STATGROUP_CoverSystem);

DECLARE_CYCLE_STAT(TEXT("FindPartitionWithinBounds"), STAT_FindPartitionsWithinBound, STATGROUP_CoverSystem);
DECLARE_CYCLE_STAT(TEXT("ExcludePartitions"), STAT_ExcludePartitions, STATGROUP_CoverSystem);

DECLARE_CYCLE_STAT(TEXT("GenerateCovers"), STAT_GenerateCovers, STATGROUP_CoverSystem);

namespace GCoverSystem
{
	static TMap<UWorld*, TWeakObjectPtr<ACoverSystem>> GCoverSystems;

	/**
	* We store here registered manual cover points as entities for faster injection to octree
	*/
	struct FManualCoverPointData
	{
		FCEntityId EntityId;
		FCoverData CoverData;
	};
	TMap<ACoverSystem*, TCEntities<FManualCoverPointData>> ManualCoverPointEntityLookup;
}

ACoverSystem::ACoverSystem()
{
	PrimaryActorTick.bCanEverTick = true;

	// Use high priority tick in post physics, so async tasks would avoid overlap with physics as much as possible
	PrimaryActorTick.bHighPriority = true;
	PrimaryActorTick.TickGroup = ETickingGroup::TG_PostPhysics;
	PrimaryActorTick.EndTickGroup = ETickingGroup::TG_PostPhysics;

	Proxy = nullptr;

	RootSceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootSceneComponent"));
	RootComponent = RootSceneComponent;

	SystemBoundsComponent = CreateDefaultSubobject<UCoverBoundBoxComponent>(TEXT("SystemBoundsComp"));
	SystemBoundsComponent->SetupAttachment(RootComponent);

	bReplicates = false;
	SetHidden(true);
	SetReplicatingMovement(false);
	SetCanBeDamaged(false);
	bEnableAutoLODGeneration = false;

#if WITH_EDITOR
	bIsSpatiallyLoaded = false;
#endif

#if WITH_EDITORONLY_DATA
	SpriteComponent = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
	if (!IsRunningCommandlet() && (SpriteComponent != nullptr))
	{
		// Structure to hold one-time initialization
		struct FConstructorStatics
		{
			ConstructorHelpers::FObjectFinderOptional<UTexture2D> SpriteTexture;
			FName ID_Info;
			FText NAME_Info;
			FConstructorStatics()
				: SpriteTexture(TEXT("/Engine/EditorResources/S_Actor"))
				, ID_Info(TEXT("Info"))
				, NAME_Info(NSLOCTEXT("SpriteCategory", "Info", "Info"))
			{
			}
		};
		static FConstructorStatics ConstructorStatics;

		SpriteComponent->Sprite = ConstructorStatics.SpriteTexture.Get();
		SpriteComponent->SpriteInfo.Category = ConstructorStatics.ID_Info;
		SpriteComponent->SpriteInfo.DisplayName = ConstructorStatics.NAME_Info;
		SpriteComponent->bIsScreenSizeScaled = true;
		SpriteComponent->SetupAttachment(RootComponent);
	}
#endif // WITH_EDITORONLY_DATA
}

#if WITH_EDITOR
void ACoverSystem::PostLoad()
{
	Super::PostLoad();

	if (RootComponent != nullptr && !IsValid(RootComponent))
	{
		RootComponent = nullptr;
	}

#if WITH_EDITORONLY_DATA
	if (SpriteComponent != nullptr && !IsValid(SpriteComponent))
	{
		SpriteComponent = nullptr;
	}
#endif
}
#endif

void ACoverSystem::BeginPlay()
{
	Super::BeginPlay();

	bTearingDown = false;

	// Create threaded task manager, this will launch additional thread
	check(WorkerTaskManager == nullptr);
	WorkerTaskManager = new FCoverWorkerTaskManager(FMath::Max(WorkerThreadTimeBudgetPerFrame, 0.1));

	ensure(!GCoverSystem::GCoverSystems.Contains(GetWorld()) && "Multiple cover systems detected! Only single system should exists at time!");
	GCoverSystem::GCoverSystems.Add(GetWorld(), this);
	GCoverSystem::ManualCoverPointEntityLookup.FindOrAdd(this, TCEntities<GCoverSystem::FManualCoverPointData>());

	// Create proxy object
	ensure(Proxy == nullptr && "Cover system proxy wasn't null. Possible memory leak happening!");
	if (!Proxy && !bUsePartitions)
	{
		Proxy = new FCoverSystemProxy();
		Proxy->Initialize(WorkerTaskManager);
	}

	if (bUsePartitions)
	{
		check(PartitionSize > 0);
	}

	DirtyPartitions.Empty();
	ActivePartitions.Empty();
	GeneratedPartitions.Empty();

	WholeSystemBound.Reset();
	CacheWholeSystemBound();

	if (CoverSystemMode == ECoverSystemMode::Dynamic && bGenerateOnBeginPlay)
	{
		bDirtyGeneration = true; // Mark generation dirty to build on next tick
	}
	else if (CoverSystemMode == ECoverSystemMode::Static)
	{
		// Load the covers from the archive where they were baked in
		LoadCoversInternal(true, true);
	}

	// Bind to navigation generation
	if (UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(GetWorld()))
	{
		NavSystem->OnNavigationGenerationFinishedDelegate.AddDynamic(this, &ACoverSystem::OnNavigationGenerationFinished);
		NavSystem->OnNavDataRegisteredEvent.AddDynamic(this, &ACoverSystem::OnNavigationGenerationFinished);
	}

	OccupiedCovers.Empty();

#if WITH_EDITOR
	// Debug cleanup of preview
	if (bFlushPreviewDebug)
	{
		FlushPersistentDebugLines(GetWorld());
	}
#endif
}

void ACoverSystem::EndPlay(EEndPlayReason::Type Reason)
{
	bTearingDown = true;

	// Unbind navigation delegate	
	if (UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(GetWorld()))
	{
		NavSystem->OnNavigationGenerationFinishedDelegate.RemoveDynamic(this, &ACoverSystem::OnNavigationGenerationFinished);
		NavSystem->OnNavDataRegisteredEvent.RemoveDynamic(this, &ACoverSystem::OnNavigationGenerationFinished);
	}

	CleanupSystem();

	// shutdown the worker thread
	if (WorkerTaskManager)
	{
		delete WorkerTaskManager;
	}
	WorkerTaskManager = nullptr;
	
	if (ensure(GCoverSystem::GCoverSystems.Contains(GetWorld())))
	{
		if (ensure(GCoverSystem::GCoverSystems[GetWorld()] == this))
		{
			GCoverSystem::GCoverSystems.Remove(GetWorld());
		}
	}

	Super::EndPlay(Reason);
}

void ACoverSystem::BeginDestroy()
{
	ensure(Proxy == nullptr && "Cover system proxy wasn't null. Possible memory leak happening!");
	ensure(PartitionProxyDeleteQueue.Num() == 0);
	ensure(PartitionProxies.Num() == 0);
	Super::BeginDestroy();
}

void ACoverSystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	SCOPE_CYCLE_COUNTER(STAT_TickCoverSystem);

	// Tick proxy per frame from game thread to handle task lifetime
	{
		SCOPE_CYCLE_COUNTER(STAT_ObserveAsyncTasks);

		if (Proxy)
		{
			Proxy->UpdateTaskProgress(DeltaTime);
		}

		for (TPair<FCoverPartitionHash, FCoverSystemProxy*>& Iterator : PartitionProxies)
		{
			FCoverSystemProxy* PartitionProxy = Iterator.Value;
			check(PartitionProxy);
			PartitionProxy->UpdateTaskProgress(DeltaTime);
		}

		// Release partition proxies from delete queue when they have completed their tasks or they are still pending in queue
		for (int32 Idx = PartitionProxyDeleteQueue.Num() - 1; Idx >= 0; Idx--)
		{
			FCoverSystemProxy* PartitionProxy = PartitionProxyDeleteQueue[Idx];
			check(PartitionProxy);
			if (!PartitionProxy->IsWorkerTaskDispatched()
				|| PartitionProxy->IsWorkerTaskInQueue())
			{
				PartitionProxy->Release();
				delete PartitionProxy;
				PartitionProxyDeleteQueue.RemoveAt(Idx, 1);
			}
		}
	}

	// Run work on task manager
	if (WorkerTaskManager)
	{
		WorkerTaskManager->RunWork();
	}

	if (CoverSystemMode == ECoverSystemMode::Dynamic)
	{
		if (bUsePartitions)
		{
			// Update active partions and start regeneration of dirty partitions
			PartitionUpdateIntervalTimer -= DeltaTime;
			if (PartitionUpdateIntervalTimer <= 0)
			{
				SCOPE_CYCLE_COUNTER(STAT_UpdatePartitions);
				PartitionUpdateIntervalTimer = PartitionEvaluateInterval;
				UpdatePartitions(PartitionEvaluateInterval);
			}

			PartitionRegenerateIntervalTimer -= DeltaTime;
			if(PartitionRegenerateIntervalTimer <= 0)
			{
				SCOPE_CYCLE_COUNTER(STAT_RegeneratePartitions);
				PartitionRegenerateIntervalTimer = PartitionRegenerateInterval;
				RegenerateDirtyPartitions();
			}
		}
		else
		{
			// Generate again, if the state is dirty
			if (bDirtyGeneration)
			{
				GenerateCovers(true, true, false);
			}
		}

		UpdateNavigationRegenerate(DeltaTime);
	}
	
	if (bDebugDraw)
	{
		SCOPE_CYCLE_COUNTER(STAT_DebugDrawCovers);
		if (bUsePartitions)
		{
			for (TPair<FCoverPartitionHash, FCoverSystemProxy*>& Iterator : PartitionProxies)
			{
				FCoverPartitionHash Hash = Iterator.Key;
				FCoverSystemProxy* PartitionProxy = Iterator.Value;
				DebugDraw(GetWorld(), PartitionProxy, false, &Hash);
			}
		}
		else
		{
			DebugDraw(GetWorld(), Proxy);
		}
	}
}

void ACoverSystem::OnNavigationGenerationFinished(class ANavigationData* NavData)
{
	CachedNavDataBounds.Reset();
	if (bGenerateOnNavigationRebuild)
	{
		NotifyNavigationRebuilt();
	}
}

ACoverSystem* ACoverSystem::GetCoverSystem(const UObject* WorldContext)
{
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::LogAndReturnNull);
	if (World)
	{
		// First find from register. This is available only runtime for faster access
		TWeakObjectPtr<ACoverSystem>* CoverSystem = GCoverSystem::GCoverSystems.Find(World);
		if (CoverSystem && CoverSystem->IsValid())
		{
			return CoverSystem->Get();
		}

		// If not in register search from the level (slower)
		TActorIterator<ACoverSystem> Itr(World);
		if (Itr)
		{
			return *Itr;
		}
	}
	return nullptr;
}

void ACoverSystem::GenerateCovers(bool bForceRegenerate, bool bAsync, bool bDeferGenerationIfBusy)
{
	if (!Proxy)
	{
		return;
	}

#if WITH_EDITOR
	// Debug cleanup of preview
	if (bFlushPreviewDebug)
	{
		FlushPersistentDebugLines(GetWorld());
	}
#endif

	if (CoverSystemMode == ECoverSystemMode::Static)
	{
		UE_LOG(LogCoverSystem, Warning, TEXT("Covers can't be generated when mode is set to Static! Use BakeStaticCovers instead!"));
		return;
	}

	GenerateCoversInternal(bForceRegenerate, bAsync, bDeferGenerationIfBusy);
}

void ACoverSystem::InvalidatePartitions(const FBoxSphereBounds& InBounds, bool bRegenerateImmediately)
{
	if (!bUsePartitions)
	{
		UE_LOG(LogCoverSystem, Warning, TEXT("Invalidate partitions can only be used when cover system uses partitioning!"));
		return;
	}

	if (CoverSystemMode == ECoverSystemMode::Static)
	{
		return; // Static mode can't invalidate
	}

	// Solve all partition hashes that fall into the given bound and remove them from generated partitions, so they are regenerated
	const FBox BoundBox = InBounds.GetBox();
	TArray<FCoverPartitionHash, TInlineAllocator<16>> Partitions;
	FCoverPartitionHash::AppendPartitionsWithinBounds<TArray<FCoverPartitionHash, TInlineAllocator<16>>>(BoundBox.GetCenter(), BoundBox.GetExtent(), PartitionSize, Partitions);

	for (const FCoverPartitionHash& Hash : Partitions)
	{
		if (ActivePartitions.Contains(Hash))
		{
			DirtyPartitions.Add(Hash);
		}
		if (bRegenerateImmediately)
		{
			GeneratedPartitions.Remove(Hash);
		}
	}

	if (bRegenerateImmediately)
	{
		GenerateCoversInternal(false, true, true);
	}
}

void ACoverSystem::NotifyNavigationRebuilt()
{
	bNavigationRebuiltRecently = true;
}

void ACoverSystem::RebaseOrigin(const FVector& NewOrigin, bool bRegenerate)
{
	if (CoverSystemMode == ECoverSystemMode::Static)
	{
		UE_LOG(LogCoverSystem, Warning, TEXT("Can't rebase cover system origin while the cover mode is Static!"));
		return;
	}

	if (bUsePartitions)
	{
		UE_LOG(LogCoverSystem, Warning, TEXT("Can't rebase cover system origin while using partitions!"));
		return;
	}

	SetActorLocation(NewOrigin);

	WholeSystemBound.Reset();
	CacheWholeSystemBound();

	if (bRegenerate)
	{
		bDirtyManualCoverPointData = true;
		GenerateCovers(true, true, true);
	}
}

bool ACoverSystem::GetCoverData(const FCoverHandle& CoverHandle, FCoverData& OutData) const
{
	if (!Proxy)
	{
		return false;
	}

	FCEntityId Id = CoverMemoryUtils::ConvertHandle(CoverHandle);
	bool bFound = false;
	FCoverData Data;

	Proxy->AccessMemory([Id, &bFound, &Data](FCoverMemory* Memory)
		{
			check(Memory);
			check(Memory->GetEntities());
			bFound = Memory->GetEntities()->IsValidId(Id);
			if (bFound)
			{
				Data = (*Memory->GetEntities())[Id].Data;
			}
		});

	OutData = Data;
	return bFound;
}

void ACoverSystem::GetCoversWithinBounds(const FBoxCenterAndExtent& InBounds, TArray<FCoverHandle>& OutCoverHandles) const
{
	if (bUsePartitions)
	{
		const FBox BoundBox = InBounds.GetBox();
		TArray<FCoverPartitionHash, TInlineAllocator<16>> Partitions;
		FCoverPartitionHash::AppendPartitionsWithinBounds<TArray<FCoverPartitionHash, TInlineAllocator<16>>>(BoundBox.GetCenter(), BoundBox.GetExtent(), PartitionSize, Partitions);

		for (const FCoverPartitionHash& Partition : Partitions)
		{
			FCoverSystemProxy* const* PartitionProxyPtr = PartitionProxies.Find(Partition);
			if (PartitionProxyPtr)
			{
				FCoverSystemProxy* PartitionProxy = *(PartitionProxyPtr);
				check(PartitionProxy);

				TArray<FCEntityId> EntityIds;
				PartitionProxy->AccessMemory([=, &EntityIds](FCoverMemory* Memory)
					{
						check(Memory);
						FCoverSolver::GetCoversWithinBounds(InBounds, Memory, EntityIds);
					});

				OutCoverHandles.Reserve(EntityIds.Num() + OutCoverHandles.Num());
				for (FCEntityId Id : EntityIds)
				{
					OutCoverHandles.Add(CoverMemoryUtils::ConvertEntityId(Id, Partition));
				}
			}
		}
	}
	else
	{
		static FCoverPartitionHash StaticPartitionHash;
		if (!Proxy)
		{
			return;
		}

		TArray<FCEntityId> EntityIds;
		Proxy->AccessMemory([=, &EntityIds](FCoverMemory* Memory)
			{
				check(Memory);
				FCoverSolver::GetCoversWithinBounds(InBounds, Memory, EntityIds);
			});

		OutCoverHandles.Reserve(EntityIds.Num());
		for (FCEntityId Id : EntityIds)
		{
			OutCoverHandles.Add(CoverMemoryUtils::ConvertEntityId(Id, StaticPartitionHash));
		}
	}
}

void ACoverSystem::GetCoversAndDataWithinBounds(const FBoxCenterAndExtent& InBounds, TArray<FCover>& OutCovers) const
{
	if (bUsePartitions)
	{
		const FBox BoundBox = InBounds.GetBox();
		TArray<FCoverPartitionHash, TInlineAllocator<16>> Partitions;
		FCoverPartitionHash::AppendPartitionsWithinBounds<TArray<FCoverPartitionHash, TInlineAllocator<16>>>(BoundBox.GetCenter(), BoundBox.GetExtent(), PartitionSize, Partitions);

		for (const FCoverPartitionHash& Partition : Partitions)
		{
			FCoverSystemProxy* const* PartitionProxyPtr = PartitionProxies.Find(Partition);
			if (PartitionProxyPtr)
			{
				FCoverSystemProxy* PartitionProxy = *(PartitionProxyPtr);
				check(PartitionProxy);

				TArray<FCover> Covers;
				PartitionProxy->AccessMemory([=, &Covers](FCoverMemory* Memory)
					{
						check(Memory);
						check(Memory->GetEntities());

						TArray<FCEntityId> EntityIds;
						FCoverSolver::GetCoversWithinBounds(InBounds, Memory, EntityIds);

						Covers.Reserve(EntityIds.Num());
						for (FCEntityId EntityId : EntityIds)
						{
							const FCoverEntity& Entity = (*Memory->GetEntities())[EntityId];
							Covers.Add(FCover(CoverMemoryUtils::ConvertEntityId(EntityId, Partition), Entity.Data));
						}
					});
				OutCovers.Append(Covers);
			}
		}
	}
	else
	{
		static FCoverPartitionHash StaticPartitionHash;
		if (!Proxy)
		{
			return;
		}

		TArray<FCover> Covers;
		Proxy->AccessMemory([=, &Covers](FCoverMemory* Memory)
			{
				check(Memory);
				check(Memory->GetEntities());

				TArray<FCEntityId> EntityIds;
				FCoverSolver::GetCoversWithinBounds(InBounds, Memory, EntityIds);

				Covers.Reserve(EntityIds.Num());
				for (FCEntityId EntityId : EntityIds)
				{
					const FCoverEntity& Entity = (*Memory->GetEntities())[EntityId];
					Covers.Add(FCover(CoverMemoryUtils::ConvertEntityId(EntityId, StaticPartitionHash), Entity.Data));
				}
			});

		OutCovers = Covers;
	}
}

void ACoverSystem::GetCoversWithinBounds(const FBoxSphereBounds& InBounds, TArray<FCoverHandle>& OutCoverHandles) const
{
	GetCoversWithinBounds(FBoxCenterAndExtent(InBounds.GetBox()), OutCoverHandles);
}

void ACoverSystem::GetCoverDataWithinBounds(const FBoxSphereBounds& InBounds, TArray<FCover>& OutCovers) const
{
	GetCoversAndDataWithinBounds(FBoxCenterAndExtent(InBounds.GetBox()), OutCovers);
}

bool ACoverSystem::OccupyCover(AController* Controller, const FCoverHandle& CoverHandle)
{
	if (!Controller)
	{
		return false;
	}

	// Check current occupier
	AController* Occupier = GetOccupyingController(CoverHandle);
	if (Occupier)
	{
		// Already occupied by someone else
		if (Controller != Occupier)
		{
			return false;
		}
		else // Occupy success because the controller was already occupying the cover
		{
			return true;
		}
	}

	OccupiedCovers.Add(CoverHandle, Controller);
	return true;
}

void ACoverSystem::UnOccupyCover(const FCoverHandle& CoverHandle)
{
	OccupiedCovers.Remove(CoverHandle);
}

void ACoverSystem::UnOccupyCoverFromController(const FCoverHandle& CoverHandle, AController* Controller)
{
	if (!Controller)
	{
		return;
	}

	AController* Occupier = GetOccupyingController(CoverHandle);
	if (Occupier == Controller)
	{
		UnOccupyCover(CoverHandle);
	}
}

AController* ACoverSystem::GetOccupyingController(const FCoverHandle& CoverHandle) const
{
	const TWeakObjectPtr<AController>* ControllerPtr = OccupiedCovers.Find(CoverHandle);
	if (ControllerPtr && (*ControllerPtr).IsValid())
	{
		return (*ControllerPtr).Get();
	}
	return nullptr;
}

bool ACoverSystem::IsCoverOccupied(const FCoverHandle& CoverHandle) const
{
	return GetOccupyingController(CoverHandle) != nullptr;
}

bool ACoverSystem::IsAsyncWorkerRunning() const
{
	bool bRunning = false;
	if (Proxy && Proxy->IsWorkerTaskDispatched())
	{
		bRunning = true;
	}

	if (bRunning)
	{
		for (const TPair<FCoverPartitionHash, FCoverSystemProxy*>& Iterator : PartitionProxies)
		{
			if (Iterator.Value->IsWorkerTaskDispatched())
			{
				bRunning = true;
				break;
			}
		}
	}

	return bRunning;
}

void ACoverSystem::RegisterManualCoverPoint(AManualCoverPoint* CoverPoint)
{
	if (bTearingDown || !CoverPoint)
	{
		return;
	}

	if (CoverSystemMode == ECoverSystemMode::Static)
	{
		// Static mode can skip registration of manual cover points.
		return;
	}

	check(!ManualCoverPoints.Contains(CoverPoint));
	TCEntities<GCoverSystem::FManualCoverPointData>& ManualEntities = GCoverSystem::ManualCoverPointEntityLookup.FindOrAdd(this);

	GCoverSystem::FManualCoverPointData Data;
	Data.CoverData = CoverPoint->GetCoverData();

	const FCEntityId EntityId = ManualEntities.Insert(MoveTemp(Data));
	ManualEntities[EntityId].EntityId = EntityId;
	ManualCoverPoints.Add(CoverPoint, CoverMemoryUtils::ConvertEntityId(EntityId, FCoverPartitionHash()));

	bDirtyManualCoverPointData = true;
}

void ACoverSystem::UnregisterManualCoverPoint(AManualCoverPoint* CoverPoint)
{
	if (bTearingDown || !CoverPoint)
	{
		return;
	}

	if (CoverSystemMode == ECoverSystemMode::Static)
	{
		// Static mode can skip unregistration of manual cover points.
		return;
	}

	check(ManualCoverPoints.Contains(CoverPoint));

	TCEntities<GCoverSystem::FManualCoverPointData>& ManualEntities = GCoverSystem::ManualCoverPointEntityLookup.FindOrAdd(this);
	ManualEntities.Remove(CoverMemoryUtils::ConvertHandle(ManualCoverPoints[CoverPoint]));
	ManualCoverPoints.Remove(CoverPoint);

	bDirtyManualCoverPointData = true;
}

void ACoverSystem::RegisterPartitionInvoker(UCoverPartitionInvokerComponent* Invoker)
{
	if (!IsValid(Invoker))
	{
		UE_LOG(LogCoverSystem, Error, TEXT("Invalid invoker tried to register into cover system!"));
		return;
	}

	PartitionInvokers.AddUnique(Invoker);
}

void ACoverSystem::UnregisterPartitionInvoker(UCoverPartitionInvokerComponent* Invoker)
{
	if (!IsValid(Invoker))
	{
		UE_LOG(LogCoverSystem, Error, TEXT("Invalid invoker tried to unregister from cover system!"));
		return;
	}

	PartitionInvokers.Remove(Invoker);
}

void ACoverSystem::GenerateCoversInternal(bool bForceRegenerate, bool bAsync, bool bDeferGenerationIfBusy, const TSet<FCoverPartitionHash>* BuildOnlyPartitionSet)
{
#if WITH_EDITOR
	// Debug cleanup of preview
	if (bFlushPreviewDebug)
	{
		FlushPersistentDebugLines(GetWorld());
	}
#endif

	SCOPE_CYCLE_COUNTER(STAT_GenerateCovers);

	// Set trace channel
	FCoverSolver::SetCoverTraceChannel(TraceChannel.GetValue());

	// Set static solver parameters
	FCoverSolver::SetCoverSolveParameters(BuildParams.AgentMaxWidth, BuildParams.AgentMaxHeightStanding, BuildParams.AgentMaxHeightCrouch);

	// Fetch manual cover entity data and give it to GenerateCovers as additional data
	if (bDirtyManualCoverPointData)
	{
		RebuildManualCoverPointData();
	}

	// Determine if we are going to run this in async or sync mode
	const bool bAsyncTask = bEnableAsyncMode ? bAsync : false;

	if (bUsePartitions)
	{
		if (bForceRegenerate)
		{
			// Clear generated partitions if force generate is true to force all partitions to be regenerated
			GeneratedPartitions.Empty();
		}

		struct FPartitionBuildCommand
		{
			FCoverPartitionHash Hash;
			float TimeSinceGenerate;

			FPartitionBuildCommand() 
			{
				TimeSinceGenerate = 0;
			}
			FPartitionBuildCommand(const FCoverPartitionHash& InHash)
			{
				Hash = InHash;
				TimeSinceGenerate = 0;
			}
			FPartitionBuildCommand(const FCoverPartitionHash& InHash, float InTimeSinceGenerate)
			{
				Hash = InHash;
				TimeSinceGenerate = InTimeSinceGenerate;
			}
		};


		TArray<FPartitionBuildCommand, TInlineAllocator<64>> BuildHashList;
		for (const FCoverPartitionHash& Hash : ActivePartitions)
		{
			// If we were provided a set of "build only", skip the partition if it is not contained in the list
			if (BuildOnlyPartitionSet != nullptr && !BuildOnlyPartitionSet->Contains(Hash))
			{
				continue;
			}

			// Create build command
			FPartitionBuildCommand BuildCommand(Hash);
			if (bSortPartitionsByPriority)
			{
				// If we are sorting soon, get time since last generate from the proxy
				FCoverSystemProxy** PartitionProxyPtr = PartitionProxies.Find(Hash);
				if (PartitionProxyPtr)
				{
					BuildCommand.TimeSinceGenerate = (*PartitionProxyPtr)->GetTimeSinceLastGenerate();
				}
			}
			BuildHashList.Add(BuildCommand);
		}

		if (bSortPartitionsByPriority)
		{
			// Sort by time since last generate, so oldest are queued first
			BuildHashList.Sort([](const FPartitionBuildCommand& A, const FPartitionBuildCommand& B)
				{
					return A.TimeSinceGenerate > B.TimeSinceGenerate;
				});
		}

		for (const FPartitionBuildCommand& Command : BuildHashList)
		{
			const FCoverPartitionHash& Hash = Command.Hash;
			if (!GeneratedPartitions.Contains(Hash))
			{
				// Not yet generated
				if (PartitionProxies.Contains(Hash))
				{
					FCoverSystemProxy* PartitionProxy = PartitionProxies[Hash];
					check(PartitionProxy);

					// Generate the proxy
					if (PartitionProxy->GenerateCovers(bAsyncTask, true, GetWorld(), BuildParams, Hash.GetPartitionOrigin(PartitionSize), PartitionSize * 0.5, &CachedManualCoverPointData))
					{
						GeneratedPartitions.Add(Hash);
						DirtyPartitions.Remove(Hash); // No longer dirty
					}
					else
					{
						if (bDeferGenerationIfBusy)
						{
							DirtyPartitions.Add(Hash); // Make the partition dirty if we were asked to regenerate it in case the async builder was still running
						}
					}
				}
			}
		}
	}
	else
	{
		if (!Proxy)
		{
			return;
		}

		if (Proxy->GenerateCovers(bAsyncTask, bForceRegenerate, GetWorld(), BuildParams, CalculateOctreeOrigin(), CalculateOctreeExtent(), &CachedManualCoverPointData))
		{
			bDirtyGeneration = false; // Build was started, so generation is no more dirty
		}
		else // Build was not started
		{
			// Defer generation on failed attempt and try again later
			if (bDeferGenerationIfBusy)
			{
				bDirtyGeneration = true;
			}
		}
	}
}

void ACoverSystem::UpdateNavigationRegenerate(float DeltaTime)
{
	// Update NAV rebuild generation
	NavigationDirtyIntervalTimer -= DeltaTime;
	if (bNavigationRebuiltRecently)
	{
		if (NavigationDirtyIntervalTimer <= 0)
		{
			NavigationDirtyIntervalTimer = MaxNavigationRegenerateInterval; // Do this again only when next interval has passed
			if (bUsePartitions)
			{
				// Invalidate all active partitions, so we regenerate them
				DirtyPartitions = ActivePartitions;
			}
			else
			{
				bDirtyGeneration = true;
			}
			bNavigationRebuiltRecently = false; // Handled
		}
	}
}

void ACoverSystem::UpdatePartitions(float DeltaTime)
{
	if (!bUsePartitions)
	{
		return;
	}

	UWorld* World = GetWorld();
	check(World);

	TArray<FVector> InvokerLocations;
	{
		SCOPE_CYCLE_COUNTER(STAT_CollectInvokers);
		GetPartitionInvokerLocations(InvokerLocations);
	}

	// Update active partitions
	TSet<FCoverPartitionHash> PreviousActivePartitions = ActivePartitions;
	ActivePartitions.Empty();

	{
		SCOPE_CYCLE_COUNTER(STAT_CollectActivePartitions);
		for (const FVector& Location : InvokerLocations)
		{
			// First get all partitions that fall into generation range
			TArray<FCoverPartitionHash, TInlineAllocator<32>> Partitions;
			{
				SCOPE_CYCLE_COUNTER(STAT_FindPartitionsWithinBound);
				FCoverPartitionHash::AppendPartitionsWithinBounds<TArray<FCoverPartitionHash, TInlineAllocator<32>>>(Location, FVector(PartitionGenerationRange, PartitionGenerationRange, PartitionGenerationRangeZ), PartitionSize, Partitions);
			}

			// Pick partitions as active partitions
			{
				SCOPE_CYCLE_COUNTER(STAT_ExcludePartitions);
				const FBox NavDataBounds = ComputeNavDataBounds();
				for (const FCoverPartitionHash& Hash : Partitions)
				{
					// Add as active partition if the partition intersects with cover system bounds and navigation bounds
					if (IsPartitionWithinSystemBounds(Hash) &&
						NavDataBounds.Intersect(Hash.GetPartitionBound(PartitionSize)))
					{
						ActivePartitions.Add(Hash);
						DeactivatedPartitions.Remove(Hash); // If was deactivated, now it isn't
					}
				}
			}
		}
	}

	// Find out what partitions went deactive
	{
		SCOPE_CYCLE_COUNTER(STAT_DestroyDeactivePartitions);
		for (const FCoverPartitionHash& Hash : PreviousActivePartitions)
		{
			if (!ActivePartitions.Contains(Hash))
			{
				DeactivatedPartitions.Add(Hash, 0.0f);
				DirtyPartitions.Remove(Hash);
			}
		}

		// Remove deactivated partitions from the system
		TArray<FCoverPartitionHash, TInlineAllocator<16>> RemoveDeactivePartitionArray;
		for (TPair<FCoverPartitionHash, float>& Iterator : DeactivatedPartitions)
		{
			float& TimeDeactiveRef = Iterator.Value;
			TimeDeactiveRef += DeltaTime;
			if (TimeDeactiveRef > DeactivePartitionDestroyDelay)
			{
				RemoveDeactivePartitionArray.Add(Iterator.Key);
			}
		}
		for (const FCoverPartitionHash& Hash : RemoveDeactivePartitionArray)
		{
			DeactivatedPartitions.Remove(Hash);
			GeneratedPartitions.Remove(Hash);
			if (PartitionProxies.Contains(Hash))
			{
				FCoverSystemProxy* PartitionProxy = PartitionProxies[Hash];
				PartitionProxyDeleteQueue.Add(PartitionProxy);
				PartitionProxies.Remove(Hash);
				DirtyPartitions.Remove(Hash);
			}
		}
	}

	// Create proxy for partitions that are active and not having one yet
	{
		SCOPE_CYCLE_COUNTER(STAT_CreatePartitionProxies);
		for (const FCoverPartitionHash& Hash : ActivePartitions)
		{
			if (!PartitionProxies.Contains(Hash))
			{
				FCoverSystemProxy* PartitionProxy = new FCoverSystemProxy();
				PartitionProxy->Initialize(WorkerTaskManager);
				PartitionProxies.Add(Hash, PartitionProxy);
				DirtyPartitions.Add(Hash);
			}
		}
	}
}

void ACoverSystem::RegenerateDirtyPartitions()
{
	if (!bUsePartitions)
	{
		return;
	}

	if (DirtyPartitions.Num() <= 0)
	{
		return;
	}

	// Cleanup first dirty partitions that are not relevant (not having proxy)
	TArray<FCoverPartitionHash, TInlineAllocator<16>> InvalidDirtyPartitions;
	for (const FCoverPartitionHash& Hash : DirtyPartitions)
	{
		if (!PartitionProxies.Contains(Hash))
		{
			InvalidDirtyPartitions.Add(Hash);
		}
	}
	for (const FCoverPartitionHash& Hash : InvalidDirtyPartitions)
	{
		DirtyPartitions.Remove(Hash);
	}

	// Generate all partitions in parallel by not limiting build job num by default
	TSet<FCoverPartitionHash> PartitionBuildSet = DirtyPartitions;
	if (PartitionBuildTaskQueueSize > 0)
	{
		// Find out active build job count
		int32 ActiveJobNum = 0;
		for (TPair<FCoverPartitionHash, FCoverSystemProxy*>& Iterator : PartitionProxies)
		{
			FCoverSystemProxy* PartitionProxy = Iterator.Value;
			check(PartitionProxy);
			if (PartitionProxy->IsWorkerTaskDispatched())
			{
				ActiveJobNum++;
			}
			if (ActiveJobNum > PartitionBuildTaskQueueSize)
			{
				return;
			}
		}

		// Find out how many jobs we can start
		const int32 FreeJobNum = PartitionBuildTaskQueueSize - ActiveJobNum;
		if (FreeJobNum > 0)
		{
			// Pick partitions to generate
			PartitionBuildSet.Reset();

			int32 JobsQueued = 0;
			for (const FCoverPartitionHash& Hash : DirtyPartitions)
			{
				PartitionBuildSet.Add(Hash);
				JobsQueued++;
				if (JobsQueued >= PartitionBuildTaskQueueSize)
				{
					break;
				}
			}
		}
		else
		{
			// Else there were no space for more jobs, try again later
			return;
		}
	}

	for (const FCoverPartitionHash& Hash : PartitionBuildSet)
	{
		// Mark as not generated, so we start building it again
		GeneratedPartitions.Remove(Hash);
	}

	GenerateCoversInternal(false, true, true, &PartitionBuildSet);
}

void ACoverSystem::LoadCoversInternal(bool bAsync, bool bDeserializeArchive)
{
	if (!ensure(CoverSystemMode == ECoverSystemMode::Static))
	{
		return;
	}

#if WITH_EDITOR
	// Debug cleanup of preview
	if (bFlushPreviewDebug)
	{
		FlushPersistentDebugLines(GetWorld());
	}
#endif

	const bool bUseAsync = bAsync && bAsyncLoadStaticCovers;
	if (bUsePartitions)
	{
		// Load all
		for (TPair<FCoverPartitionHash, FCoverSerializedArchive>& Iterator : PartitionedStaticCoverArchives)
		{
			const FCoverPartitionHash& Hash = Iterator.Key;
			FCoverSerializedArchive& Archive = Iterator.Value;

			// Create proxy for partitions and ask to load it
			if (!PartitionProxies.Contains(Hash))
			{
				FCoverSystemProxy* PartitionProxy = new FCoverSystemProxy();
				PartitionProxy->Initialize(WorkerTaskManager);
				PartitionProxies.Add(Hash, PartitionProxy);

				const bool bLoadTaskSuccess = PartitionProxy->LoadCoversFromArchive(Archive, bUseAsync, Hash.GetPartitionOrigin(PartitionSize), PartitionSize * 0.5, bDeserializeArchive);
				ensure(bLoadTaskSuccess);
			}
		}
	}
	else
	{
		// Ask proxy to load the covers from archive
		check(Proxy);
		if (Proxy->LoadCoversFromArchive(StaticCoverArchive, bUseAsync, CalculateOctreeOrigin(), CalculateOctreeExtent(), bDeserializeArchive))
		{
			bDirtyGeneration = false;
		}
	}
}

void ACoverSystem::GetPartitionInvokerLocations(TArray<FVector>& OutLocations) const
{
	if (!GetWorld())
	{
		return;
	}

	// Collect player pawns as invokers
	TSet<AActor*> InvokerActors;
	if (bUsePlayerPawnsAsPartitionInvokers)
	{
		const int32 NumPlayers = UGameplayStatics::GetNumPlayerControllers(this);
		for (int32 Idx = 0; Idx < NumPlayers; Idx++)
		{
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, Idx))
			{
				if (APawn* PlayerPawn = PC->GetPawn())
				{
					InvokerActors.Add(PlayerPawn);
				}
			}
		}
	}

	for (AActor* Invoker : InvokerActors)
	{
		check(Invoker);
		OutLocations.Add(Invoker->GetActorLocation());
	}

	// Collect invoker components
	for (const TObjectPtr<UCoverPartitionInvokerComponent>& InvokerComp : PartitionInvokers)
	{
		if (IsValid(InvokerComp))
		{
			OutLocations.Add(InvokerComp->GetInvokerLocation());
		}
	}
}

FVector ACoverSystem::CalculateOctreeOrigin() const
{
	check(SystemBoundsComponent);
	FVector Origin = SystemBoundsComponent->GetComponentLocation();
	Origin.X = (double)FMath::TruncToInt(Origin.X);
	Origin.Y = (double)FMath::TruncToInt(Origin.Y);
	Origin.Z = (double)FMath::TruncToInt(Origin.Z);
	return Origin;
}

double ACoverSystem::CalculateOctreeExtent() const
{
	check(SystemBoundsComponent);
	const FVector ScaledExtent = SystemBoundsComponent->GetScaledBoxExtent();
	return FMath::Max(FMath::Max(ScaledExtent.X, ScaledExtent.Y), ScaledExtent.Z);
}

void ACoverSystem::RebuildManualCoverPointData()
{
	if (bTearingDown)
	{
		return;
	}

	const FBoxCenterAndExtent BoundCenterAndExtent = FBoxCenterAndExtent(CalculateOctreeOrigin(), FVector(CalculateOctreeExtent()));
	const FBox BoundingBox = BoundCenterAndExtent.GetBox();
	
	TCEntities<GCoverSystem::FManualCoverPointData>& ManualEntities = GCoverSystem::ManualCoverPointEntityLookup.FindOrAdd(this);

	CachedManualCoverPointData.Empty();
	CachedManualCoverPointData.Reserve(ManualEntities.Num());

	for (const GCoverSystem::FManualCoverPointData& ManualCoverData : ManualEntities)
	{
		if (FMath::PointBoxIntersection(ManualCoverData.CoverData.Location, BoundingBox))
		{
			CachedManualCoverPointData.Add(ManualCoverData.CoverData);
		}
	}
}

bool ACoverSystem::IsPartitionWithinSystemBounds(const FCoverPartitionHash& Hash) const
{
	CacheWholeSystemBound();
	check(WholeSystemBound.IsSet());
	return Hash.GetPartitionBound(PartitionSize).Intersect(WholeSystemBound.GetValue());
}


void ACoverSystem::CacheWholeSystemBound() const
{
	if (!WholeSystemBound.IsSet())
	{
		check(SystemBoundsComponent);
		const FVector Origin = SystemBoundsComponent->GetComponentLocation();
		const FVector Extent = SystemBoundsComponent->GetScaledBoxExtent();
		WholeSystemBound = FBoxCenterAndExtent(Origin, Extent).GetBox();
	}
}

FBox ACoverSystem::ComputeNavDataBounds() const
{
	if (CachedNavDataBounds.IsSet())
	{
		return CachedNavDataBounds.GetValue();
	}

	FBox TotalBounds = FBox(ForceInit);

	UWorld* World = GetWorld();
	const UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetNavigationSystem(World);
	if (NavSystem)
	{
		const TSet<FNavigationBounds>& BoundSet = NavSystem->GetNavigationBounds();

		
		for (const FNavigationBounds& NavBound : BoundSet)
		{
			TotalBounds += NavBound.AreaBox;
		}

		CachedNavDataBounds = TotalBounds;
	}

	return TotalBounds;
}

void ACoverSystem::CleanupSystem()
{
	// Delete proxy object. Syncs any left tasks here
	if (Proxy)
	{
		Proxy->Release();
		delete Proxy;
		Proxy = nullptr;
	}

	// Release all partitioned proxies
	for (TPair<FCoverPartitionHash, FCoverSystemProxy*>& Iterator : PartitionProxies)
	{
		FCoverSystemProxy* PartitionProxy = Iterator.Value;
		PartitionProxy->Release();
		delete PartitionProxy;
	}
	PartitionProxies.Empty();

	// Release all proxies that were in delete queue
	for (FCoverSystemProxy* PartitionProxy : PartitionProxyDeleteQueue)
	{
		PartitionProxy->Release();
		delete PartitionProxy;
	}
	PartitionProxyDeleteQueue.Empty();

	PartitionProxies.Empty();
	DirtyPartitions.Empty();
	ActivePartitions.Empty();
	GeneratedPartitions.Empty();
	CachedManualCoverPointData.Empty();

	// Clear manual cover point entity lookup
	if (GCoverSystem::ManualCoverPointEntityLookup.Contains(this))
	{
		GCoverSystem::ManualCoverPointEntityLookup.Remove(this);
	}

	WholeSystemBound.Reset();
}

void ACoverSystem::DebugDraw(UWorld* World, FCoverSystemProxy* InProxy, bool bPersistent, const FCoverPartitionHash* Partition)
{
#if WITH_EDITOR

	if (!World)
	{
		return;
	}
	if (!InProxy)
	{
		return;
	}

	if (bPersistent)
	{
		bFlushPreviewDebug = true;
	}

	// Use actor origin as fallback
	FVector DebugOrigin = SystemBoundsComponent->GetComponentLocation();

	// Get viewport origin and use it instead of actor location if possible
	if (GEditor && GEditor->bIsSimulatingInEditor)
	{
		if (UUnrealEditorSubsystem* EditorSubSys = GEditor->GetEditorSubsystem<UUnrealEditorSubsystem>())
		{
			FVector VPCameraLocation;
			FRotator VPCameraRotation;
			if (EditorSubSys->GetLevelViewportCameraInfo(VPCameraLocation, VPCameraRotation))
			{
				DebugOrigin = VPCameraLocation;
			}
		}
	}
	else if(World->IsGameWorld())
	{
		// When not ejected, use player camera location
		if (APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(this, 0))
		{
			DebugOrigin = CameraManager->GetCameraLocation();
		}
	}

	const FCoverPartitionHash PartitionHash = Partition ? *Partition : FCoverPartitionHash();
	InProxy->AccessMemory([=, this, &PartitionHash](FCoverMemory* Memory)
		{
			const float DebugExtent = DebugDrawDistance;

			TArray<FCEntityId> EntityIds;
			FCoverSolver::GetCoversWithinBounds(FBoxCenterAndExtent(DebugOrigin, FVector(DebugExtent)), Memory, EntityIds);

			for (const FCEntityId& Id : EntityIds)
			{
				if (!Memory->GetEntities()->IsValidId(Id))
				{
					continue;
				}

				const FCoverEntity& Entity = (*Memory->GetEntities())[Id];
				const bool bOccupied = IsCoverOccupied(CoverMemoryUtils::ConvertEntityId(Id, PartitionHash));
				Deams51::FDeams51CoverGenerator::DebugDrawCover(World, Entity.Data, DebugExtent, BuildParams.AgentMaxHeightCrouch, BuildParams.AgentMaxHeightStanding, bOccupied, bPersistent);
			}

			if (bDrawOctreeBounds)
			{
				const FBoxCenterAndExtent OctreeBounds = Memory->GetOctree()->GetRootBounds();
				DrawDebugBox(World, OctreeBounds.Center, FVector(OctreeBounds.Extent), FQuat::Identity, FColor::Emerald, bPersistent);
			}
		});
#endif
}

#if WITH_EDITOR

void ACoverSystem::Preview()
{
	if (!ensure(Proxy == nullptr))
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (CoverSystemMode == ECoverSystemMode::Dynamic && bUsePartitions)
	{
		// Preview generate is disabled in partitioned mode
		UE_LOG(LogCoverSystem, Warning, TEXT("Preview generate is disabled for dynamic mode when using partitions."));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Red, TEXT("Preview generate is disabled for dynamic cover mode when using partitions."));
		}
		return;
	}

	if (CoverSystemMode == ECoverSystemMode::Dynamic)
	{
		// Search all manual cover points and cache them for the preview generation
		const FBoxCenterAndExtent BoundCenterAndExtent = FBoxCenterAndExtent(CalculateOctreeOrigin(), FVector(CalculateOctreeExtent()));
		const FBox BoundingBox = BoundCenterAndExtent.GetBox();
		CachedManualCoverPointData.Empty();

		for (TActorIterator<AManualCoverPoint> Itr(World); Itr; ++Itr)
		{
			AManualCoverPoint* ManualPointActor = *Itr;
			if (ManualPointActor)
			{
				const FCoverData ManualPointCoverData = ManualPointActor->GetCoverData();
				if (FMath::PointBoxIntersection(ManualPointCoverData.Location, BoundingBox))
				{
					CachedManualCoverPointData.Add(ManualPointCoverData);
				}
			}
		}

		// Create a new proxy and generate covers in sync mode for the preview
		Proxy = new FCoverSystemProxy();
		Proxy->Initialize(nullptr);

		GenerateCoversInternal(true, false, false);
	}
	else // Static
	{
		if (bUsePartitions)
		{
			// Deserialize only once for the preview
			if (!bPreviewHasLoadedArchive)
			{
				for (TPair<FCoverPartitionHash, FCoverSerializedArchive>& Iterator : PartitionedStaticCoverArchives)
				{
					Iterator.Value.ReadBytes();
				}
				bPreviewHasLoadedArchive = true;
			}
			// Load the covers into partitions
			LoadCoversInternal(false, false);
		}
		else
		{
			// Create a new proxy to load the covers in LoadCoversInternal
			Proxy = new FCoverSystemProxy();
			Proxy->Initialize(nullptr);

			LoadCoversInternal(false, !bPreviewHasLoadedArchive);
			bPreviewHasLoadedArchive = true;
		}
	}

	if (bUsePartitions)
	{
		for (TPair<FCoverPartitionHash, FCoverSystemProxy*>& Iterator : PartitionProxies)
		{
			FCoverPartitionHash Hash = Iterator.Key;
			FCoverSystemProxy* PartitionProxy = Iterator.Value;
			DebugDraw(GetWorld(), PartitionProxy, true, &Hash);
		}
	}
	else
	{
		DebugDraw(GetWorld(), Proxy, true);
	}

	CleanupSystem();
}

void ACoverSystem::CleanPreview()
{
	FlushPersistentDebugLines(GetWorld());
}

void ACoverSystem::BakeStaticCovers()
{
	if (CoverSystemMode != ECoverSystemMode::Static)
	{
		UE_LOG(LogCoverSystem, Warning, TEXT("Covers can be baked only if CoverSystemMode is set to Static!"));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Red, TEXT("Covers can be baked only if CoverSystemMode is set to Static!"));
		}
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UE_LOG(LogCoverSystem, Log, TEXT("Baking static cover data..."));

	this->Modify();

	{
		int32 BytesWritten = 0;
		int32 PartitionsCreated = 0;
		int32 CoversCreated = 0;

		// Show progress of the task
		FScopedSlowTask BakeTask(5, FText::FromString(TEXT("Building static covers...")));
		BakeTask.MakeDialog(false, false);

		// Cleanup first
		BakeTask.EnterProgressFrame(1.0f, FText::FromString(TEXT("Removing old data...")));
		StaticCoverArchive.Reset();
		PartitionedStaticCoverArchives.Empty();
		
		// Collect manual cover points
		BakeTask.EnterProgressFrame(1.0f, FText::FromString(TEXT("Collecting manual covers...")));

		const FBoxCenterAndExtent BoundCenterAndExtent = FBoxCenterAndExtent(CalculateOctreeOrigin(), FVector(CalculateOctreeExtent()));
		const FBox BoundingBox = BoundCenterAndExtent.GetBox();
		CachedManualCoverPointData.Empty();

		for (TActorIterator<AManualCoverPoint> Itr(World); Itr; ++Itr)
		{
			AManualCoverPoint* ManualPointActor = *Itr;
			if (ManualPointActor)
			{
				const FCoverData ManualPointCoverData = ManualPointActor->GetCoverData();
				if (FMath::PointBoxIntersection(ManualPointCoverData.Location, BoundingBox))
				{
					CachedManualCoverPointData.Add(ManualPointCoverData);
				}
			}
		}

		// Create a new proxy and generate covers in sync mode
		BakeTask.EnterProgressFrame(1.0f, FText::FromString(TEXT("Generating cover data...")));

		if (bUsePartitions)
		{
			// Create proxy per partition
			const UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetNavigationSystem(World);
			if (NavSystem)
			{
				// Get all partitions within the system
				TArray<FCoverPartitionHash, TInlineAllocator<128>> Partitions;
				FCoverPartitionHash::AppendPartitionsWithinBounds<TArray<FCoverPartitionHash, TInlineAllocator<128>>>(CalculateOctreeOrigin(), FVector(CalculateOctreeExtent()), PartitionSize, Partitions);

				// Pick partitions as active partitions
				ActivePartitions.Empty();

				const FBox NavDataBounds = ComputeNavDataBounds();
				for (const FCoverPartitionHash& Hash : Partitions)
				{
					// Add as active partition if the partition intersects with cover system bounds and navigation bounds
					if (IsPartitionWithinSystemBounds(Hash) &&
						NavDataBounds.Intersect(Hash.GetPartitionBound(PartitionSize)))
					{
						ActivePartitions.Add(Hash);
					}
				}

				// Create proxy for partitions
				for (const FCoverPartitionHash& Hash : ActivePartitions)
				{
					if (!PartitionProxies.Contains(Hash))
					{
						FCoverSystemProxy* PartitionProxy = new FCoverSystemProxy();
						PartitionProxy->Initialize(nullptr);
						PartitionProxies.Add(Hash, PartitionProxy);
					}
				}

				// Generate for active partitions that covers the whole bounds
				GenerateCoversInternal(true, false, false, nullptr);
			}

			// Archive the partitions
			BakeTask.EnterProgressFrame(1.0f, FText::FromString(TEXT("Archiving generated data...")));

			for (TPair<FCoverPartitionHash, FCoverSystemProxy*>& Iterator : PartitionProxies)
			{
				FCoverPartitionHash Hash = Iterator.Key;
				FCoverSystemProxy* PartitionProxy = Iterator.Value;
				check(PartitionProxy);

				// Save to the archive
				FCoverSerializedArchive& ArchiveRef = PartitionedStaticCoverArchives.FindOrAdd(Hash);
				PartitionProxy->AccessMemory([&ArchiveRef, &BytesWritten, &CoversCreated](FCoverMemory* Memory)
					{
						ArchiveRef.Save(Memory);
						BytesWritten += ArchiveRef.GetSerializedByteNum();
						CoversCreated += ArchiveRef.GetCoverDataNum();
					});

				// Remove ones that didn't have any data
				if (!ArchiveRef.HasSaveData())
				{
					PartitionedStaticCoverArchives.Remove(Hash);
				}
			}

			PartitionsCreated += PartitionedStaticCoverArchives.Num();
		}
		else
		{
			Proxy = new FCoverSystemProxy();
			Proxy->Initialize(nullptr);
			GenerateCoversInternal(true, false, false);

			// Store the generated covers in the memory to an archive
			BakeTask.EnterProgressFrame(1.0f, FText::FromString(TEXT("Archiving generated data...")));
			Proxy->AccessMemory([this](FCoverMemory* Memory)
				{
					StaticCoverArchive.Save(Memory);
				});

			BytesWritten += StaticCoverArchive.GetSerializedByteNum();
			CoversCreated += StaticCoverArchive.GetCoverDataNum();
		}
		
		
		// Delete the proxy after bake process is completed
		BakeTask.EnterProgressFrame(1.0f, FText::FromString(TEXT("Cleaning up...")));

		CleanupSystem();

		bPreviewHasLoadedArchive = false;

		const FString ResultMessage = FString::Printf(TEXT("Cover bake result: Bake size: %d bytes, generated %d covers in %d partitions"), BytesWritten, CoversCreated, PartitionsCreated);
		UE_LOG(LogCoverSystem, Log, TEXT("%s"), *ResultMessage);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Green, ResultMessage);
		}
	}	
}

void ACoverSystem::ResetStaticCovers()
{
	UE_LOG(LogCoverSystem, Log, TEXT("Removing static cover data..."));
	this->Modify();
	StaticCoverArchive.Reset();
	PartitionedStaticCoverArchives.Empty();
}

void ACoverSystem::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = (PropertyChangedEvent.Property != NULL) ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if (PropertyName == GET_MEMBER_NAME_CHECKED(ACoverSystem, bUsePartitions) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(ACoverSystem, PartitionSize) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(ACoverSystem, CoverSystemMode))
	{
		// Remove baked data if some of properties that affect it changes
		StaticCoverArchive.Reset();
		PartitionedStaticCoverArchives.Empty();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}


#endif