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

		if (UEnemyDirectorSubsystem* Director = World->GetSubsystem<UEnemyDirectorSubsystem>())
			Director->OnEnemyDied.RemoveDynamic(this, &UEnemyMoraleComponent::HandleEnemyDied);
	}

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

// --- Director death handler ---

void UEnemyMoraleComponent::HandleEnemyDied(AEnemyCharacter* DeadEnemy, FVector Location, bool bWasOfficer)
{
	if (bFearless) return;
	if (!OwnerEnemy.IsValid()) return;
	if (CachedHealthComp.IsValid() && CachedHealthComp->IsDead()) return;
	if (DeadEnemy == OwnerEnemy.Get()) return;

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
	CurrentMorale = FMath::Clamp(CurrentMorale + ScaledDelta, FMath::Max(MoraleFloor, 0.f), 100.f);

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
