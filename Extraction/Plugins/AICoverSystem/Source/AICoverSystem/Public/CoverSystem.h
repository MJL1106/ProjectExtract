//// Copyright, (c) Sami Kangasmaa 2022

#pragma once

#include "CoreMinimal.h"
#include "CoverSystemVersion.h"
#include "GameFramework/Info.h"
#include "Components/BoxComponent.h"
#include "AIController.h"
#include "CoverSystemPublicData.h"
#include "StaticCoverArchive.h"
#include "Math/GenericOctree.h"
#include "CoverSystem.generated.h"

class FCoverSystemProxy;
class FCoverWorkerTaskManager;
class AManualCoverPoint;
class UCoverPartitionInvokerComponent;

UENUM(BlueprintType)
enum class ECoverSystemMode : uint8
{
	// Covers are generated in runtime
	Dynamic = 0,

	// Covers are generated offline and loaded.
	Static,
};

UCLASS(BlueprintType, NotBlueprintable, hideCategories = (Physics, Actor, Navigation, Collision, Rendering, PathTracing))
class UCoverBoundBoxComponent : public UBoxComponent
{
	GENERATED_BODY()

public:

	UCoverBoundBoxComponent()
	{
		SetCanEverAffectNavigation(false);
		SetCollisionProfileName(TEXT("NoCollision"), false);
		SetCollisionEnabled(ECollisionEnabled::NoCollision);
		InitBoxExtent(FVector(64000.0, 64000.0, 32000.0));
	}
};

/**
 * Manager class of the cover system.
 * Place this into the persistent level
 */
UCLASS(Blueprintable, hidecategories = (Navigation, Physics, Collision, Input, Movement, Collision, Rendering, HLOD, WorldPartition))
class AICOVERSYSTEM_API ACoverSystem : public AActor
{
	GENERATED_BODY()
	
public:

	ACoverSystem();

#if WITH_EDITOR
	virtual void PostLoad() override;
	virtual bool ActorTypeSupportsDataLayer() const override { return false; }
	virtual bool CanChangeIsSpatiallyLoadedFlag() const override { return false; }
#endif

	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type Reason) override;
	virtual void BeginDestroy() override;
	virtual void Tick(float DeltaTime) override;

private:

	UFUNCTION()
	void OnNavigationGenerationFinished(class ANavigationData* NavData);

public:

	/**
	 * Returns cover system from the level.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cover System", meta =(WorldContext="WorldContext"))
	static ACoverSystem* GetCoverSystem(const UObject* WorldContext);

	/**
	 * Launches generation process of covers manually.
	 * Runs in async mode by default, so the result are not available immediately.
	 * To access generated data immediately, use sync mode (bAsync=false)
	 * Always runs in sync mode, if bEnableAsyncMode is set to false
	 * NOTE! Sync mode will not run, if there's already async build task running.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cover System")
	void GenerateCovers(bool bForceRegenerate = false, bool bAsync = true, bool bDeferGenerationIfBusy = true);

	/**
	 * When using partition, invalidates an area, so it will be regenerated.
	 * When bRegenerateImmediately is false, the partitions are marked dirty and are rebuilt later, when true, generation is started right away.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cover System")
	void InvalidatePartitions(const FBoxSphereBounds& InBounds, bool bRegenerateImmediately = false);

	/**
	* Manually notifies that navigation has been rebuilt which triggers regeneration.
	*/
	UFUNCTION(BlueprintCallable, Category = "Cover System")
	void NotifyNavigationRebuilt();

	/**
	 * Rebases the origin of the system.
	 * Use this in large worlds to move cover generation with the player
	 */
	UFUNCTION(BlueprintCallable, Category = "Cover System")
	void RebaseOrigin(const FVector& NewOrigin, bool bRegenerate = true);

	/**
	 * Outputs data of given cover by handle.
	 * Return true if the data was found and is valid
	 */
	UFUNCTION(BlueprintCallable, Category = "Cover System")
	bool GetCoverData(const FCoverHandle& CoverHandle, FCoverData& OutData) const;

	/**
	 * Outputs all cover handles that are inside given bounds
	 */
	void GetCoversWithinBounds(const class FBoxCenterAndExtent& InBounds, TArray<FCoverHandle>& OutCoverHandles) const;

	/**
	 * Outputs all covers that are inside given bounds
	 */
	void GetCoversAndDataWithinBounds(const class FBoxCenterAndExtent& InBounds, TArray<FCover>& OutCovers) const;

	/**
	 * Outputs all cover handles that are inside given bounds
	 */
	UFUNCTION(BlueprintCallable, Category = "Cover System")
	void GetCoversWithinBounds(const FBoxSphereBounds& InBounds, TArray<FCoverHandle>& OutCoverHandles) const;

	/**
	 * Runs a query to octree and retreves list of covers from entity database that are inside given bounds
	 */
	UFUNCTION(BlueprintCallable, Category = "Cover System")
	void GetCoverDataWithinBounds(const FBoxSphereBounds& InBounds, TArray<FCover>& OutCovers) const;

	/*
	* Occupies cover for given controller
	* AI's can test, if certain cover is already in use
	*/
	UFUNCTION(BlueprintCallable, Category = "Cover System")
	bool OccupyCover(AController* Controller, const FCoverHandle& CoverHandle);

	/**
	 * Releases cover, so it is no more occupied
	 */
	UFUNCTION(BlueprintCallable, Category = "Cover System")
	void UnOccupyCover(const FCoverHandle& CoverHandle);

	/**
	 * Releases cover, so it is no more occupied
	 * Specialized version of UnOccupyCover which unoccupies the cover only, if it is already occupied by given controller
	 */
	UFUNCTION(BlueprintCallable, Category = "Cover System")
	void UnOccupyCoverFromController(const FCoverHandle& CoverHandle, AController* Controller);

	/**
	 * Returns a controller that is currently occupying given cover (null, if the cover is empty or invalid)
	 */
	UFUNCTION(BlueprintPure, Category = "Cover System")
	AController* GetOccupyingController(const FCoverHandle& CoverHandle) const;

	/*
	* Checks if cover is occupied
	*/
	UFUNCTION(BlueprintPure, Category = "Cover System")
	bool IsCoverOccupied(const FCoverHandle& CoverHandle) const;

	/**
	 * Checks if async worker task is currently running
	 */
	UFUNCTION(BlueprintPure, Category = "Cover System")
	bool IsAsyncWorkerRunning() const;

protected:

	/**
	* Mode of the cover system (dynamic or static)
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings")
	ECoverSystemMode CoverSystemMode = ECoverSystemMode::Dynamic;

	/*
	* Should the system be split into partitions?
	* Use this for large open world levels to generate smaller chunks at time.
	* When partitioning is toggled on, cover system generates areas only around partition invokers and supports parallel generation of multiple partitions
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings")
	bool bUsePartitions = false;

	// Size of single partition in world units.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings", meta = (EditCondition="bUsePartitions", EditConditionHides, ClampMin="4096.0"))
	double PartitionSize = 8196.0;

	// Generation range of the partitions from any partition invoker
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings", meta = (EditCondition="bUsePartitions && CoverSystemMode==ECoverSystemMode::Dynamic", EditConditionHides))
	double PartitionGenerationRange = 9000.0;

	// Generation range in up/down axis of the partitions from any partition invoker
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings", meta = (EditCondition="bUsePartitions && CoverSystemMode==ECoverSystemMode::Dynamic", EditConditionHides))
	double PartitionGenerationRangeZ = 4000.0;

	/*
	* When toggled on, player pawns are used to activate partition generation around generation radius.
	* Use cover partition invoker component to manually specify generation origins if you turn this off
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings", meta = (EditCondition="bUsePartitions && CoverSystemMode==ECoverSystemMode::Dynamic", EditConditionHides))
	bool bUsePlayerPawnsAsPartitionInvokers = true;

	// Should generation process started on begin play?
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings", meta = (EditCondition="CoverSystemMode==ECoverSystemMode::Dynamic", EditConditionHides))
	bool bGenerateOnBeginPlay = true;

	// Should generation process started automatically after navigation rebuild?
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings", meta = (EditCondition="CoverSystemMode==ECoverSystemMode::Dynamic", EditConditionHides))
	bool bGenerateOnNavigationRebuild = false;

	// When bGenerateOnNavigationRebuild is toggled on, how often the regeneration can be started?
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings", meta = (EditCondition="CoverSystemMode==ECoverSystemMode::Dynamic && bGenerateOnNavigationRebuild", EditConditionHides))
	float MaxNavigationRegenerateInterval = 1.0f;

	// Should async build tasks used to generate covers? You should disable this only for debugging or when you generate covers manually during loading screens.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings", meta = (EditCondition="CoverSystemMode==ECoverSystemMode::Dynamic", EditConditionHides))
	bool bEnableAsyncMode = true;

	/*
	* Should baked covers loaded and inserted into octree in background thread when level is started?
	* When this is toggled off, cover data is ready on first frame but may cause a hitch in larger levels when starting up the level.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings", meta = (EditCondition="CoverSystemMode==ECoverSystemMode::Static", EditConditionHides))
	bool bAsyncLoadStaticCovers = true;

	/*
	* When using partitions, how many partition build task can be in queue?
	* Use negative or zero to allow unlimited queue size
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings", AdvancedDisplay, meta = (EditCondition="bUsePartitions && CoverSystemMode==ECoverSystemMode::Dynamic", EditConditionHides))
	int32 PartitionBuildTaskQueueSize = 64;

	/**
	 * Should dirty partitions sorted in order, so newly activated partitions or partitions that have longest time since regeneration are queued first?
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings", AdvancedDisplay, meta = (EditCondition="bUsePartitions && CoverSystemMode==ECoverSystemMode::Dynamic", EditConditionHides))
	bool bSortPartitionsByPriority = true;

	// How often active partitions are updated around invokers?
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings", AdvancedDisplay, meta = (EditCondition = "bUsePartitions && CoverSystemMode==ECoverSystemMode::Dynamic", EditConditionHides))
	float PartitionEvaluateInterval = 0.25f;

	// How often partition regenerate is evaluated. All dirty partitions (or lately activated) are queued to task manager for processing.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings", AdvancedDisplay, meta = (EditCondition = "bUsePartitions && CoverSystemMode==ECoverSystemMode::Dynamic", EditConditionHides))
	float PartitionRegenerateInterval = 0.1f;

	/**
	 * When partition goes deactive, it will be removed from the system if it doesn't come active again in set time.
	 * This prevents partitions to be destroyed and created immediately again when invoker is moving back and forth
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings", AdvancedDisplay, meta = (EditCondition="bUsePartitions && CoverSystemMode==ECoverSystemMode::Dynamic", EditConditionHides))
	float DeactivePartitionDestroyDelay = 5.0f;

	/*
	* How much we give time budget for worker thread per frame to work on threaded tasks in milliseconds.
	* We want to prevent cover generator task running simultaneously with physics as they both require access to physics scene which slowdown the both.
	* Cover system ticks in post physics, so the generator starts running in background after physics have been calculated.
	* Optimal time budget is leftover time of your frame after physics simulation (PostPhysicsTick + Idle), so background process would run when game thread is running post physics or is idle and waiting for GPU.
	* Giving less time budget generate covers slower but may help to improve performance if your game is running heavy physics simulation or you do lot of physics related actions in pre-physics tick.
	* Time budgeting is not used when loading static covers, as they aren't dependant on physics scene.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings", AdvancedDisplay, meta = (EditCondition="CoverSystemMode==ECoverSystemMode::Dynamic", EditConditionHides))
	double WorkerThreadTimeBudgetPerFrame = 6.5;

	// Parameters for cover generation
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings", meta = (ExposeOnSpawn))
	FCoverBuildParams BuildParams;

	/*
	* Trace channel to use when finding blocking geometry
	* To customize cover generation, you can create a new trace channel in project settings
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECollisionChannel::ECC_WorldStatic;
	
	// Toggles debug draw on/off
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Debug")
	bool bDebugDraw = false;

	// Culling distance for debug draw
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	float DebugDrawDistance = 12000.0f;

	// Should debug draw octree root node bounds?
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug", meta = (EditCondition="bDebugDraw"))
	bool bDrawOctreeBounds = true;

protected:

	/**
	* Root component of the cover system
	*/
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cover System")
	TObjectPtr<USceneComponent> RootSceneComponent;

	/**
	 * Component that defines bounds where covers are generated
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cover System", meta = (DisplayName="Cover System Bounds"))
	TObjectPtr<UCoverBoundBoxComponent> SystemBoundsComponent;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<class UBillboardComponent> SpriteComponent;
#endif

protected:

	/**
	 * Register/unregister of manual cover points.
	 * Manual covers register/unregister in begin / end play
	 */
	friend class AManualCoverPoint;
	virtual void RegisterManualCoverPoint(AManualCoverPoint* CoverPoint);
	virtual void UnregisterManualCoverPoint(AManualCoverPoint* CoverPoint);

	friend class UCoverPartitionInvokerComponent;
	void RegisterPartitionInvoker(UCoverPartitionInvokerComponent* Invoker);
	void UnregisterPartitionInvoker(UCoverPartitionInvokerComponent* Invoker);

	// Internal function that launches cover generation
	virtual void GenerateCoversInternal(bool bForceRegenerate, bool bAsync, bool bDeferGenerationIfBusy, const TSet<FCoverPartitionHash>* BuildOnlyPartitionSet = nullptr);

	// Launches regeneration if navigation was rebuilt recently
	virtual void UpdateNavigationRegenerate(float DeltaTime);

	// Updates partition states
	virtual void UpdatePartitions(float DeltaTime);

	// Launches generation process for dirty partitions
	virtual void RegenerateDirtyPartitions();

	// Loads static covers from archive into cover system proxy
	virtual void LoadCoversInternal(bool bAsync, bool bDeserializeArchive);

	// Calculates octree origin for generation
	FVector CalculateOctreeOrigin() const;

	// Calculates octree extent for generation (from bound component)
	double CalculateOctreeExtent() const;

	// Rebuilds manual cover point data to CachedManualCoverPointData
	void RebuildManualCoverPointData();

	/**
	 * Outputs locations of invokers that specify which partitions should be generated
	 */
	void GetPartitionInvokerLocations(TArray<FVector>& OutLocations) const;

	// Checks if given partition hash is inside the whole system bounds
	bool IsPartitionWithinSystemBounds(const FCoverPartitionHash& Hash) const;

private:

	// Task manager to run generation in a separate worker thread
	FCoverWorkerTaskManager* WorkerTaskManager = nullptr;

	/**
	 * Archive where the static covers are stored (serialization/deserialization)
	 */
	UPROPERTY()
	FCoverSerializedArchive StaticCoverArchive;

	/**
	 * Archive where the static covers are stored when using partition (serialization/deserialization)
	 */
	UPROPERTY()
	TMap<FCoverPartitionHash, FCoverSerializedArchive> PartitionedStaticCoverArchives;

	// Is tearing the cover system down? (ie. ending play)
	bool bTearingDown = false;

	// Pointer to pure C++ proxy object that manages build tasks and cover memory
	FCoverSystemProxy* Proxy = nullptr;

	// Proxies by partition has when using partitioned data structure
	TMap<FCoverPartitionHash, FCoverSystemProxy*> PartitionProxies;

	// Queue for partition proxies that must be deleted
	TArray<FCoverSystemProxy*> PartitionProxyDeleteQueue;

	// Mapping for occupied covers by controllers
	TMap<FCoverHandle, TWeakObjectPtr<AController>> OccupiedCovers;

	// Mapping of manual cover points CoverPointActor -> ManualCoverPointEntityLookup
	TMap<AManualCoverPoint*, FCoverHandle> ManualCoverPoints;

	// Cached data from manual cover points that is appended to build results
	TArray<FCoverData> CachedManualCoverPointData;

	// Is generation currently dirty and requires update once available?
	bool bDirtyGeneration = false;

	// Was navigation rebuild recently, so we must mask the system dirty?
	bool bNavigationRebuiltRecently = false;

	// Inverval timers
	float NavigationDirtyIntervalTimer = 0.0f;
	float PartitionUpdateIntervalTimer = 0.0f;
	float PartitionRegenerateIntervalTimer = 0.0f;

	// Partitions that are marked dirty and requires regeneration
	TSet<FCoverPartitionHash> DirtyPartitions;

	// Partitions that are currently active
	TSet<FCoverPartitionHash> ActivePartitions;

	// Partitions that are generated already. If partition is not generated and it is in ActivePartitions, generation process is started for it
	TSet<FCoverPartitionHash> GeneratedPartitions;

	// Mapping of lately deactivated partitions. They get removed when they have been deactive for a while
	TMap<FCoverPartitionHash, float> DeactivatedPartitions;

	// Registered invokers
	UPROPERTY(Transient)
	TArray<TObjectPtr<UCoverPartitionInvokerComponent>> PartitionInvokers;

	// Is manual cover point data dirty and requires re-caching?
	bool bDirtyManualCoverPointData = false;

	// Cached bounds of the whole system
	void CacheWholeSystemBound() const;
	mutable TOptional<FBox> WholeSystemBound;

	// Computes nav data bounds.
	FBox ComputeNavDataBounds() const;
	mutable TOptional<FBox> CachedNavDataBounds;

	// Cleans up the whole system, so it is in initial state
	void CleanupSystem();

private:

	void DebugDraw(UWorld* World, FCoverSystemProxy* InProxy, bool bPersistent = false, const FCoverPartitionHash* Partition = nullptr);
	bool bFlushPreviewDebug = false;

public:
#if WITH_EDITOR

	/**
	 * Cleans up preview covers from debug draw
	 */
	UFUNCTION(Category = "Debug", meta = (CallInEditor = "true"))
	void CleanPreview();

	/**
	 * Runs generation process in the editor to preview covers.
	 * When the cover mode is "Static" this will only debug draw covers from baked data.
	 */
	UFUNCTION(Category = "Debug", meta = (CallInEditor = "true"))
	void Preview();

	/**
	 * Bake the covers when CoverSystemMode is "Static".
	 * Baked covers are loaded instead of generated
	 */
	UFUNCTION(Category = "Settings", meta = (CallInEditor = "true"))
	void BakeStaticCovers();

	/**
	* Empties static covers that were earlier baked
	*/
	UFUNCTION(Category = "Settings", meta = (CallInEditor = "true"))
	void ResetStaticCovers();

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

private:

	bool bPreviewHasLoadedArchive = false;

#endif
};
