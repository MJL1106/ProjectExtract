// UEnemyAwarenessComponent — awareness state ladder driven by perception stimuli and damage events.

#include "EnemyAwarenessComponent.h"
#include "AI/AITargetingStatics.h"
#include "EnemyAIController.h"
#include "EnemyArchetypeData.h"
#include "EnemyCharacter.h"
#include "EnemyDirectorSubsystem.h"
#include "Squad/EnemySquadSubsystem.h"
#include "Squad/EnemySquad.h"
#include "BarkSubsystem.h"
#include "BarkSetData.h"
#include "CompanionCharacter.h"
#include "HealthComponent.h"
#include "SuppressionComponent.h"
#include "EnemyMoraleComponent.h"
#include "Character/ExtractionPlayerInterface.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISense_Hearing.h"
#include "GenericTeamAgentInterface.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "Components/CapsuleComponent.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "EnemyDebug.h"

#if ENABLE_DRAW_DEBUG
#include "DrawDebugHelpers.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#endif

UEnemyAwarenessComponent::UEnemyAwarenessComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UEnemyAwarenessComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearInvestigateBody();

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
	{
		Dir->OnGlobalAlertChanged.AddUniqueDynamic(this, &UEnemyAwarenessComponent::HandleGlobalAlertChanged);

		if (!IsOwnerIsolatedEncounter() && Dir->GetAlertLevel() == EGlobalAlertLevel::Loud && CurrentState == EEnemyAwarenessState::Unaware)
		{
			const AAIController* MyController = Cast<AAIController>(GetOwner());
			const APawn* MyPawn = MyController ? MyController->GetPawn() : nullptr;
			if (IsValid(MyPawn))
			{
				Bark(EBarkType::SearchArea);
				SetInvestigateLocation(MyPawn->GetActorLocation());
				TimeSpentSearching = 0.f;
				SetState(EEnemyAwarenessState::Searching);
			}
		}
	}

	SquadSubsystem = World->GetSubsystem<UEnemySquadSubsystem>();

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

	if (GetDetectionLogLevel() > 0 && Stimulus.Type == UAISense::GetSenseID<UAISense_Sight>())
		if (const AEnemyCharacter* EC = Cast<AEnemyCharacter>(Actor))
			UE_LOG(LogTemp, Warning, TEXT("[BODYDBG] %s sight-stim from enemy %s sensed=%d alive=%d state=%s"),
				*GetNameSafe(GetOwner()), *GetNameSafe(Actor), Stimulus.WasSuccessfullySensed() ? 1 : 0,
				IsActorAlive(EC) ? 1 : 0, *UEnum::GetValueAsString(CurrentState));

	// Dead allies arrive as neutral stimuli — body discovery runs before the hostility filter.
	if (Stimulus.Type == UAISense::GetSenseID<UAISense_Sight>() && Stimulus.WasSuccessfullySensed())
	{
		if (AEnemyCharacter* Body = Cast<AEnemyCharacter>(Actor); Body && !IsActorAlive(Body))
		{
			HandleBodySighted(Body);
			return;
		}
	}

	static const FName WeaponFireTag(TEXT("WeaponFire"));
	const bool bCompanionWeaponFireNoise = IsCompanionActor(Actor)
		&& Stimulus.Type == UAISense::GetSenseID<UAISense_Hearing>()
		&& Stimulus.Tag == WeaponFireTag;
	if (ShouldIgnoreCompanionStimulus(Actor) && !bCompanionWeaponFireNoise) return;
	if (!IsHostile(Actor)) return;

	if (Stimulus.Type == UAISense::GetSenseID<UAISense_Sight>())
		HandleSightStimulus(Actor, Stimulus);
	else if (Stimulus.Type == UAISense::GetSenseID<UAISense_Hearing>())
		HandleHearingStimulus(Actor, Stimulus);
}

void UEnemyAwarenessComponent::HandleBodySighted(AEnemyCharacter* Body)
{
	if (GetDetectionLogLevel() > 0)
		UE_LOG(LogTemp, Warning, TEXT("[BODYDBG] %s HandleBodySighted body=%s state=%s alreadyDiscovered=%d"),
			*GetNameSafe(GetOwner()), *GetNameSafe(Body), *UEnum::GetValueAsString(CurrentState),
			DiscoveredBodies.Contains(Body) ? 1 : 0);

	if (CurrentState == EEnemyAwarenessState::Combat) return;

	const bool bAlreadyDiscovered = DiscoveredBodies.Contains(Body);

	// One-shot report and bark on first discovery only.
	if (!bAlreadyDiscovered)
	{
		DiscoveredBodies.Add(Body);
		if (Body->TryMarkBodyReported())
			if (UEnemyDirectorSubsystem* Dir = Director.Get())
				Dir->ReportBodyDiscovered();
		Bark(EBarkType::BodyFound);
	}

	// Route to this body only when free — never abandon a body already being investigated.
	if (CurrentInvestigateBody.IsValid()) return;

	ClearInvestigateBody();
	Body->IncrementInvestigators();
	CurrentInvestigateBody = Body;
	SetInvestigateLocation(Body->GetCorpseLocation());
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

	if (GetDetectionLogLevel() > 0)
		UE_LOG(LogTemp, Warning, TEXT("[DETECTDBG] SightStim tgt=%s success=%d state=%s stimLoc=(%.0f,%.0f,%.0f)"),
			*GetNameSafe(Actor), Stimulus.WasSuccessfullySensed() ? 1 : 0, *UEnum::GetValueAsString(CurrentState),
			Stimulus.StimulusLocation.X, Stimulus.StimulusLocation.Y, Stimulus.StimulusLocation.Z);

	// Searching fast-track: re-acquire combat on clean sight (own perception only, not squad relay)
	if (CurrentState == EEnemyAwarenessState::Searching && Stimulus.WasSuccessfullySensed() && IsActorAlive(Actor))
	{
		EnterCombat(Actor, true);
		return;
	}

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
	if (IsOwnerIsolatedEncounter()) return;
	if (!Stimulus.WasSuccessfullySensed()) return;
	if (!IsValid(ArchetypeData)) return;

	if (GetDetectionLogLevel() > 0)
		UE_LOG(LogTemp, Warning, TEXT("[DETECTDBG] HearStim actor=%s strength=%.2f state=%s"),
			*Actor->GetName(), Stimulus.Strength, *UEnum::GetValueAsString(CurrentState));

	FSuspicionTrack& Track = SuspicionTracks.FindOrAdd(Actor);
	Track.LastStimulusLocation = Stimulus.StimulusLocation;

	// During Combat, only update track bookkeeping (location) — suspicion gain is irrelevant
	if (CurrentState == EEnemyAwarenessState::Combat) return;

	const float Gain = Stimulus.Strength * ArchetypeData->NoiseSuspicionGain;
	Track.Suspicion = FMath::Min(Track.Suspicion + Gain, NoiseSuspicionCap);

	static const FName WeaponFireTag(TEXT("WeaponFire"));
	if (Stimulus.Tag == WeaponFireTag && Track.Suspicion >= ArchetypeData->SuspiciousThreshold)
	{
		Track.Suspicion = FMath::Max(Track.Suspicion, ArchetypeData->SearchingThreshold);
		SetInvestigateLocation(Stimulus.StimulusLocation);
		TimeSpentSearching = 0.f;
		if (CurrentState < EEnemyAwarenessState::Searching)
			Bark(EBarkType::SearchArea);
		SetState(EEnemyAwarenessState::Searching);
	}
}

// --- Damage Notification ---

void UEnemyAwarenessComponent::NotifyDamaged(AController* Instigator)
{
	if (bStopped) return;
	if (!IsValid(Instigator)) return;

	APawn* InstigatorPawn = Instigator->GetPawn();
	if (!IsValid(InstigatorPawn)) return;
	if (ShouldIgnoreCompanionStimulus(InstigatorPawn)) return;
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

// --- Shot-At Notification ---

void UEnemyAwarenessComponent::NotifyShotAt(AActor* InstigatorPawn, const FVector& ShotOrigin)
{
	if (bStopped) return;
	if (!IsValid(InstigatorPawn)) return;
	if (ShouldIgnoreCompanionStimulus(InstigatorPawn)) return;
	if (!IsHostile(InstigatorPawn)) return;
	if (!IsValid(ArchetypeData) || !ArchetypeData->bReactsToBeingShotAt) return;

	if (GetDetectionLogLevel() > 0)
		UE_LOG(LogTemp, Warning, TEXT("[DETECTDBG] ShotAt instigator=%s state=%s"),
			*InstigatorPawn->GetName(), *UEnum::GetValueAsString(CurrentState));

	// Already in Combat — only refresh the track location; let the existing loop run.
	if (CurrentState == EEnemyAwarenessState::Combat)
	{
		FSuspicionTrack& Track = SuspicionTracks.FindOrAdd(InstigatorPawn);
		Track.LastStimulusLocation = ShotOrigin;
		return;
	}

	// Per-instigator rate-limit folded into FSuspicionTrack (fix #4 — no separate map).
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	FSuspicionTrack& Track = SuspicionTracks.FindOrAdd(InstigatorPawn);
	if ((Now - Track.LastShotAtTime) < ShotAtRateLimit) return;
	Track.LastShotAtTime = Now;

	// Clamp to at least SearchingThreshold so a mistuned DA can't decay out on the next tick (fix #7).
	Track.LastStimulusLocation = ShotOrigin;
	const float Floor = FMath::Max(ArchetypeData->ShotAtSuspicionFloor, ArchetypeData->SearchingThreshold);
	Track.Suspicion = FMath::Max(Track.Suspicion, Floor);

	// LOS trace: eye → instigator. If clear, snap to Combat (they can see their attacker).
	const AAIController* MyController = Cast<AAIController>(GetOwner());
	const APawn* MyPawn = MyController ? MyController->GetPawn() : nullptr;
	if (!IsValid(MyPawn)) return;

	const FVector EyeLocation = MyPawn->GetPawnViewLocation();
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(EnemyShotAtLoS), false);
	QueryParams.AddIgnoredActor(MyPawn);
	QueryParams.AddIgnoredActor(InstigatorPawn);

	const bool bLOSClear = !GetWorld()->LineTraceTestByChannel(EyeLocation, InstigatorPawn->GetActorLocation(), ECC_Visibility, QueryParams);
	if (GetDetectionLogLevel() > 0)
		UE_LOG(LogTemp, Warning, TEXT("[DETECTDBG] ShotAt %s LOS-to-center=%s"),
			*InstigatorPawn->GetName(), bLOSClear ? TEXT("CLEAR->COMBAT") : TEXT("BLOCKED->SEARCHING"));
	if (bLOSClear)
	{
		EnterCombat(InstigatorPawn, /*bConfirmedVisual=*/true);
		return;
	}

	// LOS blocked — transition to Searching toward the shot origin.
	SetInvestigateLocation(ShotOrigin);
	TimeSpentSearching = 0.f;
	if (CurrentState < EEnemyAwarenessState::Searching)
		Bark(EBarkType::SearchArea);
	SetState(EEnemyAwarenessState::Searching);
}

// --- Pawn Death ---

void UEnemyAwarenessComponent::HandlePawnDeath()
{
	bStopped = true;
	ClearInvestigateBody();

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
	}
	else
	{
		UpdateSuspicion();

		if (CurrentState == EEnemyAwarenessState::Searching)
		{
			if (CurrentInvestigateBody.IsValid())
			{
				// Track the ragdoll — update the BT move goal so it follows a settling/sliding body.
				SetInvestigateLocation(CurrentInvestigateBody->GetCorpseLocation());

				const AAIController* MyController = Cast<AAIController>(GetOwner());
				const APawn* MyPawn = MyController ? MyController->GetPawn() : nullptr;
				if (IsValid(MyPawn))
				{
					const float DistToBody = FVector::Dist(MyPawn->GetActorLocation(), CurrentInvestigateBody->GetCorpseLocation());
					if (DistToBody <= CorpseReachRadius)
					{
						CurrentInvestigateBody->BeginCorpseRemoval();
						ClearInvestigateBody();
					}
				}
			}

			// Hold the search timeout while actively investigating a body — the enemy must reach it
			// or lose it (hard-cap destroy nulls the weak ref) before reverting to Unaware.
			if (!CurrentInvestigateBody.IsValid())
			{
				TimeSpentSearching += UpdateInterval;
				if (TimeSpentSearching >= ArchetypeData->SearchDuration)
				{
					ClearInvestigateBody();
					SetCombatTarget(nullptr);
					SetState(EEnemyAwarenessState::Unaware);
				}
			}
		}
	}

#if ENABLE_DRAW_DEBUG
	{
		const int32 DebugLevel = GetEnemyDrawDebugLevel();
		if (DebugLevel > 0)
		{
			const AAIController* MyController = Cast<AAIController>(GetOwner());
			const AEnemyCharacter* MyChar = MyController ? Cast<AEnemyCharacter>(MyController->GetPawn()) : nullptr;
			UWorld* World = GetWorld();
			if (IsValid(MyChar) && IsValid(World))
			{
				const float HalfHeight = MyChar->GetCapsuleComponent()
					? MyChar->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
				constexpr float HeadOffset = 30.f;
				const FVector HeadLocation = MyChar->GetActorLocation() + FVector(0.f, 0.f, HalfHeight + HeadOffset);

				// --- Level 1+: head tag ---
				const FString ArchetypeShort = UEnum::GetDisplayValueAsText(ArchetypeData->Archetype).ToString().ToUpper();
				const FString StateShort = UEnum::GetDisplayValueAsText(CurrentState).ToString().ToUpper();
				FString Tag = FString::Printf(TEXT("%s | %s | %.0f"), *ArchetypeShort, *StateShort, GetHighestSuspicion());

				if (DebugLevel >= 2)
				{
					// Append active BT task name
					const UBehaviorTreeComponent* BTComp = Cast<UBehaviorTreeComponent>(MyController->GetBrainComponent());
					const FString TaskName = IsValid(BTComp) ? BTComp->DescribeActiveTasks() : TEXT("No BT");
					Tag += FString::Printf(TEXT("\n%s"), *TaskName);

					// Append combat target name
					AActor* Target = CombatTarget.Get();
					if (IsValid(Target))
						Tag += FString::Printf(TEXT("\nTgt: %s"), *Target->GetName());

					// Morale state + value
					if (const UEnemyMoraleComponent* Morale = MyChar->GetMoraleComponent())
					{
						const FString MoraleStateStr = UEnum::GetDisplayValueAsText(Morale->GetMoraleState()).ToString();
						Tag += FString::Printf(TEXT("\nMorale: %s %.0f%%"), *MoraleStateStr, Morale->GetMorale01() * 100.f);
					}

					// Suppression value
					if (const USuppressionComponent* Supp = MyChar->GetSuppressionComponent())
					{
						Tag += FString::Printf(TEXT("\nSupp: %.0f%%%s"), Supp->GetSuppression01() * 100.f,
							Supp->IsSuppressed() ? TEXT(" [SUPPRESSED]") : TEXT(""));
					}

					// Threshold context line
					if (IsValid(ArchetypeData))
					{
						Tag += FString::Printf(TEXT("\nSusp %.0f (susp>=%.0f search>=%.0f)"),
							GetHighestSuspicion(), ArchetypeData->SuspiciousThreshold, ArchetypeData->SearchingThreshold);
					}
				}

				DrawDebugString(World, HeadLocation, Tag, nullptr, FColor::Cyan, UpdateInterval, true);
			}
		}
	}
#endif

	// --- Sight-gate diagnostic (enemy.SightDiag) ---
	if (GetSightDiagLevel() > 0)
	{
		SightDiagAccum += UpdateInterval;
		if (SightDiagAccum >= 0.5f)
		{
			SightDiagAccum = 0.f;

			if (CurrentState != EEnemyAwarenessState::Combat)
			{
				const AAIController* DiagController = Cast<AAIController>(GetOwner());
				const APawn* DiagPawn = DiagController ? DiagController->GetPawn() : nullptr;
				if (IsValid(DiagPawn) && IsValid(ArchetypeData))
				{
					FString DiagName;
#if WITH_EDITOR
					DiagName = DiagPawn->GetActorLabel();
#else
					DiagName = GetNameSafe(DiagPawn);
#endif
					const FString DiagFilter = GetSightDiagFilter();
					if (DiagFilter.IsEmpty() || DiagName.Contains(DiagFilter, ESearchCase::IgnoreCase))
					{
						APawn* PlayerPawn = nullptr;
						if (UWorld* W = GetWorld())
						{
							if (APlayerController* PC = W->GetFirstPlayerController())
								PlayerPawn = PC->GetPawn();
						}

						if (IsValid(PlayerPawn))
						{
							const FVector MyLoc = DiagPawn->GetActorLocation();
							const FVector PlayerLoc = PlayerPawn->GetActorLocation();
							const float Dist = FVector::Dist(MyLoc, PlayerLoc);

							if (Dist <= ArchetypeData->LoseSightRadius)
							{
								const FVector Eye = DiagPawn->GetPawnViewLocation();

								// Cone check
								const FVector ToTarget = (PlayerLoc - MyLoc).GetSafeNormal();
								const float Dot = FVector::DotProduct(DiagPawn->GetActorForwardVector(), ToTarget);
								const float CosHalfFOV = FMath::Cos(FMath::DegreesToRadians(ArchetypeData->PeripheralVisionDeg * 0.5f));
								const bool bInCone = Dot >= CosHalfFOV;

								// Body LOS (head-excluded — the detection gate)
								FVector BodyPt;
								const bool bBodyVisible = AITargeting::GetVisibleBodyPoint(PlayerPawn, Eye, DiagPawn, BodyPt);

								// Head clear (contrast — proves head-exclusion is the cause when body blocked)
								const FVector HeadLoc = AITargeting::GetSightLocation(PlayerPawn);
								FCollisionQueryParams HeadTraceParams(SCENE_QUERY_STAT(SightDiagHead), false);
								HeadTraceParams.AddIgnoredActor(DiagPawn);
								HeadTraceParams.AddIgnoredActor(PlayerPawn);
								const bool bHeadClear = !GetWorld()->LineTraceTestByChannel(Eye, HeadLoc, ECC_Visibility, HeadTraceParams);

								// Engine sighted state from suspicion tracks
								const FSuspicionTrack* Tr = SuspicionTracks.Find(PlayerPawn);
								const bool bEngineSighted = Tr && Tr->bSighted;
								const float Susp = Tr ? Tr->Suspicion : 0.f;

								UE_LOG(LogTemp, Warning,
									TEXT("[SIGHTDIAG] %s dist=%.0f inRange=%d inCone=%d(dot=%.2f cos=%.2f) bodyLOS=%d headClear=%d engineSighted=%d susp=%.0f state=%s"),
									*DiagName, Dist,
									(int32)(Dist <= ArchetypeData->SightRadius),
									(int32)bInCone, Dot, CosHalfFOV,
									(int32)bBodyVisible, (int32)bHeadClear, (int32)bEngineSighted,
									Susp, *UEnum::GetValueAsString(CurrentState));
							}
						}
					}
				}
			}
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

	if (bHadLOS && CombatTarget.IsValid())
	{
		LastKnownLocation = CombatTarget->GetActorLocation();
		WriteBBVectors();
		BroadcastSightingToSquad();
	}
	else
	{
		// Contact-hold evaluation: multiple signals can keep Combat alive when perception drops LOS.
		bool bHoldContact = false;
		bool bHoldViaFOVLOS = false;

		const AAIController* MyController = Cast<AAIController>(GetOwner());
		const APawn* MyPawn = MyController ? MyController->GetPawn() : nullptr;

		if (IsValid(MyPawn) && CombatTarget.IsValid())
		{
			// 1) FOV-gated geometric LOS: body-point clear AND target inside view cone
			// Snipers include the head for a standing target (sniper+standing exception).
			const FVector EyeLocation = MyPawn->GetPawnViewLocation();
			const FVector TargetLoc = CombatTarget->GetActorLocation();

			FVector VisiblePoint;
			const bool bAllowHead = AITargeting::ShouldIncludeHeadForObserver(MyPawn, CombatTarget.Get());
			const bool bTraceClear = AITargeting::GetVisibleBodyPoint(CombatTarget.Get(), EyeLocation, MyPawn, VisiblePoint, bAllowHead);
			if (bTraceClear)
			{
				const FVector ToTarget = (TargetLoc - MyPawn->GetActorLocation()).GetSafeNormal();
				const float Dot = FVector::DotProduct(MyPawn->GetActorForwardVector(), ToTarget);
				const float CosHalfFOV = FMath::Cos(FMath::DegreesToRadians(ArchetypeData->PeripheralVisionDeg * 0.5f));
				if (Dot >= CosHalfFOV)
				{
					bHoldViaFOVLOS = true;
					bHoldContact = true;
				}
			}

			// 2) Recently damaged by any hostile
			if (!bHoldContact)
			{
				const float WorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
				if ((WorldTime - RecentDamageWorldTime) < RecentDamageWindow)
					bHoldContact = true;
			}

			// 3) Currently suppressed
			if (!bHoldContact)
			{
				const AEnemyCharacter* MyEnemy = Cast<AEnemyCharacter>(MyPawn);
				if (IsValid(MyEnemy))
				{
					USuppressionComponent* SupprComp = MyEnemy->GetSuppressionComponent();
					if (IsValid(SupprComp) && SupprComp->IsSuppressed())
						bHoldContact = true;
				}
			}
		}

		if (bHoldContact)
		{
			TimeSinceLOSLost = 0.f;
			// Only refresh last-known when we actually see the target (FOV LOS)
			if (bHoldViaFOVLOS)
			{
				LastKnownLocation = CombatTarget->GetActorLocation();
				WriteBBVectors();
				BroadcastSightingToSquad();
			}
		}
		else
		{
			TimeSinceLOSLost += UpdateInterval;
			// Log contact-hold evaluation while grace is counting (not while at 0 — avoids per-tick spam)
			if (TimeSinceLOSLost > 0.f)
			{
				const float CosHalfFOV = IsValid(ArchetypeData) ? FMath::Cos(FMath::DegreesToRadians(ArchetypeData->PeripheralVisionDeg * 0.5f)) : 0.f;
				const FVector ToTarget = IsValid(MyPawn) && CombatTarget.IsValid()
					? (CombatTarget->GetActorLocation() - MyPawn->GetActorLocation()).GetSafeNormal()
					: FVector::ZeroVector;
				const float Dot = IsValid(MyPawn) ? FVector::DotProduct(MyPawn->GetActorForwardVector(), ToTarget) : 0.f;
				// bTraceClear already consumed above; use bHoldViaFOVLOS as proxy for whether trace cleared + FOV passed
				UE_LOG(LogEnemyAI, Verbose, TEXT("[AWARENESS] %s contact-hold fail: bFOVLOS=%d Dot=%.2f CosHalf=%.2f grace=%.1f/%.1f"),
					IsValid(MyPawn) ? *MyPawn->GetName() : TEXT("?"),
					(int32)bHoldViaFOVLOS, Dot, CosHalfFOV,
					TimeSinceLOSLost, IsValid(ArchetypeData) ? ArchetypeData->LostContactGrace : 0.f);
			}
			if (TimeSinceLOSLost >= ArchetypeData->LostContactGrace)
				TransitionToSearching(true);
		}
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
		if (ShouldIgnoreCompanionStimulus(Actor)) { It.RemoveCurrent(); continue; }

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
	if (!IsValid(ArchetypeData)) return 0.f;

	const float Dist = FVector::Dist(MyPawn->GetActorLocation(), Target->GetActorLocation());

	// Near-full band: 1.0 within FullFillRange, linear ramp to DistFloor between FullFillRange and LoseSightRadius
	const float MaxRange = ArchetypeData->LoseSightRadius;
	const float FullRange = FMath::Min(ArchetypeData->FullFillRange, MaxRange);
	constexpr float DistFloor = 0.15f;
	float DistFactor;
	if (Dist <= FullRange)
		DistFactor = 1.f;
	else
		DistFactor = FMath::Clamp(FMath::Lerp(1.f, DistFloor, (Dist - FullRange) / FMath::Max(MaxRange - FullRange, 1.f)), DistFloor, 1.f);

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
	if (ShouldIgnoreCompanionStimulus(Target)) return;

	if (CurrentState != EEnemyAwarenessState::Combat && GetDetectionLogLevel() > 0)
	{
		const AAIController* DbgC = Cast<AAIController>(GetOwner());
		const APawn* DbgP = DbgC ? DbgC->GetPawn() : nullptr;
		const float DbgDist = (IsValid(DbgP) && IsValid(Target)) ? FVector::Dist(DbgP->GetActorLocation(), Target->GetActorLocation()) : -1.f;
		UE_LOG(LogTemp, Warning, TEXT("[DETECTDBG] DETECTED tgt=%s confirmedVisual=%d dist=%.0f fromState=%s"),
			IsValid(Target) ? *Target->GetName() : TEXT("null"), bConfirmedVisual ? 1 : 0, DbgDist, *UEnum::GetValueAsString(CurrentState));
	}

	ClearInvestigateBody();

	LastKnownLocation = IsValid(Target) ? Target->GetActorLocation() : LastKnownLocation;
	bHadLOS = bConfirmedVisual;
	TimeSinceLOSLost = 0.f;

	for (auto& Pair : SuspicionTracks)
		Pair.Value.Suspicion = 0.f;

	SetCombatTarget(Target);
	if (CurrentState != EEnemyAwarenessState::Combat)
	{
		const float WorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
		const bool bSuppressBark = IsValid(ArchetypeData)
			&& (WorldTime - LastCombatExitWorldTime) < ArchetypeData->RecontactBarkCooldown;
		if (!bSuppressBark)
			Bark(EBarkType::Contact);
	}
	SetState(EEnemyAwarenessState::Combat);

	if (bConfirmedVisual)
		BroadcastSightingToSquad();
}

void UEnemyAwarenessComponent::TransitionToSearching(bool bContactLost)
{
	const AAIController* MyController = Cast<AAIController>(GetOwner());
	const APawn* MyPawn = MyController ? MyController->GetPawn() : nullptr;
	UE_LOG(LogEnemyAI, Log, TEXT("[AWARENESS] %s TransitionToSearching bContactLost=%d InvestigateLocation=(%.0f,%.0f,%.0f)"),
		IsValid(MyPawn) ? *MyPawn->GetName() : TEXT("?"),
		(int32)bContactLost,
		LastKnownLocation.X, LastKnownLocation.Y, LastKnownLocation.Z);

	if (bContactLost)
		Bark(EBarkType::LostTarget);

	if (const UWorld* World = GetWorld())
		LastCombatExitWorldTime = World->GetTimeSeconds();

	SetInvestigateLocation(LastKnownLocation);
	SetCombatTarget(nullptr);
	TimeSpentSearching = 0.f;
	SetState(EEnemyAwarenessState::Searching);
}

// --- Global Alert ---

void UEnemyAwarenessComponent::HandleGlobalAlertChanged(EGlobalAlertLevel OldLevel, EGlobalAlertLevel NewLevel)
{
	if (bStopped) return;
	if (IsOwnerIsolatedEncounter()) return;
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

	if (const AAIController* C = Cast<AAIController>(GetOwner()))
		UE_LOG(LogEnemyAI, Verbose, TEXT("[AWARE] %s: %s -> %s"),
			C->GetPawn() ? *C->GetPawn()->GetName() : TEXT("<no pawn>"),
			*UEnum::GetValueAsString(OldState), *UEnum::GetValueAsString(NewState));

	UBlackboardComponent* BB = BlackboardComp.Get();
	if (IsValid(BB))
		BB->SetValueAsEnum(AEnemyAIController::BB_AwarenessState, static_cast<uint8>(CurrentState));

	if (!IsOwnerIsolatedEncounter())
	{
		if (UEnemyDirectorSubsystem* Dir = Director.Get())
		{
			if (NewState == EEnemyAwarenessState::Combat) Dir->ReportEnemyCombat();
			else if (NewState == EEnemyAwarenessState::Searching) Dir->ReportEnemySearching();
		}
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

float UEnemyAwarenessComponent::GetAwarenessMeter01() const
{
	if (CurrentState == EEnemyAwarenessState::Combat) return 1.f;
	return FMath::Clamp(GetHighestSuspicion() / SuspicionMax, 0.f, 1.f);
}

bool UEnemyAwarenessComponent::IsAnyHostileSighted() const
{
	for (const auto& Pair : SuspicionTracks)
	{
		if (!Pair.Value.bSighted) continue;
		AActor* Actor = Pair.Key.Get();
		if (IsValid(Actor) && IsActorAlive(Actor) && IsHostile(Actor) && !ShouldIgnoreCompanionStimulus(Actor))
			return true;
	}
	return false;
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

void UEnemyAwarenessComponent::ClearInvestigateBody()
{
	if (AEnemyCharacter* Body = CurrentInvestigateBody.Get())
		Body->DecrementInvestigators();
	CurrentInvestigateBody.Reset();
}

bool UEnemyAwarenessComponent::IsOwnerIsolatedEncounter() const
{
	const AAIController* C = Cast<AAIController>(GetOwner());
	const AEnemyCharacter* E = C ? Cast<AEnemyCharacter>(C->GetPawn()) : nullptr;
	return E && E->IsIsolatedEncounter();
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

bool UEnemyAwarenessComponent::IsCompanionActor(const AActor* Actor) const
{
	return Cast<const ACompanionCharacter>(Actor) != nullptr;
}

bool UEnemyAwarenessComponent::ShouldIgnoreCompanionStimulus(const AActor* Actor) const
{
	return IsCompanionActor(Actor) && CurrentState != EEnemyAwarenessState::Combat;
}

bool UEnemyAwarenessComponent::CanSelectCompanionTarget(const AActor* Candidate, const FSuspicionTrack& Track, float WorldTime) const
{
	if (!IsCompanionActor(Candidate)) return true;
	if (CurrentState != EEnemyAwarenessState::Combat) return false;
	if (IsValid(ArchetypeData) && ArchetypeData->CompanionThreatScoreMultiplier <= 0.f) return false;

	// Selectable while sighted or when it recently hurt us; the score multiplier (not a hard veto)
	// decides how it competes with the player.
	const bool bRecentlyDamagedByCompanion = RecentDamageInstigatorPawn.Get() == Candidate
		&& (WorldTime - RecentDamageWorldTime) < RecentDamageWindow;
	return Track.bSighted || bRecentlyDamagedByCompanion;
}

// --- Threat-Scored Target Selection (design §10) ---

AActor* UEnemyAwarenessComponent::ScoreAndSelectTarget() const
{
	if (!IsValid(ArchetypeData)) return nullptr;

	const AAIController* MyController = Cast<AAIController>(GetOwner());
	const APawn* MyPawn = MyController ? MyController->GetPawn() : nullptr;
	if (!IsValid(MyPawn)) return nullptr;

	const float WorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

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
					if (FocusTrack && FocusTrack->bSighted
						&& CanSelectCompanionTarget(FocusTarget, *FocusTrack, WorldTime))
					{
						return FocusTarget;
					}
				}
			}
		}
	}

	const float SightRadiusInv = 1.f / FMath::Max(ArchetypeData->SightRadius, 1.f);

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
		if (!CanSelectCompanionTarget(Candidate, Track, WorldTime)) continue;

		const float Dist = FVector::Dist(MyPawn->GetActorLocation(), Candidate->GetActorLocation());
		const float ProximityTerm = ArchetypeData->ThreatWeightProximity * (1.f - FMath::Clamp(Dist * SightRadiusInv, 0.f, 1.f));
		const float LOSTerm = ArchetypeData->ThreatWeightLOS * (Track.bSighted ? 1.f : 0.f);

		float DamageTerm = 0.f;
		if (RecentDamageInstigatorPawn.Get() == Candidate && (WorldTime - RecentDamageWorldTime) < RecentDamageWindow)
			DamageTerm = ArchetypeData->ThreatWeightRecentDamage;

		float Score = ProximityTerm + LOSTerm + DamageTerm;
		// Companion competes on score, biased by the archetype multiplier (player stays preferred < 1).
		if (IsCompanionActor(Candidate)) Score *= ArchetypeData->CompanionThreatScoreMultiplier;

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
	if (ShouldIgnoreCompanionStimulus(Target)) return;

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
