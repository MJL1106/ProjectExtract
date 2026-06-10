// UEnemyAwarenessComponent — awareness state ladder driven by perception stimuli and damage events.

#include "EnemyAwarenessComponent.h"
#include "EnemyAIController.h"
#include "EnemyArchetypeData.h"
#include "EnemyCharacter.h"
#include "EnemyDirectorSubsystem.h"
#include "Squad/EnemySquadSubsystem.h"
#include "Squad/EnemySquad.h"
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

	SquadSubsystem = World->GetSubsystem<UEnemySquadSubsystem>();

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
	if (!IsValid(ArchetypeData)) return;

	FSuspicionTrack& Track = SuspicionTracks.FindOrAdd(Actor);
	Track.LastStimulusLocation = Stimulus.StimulusLocation;

	// During Combat, only update track bookkeeping (location) — suspicion gain is irrelevant
	if (CurrentState == EEnemyAwarenessState::Combat) return;

	const float Gain = Stimulus.Strength * ArchetypeData->NoiseSuspicionGain;
	Track.Suspicion = FMath::Min(Track.Suspicion + Gain, NoiseSuspicionCap);
}

// --- Damage Notification ---

void UEnemyAwarenessComponent::NotifyDamaged(AController* Instigator)
{
	if (bStopped) return;
	if (!IsValid(Instigator)) return;

	APawn* InstigatorPawn = Instigator->GetPawn();
	if (!IsValid(InstigatorPawn)) return;
	if (!IsHostile(InstigatorPawn)) return;

	RecentDamageInstigatorPawn = InstigatorPawn;
	RecentDamageWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : -1e9f;

	// Ensure the instigator has a suspicion track so threat scoring can find it even when
	// perception never delivered a stimulus (suppressed weapon, out of hearing range — QA #6).
	FSuspicionTrack& Track = SuspicionTracks.FindOrAdd(InstigatorPawn);
	Track.LastStimulusLocation = InstigatorPawn->GetActorLocation();

	EnterCombat(InstigatorPawn, false);

	// Fix #4: leaderless focus-fire — if squad has no focus target, claim it on damage
	if (UEnemySquadSubsystem* SquadSS = SquadSubsystem.Get())
	{
		const AAIController* MyController = Cast<AAIController>(GetOwner());
		AEnemyCharacter* MyChar = MyController ? Cast<AEnemyCharacter>(MyController->GetPawn()) : nullptr;
		if (IsValid(MyChar))
		{
			UEnemySquad* Squad = SquadSS->GetSquadFor(MyChar);
			if (IsValid(Squad))
				Squad->SetFocusTarget(InstigatorPawn, MyChar);
		}
	}
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
		// Target died — try to immediately acquire a sighted candidate before dropping to Searching
		AActor* NextTarget = ScoreAndSelectTarget();
		if (IsValid(NextTarget))
		{
			const FSuspicionTrack* Track = SuspicionTracks.Find(NextTarget);
			EnterCombat(NextTarget, Track && Track->bSighted);
		}
		else
		{
			TransitionToSearching(false);
		}
		return;
	}

	// Threat-scored target re-evaluation each tick
	AActor* BestTarget = ScoreAndSelectTarget();
	if (IsValid(BestTarget) && BestTarget != CombatTarget.Get())
	{
		const FSuspicionTrack* Track = SuspicionTracks.Find(BestTarget);
		EnterCombat(BestTarget, Track && Track->bSighted);
	}

	if (bHadLOS)
	{
		LastKnownLocation = CombatTarget->GetActorLocation();
		WriteBBVectors();
		BroadcastSightingToSquad();
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

	for (auto& Pair : SuspicionTracks)
		Pair.Value.Suspicion = 0.f;

	SetCombatTarget(Target);
	if (CurrentState != EEnemyAwarenessState::Combat)
		Bark(EBarkType::Contact);
	SetState(EEnemyAwarenessState::Combat);

	if (bConfirmedVisual)
		BroadcastSightingToSquad();
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
	if (!Actor) return false;

	// Fast path for enemy characters (avoids FindComponentByClass at ~133 calls/s with 20 AI)
	if (const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Actor))
	{
		const UHealthComponent* HC = Enemy->GetHealthComponent();
		return !HC || !HC->IsDead();
	}

	const UHealthComponent* HC = Actor->FindComponentByClass<UHealthComponent>();
	return !HC || !HC->IsDead();
}

// --- Threat-Scored Target Selection (design §10) ---

AActor* UEnemyAwarenessComponent::ScoreAndSelectTarget() const
{
	if (!IsValid(ArchetypeData)) return nullptr;

	const AAIController* MyController = Cast<AAIController>(GetOwner());
	const APawn* MyPawn = MyController ? MyController->GetPawn() : nullptr;
	if (!IsValid(MyPawn)) return nullptr;

	// Officer focus-fire override: if squad has a focus target we can perceive, it wins outright
	if (UEnemySquadSubsystem* SquadSS = SquadSubsystem.Get())
	{
		const AEnemyCharacter* MyChar = Cast<AEnemyCharacter>(MyPawn);
		if (IsValid(MyChar))
		{
			UEnemySquad* Squad = SquadSS->GetSquadFor(MyChar);
			if (IsValid(Squad))
			{
				AActor* FocusTarget = Squad->GetFocusTarget();
				if (IsValid(FocusTarget) && IsActorAlive(FocusTarget))
				{
					const FSuspicionTrack* FocusTrack = SuspicionTracks.Find(FocusTarget);
					if (FocusTrack && FocusTrack->bSighted) return FocusTarget;
				}
			}
		}
	}

	const float SightRadiusInv = 1.f / FMath::Max(ArchetypeData->SightRadius, 1.f);
	const float WorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

	AActor* BestTarget = nullptr;
	float BestScore = -1.f;
	float IncumbentScore = -1.f;

	for (const auto& Pair : SuspicionTracks)
	{
		AActor* Candidate = Pair.Key.Get();
		if (!IsValid(Candidate)) continue;
		if (!IsActorAlive(Candidate)) continue;
		if (!IsHostile(Candidate)) continue;

		const FSuspicionTrack& Track = Pair.Value;

		const float Dist = FVector::Dist(MyPawn->GetActorLocation(), Candidate->GetActorLocation());
		const float ProximityTerm = ArchetypeData->ThreatWeightProximity * (1.f - FMath::Clamp(Dist * SightRadiusInv, 0.f, 1.f));
		const float LOSTerm = ArchetypeData->ThreatWeightLOS * (Track.bSighted ? 1.f : 0.f);

		float DamageTerm = 0.f;
		if (RecentDamageInstigatorPawn.Get() == Candidate && (WorldTime - RecentDamageWorldTime) < RecentDamageWindow)
			DamageTerm = ArchetypeData->ThreatWeightRecentDamage;

		const float Score = ProximityTerm + LOSTerm + DamageTerm;

		if (Candidate == CombatTarget.Get())
			IncumbentScore = Score;

		if (Score > BestScore)
		{
			BestScore = Score;
			BestTarget = Candidate;
		}
	}

	if (!IsValid(BestTarget)) return nullptr;

	// Hysteresis: challenger must beat the incumbent by the hysteresis factor to switch
	if (BestTarget != CombatTarget.Get() && CombatTarget.IsValid() && IncumbentScore >= 0.f)
	{
		if (BestScore < IncumbentScore * ArchetypeData->TargetSwitchHysteresis)
			return CombatTarget.Get();
	}

	return BestTarget;
}

// --- Squad Sighting Egress ---

void UEnemyAwarenessComponent::BroadcastSightingToSquad()
{
	if (bInSquadSightingRelay) return;

	UEnemySquadSubsystem* SquadSS = SquadSubsystem.Get();
	if (!IsValid(SquadSS)) return;

	const AAIController* MyController = Cast<AAIController>(GetOwner());
	const AEnemyCharacter* MyChar = MyController ? Cast<AEnemyCharacter>(MyController->GetPawn()) : nullptr;
	if (!IsValid(MyChar)) return;

	UEnemySquad* Squad = SquadSS->GetSquadFor(MyChar);
	if (!IsValid(Squad)) return;

	AActor* Target = CombatTarget.Get();
	if (!IsValid(Target)) return;

	Squad->ReportSighting(Target, LastKnownLocation);
}

// --- Squad Sighting Ingress ---

void UEnemyAwarenessComponent::ReportSquadSighting(AActor* Target, const FVector& LastKnown)
{
	if (bStopped) return;
	if (!IsValid(Target)) return;

	// Guard: set flag to prevent re-broadcast from any combat-entry path this call triggers
	TGuardValue<bool> RelayGuard(bInSquadSightingRelay, true);

	if (CurrentState == EEnemyAwarenessState::Combat)
	{
		if (CombatTarget.Get() == Target)
		{
			LastKnownLocation = LastKnown;
			WriteBBVectors();
		}
		return;
	}

	// Below Combat: transition to Searching at the reported location (never force Combat)
	SetInvestigateLocation(LastKnown);
	TimeSpentSearching = 0.f;
	if (CurrentState < EEnemyAwarenessState::Searching)
		Bark(EBarkType::SearchArea);
	SetState(EEnemyAwarenessState::Searching);
}
