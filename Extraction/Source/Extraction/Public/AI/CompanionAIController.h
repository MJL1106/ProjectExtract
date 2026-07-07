// AI controller for the companion — perception, blackboard, behaviour tree.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "GenericTeamAgentInterface.h"
#include "Movement/TraversalTypes.h"
#include "Companion/CompanionCommandTypes.h"
#include "CompanionAIController.generated.h"

class UAISenseConfig_Sight;
class UAISenseConfig_Hearing;
class UCompanionTuningDataAsset;
class UTraversalComponent;
class ACompanionRoute;

DECLARE_LOG_CATEGORY_EXTERN(LogCompanionAI, Log, All);

UCLASS()
class EXTRACTION_API ACompanionAIController : public AAIController
{
	GENERATED_BODY()

public:
	ACompanionAIController();

	// Blackboard key names
	static const FName BB_PlayerActor;
	static const FName BB_PlayerNeedsRevive;
	static const FName BB_CombatTarget;
	static const FName BB_CoverLocation;
	static const FName BB_HasCoverPosition;
	static const FName BB_PlayerTraversalActive;
	static const FName BB_PlayerTraversalObstacle;
	static const FName BB_PlayerTraversalLanding;
	static const FName BB_PlayerTraversalType;
	static const FName BB_Posture;                       // enum: ECompanionPosture
	static const FName BB_ScoringWeight_LoSPlayer;       // float
	static const FName BB_ScoringWeight_AvoidEnemy;      // float
	static const FName BB_ScoringWeight_CoverFromTarget; // float
	static const FName BB_CoverSlot;                     // Object (AAICoverSlot)
	static const FName BB_NextCoverSlot;                 // Object (AAICoverSlot) — set by CoverSwitchMonitor, consumed by MoveToCover
	static const FName BB_ActiveRoute;                   // Object (ACompanionRoute)
	static const FName BB_RouteActive;                   // bool
	static const FName BB_RouteBlocksCombat;              // bool
	static const FName BB_CompanionCommand;              // enum ECompanionCommand
	static const FName BB_CommandTargetActor;            // Object
	static const FName BB_CommandTargetLocation;         // Vector
	static const FName BB_TakedownMethod;                // enum ETakedownMethod
	static const FName BB_BreachType;                    // enum EBreachType

	/**
	 * Write a command into the companion blackboard.
	 * Breach and Takedown both require a valid TargetActor — logs and early-returns if missing.
	 */
	void IssueCommand(ECompanionCommand Command, ETakedownMethod Method, AActor* TargetActor, const FVector& TargetLocation);

	/** Reset BB_CompanionCommand to None and clear related keys. */
	void ClearActiveCommand();

	/** Writes BB_BreachType — call before IssueCommand(Breach). Derived from companion mode at confirm time. */
	void SetBreachType(EBreachType Type);

	UFUNCTION(BlueprintPure, Category = "Companion|AI")
	APawn* GetPlayerCharacter() const { return CachedPlayerCharacter.Get(); }

	void SetPlayerCharacter(APawn* InPlayer) { CachedPlayerCharacter = InPlayer; }

	const UCompanionTuningDataAsset* GetTuning() const { return Tuning; }

	/** Resets the BB_PlayerTraversal* keys. Public so BT tasks can call on early-finish paths. */
	void ClearTraversalBlackboard();

	/** Activates a pre-authored companion route. Sets BB keys so the BT route branch activates. */
	UFUNCTION(BlueprintCallable, Category = "Companion|Route")
	void StartRoute(ACompanionRoute* Route);

	/** Clears route BB keys. Does NOT broadcast OnRouteCompleted — the BT task owns that. */
	void StopRoute(bool bAborted);

	/**
	 * AI-safe teleport: cancels any active traversal, stops movement, projects the
	 * destination onto the NavMesh, then TeleportTo's the possessed pawn.
	 * Returns false if the pawn is missing or NavMesh projection fails.
	 */
	bool TeleportToLocation(const FVector& Location, const FRotator& Rotation);

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditDefaultsOnly, Category = "Companion|AI")
	TObjectPtr<UBehaviorTree> CompanionBehaviorTree;

	UPROPERTY(EditDefaultsOnly, Category = "Companion|Tuning")
	TObjectPtr<UCompanionTuningDataAsset> Tuning;

private:
	UPROPERTY()
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	UPROPERTY()
	TObjectPtr<UAISenseConfig_Hearing> HearingConfig;

	TWeakObjectPtr<APawn> CachedPlayerCharacter;

	// Traversal mirror coupling
	TWeakObjectPtr<UTraversalComponent> PlayerTraversalComp;
	FDelegateHandle TraversalStartedHandle;
	FDelegateHandle TraversalEndedHandle;

	// Warp safety net
	FTimerHandle WarpStuckTimer;
	FTimerHandle BindRetryTimer;
	float TimeSinceClosedToPlayer = 0.f;
	float TimeOnDifferentLevel = 0.f;
	int32 BindAttempts = 0;

	void TryBindToPlayerTraversal();
	void OnPlayerTraversalStarted(ETraversalType Type, float PlayRate, FVector ObstacleLocation, FVector LandingLocation);
	void OnPlayerTraversalEnded();
	void TickWarpFallback();
	bool IsCompanionRecentlyRendered() const;
	bool ShouldWarp() const;
	void ExecuteWarpBehindPlayer();

	/** Releases any pending NextCoverSlot claim and clears the BB key. Belt-and-braces for AI teardown paths. */
	void ReleaseNextCoverSlotIfClaimed();
};
