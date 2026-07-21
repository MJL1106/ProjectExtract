// BT task - companion moves to a downed player, plays the paired revive, and completes on the hold timer.

#include "BTTask_RevivePlayer.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "CompanionAIController.h"
#include "Character/ExtractionPlayerInterface.h"
#include "CompanionCharacter.h"
#include "Companion/CompanionBarkTypes.h"
#include "HealthComponent.h"
#include "WeaponBase.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

UBTTask_RevivePlayer::UBTTask_RevivePlayer()
{
	NodeName = TEXT("Revive Player");
	bNotifyTick = true;
	bNotifyTaskFinished = true;
	bCreateNodeInstance = true;
}

EBTNodeResult::Type UBTTask_RevivePlayer::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!BB || !AIC) return EBTNodeResult::Failed;

	AActor* PlayerActor = Cast<AActor>(BB->GetValueAsObject(PlayerActorKey.SelectedKeyName));
	IExtractionPlayerInterface* Player = Cast<IExtractionPlayerInterface>(PlayerActor);
	ACompanionCharacter* Companion = Cast<ACompanionCharacter>(AIC->GetPawn());
	if (!IsValid(PlayerActor) || !Player || !Player->GetIsDBNO() || !Companion)
		return EBTNodeResult::Failed;

	Companion->StopWeaponFire();
	Companion->SetIsRevivingPlayer(true);
	CachedCompanion = Companion;
	CachedPlayerActor = PlayerActor;
	ReviveElapsed = 0.f;
	bIsHoldingRevive = false;

	UE_LOG(LogCompanionAI, Log, TEXT("Companion starting revive sequence for %s"), *GetNameSafe(PlayerActor));
	return EBTNodeResult::InProgress;
}

void UBTTask_RevivePlayer::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	ACompanionCharacter* Companion = Controller ? Cast<ACompanionCharacter>(Controller->GetPawn()) : nullptr;
	if (!Companion) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	if (UHealthComponent* CompHealth = Companion->GetHealthComponent(); CompHealth && CompHealth->IsDead())
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AActor* PlayerActor = BB ? Cast<AActor>(BB->GetValueAsObject(PlayerActorKey.SelectedKeyName)) : nullptr;
	IExtractionPlayerInterface* Player = Cast<IExtractionPlayerInterface>(PlayerActor);
	if (!IsValid(PlayerActor) || !Player)
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	if (!Player->GetIsDBNO())
		return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);

	if (!bIsHoldingRevive)
	{
		if (FVector::Dist(Companion->GetActorLocation(), PlayerActor->GetActorLocation()) > Companion->ReviveProximityRadius)
			return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

		Controller->StopMovement();
		if (UCharacterMovementComponent* Movement = Companion->GetCharacterMovement())
			Movement->StopMovementImmediately();

		const FVector PlayerLocation = PlayerActor->GetActorLocation();
		const float PlayerYaw = PlayerActor->GetActorRotation().Yaw;
		const FRotator PlayerYawRotation(0.f, PlayerYaw, 0.f);
		const FVector AuthoredOffset(Companion->RevivePairOffset.X, Companion->RevivePairOffset.Y, 0.f);
		FVector AlignedCompanionLocation = PlayerLocation + PlayerYawRotation.RotateVector(AuthoredOffset);
		AlignedCompanionLocation.Z = Companion->GetActorLocation().Z;
		Companion->SetActorLocation(AlignedCompanionLocation, false, nullptr, ETeleportType::TeleportPhysics);
		Companion->SetActorRotation(FRotator(0.f, PlayerYaw + Companion->RevivePairYawOffset, 0.f));

		bIsHoldingRevive = true;
		SetReviveAnimsActive(true);
		Companion->Bark(ECompanionBarkType::RevivingPlayer);
	}

	ReviveElapsed += DeltaSeconds;
	if (ReviveElapsed < Companion->ReviveDuration) return;

	Player->ExitDBNO();
	Companion->Bark(ECompanionBarkType::PlayerRevived);

	UE_LOG(LogCompanionAI, Log, TEXT("Companion revived %s"), *GetNameSafe(PlayerActor));
	FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
}

void UBTTask_RevivePlayer::SetReviveAnimsActive(bool bActive)
{
	ACompanionCharacter* Companion = CachedCompanion.Get();
	AActor* PlayerActor = CachedPlayerActor.Get();
	if (Companion)
	{
		if (AWeaponBase* Weapon = Companion->GetCurrentWeapon())
		{
			if (bActive || !Companion->GetIsDBNO())
				Weapon->SetWeaponHidden(bActive);
		}

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

		if (bActive) Companion->PlayReviveMontage();
		else Companion->StopReviveMontage();
	}

	if (IExtractionPlayerInterface* Player = Cast<IExtractionPlayerInterface>(PlayerActor))
		Player->SetBeingRevived(bActive, bActive && Companion ? Companion->ReviveDuration : 0.f);
}

void UBTTask_RevivePlayer::CleanupRevive()
{
	SetReviveAnimsActive(false);
	if (CachedCompanion.IsValid())
		CachedCompanion->SetIsRevivingPlayer(false);
	CachedCompanion.Reset();
	CachedPlayerActor.Reset();
	bIsHoldingRevive = false;
	ReviveElapsed = 0.f;
}

void UBTTask_RevivePlayer::OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult)
{
	CleanupRevive();
	Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);
}

EBTNodeResult::Type UBTTask_RevivePlayer::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	CleanupRevive();
	return Super::AbortTask(OwnerComp, NodeMemory);
}

FString UBTTask_RevivePlayer::GetStaticDescription() const
{
	return TEXT("Revive downed player (threat-gated window)");
}
