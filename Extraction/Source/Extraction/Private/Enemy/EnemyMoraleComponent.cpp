// UEnemyMoraleComponent — morale system: per-enemy confidence that gates behaviour (design §7).

#include "EnemyMoraleComponent.h"
#include "EnemyCharacter.h"
#include "EnemyArchetypeData.h"
#include "EnemyDirectorSubsystem.h"
#include "EnemyAIController.h"
#include "HealthComponent.h"
#include "BarkSubsystem.h"
#include "BarkSetData.h"
#include "SuppressionComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "TimerManager.h"
#include "Engine/World.h"

UEnemyMoraleComponent::UEnemyMoraleComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UEnemyMoraleComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerEnemy = Cast<AEnemyCharacter>(GetOwner());
	if (!OwnerEnemy.IsValid()) return;

	CachedHealthComp = OwnerEnemy->GetHealthComponent();

	if (IsValid(OwnerEnemy->GetArchetypeData()))
		CachedBarkSet = OwnerEnemy->GetArchetypeData()->BarkSet;

	if (UWorld* World = GetWorld())
	{
		if (UEnemyDirectorSubsystem* Director = World->GetSubsystem<UEnemyDirectorSubsystem>())
			Director->OnEnemyDied.AddUniqueDynamic(this, &UEnemyMoraleComponent::HandleEnemyDied);
	}

	CachedSuppressionComp = OwnerEnemy->FindComponentByClass<USuppressionComponent>();
	if (CachedSuppressionComp.IsValid())
		CachedSuppressionComp->OnSuppressedStateChanged.AddUniqueDynamic(this, &UEnemyMoraleComponent::HandleSuppressedStateChanged);

	const float Stagger = FMath::FRandRange(0.f, MoraleTickInterval);
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			MoraleTickHandle, this, &UEnemyMoraleComponent::MoraleTick,
			MoraleTickInterval, true, Stagger);
	}
}

void UEnemyMoraleComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(MoraleTickHandle);
		World->GetTimerManager().ClearTimer(RallyFloorTimerHandle);

		if (UEnemyDirectorSubsystem* Director = World->GetSubsystem<UEnemyDirectorSubsystem>())
			Director->OnEnemyDied.RemoveDynamic(this, &UEnemyMoraleComponent::HandleEnemyDied);
	}

	if (CachedSuppressionComp.IsValid())
		CachedSuppressionComp->OnSuppressedStateChanged.RemoveDynamic(this, &UEnemyMoraleComponent::HandleSuppressedStateChanged);

	Super::EndPlay(EndPlayReason);
}

void UEnemyMoraleComponent::DeactivateForDeath()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(MoraleTickHandle);
		World->GetTimerManager().ClearTimer(RallyFloorTimerHandle);

		if (UEnemyDirectorSubsystem* Director = World->GetSubsystem<UEnemyDirectorSubsystem>())
			Director->OnEnemyDied.RemoveDynamic(this, &UEnemyMoraleComponent::HandleEnemyDied);
	}

	RallyFloorRaise = 0.f;

	if (CachedSuppressionComp.IsValid())
		CachedSuppressionComp->OnSuppressedStateChanged.RemoveDynamic(this, &UEnemyMoraleComponent::HandleSuppressedStateChanged);
}

void UEnemyMoraleComponent::InitFromArchetype(const UEnemyArchetypeData* Data)
{
	if (!IsValid(Data)) return;

	bFearless = Data->bFearless;
	MoraleFloor = Data->MoraleFloor;
	MoraleEventResistance = FMath::Max(Data->MoraleEventResistance, 0.1f);
	ShakenThreshold = Data->ShakenThreshold;
	BrokenThreshold = Data->BrokenThreshold;
	RecoveryPerSecond = Data->MoraleRecoveryPerSecond;
	LossAllyDied = Data->MoraleLossAllyDied;
	LossOfficerDied = Data->MoraleLossOfficerDied;
	LossSustainedSuppression = Data->MoraleLossSustainedSuppression;
	LossLowHealth = Data->MoraleLossLowHealth;
	LossFlanked = Data->MoraleLossFlanked;
	GainDamagedTarget = Data->MoraleGainDamagedTarget;
	GainTargetDowned = Data->MoraleGainTargetDowned;
	RallyFloorDuration = Data->RallyFloorDuration;
	CachedBarkSet = Data->BarkSet;

	CurrentMorale = 100.f;
	CurrentState = EMoraleState::Confident;
	bLowHealthFired = false;
}

// --- Event ingress ---

void UEnemyMoraleComponent::NotifyDamagedTarget()
{
	if (bFearless) return;
	ApplyMoraleDelta(GainDamagedTarget);
}

void UEnemyMoraleComponent::NotifyTargetDowned()
{
	if (bFearless) return;
	ApplyMoraleDelta(GainTargetDowned);
}

void UEnemyMoraleComponent::NotifyLowHealth()
{
	if (bFearless) return;
	if (bLowHealthFired) return;
	bLowHealthFired = true;
	ApplyMoraleDelta(-LossLowHealth);
}

// --- Phase 5: squad-routed ingress ---

void UEnemyMoraleComponent::NotifySquadAllyDied(bool bWasOfficer)
{
	if (bFearless) return;
	if (!OwnerEnemy.IsValid()) return;
	if (CachedHealthComp.IsValid() && CachedHealthComp->IsDead()) return;

	const float Loss = bWasOfficer ? LossOfficerDied : LossAllyDied;
	ApplyMoraleDelta(-Loss);
	RequestBark(EBarkType::ManDown);
}

void UEnemyMoraleComponent::NotifyRally(float MoraleBoost, float FloorRaise)
{
	if (bFearless) return;
	if (!OwnerEnemy.IsValid()) return;
	if (CachedHealthComp.IsValid() && CachedHealthComp->IsDead()) return;

	// Apply floor raise BEFORE boost so the clamp in ApplyMoraleDelta uses the new floor.
	RallyFloorRaise = FMath::Max(RallyFloorRaise, FloorRaise);

	// Clamp morale up to the new effective floor (rally can't leave you below it).
	CurrentMorale = FMath::Max(CurrentMorale, GetEffectiveMoraleFloor());

	ApplyMoraleDelta(MoraleBoost);

	// Guarantee post-rally morale clears BrokenThreshold so the enemy actually un-pins.
	const float MinRallyMorale = BrokenThreshold + 1.f;
	if (CurrentMorale < MinRallyMorale)
		CurrentMorale = FMath::Min(MinRallyMorale, 100.f);

	UWorld* World = GetWorld();
	if (!World) return;

	World->GetTimerManager().ClearTimer(RallyFloorTimerHandle);
	World->GetTimerManager().SetTimer(
		RallyFloorTimerHandle, this, &UEnemyMoraleComponent::ClearRallyFloor,
		RallyFloorDuration, false);

	EvaluateState();
}

void UEnemyMoraleComponent::ClearRallyFloor()
{
	RallyFloorRaise = 0.f;
	EvaluateState();
}

float UEnemyMoraleComponent::GetEffectiveMoraleFloor() const
{
	return FMath::Max(MoraleFloor + RallyFloorRaise, 0.f);
}

// --- Director death handler ---

void UEnemyMoraleComponent::HandleEnemyDied(AEnemyCharacter* DeadEnemy, FVector Location, bool bWasOfficer)
{
	if (bFearless) return;
	if (!OwnerEnemy.IsValid()) return;
	if (CachedHealthComp.IsValid() && CachedHealthComp->IsDead()) return;
	if (DeadEnemy == OwnerEnemy.Get()) return;

	// Phase 5: skip radius path when the dead enemy shares the owner's squad. Same-squad deaths
	// arrive via NotifySquadAllyDied (exactly-once). FName comparison is used because the subsystem
	// ejects the dead member before this broadcast fires, making pointer lookups unreliable.
	if (OwnerEnemy->SquadId != NAME_None && IsValid(DeadEnemy) && DeadEnemy->SquadId == OwnerEnemy->SquadId)
		return;

	const float DistSq = FVector::DistSquared(OwnerEnemy->GetActorLocation(), Location);

	if (bWasOfficer)
	{
		const float OfficerRadius = AllyDeathRadius * 2.f;
		if (DistSq > OfficerRadius * OfficerRadius) return;

		ApplyMoraleDelta(-LossOfficerDied);
		RequestBark(EBarkType::ManDown);
		return;
	}

	if (DistSq > AllyDeathRadius * AllyDeathRadius) return;

	ApplyMoraleDelta(-LossAllyDied);
	RequestBark(EBarkType::ManDown);
}

// --- Suppression handler ---

void UEnemyMoraleComponent::HandleSuppressedStateChanged(bool bNowSuppressed)
{
	if (!bNowSuppressed) return;
	if (bFearless) return;
	if (CachedHealthComp.IsValid() && CachedHealthComp->IsDead()) return;
	RequestBark(EBarkType::Pinned);
}

// --- Periodic tick ---

void UEnemyMoraleComponent::MoraleTick()
{
	if (bFearless) return;
	if (!OwnerEnemy.IsValid()) return;

	if (CachedHealthComp.IsValid() && CachedHealthComp->IsDead()) return;

	// Sustained suppression drain
	if (CachedSuppressionComp.IsValid() && CachedSuppressionComp->IsSuppressed())
		ApplyMoraleDelta(-LossSustainedSuppression);

	// Low-HP one-shot check
	if (!bLowHealthFired && CachedHealthComp.IsValid())
	{
		if (CachedHealthComp->GetHealthPercent() <= LowHealthThreshold)
			NotifyLowHealth();
	}

	// Flanked check: combat target behind owner's facing
	CheckFlanked();

	// Recovery drift
	const float WorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	const float TimeSinceLoss = WorldTime - LastLossWorldTime;
	if (TimeSinceLoss >= RecoveryGraceSeconds && CurrentMorale < 100.f)
		ApplyMoraleDelta(RecoveryPerSecond * MoraleTickInterval);
}

// --- Internals ---

void UEnemyMoraleComponent::ApplyMoraleDelta(float Delta)
{
	const float ScaledDelta = (Delta < 0.f) ? (Delta / MoraleEventResistance) : Delta;
	const float OldMorale = CurrentMorale;
	CurrentMorale = FMath::Clamp(CurrentMorale + ScaledDelta, GetEffectiveMoraleFloor(), 100.f);

	if (ScaledDelta < 0.f && GetWorld())
		LastLossWorldTime = GetWorld()->GetTimeSeconds();

	if (CurrentMorale != OldMorale)
		EvaluateState();
}

void UEnemyMoraleComponent::EvaluateState()
{
	EMoraleState NewState;
	if (CurrentMorale <= BrokenThreshold)
		NewState = EMoraleState::Broken;
	else if (CurrentMorale <= ShakenThreshold)
		NewState = EMoraleState::Shaken;
	else
		NewState = EMoraleState::Confident;

	if (NewState == CurrentState) return;

	const EMoraleState OldState = CurrentState;
	CurrentState = NewState;

	UE_LOG(LogEnemyAI, Log, TEXT("%s: Morale %s -> %s (%.0f)"),
		OwnerEnemy.IsValid() ? *OwnerEnemy->GetName() : TEXT("???"),
		*UEnum::GetValueAsString(OldState),
		*UEnum::GetValueAsString(NewState),
		CurrentMorale);

	OnMoraleStateChanged.Broadcast(OldState, NewState);

	if (NewState == EMoraleState::Broken)
		RequestBark(EBarkType::FallingBack);
}

void UEnemyMoraleComponent::CheckFlanked()
{
	if (!OwnerEnemy.IsValid()) return;

	AAIController* AIC = Cast<AAIController>(OwnerEnemy->GetController());
	if (!IsValid(AIC)) return;

	UBlackboardComponent* BB = AIC->GetBlackboardComponent();
	if (!BB) return;

	AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));
	if (!IsValid(Target)) return;

	const FVector ToTarget = (Target->GetActorLocation() - OwnerEnemy->GetActorLocation()).GetSafeNormal2D();
	const FVector Forward = OwnerEnemy->GetActorForwardVector().GetSafeNormal2D();
	const float Dot = FVector::DotProduct(Forward, ToTarget);

	if (Dot < FlankedDotThreshold)
		ApplyMoraleDelta(-LossFlanked);
}

void UEnemyMoraleComponent::RequestBark(EBarkType Type) const
{
	if (!OwnerEnemy.IsValid()) return;
	if (!IsValid(CachedBarkSet)) return;

	UWorld* World = GetWorld();
	if (!World) return;

	UBarkSubsystem* BarkSys = World->GetSubsystem<UBarkSubsystem>();
	if (!IsValid(BarkSys)) return;

	const FText SpeakerName = IsValid(OwnerEnemy->GetArchetypeData())
		? OwnerEnemy->GetArchetypeData()->DisplayName
		: FText::FromString(TEXT("Enemy"));

	BarkSys->RequestBark(OwnerEnemy.Get(), CachedBarkSet.Get(), Type, SpeakerName);
}
