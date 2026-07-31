// BT task (latent) -- owns out-of-combat commanded-cover state.

#include "AI/Tasks/BTTask_CompanionHoldCover.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AI/BlackboardKeyType_Cover.h"
#include "AI/CompanionCoverStatics.h"
#include "Animation/CompanionAnimInstance.h"
#include "Companion/CompanionCharacter.h"
#include "Companion/CompanionCommandTypes.h"
#include "CompanionAIController.h"
#include "CoverGeometryStatics.h"
#include "CoverPoseComponent.h"
#include "CoverSystemPublicData.h"
#include "Kismet/GameplayStatics.h"

// LogCompanionAI is declared by CompanionAIController.h above — re-declaring it here is a struct
// redefinition (C2011), not a harmless duplicate.

namespace
{
	void ReleaseHoldFocus(UBehaviorTreeComponent& OwnerComp)
	{
		if (AAIController* C = OwnerComp.GetAIOwner())
			C->ClearFocus(EAIFocusPriority::Gameplay);
	}

	UCompanionAnimInstance* GetCompanionAnim(ACompanionCharacter* Companion)
	{
		if (!IsValid(Companion)) return nullptr;
		USkeletalMeshComponent* Mesh = Companion->GetMesh();
		return IsValid(Mesh) ? Cast<UCompanionAnimInstance>(Mesh->GetAnimInstance()) : nullptr;
	}

	/** Resolve a peek side from the cover's baked flags at the given height. Without a threat
	 *  location, pick the side the player is on relative to the cover wall to make the lean
	 *  read naturally. Falls back to Left if neither side has a flag. */
	EPeekSide ResolveSideFromFlags(const FCoverData& Data, ECoverHeight Height, const FVector& CoverLoc, UWorld* World)
	{
		const bool bCrouched = (Height == ECoverHeight::Crouch);
		const bool bLeft  = bCrouched ? Data.bLeftCoverCrouched  : Data.bLeftCoverStanding;
		const bool bRight = bCrouched ? Data.bRightCoverCrouched : Data.bRightCoverStanding;

		if (bLeft && !bRight) return EPeekSide::Left;
		if (bRight && !bLeft) return EPeekSide::Right;

		// Both or neither: break tie toward the player's side of the cover wall.
		if (IsValid(World))
		{
			if (const APawn* Player = Cast<APawn>(UGameplayStatics::GetPlayerPawn(World, 0)))
			{
				const FVector Lateral = Data.Rotation.RotateVector(FVector::RightVector);
				const float Dot = FVector::DotProduct(Player->GetActorLocation() - CoverLoc, Lateral);
				return (Dot >= 0.f) ? EPeekSide::Right : EPeekSide::Left;
			}
		}
		return EPeekSide::Left;
	}

	/** Exit cover through the anim instance (stops montages + resets pose component). Falls
	 *  back to a raw component reset when the anim instance is unavailable. */
	void ExitCoverCleanly(ACompanionCharacter* Companion)
	{
		if (!IsValid(Companion)) return;
		if (UCompanionAnimInstance* Anim = GetCompanionAnim(Companion))
		{
			Anim->ExitCoverPose();
		}
		else if (UCoverPoseComponent* PoseComp = Companion->GetCoverPoseComponent())
		{
			PoseComp->ResetCoverPose();
		}
	}

	/** True while BB_CompanionCommand still names this branch (TakeCover, or None once the branch
	 *  has cleared it). False the instant a foreign command — Breach, Loot, Explore, Takedown —
	 *  supersedes the hold. Null BB reads as "not ours": every caller's safe direction is to treat
	 *  the hold as over. */
	bool IsCommandStillOurs(const UBlackboardComponent* BB)
	{
		if (!BB) return false;
		const uint8 Cmd = BB->GetValueAsEnum(ACompanionAIController::BB_CompanionCommand);
		return Cmd == static_cast<uint8>(ECompanionCommand::TakeCover)
			|| Cmd == static_cast<uint8>(ECompanionCommand::None);
	}

	/** True when combat is genuinely inheriting the hold (live target AND the command is still
	 *  TakeCover or cleared to None). A new command (Breach, Loot, etc.) must exit the pose
	 *  even with a live combat target. Used by both TickTask and AbortTask. */
	bool IsCombatTakingOver(const UBlackboardComponent* BB, const FBlackboardKeySelector& CombatTargetKey)
	{
		if (!BB) return false;
		return IsCommandStillOurs(BB)
			&& IsValid(Cast<AActor>(BB->GetValueAsObject(CombatTargetKey.SelectedKeyName)));
	}
}

UBTTask_CompanionHoldCover::UBTTask_CompanionHoldCover()
{
	NodeName = TEXT("Companion Hold Cover (Out-of-Combat)");
	bNotifyTick = true;
	bNotifyTaskFinished = false;
	bCreateNodeInstance = true;
}

void UBTTask_CompanionHoldCover::InitializeFromAsset(UBehaviorTree& Asset)
{
	Super::InitializeFromAsset(Asset);
	if (UBlackboardData* BBAsset = GetBlackboardAsset())
	{
		CoverTargetKey.ResolveSelectedKey(*BBAsset);
		CombatTargetKey.ResolveSelectedKey(*BBAsset);
	}
}

EBTNodeResult::Type UBTTask_CompanionHoldCover::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* /*NodeMemory*/)
{
	bCoverPoseEntered = false;

	AAIController* Controller = OwnerComp.GetAIOwner();
	if (!IsValid(Controller))
	{
		UE_LOG(LogCompanionAI, Warning, TEXT("HoldCover: ExecuteTask FAILED reason=no-controller"));
		CompanionCover::ClearCommandIfStillActive(OwnerComp, ECompanionCommand::TakeCover);
		return EBTNodeResult::Failed;
	}

	ACompanionCharacter* Companion = Cast<ACompanionCharacter>(Controller->GetPawn());
	if (!IsValid(Companion))
	{
		UE_LOG(LogCompanionAI, Warning, TEXT("HoldCover: ExecuteTask FAILED reason=no-companion"));
		CompanionCover::ClearCommandIfStillActive(OwnerComp, ECompanionCommand::TakeCover);
		return EBTNodeResult::Failed;
	}

	if (!Companion->IsCommandedCoverHoldActive())
	{
		UE_LOG(LogCompanionAI, Warning, TEXT("%s: HoldCover ExecuteTask FAILED reason=hold-not-active"), *Companion->GetName());
		CompanionCover::ClearCommandIfStillActive(OwnerComp, ECompanionCommand::TakeCover);
		return EBTNodeResult::Failed;
	}

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		UE_LOG(LogCompanionAI, Warning, TEXT("%s: HoldCover ExecuteTask FAILED reason=no-BB"), *Companion->GetName());
		CompanionCover::ClearCommandIfStillActive(OwnerComp, ECompanionCommand::TakeCover);
		return EBTNodeResult::Failed;
	}

	const FCover Cover = BB->GetValue<UBlackboardKeyType_Cover>(CoverTargetKey.GetSelectedKeyID());
	if (!Cover.IsValid())
	{
		UE_LOG(LogCompanionAI, Warning, TEXT("%s: HoldCover ExecuteTask FAILED reason=cover-invalid keyID=%d"),
			*Companion->GetName(),
			(CoverTargetKey.GetSelectedKeyID() == FBlackboard::InvalidKey) ? -1 : (int32)CoverTargetKey.GetSelectedKeyID());
		Companion->ClearCommandedCoverHold();
		CompanionCover::ClearCommandIfStillActive(OwnerComp, ECompanionCommand::TakeCover);
		return EBTNodeResult::Failed;
	}

	// Resolve cover height and peek side from baked flags.
	const ECoverHeight Height = UCoverGeometryStatics::GetCoverHeight(Cover.Data);
	ResolvedSide = ResolveSideFromFlags(Cover.Data, Height, Cover.Data.Location, Companion->GetWorld());

	UE_LOG(LogCompanionAI, Log,
		TEXT("%s: HoldCover ENTRY height=%s side=%s flags: LeftStand=%d RightStand=%d FrontCrouch=%d Crouched=%d"),
		*Companion->GetName(),
		Height == ECoverHeight::Crouch ? TEXT("Crouch") : TEXT("Stand"),
		ResolvedSide == EPeekSide::Left ? TEXT("Left") : TEXT("Right"),
		(int32)Cover.Data.bLeftCoverStanding, (int32)Cover.Data.bRightCoverStanding,
		(int32)Cover.Data.bFrontCoverCrouched, (int32)Cover.Data.bCrouchedCover);

	// Assert the correct stance for the derived cover height.
	if (Height == ECoverHeight::Crouch && !Companion->bIsCrouched)
		Companion->Crouch();
	else if (Height == ECoverHeight::Stand && Companion->bIsCrouched
		&& !Companion->IsStanceOwnedElsewhere() && !Companion->IsCrouchOwnedByStealth())
		Companion->UnCrouch();

	// Enter cover through the anim instance (same path as combat at :3090).
	// bPlayEnterMontage = true: this is a fresh cover arrival.
	if (UCompanionAnimInstance* Anim = GetCompanionAnim(Companion))
	{
		Anim->EnterCoverPose(ResolvedSide, Height, /*bPlayEnterMontage*/ true);
		bCoverPoseEntered = true;
	}

	// Face the fire arc (outward from the wall). Let the focal point drive rotation smoothly.
	const FVector FireArcFwd = UCoverGeometryStatics::GetFireArcForward(Cover.Data);
	Controller->SetFocalPoint(Companion->GetActorLocation() + FireArcFwd * 500.f, EAIFocusPriority::Gameplay);

	return EBTNodeResult::InProgress;
}

void UBTTask_CompanionHoldCover::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* /*NodeMemory*/, float /*DeltaSeconds*/)
{
	// Combat target appeared -- finish so the sequence completes (ClearCommand runs next),
	// the branch ends, and the tree falls through to the combat branch. The commanded hold
	// latch stays active so the companion fights from the cover the player picked.
	// Do NOT exit the cover pose -- combat inherits it.
	if (UBlackboardComponent* CombatBB = OwnerComp.GetBlackboardComponent())
	{
		if (IsValid(Cast<AActor>(CombatBB->GetValueAsObject(CombatTargetKey.SelectedKeyName))))
		{
			ReleaseHoldFocus(OwnerComp);
			return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		}
	}

	AAIController* Controller = OwnerComp.GetAIOwner();
	ACompanionCharacter* Companion = Controller ? Cast<ACompanionCharacter>(Controller->GetPawn()) : nullptr;
	if (!IsValid(Companion))
	{
		UE_LOG(LogCompanionAI, Warning, TEXT("HoldCover: TickTask FAILED reason=no-companion"));
		ReleaseHoldFocus(OwnerComp);
		CompanionCover::ClearCommandIfStillActive(OwnerComp, ECompanionCommand::TakeCover);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	// Hold released -- exit cleanly.
	if (!Companion->IsCommandedCoverHoldActive())
	{
		const UBlackboardComponent* HoldBB = OwnerComp.GetBlackboardComponent();

		// Exit cover pose unless combat is genuinely inheriting the hold (live target AND the
		// command is still ours). A new command clears the hold via the service, and the pose
		// must not leak into the new action.
		if (!IsCombatTakingOver(HoldBB, CombatTargetKey))
			ExitCoverCleanly(Companion);
		ReleaseHoldFocus(OwnerComp);

		// ALWAYS succeed, including when a foreign command released the hold. BR_TakeCover is the
		// LAST child of the BR_ExecuteCommand selector, so failing here fails that selector, and
		// its decorator ("CompanionCommand != None", abort Both) only re-enters on a None -> set
		// edge. A TakeCover -> Breach change never passes through None, so a failure here strands
		// the new command permanently. The downstream Clear Companion Command node is guarded to
		// ExpectedCommand == TakeCover and will not touch a foreign command.
		return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
	}

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		UE_LOG(LogCompanionAI, Warning, TEXT("%s: HoldCover TickTask FAILED reason=no-BB"), *Companion->GetName());
		ExitCoverCleanly(Companion);
		ReleaseHoldFocus(OwnerComp);
		CompanionCover::ClearCommandIfStillActive(OwnerComp, ECompanionCommand::TakeCover);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	const FCover Cover = BB->GetValue<UBlackboardKeyType_Cover>(CoverTargetKey.GetSelectedKeyID());
	if (!Cover.IsValid())
	{
		UE_LOG(LogCompanionAI, Warning, TEXT("%s: HoldCover TickTask FAILED reason=cover-invalid"), *Companion->GetName());
		ExitCoverCleanly(Companion);
		Companion->ClearCommandedCoverHold();
		ReleaseHoldFocus(OwnerComp);
		CompanionCover::ClearCommandIfStillActive(OwnerComp, ECompanionCommand::TakeCover);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	// Re-assert stance each tick. Stand-height cover must actively uncrouch a companion that
	// arrived crouched from a previous state (stealth, prior crouch cover, stance backstop).
	const ECoverHeight Height = UCoverGeometryStatics::GetCoverHeight(Cover.Data);
	if (Height == ECoverHeight::Crouch && !Companion->bIsCrouched)
		Companion->Crouch();
	else if (Height == ECoverHeight::Stand && Companion->bIsCrouched
		&& !Companion->IsStanceOwnedElsewhere() && !Companion->IsCrouchOwnedByStealth())
		Companion->UnCrouch();

	// Re-enter cover pose if it was lost (e.g. another system reset it). Do not replay the
	// enter montage every tick -- only re-enter when the pose flag is cleared.
	if (!bCoverPoseEntered)
	{
		if (UCompanionAnimInstance* Anim = GetCompanionAnim(Companion))
		{
			Anim->EnterCoverPose(ResolvedSide, Height, /*bPlayEnterMontage*/ true);
			bCoverPoseEntered = true;
		}
	}
	else
	{
		// Check if the pose was lost by something else. Re-enter silently if needed.
		if (UCoverPoseComponent* PoseComp = Companion->GetCoverPoseComponent())
		{
			if (!PoseComp->bInCover)
			{
				if (UCompanionAnimInstance* Anim = GetCompanionAnim(Companion))
					Anim->EnterCoverPose(ResolvedSide, Height, /*bPlayEnterMontage*/ false);
			}
		}
	}

	// Re-assert fire-arc facing.
	const FVector FireArcFwd = UCoverGeometryStatics::GetFireArcForward(Cover.Data);
	Controller->SetFocalPoint(Companion->GetActorLocation() + FireArcFwd * 500.f, EAIFocusPriority::Gameplay);
}

EBTNodeResult::Type UBTTask_CompanionHoldCover::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* /*NodeMemory*/)
{
	// Exit cover pose unless combat is genuinely inheriting the hold (symmetric with TickTask's
	// hold-released path). A new command must exit the pose even with a live combat target.
	if (!IsCombatTakingOver(OwnerComp.GetBlackboardComponent(), CombatTargetKey))
	{
		if (AAIController* C = OwnerComp.GetAIOwner())
			if (ACompanionCharacter* Comp = Cast<ACompanionCharacter>(C->GetPawn()))
				ExitCoverCleanly(Comp);
	}

	ReleaseHoldFocus(OwnerComp);
	CompanionCover::ClearCommandIfStillActive(OwnerComp, ECompanionCommand::TakeCover);
	return EBTNodeResult::Aborted;
}

FString UBTTask_CompanionHoldCover::GetStaticDescription() const
{
	return TEXT("Hold commanded cover: re-assert pose + facing until the hold releases");
}
