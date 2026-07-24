//// Copyright, (c) Sami Kangasmaa 2022

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Async/AsyncWork.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationSystem.h"
#include "Engine/World.h"

#include "CoverSystemPublicData.h"
#include "CoverWorkerThread.h"

class FCoverSystemProxy;
struct FCoverSerializedArchive;

/**
 * Task to generate cover data in background thread
 */
class FCoverGenerateTask : public FCoverThreadedTask
{
public:

	friend class TCoverWorkerTask<FCoverGenerateTask>;

	FCoverGenerateTask(UWorld* World, FCoverSystemProxy* InProxy, ARecastNavMesh* NavMesh, const FCoverBuildParams& Params, const FVector& InOrigin, double InExtent, const TArray<FCoverData>* AdditionalData);
	virtual ~FCoverGenerateTask();

	FCoverSystemProxy* Proxy = nullptr;
	TWeakObjectPtr<ARecastNavMesh> NavMeshPtr;
	TWeakObjectPtr<UWorld> WorldPtr;
	FCoverBuildParams BuildParams;
	FVector Origin = FVector::ZeroVector;
	double Extent = 0.0;
	TArray<FCoverData> AdditionalCoverData;

	virtual void Run(TFunctionRef<void()> EvalTaskBudget) override;
};

/**
 * Task to load static covers in background thread
 */
class FCoverLoadTask : public FCoverThreadedTask
{
	friend class TCoverWorkerTask<FCoverLoadTask>;

	FCoverLoadTask(FCoverSystemProxy* InProxy, FCoverSerializedArchive* InArchive, const FVector& InOrigin, double InExtent, bool bInDeserialize);
	virtual ~FCoverLoadTask();

	FCoverSerializedArchive* Archive = nullptr;
	FCoverSystemProxy* Proxy = nullptr;
	FVector Origin = FVector::ZeroVector;
	double Extent = 0.0;
	bool bDeserialize = false;

	virtual void Run(TFunctionRef<void()> EvalTaskBudget) override;
};
