// BT task — cover-aware companion combat. State machine drives EngageFromOpen, EngageFromCover, StandUpFire.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "Companion/CompanionTypes.h"
#include "BTTask_CompanionCombat.generated.h"

class UAnimMontage;
class AAICoverSlot;
class ACompanionCharacter;
class UCompanionAnimInstance;

UCLASS()
class EXTRACTION_API UBTTask_CompanionCombat : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_CompanionCombat();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult) override;
	virtual FString GetStaticDescription() const override;

protected:
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector CombatTargetKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector HasCoverPositionKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector CoverLocationKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector CoverSlotKey;

	// --- Burst timing ---

	/** Min seconds of fire per cover stand-up burst. */
	UPROPERTY(EditAnywhere, Category = "Combat|Burst", meta = (ClampMin = "0.1"))
	float MinFireBurst = 0.9f;

	/** Max seconds of fire per cover stand-up burst. */
	UPROPERTY(EditAnywhere, Category = "Combat|Burst", meta = (ClampMin = "0.1"))
	float MaxFireBurst = 2.0f;

	/** Min seconds of a quick-peek burst (less exposure). */
	UPROPERTY(EditAnywhere, Category = "Combat|Burst", meta = (ClampMin = "0.1"))
	float MinQuickPeekBurst = 0.25f;

	/** Max seconds of a quick-peek burst. */
	UPROPERTY(EditAnywhere, Category = "Combat|Burst", meta = (ClampMin = "0.1"))
	float MaxQuickPeekBurst = 0.6f;

	/** Seconds of pause between bursts in open-engage. */
	UPROPERTY(EditAnywhere, Category = "Combat|Burst", meta = (ClampMin = "0.1"))
	float FirePauseDuration = 0.5f;

	// --- Peek action weights (normal health) ---

	/** Relative weight for standing up and firing a full burst. */
	UPROPERTY(EditAnywhere, Category = "Combat|PeekWeights", meta = (ClampMin = "0.0"))
	float StandWeight = 60.f;

	/** Relative weight for a short quick-peek burst. */
	UPROPERTY(EditAnywhere, Category = "Combat|PeekWeights", meta = (ClampMin = "0.0"))
	float QuickWeight = 25.f;

	/** Relative weight for staying in cover one more cycle. */
	UPROPERTY(EditAnywhere, Category = "Combat|PeekWeights", meta = (ClampMin = "0.0"))
	float HoldWeight = 15.f;

	// --- Peek action weights (low health) ---

	/** Stand weight when health is below LowHealthFraction. */
	UPROPERTY(EditAnywhere, Category = "Combat|PeekWeights|LowHealth", meta = (ClampMin = "0.0"))
	float LowHpStandWeight = 15.f;

	/** Quick weight when health is below LowHealthFraction. */
	UPROPERTY(EditAnywhere, Category = "Combat|PeekWeights|LowHealth", meta = (ClampMin = "0.0"))
	float LowHpQuickWeight = 35.f;

	/** Hold weight when health is below LowHealthFraction. */
	UPROPERTY(EditAnywhere, Category = "Combat|PeekWeights|LowHealth", meta = (ClampMin = "0.0"))
	float LowHpHoldWeight = 50.f;

	/** Health fraction below which low-health weights apply. */
	UPROPERTY(EditAnywhere, Category = "Combat|PeekWeights|LowHealth", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LowHealthFraction = 0.35f;

	/** Multiplier applied to next peek cooldown when below LowHealthFraction. */
	UPROPERTY(EditAnywhere, Category = "Combat|PeekWeights|LowHealth", meta = (ClampMin = "1.0"))
	float LowHealthCooldownMultiplier = 2.0f;

	/** How many consecutive Hold cycles before the companion is forced to Stand. */
	UPROPERTY(EditAnywhere, Category = "Combat|PeekWeights", meta = (ClampMin = "0", ClampMax = "5"))
	uint8 MaxConsecutiveHolds = 2;

	// --- Suppression ---

	/** Recent damage within this window flags the companion as suppressed. 0 disables. */
	UPROPERTY(EditAnywhere, Category = "Combat|Suppression", meta = (ClampMin = "0.0"))
	float SuppressionWindowSeconds = 1.8f;

	/** Multiplier applied to the next peek cooldown when suppressed. */
	UPROPERTY(EditAnywhere, Category = "Combat|Suppression", meta = (ClampMin = "1.0"))
	float SuppressionCooldownMultiplier = 1.5f;

	// --- Open-engage LoS abandon ---

	UPROPERTY(EditAnywhere, Category = "Combat", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float LosBlockedAbandonSeconds = 2.0f;

	UPROPERTY(EditAnywhere, Category = "Combat", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float AimDropOnLosBlockedSeconds = 0.3f;

	// --- Cover timing ---

	/** Minimum dwell in cover-idle before the first peek can fire. */
	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "0.0"))
	float MinCoverIdleDwell = 0.4f;

	/** Z offset above slot location for the stand-up-fire LoS trace. */
	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "0.0"))
	float StandFireEyeHeight = 150.f;

	/** Min peek cooldown (randomised per cycle). */
	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "0.0"))
	float MinPeekCooldown = 0.8f;

	/** Max peek cooldown (randomised per cycle). */
	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "0.0"))
	float MaxPeekCooldown = 2.2f;

	/** Interval (seconds) between cover-validity LOS rechecks. */
	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "0.25", ClampMax = "5.0"))
	float CoverValidityCheckInterval = 1.0f;

	/** Minimum time at current cover before a failed re-eval can abandon the slot. */
	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float MinCoverDwellBeforeReEval = 2.0f;

	/** Toggle verbose exit-gate logs + debug draw under LogCompanionAI. */
	UPROPERTY(EditAnywhere, Category = "Debug")
	bool bDebugLogging = false;

private:
	enum class EPeekAction : uint8 { Stand, Quick, Hold };

	static EPeekAction RollPeekAction(float StandW, float QuickW, float HoldW);

	// Per-burst helpers
	void ReturnToCover(ACompanionCharacter* Companion, UCompanionAnimInstance* Anim,
		class AAICoverSlot* Slot, bool bSuppressed, bool bLowHealth);

	float BurstTimer = 0.0f;
	bool bIsFiringBurst = false;

	float TimeInCoverIdle = 0.f;
	float PeekCooldown = 0.f;
	EPeekSide ResolvedPeekSide = EPeekSide::Right;

	float CoverValidityCheckTimer = 0.f;
	float TimeAtCurrentCover = 0.f;

	FVector LastPeekResolveCoverLoc = FVector::ZeroVector;
	FVector LastPeekResolveTargetLoc = FVector::ZeroVector;
	static constexpr float PeekResolveDistThresholdSq = 50.f * 50.f;

	float LosBlockedAccum = 0.f;
	int8 LastTickBranch = -1;
	bool bLastLosBlocked = false;
	TWeakObjectPtr<AActor> LastLosBlocker;

	// Peek action state
	EPeekAction CurrentBurstAction = EPeekAction::Stand;
	uint8 ConsecutiveHolds = 0;

	// Debug-only rate limiter for stand-burst LoS trace
	float DebugBurstLosCheckTimer = 0.f;

	// Active peek montage (weak — anim owns it)
	TWeakObjectPtr<UAnimMontage> ActivePeekMontage;
};
