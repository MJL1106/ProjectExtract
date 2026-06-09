// UEnemyAwarenessComponent — awareness state ladder driven by perception stimuli and damage events.

#include "EnemyAwarenessComponent.h"
#include "EnemyAIController.h"
#include "EnemyArchetypeData.h"
#include "EnemyCharacter.h"
#include "EnemyDirectorSubsystem.h"
#include "BarkSubsystem.h"
#include "BarkSetData.h"
#include "HealthComponent.h"
#include "Character/ExtractionPlayerInterface.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISense_Hearing.h"
#include "GenericTeamAgentInterface.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "TimerManager.h"
#include "Engine/World.h"

UEnemyAwarenessComponent::UEnemyAwarenessComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UEnemyAwarenessComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const UWorld* World = GetWorld())
		World->GetTimerManager().ClearAllTimersForObject(this);

	if (UEnemyDirectorSubsystem* Dir = Director.Get())
		Dir->OnGlobalAlertChanged.RemoveDynamic(this, &UEnemyAwarenessComponent::HandleGlobalAlertChanged);

	Super::EndPlay(EndPlayReason);
}

void UEnemyAwarenessComponent::Initialize(UBlackboardComponent* InBB, const UEnemyArchetypeData* InDA)
{
	BlackboardComp = InBB;
	ArchetypeData = InDA;

	UWorld* World = GetWorld();
	if (!IsValid(World)) return;

	Director = World->GetSubsystem<UEnemyDirectorSubsystem>();
	if (UEnemyDirectorSubsystem* Dir = Director.Get())
		Dir->OnGlobalAlertChanged.AddUniqueDynamic(this, &UEnemyAwarenessComponent::HandleGlobalAlertChanged);

	// Stagger the repeating update timer with a random initial delay to avoid all enemies ticking together
	const float InitialDelay = FMath::RandRange(0.f, UpdateInterval);
	World->GetTimerManager().SetTimer(
		UpdateTimerHandle,
		this,
		&UEnemyAwarenessComponent::UpdateAwareness,
		UpdateInterval,
		true,
		InitialDelay);
}

// --- Perception Callback ---

void UEnemyAwarenessComponent::OnTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	if (bStopped) return;
	if (!IsValid(Actor)) return;

	// Dead allies arrive as neutral stimuli — body discovery runs before the hostility filter.
	if (Stimulus.Type == UAISense::GetSenseID<UAISense_Sight>() && Stimulus.WasSuccessfullySensed())
	{
		if (AEnemyCharacter* Body = Cast<AEnemyCharacter>(Actor); Body && !IsActorAlive(Body))
		{
			HandleBodySighted(Body);
			return;
		}
	}

	if (!IsHostile(Actor)) return;

	if (Stimulus.Type == UAISense::GetSenseID<UAISense_Sight>())
		HandleSightStimulus(Actor, Stimulus);
	else if (Stimulus.Type == UAISense::GetSenseID<UAISense_Hearing>())
		HandleHearingStimulus(Actor, Stimulus);
}

void UEnemyAwarenessComponent::HandleBodySighted(AEnemyCharacter* Body)
{
	if (CurrentState == EEnemyAwarenessState::Combat) return;
	if (DiscoveredBodies.Contains(Body)) return;
	DiscoveredBodies.Add(Body);

	if (Body->TryMarkBodyReported())
	{
		if (UEnemyDirectorSubsystem* Dir = Director.Get())
			Dir->ReportBodyDiscovered();
	}

	Bark(EBarkType::BodyFound);
	SetInvestigateLocation(Body->GetActorLocation());
	TimeSpentSearching = 0.f;
	SetState(EEnemyAwarenessState::Searching);
}

void UEnemyAwarenessComponent::HandleSightStimulus(AActor* Actor, const FAIStimulus& Stimulus)
{
	if (Stimulus.WasSuccessfullySensed() && !IsActorAlive(Actor)) return;

	// Track bookkeeping runs in every state so visibility survives a Combat exit.
	FSuspicionTrack& Track = SuspicionTracks.FindOrAdd(Actor);
	Track.bSighted = Stimulus.WasSuccessfullySensed();
	if (Track.bSighted)
		Track.LastStimulusLocation = Actor->GetActorLocation();

	if (CurrentState != EEnemyAwarenessState::Combat) return;

	if (Stimulus.WasSuccessfullySensed())
	{
		const bool bIsCurrentTarget = (Actor == CombatTarget.Get());
		const bool bNoCurrentTarget = !CombatTarget.IsValid();

		// Adopt a new target only when we have no current target, or our current target is already
		// out of LOS. This keeps us locked onto a visible target when a second hostile flickers
		// in and out of perception.
		if (bIsCurrentTarget || bNoCurrentTarget || !bHadLOS)
			EnterCombat(Actor, true);
	}
	else
	{
		// Only clear LOS when the actor that dropped out of sight IS our current target.
		if (Actor == CombatTarget.Get())
			bHadLOS = false;
	}
}

void UEnemyAwarenessComponent::HandleHearingStimulus(AActor* Actor, const FAIStimulus& Stimulus)
{
	if (!Stimulus.WasSuccessfullySensed()) return;
	if (CurrentState == EEnemyAwarenessState::Combat) return;
	if (!IsValid(ArchetypeData)) return;

	FSuspicionTrack& Track = SuspicionTracks.FindOrAdd(Actor);
	const float Gain = Stimulus.Strength * ArchetypeData->NoiseSuspicionGain;
	Track.Suspicion = FMath::Min(Track.Suspicion + Gain, NoiseSuspicionCap);
	Track.LastStimulusLocation = Stimulus.StimulusLocation;
}

// --- Damage Notification ---

void UEnemyAwarenessComponent::NotifyDamaged(AController* Instigator)
{
	if (bStopped) return;
	if (!IsValid(Instigator)) return;

	APawn* InstigatorPawn = Instigator->GetPawn();
	if (!IsValid(InstigatorPawn)) return;
	if (!IsHostile(InstigatorPawn)) return;

	EnterCombat(InstigatorPawn, false);
}

// --- Pawn Death ---

void UEnemyAwarenessComponent::HandlePawnDeath()
{
	bStopped = true;

	if (const UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(UpdateTimerHandle);
}

// --- Update Timer ---

void UEnemyAwarenessComponent::UpdateAwareness()
{
	if (bStopped) return;
	if (!IsValid(ArchetypeData)) return;

	if (CurrentState == EEnemyAwarenessState::Combat)
	{
		UpdateCombat();
		return;
	}

	UpdateSuspicion();

	if (CurrentState == EEnemyAwarenessState::Searching)
	{
		TimeSpentSearching += UpdateInterval;
		if (TimeSpentSearching >= ArchetypeData->SearchDuration)
		{
			SetCombatTarget(nullptr);
			SetState(EEnemyAwarenessState::Unaware);
		}
	}
}

void UEnemyAwarenessComponent::UpdateCombat()
{
	bool bTargetGone = !CombatTarget.IsValid();
	if (!bTargetGone)
		bTargetGone = !IsActorAlive(CombatTarget.Get());

	if (bTargetGone)
	{
		// Target died or vanished — no "lost him" bark for a kill.
		TransitionToSearching(false);
	}
	else if (bHadLOS)
	{
		LastKnownLocation = CombatTarget->GetActorLocation();
		WriteBBVectors();
	}
	else
	{
		TimeSinceLOSLost += UpdateInterval;
		if (TimeSinceLOSLost >= ArchetypeData->LostContactGrace)
			TransitionToSearching(true);
	}
}

void UEnemyAwarenessComponent::UpdateSuspicion()
{
	const AAIController* MyController = Cast<AAIController>(GetOwner());
	const APawn* MyPawn = MyController ? MyController->GetPawn() : nullptr;
	if (!IsValid(MyPawn)) return;

	const float AutoCombatRangeSq = FMath::Square(ArchetypeData->AutoCombatRange);
	float MaxSuspicion = 0.f;
	FVector MaxLocation = FVector::ZeroVector;

	for (auto It = SuspicionTracks.CreateIterator(); It; ++It)
	{
		AActor* Actor = It.Key().Get();
		if (!Actor) { It.RemoveCurrent(); continue; }

		FSuspicionTrack& Track = It.Value();
		if (Track.bSighted && IsActorAlive(Actor))
		{
			Track.Suspicion += ComputeSightFillRate(MyPawn, Actor) * UpdateInterval;
			Track.LastStimulusLocation = Actor->GetActorLocation();

			const bool bPointBlank = FVector::DistSquared(MyPawn->GetActorLocation(), Actor->GetActorLocation()) <= AutoCombatRangeSq;
			if (Track.Suspicion >= SuspicionMax || bPointBlank)
			{
				EnterCombat(Actor, true);
				return;
			}
		}
		else
		{
			Track.Suspicion -= ArchetypeData->SuspicionDecayRate * UpdateInterval;
			if (Track.Suspicion <= 0.f) { It.RemoveCurrent(); continue; }
		}

		if (Track.Suspicion > MaxSuspicion)
		{
			MaxSuspicion = Track.Suspicion;
			MaxLocation = Track.LastStimulusLocation;
		}
	}

	ApplySuspicionState(MaxSuspicion, MaxLocation);
}

void UEnemyAwarenessComponent::ApplySuspicionState(float MaxSuspicion, const FVector& StimulusLocation)
{
	if (CurrentState == EEnemyAwarenessState::Searching)
	{
		// A live stimulus keeps the search fresh; the timeout owns the exit.
		if (MaxSuspicion >= ArchetypeData->SearchingThreshold)
		{
			SetInvestigateLocation(StimulusLocation);
			TimeSpentSearching = 0.f;
		}
		return;
	}

	if (MaxSuspicion >= ArchetypeData->SearchingThreshold)
	{
		if (CurrentState != EEnemyAwarenessState::Searching)
			Bark(EBarkType::SearchArea);
		SetInvestigateLocation(StimulusLocation);
		TimeSpentSearching = 0.f;
		SetState(EEnemyAwarenessState::Searching);
	}
	else if (MaxSuspicion >= ArchetypeData->SuspiciousThreshold)
	{
		if (CurrentState != EEnemyAwarenessState::Suspicious)
			Bark(EBarkType::HeardSomething);
		SetInvestigateLocation(StimulusLocation);
		SetState(EEnemyAwarenessState::Suspicious);
	}
	else if (CurrentState == EEnemyAwarenessState::Suspicious)
	{
		SetState(EEnemyAwarenessState::Unaware);
	}
}

float UEnemyAwarenessComponent::ComputeSightFillRate(const APawn* MyPawn, const AActor* Target) const
{
	const float Dist = FVector::Dist(MyPawn->GetActorLocation(), Target->GetActorLocation());
	const float DistFactor = FMath::Clamp(1.f - Dist / FMath::Max(ArchetypeData->SightRadius, 1.f), 0.15f, 1.f);

	// View-angle factor: 1 at centre, AngleEdgeFillFactor at the cone edge
	const FVector ToTarget = (Target->GetActorLocation() - MyPawn->GetActorLocation()).GetSafeNormal();
	const float Dot = FVector::DotProduct(MyPawn->GetActorForwardVector(), ToTarget);
	const float CosHalfFOV = FMath::Cos(FMath::DegreesToRadians(ArchetypeData->PeripheralVisionDeg * 0.5f));
	const float AngleAlpha = FMath::Clamp((Dot - CosHalfFOV) / FMath::Max(1.f - CosHalfFOV, KINDA_SMALL_NUMBER), 0.f, 1.f);
	const float AngleFactor = FMath::Lerp(ArchetypeData->AngleEdgeFillFactor, 1.f, AngleAlpha);

	const float Speed = Target->GetVelocity().Size();
	float SpeedFactor = 1.f;
	if (Speed >= ArchetypeData->SprintSpeedThreshold) SpeedFactor = ArchetypeData->SprintFillFactor;
	else if (Speed <= StillSpeedThreshold) SpeedFactor = ArchetypeData->StillFillFactor;

	float StanceFactor = 1.f;
	const IExtractionPlayerInterface* PlayerInterface = Cast<IExtractionPlayerInterface>(Target);
	const ACharacter* TargetChar = Cast<ACharacter>(Target);
	if (PlayerInterface && PlayerInterface->GetIsProne())
		StanceFactor = ArchetypeData->ProneFillFactor;
	else if (TargetChar && TargetChar->bIsCrouched)
		StanceFactor = ArchetypeData->CrouchFillFactor;

	return ArchetypeData->SuspicionFillRate * DistFactor * AngleFactor * SpeedFactor * StanceFactor;
}

// --- Transitions ---

void UEnemyAwarenessComponent::EnterCombat(AActor* Target, bool bConfirmedVisual)
{
	LastKnownLocation = IsValid(Target) ? Target->GetActorLocation() : LastKnownLocation;
	bHadLOS = bConfirmedVisual;
	TimeSinceLOSLost = 0.f;

	// Zero the meters but keep the tracks — bSighted bookkeeping must survive Combat so a
	// continuously-visible second hostile is still known when Combat ends (no fresh gain event fires).
	for (auto& Pair : SuspicionTracks)
		Pair.Value.Suspicion = 0.f;

	SetCombatTarget(Target);
	if (CurrentState != EEnemyAwarenessState::Combat)
		Bark(EBarkType::Contact);
	SetState(EEnemyAwarenessState::Combat);
}

void UEnemyAwarenessComponent::TransitionToSearching(bool bContactLost)
{
	if (bContactLost)
		Bark(EBarkType::LostTarget);
	SetInvestigateLocation(LastKnownLocation);
	SetCombatTarget(nullptr);
	TimeSpentSearching = 0.f;
	SetState(EEnemyAwarenessState::Searching);
}

// --- Global Alert ---

void UEnemyAwarenessComponent::HandleGlobalAlertChanged(EGlobalAlertLevel OldLevel, EGlobalAlertLevel NewLevel)
{
	if (bStopped) return;
	if (NewLevel != EGlobalAlertLevel::Loud) return;
	if (CurrentState != EEnemyAwarenessState::Unaware) return;

	// Stealth is over — dormant enemies wake up and sweep their post.
	const AAIController* MyController = Cast<AAIController>(GetOwner());
	const APawn* MyPawn = MyController ? MyController->GetPawn() : nullptr;
	if (!IsValid(MyPawn)) return;

	Bark(EBarkType::SearchArea);
	SetInvestigateLocation(MyPawn->GetActorLocation());
	TimeSpentSearching = 0.f;
	SetState(EEnemyAwarenessState::Searching);
}

// --- Helpers ---

void UEnemyAwarenessComponent::SetState(EEnemyAwarenessState NewState)
{
	if (NewState == CurrentState) return;

	const EEnemyAwarenessState OldState = CurrentState;
	CurrentState = NewState;

	UBlackboardComponent* BB = BlackboardComp.Get();
	if (IsValid(BB))
		BB->SetValueAsEnum(AEnemyAIController::BB_AwarenessState, static_cast<uint8>(CurrentState));

	if (UEnemyDirectorSubsystem* Dir = Director.Get())
	{
		if (NewState == EEnemyAwarenessState::Combat) Dir->ReportEnemyCombat();
		else if (NewState == EEnemyAwarenessState::Searching) Dir->ReportEnemySearching();
	}

	OnAwarenessStateChanged.Broadcast(OldState, NewState);
}

void UEnemyAwarenessComponent::SetCombatTarget(AActor* NewTarget)
{
	CombatTarget = NewTarget;

	UBlackboardComponent* BB = BlackboardComp.Get();
	if (!IsValid(BB)) return;

	BB->SetValueAsObject(AEnemyAIController::BB_CombatTarget, NewTarget);

	if (IsValid(NewTarget))
		WriteBBVectors();
}

void UEnemyAwarenessComponent::SetInvestigateLocation(const FVector& Location)
{
	UBlackboardComponent* BB = BlackboardComp.Get();
	if (IsValid(BB))
		BB->SetValueAsVector(AEnemyAIController::BB_InvestigateLocation, Location);
}

void UEnemyAwarenessComponent::WriteBBVectors()
{
	UBlackboardComponent* BB = BlackboardComp.Get();
	if (!IsValid(BB)) return;

	BB->SetValueAsVector(AEnemyAIController::BB_LastKnownLocation, LastKnownLocation);
}

float UEnemyAwarenessComponent::GetHighestSuspicion() const
{
	float Max = 0.f;
	for (const auto& Pair : SuspicionTracks)
		Max = FMath::Max(Max, Pair.Value.Suspicion);
	return Max;
}

void UEnemyAwarenessComponent::Bark(EBarkType Type) const
{
	const AAIController* MyController = Cast<AAIController>(GetOwner());
	const AEnemyCharacter* MyPawn = MyController ? Cast<AEnemyCharacter>(MyController->GetPawn()) : nullptr;
	if (!IsValid(MyPawn) || !IsValid(ArchetypeData) || !IsValid(ArchetypeData->BarkSet)) return;

	const UWorld* World = GetWorld();
	UBarkSubsystem* Barks = IsValid(World) ? World->GetSubsystem<UBarkSubsystem>() : nullptr;
	if (Barks)
		Barks->RequestBark(MyPawn, ArchetypeData->BarkSet, Type, ArchetypeData->DisplayName);
}

bool UEnemyAwarenessComponent::IsHostile(AActor* Actor) const
{
	const AAIController* MyController = Cast<AAIController>(GetOwner());
	if (!MyController) return false;

	const ETeamAttitude::Type Attitude = MyController->GetTeamAttitudeTowards(*Actor);
	return Attitude == ETeamAttitude::Hostile;
}

bool UEnemyAwarenessComponent::IsActorAlive(const AActor* Actor)
{
	const UHealthComponent* HC = Actor ? Actor->FindComponentByClass<UHealthComponent>() : nullptr;
	return !HC || !HC->IsDead();
}
