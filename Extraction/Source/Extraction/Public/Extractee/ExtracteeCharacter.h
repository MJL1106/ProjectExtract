// AExtracteeCharacter -- rescue-and-follow civilian NPC. Idles at a placed spot until the player
// interacts (IWorldInteractable), then trails behind the player/companion. Under fire it runs to
// nearby registered cover (or cowers in place when none is close), loops a scared montage, holds
// position while the player or companion is DBNO, and sprint-catches-up when left behind.
// Enum state machine on a slow timer -- no behavior tree. Takes no damage; post-rescue, bullets
// pass straight through it.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "World/WorldInteractable.h"
#include "ExtracteeCharacter.generated.h"

class AAICoverSlot;
class ACompanionCharacter;
class AEnemyCharacter;
class AExtracteeAIController;
class UAnimMontage;
class UExtracteeTuningDataAsset;

DECLARE_LOG_CATEGORY_EXTERN(LogExtractee, Log, All);

UENUM()
enum class EExtracteeState : uint8
{
	Idle,		// pre-rescue, standing at the placed spot
	Follow,		// trailing the player at FollowDistance
	SeekCover,	// combat broke out, moving to a claimed cover slot
	Cower,		// stationary scared loop (behind cover or in place)
	CatchUp,	// left behind -- sprinting back to the player
	Hold,		// player or companion is DBNO -- stay put until both are up
};

UCLASS(Blueprintable)
class EXTRACTION_API AExtracteeCharacter : public ACharacter, public IWorldInteractable
{
	GENERATED_BODY()

public:
	AExtracteeCharacter();

	virtual void Tick(float DeltaSeconds) override;
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
		AController* EventInstigator, AActor* DamageCauser) override;

	// --- IWorldInteractable ---
	virtual bool CanWorldInteract_Implementation(AActor* Interactor) const override;
	virtual void WorldInteract_Implementation(AActor* Interactor) override;
	virtual FText GetWorldInteractionPrompt_Implementation(AActor* Interactor) const override;

	UFUNCTION(BlueprintPure, Category = "Extractee")
	bool IsRescued() const { return State != EExtracteeState::Idle; }

	EExtracteeState GetExtracteeState() const { return State; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditDefaultsOnly, Category = "Extractee")
	TObjectPtr<UExtracteeTuningDataAsset> Tuning;

	/** Looping scared/cower animation, played on the mesh's default montage slot while cowering
	 *  or holding under fire. Optional -- until assigned the extractee just stands. */
	UPROPERTY(EditDefaultsOnly, Category = "Extractee|Animation")
	TObjectPtr<UAnimMontage> ScaredLoopMontage;

private:
	EExtracteeState State = EExtracteeState::Idle;

	FTimerHandle StateTimerHandle;

	TWeakObjectPtr<APawn> PlayerPawn;
	TWeakObjectPtr<ACompanionCharacter> Companion;
	TWeakObjectPtr<AAICoverSlot> ClaimedSlot;

	FVector CoverDestination = FVector::ZeroVector;

	/** World seconds of the freshest "a fight is on" evidence (director combat report, or a live
	 *  enemy currently in Combat awareness). Compared against tuning windows for enter/exit. */
	float LastCombatSignalTime = -1e9f;

	// Catch-up stuck tracking
	float BestCatchUpDist = 0.f;
	float LastCatchUpProgressTime = 0.f;

	// --- State machine (0.25s timer) ---
	void EvaluateState();
	void HandleFollow();
	void HandleSeekCover();
	void HandleCower();
	void HandleCatchUp();
	void HandleHold();
	void EnterFollow();
	void EnterSeekCoverOrCower();
	void EnterCower();
	void EnterCatchUp();
	void EnterHold();

	// --- Helpers ---
	const UExtracteeTuningDataAsset* GetTuning() const;
	AExtracteeAIController* GetExtracteeController() const;
	void RefreshPartyRefs();
	void UpdateCombatSignal();
	bool IsCombatActive(float MaxAge) const;
	bool IsPartyMemberDown() const;
	AEnemyCharacter* FindNearestCombatEnemy(float MaxRange) const;
	float DistToPlayer2D() const;
	bool IsMoveActive() const;
	/** bMoving: orient to path, no focus. Stationary: focus the player so we face the party. */
	void SetMovementFacing(bool bMoving);
	void ApplyMoveSpeed(bool bSprint);
	void StartScaredLoop();
	void StopScaredLoop();
	void ReleaseCover();
	void TeleportBehindPlayer();

	// --- Soft collision (companion self-push pattern) ---
	void TickSoftSeparation();
	void SoftPushAwayFrom(const ACharacter* Other);
	void EnsureSoftCollisionIgnores(ACharacter* Other);
};
