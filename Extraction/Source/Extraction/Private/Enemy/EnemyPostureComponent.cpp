// UEnemyPostureComponent — per-enemy Hold / Press / FallBack evaluation on a staggered timer.

#include "EnemyPostureComponent.h"
#include "EnemyCharacter.h"
#include "EnemyAIController.h"
#include "EnemyArchetypeData.h"
#include "EnemyAwarenessComponent.h"
#include "EnemyMoraleComponent.h"
#include "SuppressionComponent.h"
#include "Squad/EnemySquad.h"
#include "Engine/World.h"
#include "TimerManager.h"

/** Own suppression must be below this for the threat to count as passive (they're not pinning us). */
static constexpr float PassiveSuppressionThreshold = 0.25f;
/** Squad size at which the numeric-advantage term saturates. */
static constexpr float SquadAdvantageSaturation = 3.f;
/** Fallback eval interval when the archetype DA is not yet available at BeginPlay. */
static constexpr float DefaultEvalInterval = 0.5f;

UEnemyPostureComponent::UEnemyPostureComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UEnemyPostureComponent::BeginPlay()
{
	Super::BeginPlay();

	const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(GetOwner());
	const UEnemyArchetypeData* DA = IsValid(Enemy) ? Enemy->GetArchetypeData() : nullptr;
	const float Interval = IsValid(DA) ? DA->PostureEvalInterval : DefaultEvalInterval;

	// Random first-fire phase is the ONLY anti-sync mechanism — enemies evaluate (and therefore
	// advance) on independent clocks, so grunts never move as a synchronized line.
	GetWorld()->GetTimerManager().SetTimer(EvalTimerHandle, this,
		&UEnemyPostureComponent::EvaluatePosture, Interval, true,
		FMath::FRandRange(0.f, Interval));
}

void UEnemyPostureComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(EvalTimerHandle);
	Super::EndPlay(EndPlayReason);
}

void UEnemyPostureComponent::DeactivateForDeath()
{
	bStopped = true;
	CurrentPosture = EEnemyPosture::Hold;
	bAdvanceRequested = false;
	if (UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(EvalTimerHandle);
}

bool UEnemyPostureComponent::ConsumeAdvanceRequest()
{
	if (!bAdvanceRequested) return false;
	bAdvanceRequested = false;
	return true;
}

void UEnemyPostureComponent::NotifyAdvanceExecuted()
{
	const UWorld* World = GetWorld();
	LastAdvanceWorldTime = World ? World->GetTimeSeconds() : 0.f;
	PressHeldSeconds = 0.f;
}

void UEnemyPostureComponent::EvaluatePosture()
{
	if (bStopped) return;

	const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(GetOwner());
	if (!IsValid(Enemy)) return;

	const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
	if (!IsValid(DA) || !DA->bPostureSystemEnabled)
	{
		CurrentPosture = EEnemyPosture::Hold;
		PressHeldSeconds = 0.f;
		bAdvanceRequested = false;
		return;
	}

	const AEnemyAIController* Controller = Cast<AEnemyAIController>(Enemy->GetController());
	const UEnemyAwarenessComponent* Awareness = Controller ? Controller->GetAwarenessComponent() : nullptr;
	const AActor* Target = IsValid(Awareness) ? Awareness->GetCombatTarget() : nullptr;

	if (!IsValid(Target))
	{
		CurrentPosture = EEnemyPosture::Hold;
		PressHeldSeconds = 0.f;
		bAdvanceRequested = false;
		return;
	}

	const UEnemyMoraleComponent* Morale = Enemy->GetMoraleComponent();
	const EMoraleState MoraleState = IsValid(Morale) ? Morale->GetMoraleState() : EMoraleState::Confident;
	const USuppressionComponent* Suppression = Enemy->GetSuppressionComponent();
	const bool bSuppressed = IsValid(Suppression) && Suppression->IsSuppressed();

	const float Aggression = ComputeAggression01(Enemy, DA, Target);

	if (MoraleState == EMoraleState::Broken || Aggression <= DA->FallBackEnterThreshold)
	{
		CurrentPosture = EEnemyPosture::FallBack;
		PressHeldSeconds = 0.f;
		bAdvanceRequested = false;
		return;
	}

	if (CurrentPosture == EEnemyPosture::Press)
	{
		const bool bStayPressing = DA->bCanPress && !bSuppressed
			&& MoraleState == EMoraleState::Confident
			&& Aggression >= DA->PressExitThreshold;
		if (!bStayPressing)
		{
			// Leaving Press invalidates any pending advance — a re-entered Press must re-earn
			// the hold time, or a stale latch fires the moment the next Pause arrives.
			CurrentPosture = EEnemyPosture::Hold;
			PressHeldSeconds = 0.f;
			bAdvanceRequested = false;
			return;
		}

		PressHeldSeconds += DA->PostureEvalInterval;
		const UWorld* World = GetWorld();
		const float Now = World ? World->GetTimeSeconds() : 0.f;
		if (PressHeldSeconds >= DA->PressAdvanceHoldTime
			&& (Now - LastAdvanceWorldTime) >= DA->AdvanceRelocateCooldown)
			bAdvanceRequested = true;
		return;
	}

	if (DA->bCanPress && !bSuppressed && MoraleState == EMoraleState::Confident
		&& Aggression >= DA->PressEnterThreshold)
	{
		CurrentPosture = EEnemyPosture::Press;
		PressHeldSeconds = 0.f;
		return;
	}

	CurrentPosture = EEnemyPosture::Hold;
	PressHeldSeconds = 0.f;
}

float UEnemyPostureComponent::ComputeAggression01(const AEnemyCharacter* Enemy,
	const UEnemyArchetypeData* DA, const AActor* Target) const
{
	const AEnemyAIController* Controller = Cast<AEnemyAIController>(Enemy->GetController());
	const UEnemyAwarenessComponent* Awareness = Controller ? Controller->GetAwarenessComponent() : nullptr;

	// Term A: the threat hasn't hurt us recently (ramps 0 -> 1 across the window).
	const float TimeSinceDamaged = IsValid(Awareness) ? Awareness->GetTimeSinceDamagedBy(Target) : BIG_NUMBER;
	const float NoDamageTerm = FMath::Clamp(TimeSinceDamaged / DA->PostureRecentDamageWindow, 0.f, 1.f);

	// Term B: the threat is fully passive — not hurting us AND not pinning us with fire.
	const USuppressionComponent* Suppression = Enemy->GetSuppressionComponent();
	const float Suppression01 = IsValid(Suppression) ? Suppression->GetSuppression01() : 0.f;
	const float ThreatPassiveTerm = (NoDamageTerm >= 1.f && Suppression01 < PassiveSuppressionThreshold) ? 1.f : 0.f;

	// Term C: numeric advantage (optional, weight defaults to 0).
	float SquadTerm = 0.f;
	if (DA->PostureWeightSquadAdvantage > 0.f)
	{
		const UEnemySquad* Squad = Enemy->GetSquad();
		if (IsValid(Squad))
			SquadTerm = FMath::Clamp(static_cast<float>(Squad->NumAlive()) / SquadAdvantageSaturation, 0.f, 1.f);
	}

	const float WeightSum = DA->PostureWeightNoDamageRecently + DA->PostureWeightThreatPassive
		+ DA->PostureWeightSquadAdvantage;
	if (WeightSum <= 0.f) return 0.f;

	return FMath::Clamp((DA->PostureWeightNoDamageRecently * NoDamageTerm
		+ DA->PostureWeightThreatPassive * ThreatPassiveTerm
		+ DA->PostureWeightSquadAdvantage * SquadTerm) / WeightSum, 0.f, 1.f);
}
