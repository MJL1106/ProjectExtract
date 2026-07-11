#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LevelObjectiveFlow.generated.h"

class ACompanionRoute;
class ADoorBase;
class AEnemyCharacter;
class AExtractionTargetActor;
class ALevelCompletionLiftGate;
class ALootContainer;

UENUM(BlueprintType)
enum class ELevelObjectiveStep : uint8
{
	Inactive,
	BreachRoofDoor,
	FollowCompanionDownstairs,
	BreachStairwellDoor,
	ClearRoom1,
	FindOfficeKeycard,
	UnlockStairwellDoor,
	SwitchCompanionToStealth,
	FirstDoubleTakedown,
	SecondDoubleTakedown,
	ReachExtractionTarget,
	DefendPosition,
	UseLift,
	Completed,
};

enum class ELevelObjectiveEvent : uint8
{
	RoofDoorOpened,
	RouteCompleted,
	StairwellDoorOpened,
	Room1Cleared,
	KeycardLooted,
	ExitDoorOpened,
	Room2DoorOpened,
	FirstPairCleared,
	SecondPairCleared,
	ExtractionStarted,
	ExtractionCompleted,
	LiftCompleted,
};

/** Pure transition core shared by the placed flow actor and automation coverage. */
struct EXTRACTION_API FLevelObjectiveStateMachine
{
	bool Activate();
	bool Advance(ELevelObjectiveEvent Event);
	static ELevelObjectiveStep GetNextStep(ELevelObjectiveStep Step, ELevelObjectiveEvent Event);
	ELevelObjectiveStep GetCurrentStep() const { return CurrentStep; }

private:
	ELevelObjectiveStep CurrentStep = ELevelObjectiveStep::Inactive;
};

/** DemoMap's explicit, non-ticking, server-authoritative objective sequence. */
UCLASS(Blueprintable)
class EXTRACTION_API ALevelObjectiveFlow : public AActor
{
	GENERATED_BODY()

public:
	ALevelObjectiveFlow();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, Category = "Objective Flow")
	bool ActivateFlow();

	UFUNCTION(BlueprintPure, Category = "Objective Flow")
	ELevelObjectiveStep GetCurrentStep() const { return CurrentStep; }

	UFUNCTION(BlueprintCallable, Category = "Objective Flow")
	void NotifyLiftCompleted();
#if WITH_DEV_AUTOMATION_TESTS
	void TestSetCurrentStep(ELevelObjectiveStep Step) { CurrentStep = Step; }
	void TestEvaluateCurrentEnemyStep() { RefreshRecordedEnemyDeaths(); EvaluateCurrentEnemyStep(); }
	void TestActivateOptionalSupplies() { ActivateOptionalSupplies(); }
	void TestRefreshOptionalSupplies() { UpdateOptionalSupplies(); }
	bool TestValidateReferences() const { return ValidateReferences(); }
	bool TestIsOptionalSuppliesActive() const { return bOptionalSuppliesActive; }
	ALootContainer* TestGetOptionalTarget() const { return CurrentOptionalTarget; }
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective Flow")
	bool bAutoActivate = true;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Objective Flow|Roof")
	TObjectPtr<ADoorBase> RoofDoor;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Objective Flow|Roof")
	TObjectPtr<ACompanionRoute> DownstairsRoute;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Objective Flow|Roof")
	TObjectPtr<ADoorBase> StairwellBreachDoor;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Objective Flow|Room 1")
	TArray<TObjectPtr<AEnemyCharacter>> Room1Enemies;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Objective Flow|Room 1")
	TObjectPtr<ALootContainer> KeycardContainer;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Objective Flow|Room 1")
	TObjectPtr<ADoorBase> Room1ExitDoor;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Objective Flow|Room 2")
	TObjectPtr<ADoorBase> Room2EntryDoor;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Objective Flow|Room 2")
	TArray<TObjectPtr<AEnemyCharacter>> FirstTakedownPair;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Objective Flow|Room 2")
	TArray<TObjectPtr<AEnemyCharacter>> SecondTakedownPair;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Objective Flow|Room 2")
	TArray<TObjectPtr<ALootContainer>> SupplyCrates;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Objective Flow|Extraction")
	TObjectPtr<AExtractionTargetActor> ExtractionTarget;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Objective Flow|Extraction")
	TObjectPtr<ALevelCompletionLiftGate> LiftGate;

private:
	static const FName PrimaryObjectiveId;
	static const FName OptionalSuppliesObjectiveId;

	UPROPERTY(ReplicatedUsing = OnRep_CurrentStep)
	ELevelObjectiveStep CurrentStep = ELevelObjectiveStep::Inactive;
	UPROPERTY(ReplicatedUsing = OnRep_OptionalSuppliesActive)
	bool bOptionalSuppliesActive = false;
	UPROPERTY(ReplicatedUsing = OnRep_PresentationRevision)
	TObjectPtr<AActor> CurrentPrimaryTarget;
	UPROPERTY(ReplicatedUsing = OnRep_PresentationRevision)
	TObjectPtr<ALootContainer> CurrentOptionalTarget;
	UPROPERTY(ReplicatedUsing = OnRep_PresentationRevision)
	uint16 PresentationRevision = 0;

	TSet<TWeakObjectPtr<ALootContainer>> CompletedSupplyCrates;
	TSet<int32> Room1DeadIndices;
	TSet<int32> FirstPairDeadIndices;
	TSet<int32> SecondPairDeadIndices;

	bool ValidateReferences() const;
	void BindDelegates();
	void UnbindDelegates();
	void Advance(ELevelObjectiveEvent Event);
	void UpdatePrimaryObjective();
	void EvaluateCurrentEnemyStep();
	void RefreshRecordedEnemyDeaths();
	void ActivateOptionalSupplies();
	void UpdateOptionalSupplies();
	bool IsEnemyGroupDead(const TArray<TObjectPtr<AEnemyCharacter>>& Enemies, const TSet<int32>& DeadIndices) const;
	AActor* FindFirstLivingEnemy(const TArray<TObjectPtr<AEnemyCharacter>>& Enemies) const;
	ALootContainer* FindNearestUnlootedSupply(const FVector& Origin) const;

	UFUNCTION()
	void HandleDoorOpened(AActor* Door);
	UFUNCTION()
	void HandleRouteCompleted(bool bAborted);
	UFUNCTION()
	void HandleTrackedEnemyDeath();
	UFUNCTION()
	void HandleTrackedEnemyDestroyed(AActor* DestroyedActor);
	UFUNCTION()
	void HandleLootCompleted(ALootContainer* Container, AActor* Looter);
	UFUNCTION()
	void HandleSupplyDestroyed(AActor* DestroyedActor);
	UFUNCTION()
	void HandleExtractionStarted();
	UFUNCTION()
	void HandleExtractionCompleted();
	UFUNCTION()
	void OnRep_CurrentStep();
	UFUNCTION()
	void OnRep_OptionalSuppliesActive();
	UFUNCTION()
	void OnRep_PresentationRevision();
};