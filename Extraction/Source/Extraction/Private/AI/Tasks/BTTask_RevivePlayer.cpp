// BT task — companion sprints to downed player and revives them after a hold timer.

#include "BTTask_RevivePlayer.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "CompanionAIController.h" // LogCompanionAI — revive lifecycle must show in the filtered log
#include "Character/ExtractionPlayerInterface.h"
#include "CompanionCharacter.h"
#include "HealthComponent.h"
#include "WeaponBase.h"
#include "HAL/IConsoleManager.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"

// Two-axis live-tuning offsets (player-frame cm) for where the companion starts the paired revive
// clips. The clips share an authored scene frame with the companion beginning offset from the downed
// player, which a single forward distance can't express. Either non-zero overrides revive.AlignDistance.
static float GReviveCompanionOffsetFwd = 0.f;
static FAutoConsoleVariableRef CVarReviveCompanionOffsetFwd(
	TEXT("revive.CompanionOffsetFwd"), GReviveCompanionOffsetFwd,
	TEXT("Companion revive-start offset along the downed player's forward, cm (negative = behind)."));

static float GReviveCompanionOffsetRight = 0.f;
static FAutoConsoleVariableRef CVarReviveCompanionOffsetRight(
	TEXT("revive.CompanionOffsetRight"), GReviveCompanionOffsetRight,
	TEXT("Companion revive-start offset along the downed player's right, cm (negative = left)."));


UBTTask_RevivePlayer::UBTTask_RevivePlayer()
{
	NodeName = TEXT("Revive Player");
	bNotifyTick = true;
	bCreateNodeInstance = true;
}

EBTNodeResult::Type UBTTask_RevivePlayer::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB) return EBTNodeResult::Failed;

	IExtractionPlayerInterface* Player = Cast<IExtractionPlayerInterface>(
		BB->GetValueAsObject(PlayerActorKey.SelectedKeyName));
	if (!Player || !Player->GetIsDBNO()) return EBTNodeResult::Failed;

	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC) return EBTNodeResult::Failed;
	ACompanionCharacter* Companion = Cast<ACompanionCharacter>(AIC->GetPawn());
	if (!Companion) return EBTNodeResult::Failed;

	// Stop any combat activity
	Companion->StopWeaponFire();
	Companion->SetIsRevivingPlayer(true);
	CachedCompanion = Companion;
	CachedPlayerActor = Cast<AActor>(BB->GetValueAsObject(PlayerActorKey.SelectedKeyName));

	ReviveElapsed = 0.0f;
	bIsHoldingRevive = false;

	UE_LOG(LogCompanionAI, Log, TEXT("Companion starting revive sequence for %s"),
		*GetNameSafe(Cast<AActor>(BB->GetValueAsObject(PlayerActorKey.SelectedKeyName))));

	return EBTNodeResult::InProgress;
}

void UBTTask_RevivePlayer::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	if (!Controller) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	ACompanionCharacter* Companion = Cast<ACompanionCharacter>(Controller->GetPawn());
	if (!Companion) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	if (UHealthComponent* CompHealth = Companion->GetHealthComponent())
	{
		if (CompHealth->IsDead()) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	UObject* PlayerObj = BB->GetValueAsObject(PlayerActorKey.SelectedKeyName);
	IExtractionPlayerInterface* PlayerIface = Cast<IExtractionPlayerInterface>(PlayerObj);
	AActor* PlayerActor = Cast<AActor>(PlayerObj);

	if (!PlayerIface || !IsValid(PlayerActor))
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	// Player no longer DBNO — either the get-up montage completed the revive (montage-driven
	// ExitDBNO) or something external revived/killed them. Done either way.
	if (!PlayerIface->GetIsDBNO())
	{
		UE_LOG(LogCompanionAI, Log, TEXT("Companion revive complete t=%.2f — player no longer DBNO"), ReviveElapsed);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
	}

	const float DistToPlayer = FVector::Dist(Companion->GetActorLocation(), PlayerActor->GetActorLocation());

	// Not close enough to START — fail so the sequence re-runs the follow task. Only gates entry:
	// once holding, the root-motion revive clips legitimately move both capsules, so a mid-hold
	// distance re-check would false-fail the revive.
	if (!bIsHoldingRevive && DistToPlayer > Companion->ReviveProximityRadius)
	{
		ReviveElapsed = 0.0f;
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	// In range — stop moving, snap the pair into the authored arrangement, and hold for revive.
	if (!bIsHoldingRevive)
	{
		Controller->StopMovement();
		bIsHoldingRevive = true;

		// Anims/locks first: SetBeingRevived (inside) suspends the player's controller-yaw follow so
		// the rotations below stick; it also adds the pair move-ignore before the snap.
		SetReviveAnimsActive(true);

		// The DOWNED PLAYER is the arrangement anchor: the paired clips share an authored frame, so
		// the companion always takes the same position/facing RELATIVE TO THE PLAYER'S BODY,
		// regardless of which side it approached from (fixes "revives from behind").
		PlayerIface->AlignForRevive(Companion->GetActorLocation());

		const FVector PlayerLoc = PlayerActor->GetActorLocation();
		const float PlayerYaw = PlayerActor->GetActorRotation().Yaw;

		const FVector PreSnapLoc = Companion->GetActorLocation();
		const float PreSnapYaw = Companion->GetActorRotation().Yaw;

		const float CompYawOffset = ACompanionCharacter::GetReviveCompanionYawOffset();

		// Authored pair arrangement, measured from the anim pack's demo map (both SkeletalMeshActors
		// at the same spot, reviver actor yawed 179.1°) composed with each clip's root transform at
		// t=0 (reviver R: loc (-65.67,-55.42) yaw 135; patient L: loc (0.39,-5.20) yaw 0), then
		// mapped into the root-locked capsule frame (mesh -90° convention folded in). Player frame:
		// companion starts 60cm ahead, 64.5cm left, angled -46° toward the player. CVar nudges/yaws
		// remain additive on top.
		constexpr float AuthoredFwd = 60.0f;
		constexpr float AuthoredRight = -64.5f;
		constexpr float AuthoredYaw = -46.0f;

		const FRotator PlayerFrame(0.f, PlayerYaw, 0.f);
		FVector AlignLoc = PlayerLoc + PlayerFrame.RotateVector(FVector(
			AuthoredFwd + GReviveCompanionOffsetFwd, AuthoredRight + GReviveCompanionOffsetRight, 0.f));
		AlignLoc.Z = Companion->GetActorLocation().Z;
		Companion->SetActorLocation(AlignLoc, false, nullptr, ETeleportType::TeleportPhysics);
		Companion->SetActorRotation(FRotator(0.0f, PlayerYaw + AuthoredYaw + CompYawOffset, 0.0f));

		const FVector PostSnapLoc = Companion->GetActorLocation();
		UE_LOG(LogCompanionAI, Log,
			TEXT("REVIVE ALIGN(companion): playerLoc=%s playerYaw=%.1f | companion %s -> %s (snapMoved=%.1f) yaw %.1f -> %.1f meshRelYaw=%.1f | nudgeFwd=%.1f nudgeRight=%.1f yawOffset=%.1f actualDist2D=%.1f"),
			*PlayerLoc.ToCompactString(), PlayerYaw,
			*PreSnapLoc.ToCompactString(), *PostSnapLoc.ToCompactString(),
			FVector::Dist(PreSnapLoc, PostSnapLoc),
			PreSnapYaw, Companion->GetActorRotation().Yaw,
			Companion->GetMesh() ? Companion->GetMesh()->GetRelativeRotation().Yaw : 0.f,
			GReviveCompanionOffsetFwd, GReviveCompanionOffsetRight,
			CompYawOffset, FVector::Dist2D(PostSnapLoc, PlayerLoc));

		HoldLogAccumulator = 0.0f;
	}

	// Re-assert the reviver montage ONLY when a different same-group montage (cover teardown, fire
	// anim, takedown) has stomped it mid-hold. Unconditionally re-playing every tick used to restart
	// the kneel the moment the montage naturally ended near the hold's end — the visible double play.
	if (Companion->ShouldReassertReviveMontage())
		Companion->PlayReviveMontage();

	// 1Hz hold diagnostic: if the reviver anim is still invisible, this names the stomper.
	HoldLogAccumulator += DeltaSeconds;
	if (HoldLogAccumulator >= 1.0f)
	{
		HoldLogAccumulator = 0.0f;

		// Facing error: 0 = each actor looks dead at the other; drift here means something rotated
		// or moved one of the pair mid-hold.
		const FVector CompLoc = Companion->GetActorLocation();
		const FVector PlyLoc = PlayerActor->GetActorLocation();
		const float BearingCompToPlayer = (PlyLoc - CompLoc).GetSafeNormal2D().Rotation().Yaw;
		const float CompFacingError = FMath::FindDeltaAngleDegrees(Companion->GetActorRotation().Yaw, BearingCompToPlayer);
		const float PlayerFacingError = FMath::FindDeltaAngleDegrees(PlayerActor->GetActorRotation().Yaw, BearingCompToPlayer + 180.f);

		// Mesh world yaw catches an authored root offset that actor-yaw facing errors would read as 0.
		const USkeletalMeshComponent* CompMesh = Companion->GetMesh();
		const float CompMeshWorldYaw = CompMesh ? CompMesh->GetComponentRotation().Yaw : 0.f;

		// Player-half montage state: if the reviver kneels but this reads 0, the downed body never
		// took over the pose (stays flat) — the pair-alignment symptom.
		const int32 PlayerMontagePlaying = (int32)PlayerIface->IsBeingRevivedMontagePlaying();

		UE_LOG(LogCompanionAI, Log,
			TEXT("REVIVE HOLD t=%.1f reviverMontagePlaying=%d plyRevMontagePlaying=%d activeBodyMontage=%s | dist2D=%.1f compLoc=%s plyLoc=%s compYaw=%.1f plyYaw=%.1f compMeshWorldYaw=%.1f compFaceErr=%.1f plyFaceErr=%.1f"),
			ReviveElapsed, (int32)Companion->IsReviveMontagePlaying(), PlayerMontagePlaying, *Companion->GetActiveBodyMontageName(),
			FVector::Dist2D(CompLoc, PlyLoc), *CompLoc.ToCompactString(), *PlyLoc.ToCompactString(),
			Companion->GetActorRotation().Yaw, PlayerActor->GetActorRotation().Yaw, CompMeshWorldYaw,
			CompFacingError, PlayerFacingError);
	}

	ReviveElapsed += DeltaSeconds;

	// The player's get-up montage owns completion (its blend-out fires ExitDBNO, caught by the
	// !GetIsDBNO check above). The timer is the fallback for a missing/stomped montage — while the
	// montage is still visibly playing, give it a 1s grace past the hold before forcing the exit.
	if (ReviveElapsed >= Companion->GetEffectiveReviveDuration())
	{
		const bool bMontageStillPlaying = PlayerIface->IsBeingRevivedMontagePlaying();
		if (bMontageStillPlaying && ReviveElapsed < Companion->GetEffectiveReviveDuration() + 1.0f) return;

		UE_LOG(LogCompanionAI, Log, TEXT("REVIVE fallback completion t=%.2f (montagePlaying=%d) — timer-driven ExitDBNO"),
			ReviveElapsed, (int32)bMontageStillPlaying);

		if (Companion->HasAuthority())
			PlayerIface->ExitDBNO();

		UE_LOG(LogCompanionAI, Log, TEXT("Companion revived %s"), *GetNameSafe(PlayerActor));
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
	}
}

void UBTTask_RevivePlayer::SetReviveAnimsActive(bool bActive)
{
	ACompanionCharacter* Companion = CachedCompanion.Get();
	AActor* PlayerActor = CachedPlayerActor.Get();

	if (Companion)
	{
		if (bActive) Companion->PlayReviveMontage();
		else Companion->StopReviveMontage();

		// The reviver clips are authored empty-handed — hide the held weapon for the hold.
		// SetWeaponHidden, not SetActorHiddenInGame: the visible gun is a separate visual actor.
		// Guard the un-hide: a companion downed mid-revive keeps its weapon hidden via EnterDBNO.
		if (AWeaponBase* Weapon = Companion->GetCurrentWeapon())
		{
			if (bActive || !Companion->GetIsDBNO())
				Weapon->SetWeaponHidden(bActive);
		}

		// Mutual move-ignore for the hold: lets ReviveAlignDistance tuck the pair closer than the
		// two capsule radii would allow without CMC depenetration jitter. Both are stationary
		// (StopMovement + input locks) so overlap is inert; restored on every exit path.
		if (PlayerActor)
		{
			if (bActive) Companion->MoveIgnoreActorAdd(PlayerActor);
			else Companion->MoveIgnoreActorRemove(PlayerActor);

			if (APawn* PlayerPawn = Cast<APawn>(PlayerActor))
			{
				if (bActive) PlayerPawn->MoveIgnoreActorAdd(Companion);
				else PlayerPawn->MoveIgnoreActorRemove(Companion);
			}
		}
	}

	if (IExtractionPlayerInterface* Player = Cast<IExtractionPlayerInterface>(PlayerActor))
		Player->SetBeingRevived(bActive, Companion ? Companion->GetEffectiveReviveDuration() : 0.f);
}

void UBTTask_RevivePlayer::OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult)
{
	SetReviveAnimsActive(false);
	if (CachedCompanion.IsValid())
		CachedCompanion->SetIsRevivingPlayer(false);
	CachedCompanion.Reset();
	CachedPlayerActor.Reset();
	Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);
}

EBTNodeResult::Type UBTTask_RevivePlayer::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	SetReviveAnimsActive(false);
	if (CachedCompanion.IsValid())
		CachedCompanion->SetIsRevivingPlayer(false);
	CachedCompanion.Reset();
	CachedPlayerActor.Reset();
	return Super::AbortTask(OwnerComp, NodeMemory);
}

FString UBTTask_RevivePlayer::GetStaticDescription() const
{
	return TEXT("Revive downed player (threat-gated window)");
}
