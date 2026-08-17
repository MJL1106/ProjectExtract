// UEnemyAwarenessComponent — awareness state ladder driven by perception stimuli and damage events.

#include "EnemyAwarenessComponent.h"
#include "AI/AIAcoustics.h"
#include "AI/AITargetingStatics.h"
#include "AI/SearchRoomExposure.h"
#include "EnemyAIController.h"
#include "EnemyArchetypeData.h"
#include "EnemyCharacter.h"
#include "EnemyDirectorSubsystem.h"
#include "Director/DirectorConfigData.h"
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
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "NavigationSystem.h"
#include "EngineUtils.h" // TActorIterator — DBNO combat-handoff companion lookup

namespace
{
	/** Vertical slack allowed when snapping a search / rally point onto the navmesh.
	 *  MUST stay well under the level's storey pitch (400uu on DemoMap): at 400 the
	 *  projection box reaches the floor above, so a candidate landing over a stairwell
	 *  void or balcony lip snaps up a storey and the enemy walks upstairs to "search"
	 *  a point the player was never near. Failing the projection instead is correct —
	 *  both callers degrade to the un-offset last-known location, which is reachable. */
	constexpr float EnemyNavProjectZExtent = 150.f;
}

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

	if (ACompanionCharacter* Companion = Cast<ACompanionCharacter>(Actor))
	{
		// CachedPerceivedCompanion feeds search-room exposure, and only the primary can ever own an
		// exposure generation, so the VIP must not evict it.
		if (Companion->IsPrimaryCompanion())
			CachedPerceivedCompanion = Companion;

		// Dedicated slot: the stimulus below is about to be dropped, so remember who it came from.
		// Only a cloaked companion writes here, so the primary can never evict the VIP.
		if (Companion->IsAlwaysSightCloaked())
			AlwaysCloakedCompanion = Companion;
	}

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
	if (IsCompanionActor(Actor))
	{
		const bool bHearing = Stimulus.Type == UAISense::GetSenseID<UAISense_Hearing>();
		const bool bFireNoise = bHearing && Stimulus.Tag == WeaponFireTag;
		if (bFireNoise && IsCompanionFireInaudible(Actor)) return;

		// Quiet entry stays acoustically silent. Exposure lifts sight only; it must not make a
		// non-watcher react to footsteps/reloads emitted during an unbroken Stealth command.
		if (bHearing && !bFireNoise)
			if (const ACompanionCharacter* Companion = Cast<ACompanionCharacter>(Actor))
				if (Companion->IsStealthActive()) return;

		if (!bFireNoise && IsCompanionSightCloaked(Actor)) return;
	}

	// Ally coordination: a mate's gunfire is the only friendly stimulus that matters — every other
	// friendly noise (reloads, traversal) falls through to the hostility filter and drops.
	if (Stimulus.Type == UAISense::GetSenseID<UAISense_Hearing>() && Stimulus.Tag == WeaponFireTag)
	{
		if (AEnemyCharacter* AllyShooter = Cast<AEnemyCharacter>(Actor); AllyShooter && !IsHostile(Actor))
		{
			HandleAllyGunfireHeard(AllyShooter, Stimulus);
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

void UEnemyAwarenessComponent::UpdateProximityBodyNotice()
{
	if (!IsValid(ArchetypeData) || !ArchetypeData->bEnableProximityBodyNotice) return;
	if (CurrentState == EEnemyAwarenessState::Combat) return;

	// Already walking to a body — nothing to notice.
	if (CurrentInvestigateBody.IsValid()) return;

	const AAIController* MyController = Cast<AAIController>(GetOwner());
	APawn* MyPawn = MyController ? MyController->GetPawn() : nullptr;
	if (!IsValid(MyPawn)) return;

	// An enemy the companion has armed a takedown on must stay asleep until that takedown resolves.
	// Waking it here would re-create the exact bug this delay exists to avoid: the second pocket
	// enemy alerting off its mate's corpse and turning the pending kill into a visible whiff.
	// Both clauses are needed: the reservation only ever covers the VICTIM and clears the instant the
	// kill resolves, while the window covers its PARTNER too and trails a beat past the kill — which is
	// the frame the partner would otherwise notice the fresh corpse in a same-instant double takedown.
	if (const AEnemyCharacter* MyChar = Cast<AEnemyCharacter>(MyPawn))
		if (MyChar->IsReservedForTakedown() || MyChar->IsInTakedownWindow()) return;

	const UEnemyDirectorSubsystem* Dir = Director.Get();
	UWorld* World = GetWorld();
	if (!Dir || !IsValid(World)) return;

	const float RadiusSq = FMath::Square(ArchetypeData->BodyNoticeRadius);
	const FVector EyeLocation = MyPawn->GetPawnViewLocation();

	// Nearest undiscovered corpse in radius. The director's list is capped (MaxCorpses), so this is
	// a bounded scan; the LOS trace only runs for the single winner.
	AEnemyCharacter* Best = nullptr;
	float BestDistSq = RadiusSq;
	for (const TWeakObjectPtr<AEnemyCharacter>& WeakCorpse : Dir->GetCorpses())
	{
		AEnemyCharacter* Corpse = WeakCorpse.Get();
		if (!IsValid(Corpse) || Corpse == MyPawn) continue;
		if (DiscoveredBodies.Contains(Corpse)) continue;

		const float DistSq = FVector::DistSquared(MyPawn->GetActorLocation(), Corpse->GetCorpseLocation());
		if (DistSq > BestDistSq) continue;

		BestDistSq = DistSq;
		Best = Corpse;
	}

	if (!Best)
	{
		BodyNoticeCandidate.Reset();
		BodyNoticeElapsed = 0.f;
		return;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(EnemyBodyNoticeLoS), false);
	QueryParams.AddIgnoredActor(MyPawn);
	QueryParams.AddIgnoredActor(Best);
	if (World->LineTraceTestByChannel(EyeLocation, Best->GetCorpseLocation(), ECC_Visibility, QueryParams))
	{
		BodyNoticeCandidate.Reset();
		BodyNoticeElapsed = 0.f;
		return;
	}

	// Switching candidate restarts the clock — the delay must be per-body, not cumulative.
	if (BodyNoticeCandidate.Get() != Best)
	{
		BodyNoticeCandidate = Best;
		BodyNoticeElapsed = 0.f;
	}

	BodyNoticeElapsed += UpdateInterval;
	if (BodyNoticeElapsed < ArchetypeData->BodyNoticeDelaySeconds) return;

	UE_LOG(LogEnemyAI, Log, TEXT("[BODY] %s noticed %s at its feet after %.1fs (dist=%.0f, out of view cone)"),
		*MyPawn->GetName(), *Best->GetName(), BodyNoticeElapsed, FMath::Sqrt(BestDistSq));

	BodyNoticeCandidate.Reset();
	BodyNoticeElapsed = 0.f;
	HandleBodySighted(Best);
}

void UEnemyAwarenessComponent::HandleSightStimulus(AActor* Actor, const FAIStimulus& Stimulus)
{
	if (Stimulus.WasSuccessfullySensed() && !IsActorAlive(Actor)) return;

	// Track bookkeeping runs in every state so visibility survives a Combat exit.
	FSuspicionTrack& Track = SuspicionTracks.FindOrAdd(Actor);
	Track.bSighted = Stimulus.WasSuccessfullySensed();
	if (Track.bSighted)
		StampTrack(Track, Actor->GetActorLocation());
	else
		// Lost-sight edge: stamp the last-seen location so the multi-threat memory window counts
		// from LOS-break, not from the sight-GAIN edge (Combat never tick-refreshes these tracks —
		// without this a threat visible longer than the window is evicted the tick it ducks).
		StampTrack(Track, Stimulus.StimulusLocation);

	if (Track.bSighted)
		if (ACompanionCharacter* Companion = Cast<ACompanionCharacter>(Actor))
			ApplySilentSearchRoomStartle(Companion, Track);

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

bool UEnemyAwarenessComponent::TryApplyBreachSearchRoomStartle(AActor* Actor,
	const FAIStimulus& Stimulus, float NormalGain, FSuspicionTrack& Track)
{
	static const FName BreachTag(TEXT("Breach"));
	ACompanionCharacter* Companion = Cast<ACompanionCharacter>(Actor);
	if (!IsValid(Companion) || Stimulus.Tag != BreachTag) return false;

	const uint32 ExposureGeneration = Companion->GetActiveSearchRoomExposureGeneration();
	if (ExposureGeneration == 0 || LastStartledSearchRoomExposureGeneration == ExposureGeneration)
		return false;

	const AAIController* MyController = Cast<AAIController>(GetOwner());
	const APawn* MyPawn = MyController ? MyController->GetPawn() : nullptr;
	if (!IsValid(MyPawn)
		|| !Companion->IsSearchRoomExposureObserverInScope(MyPawn->GetActorLocation()))
	{
		return false;
	}

	Track.Suspicion = SearchRoomExposure::ApplyStartleSuspicion(Track.Suspicion, NormalGain,
		ArchetypeData->BreachStartleSuspicionFloor, NoiseSuspicionCap);
	LastStartledSearchRoomExposureGeneration = ExposureGeneration;
	UE_LOG(LogEnemyAI, Log, TEXT("[SEARCH EXPOSURE] %s breach startle gen=%u suspicion=%.0f"),
		*GetNameSafe(GetOwner()), ExposureGeneration, Track.Suspicion);
	return true;
}

void UEnemyAwarenessComponent::HandleHearingStimulus(AActor* Actor, const FAIStimulus& Stimulus)
{
	if (IsOwnerIsolatedEncounter()) return;
	if (!Stimulus.WasSuccessfullySensed()) return;
	if (!IsValid(ArchetypeData)) return;

	// Armed-takedown hush: while the companion is lined up on this enemy's pocket it ignores gunfire,
	// walking and reloads, so the player taking their half of the synced kill doesn't make the partner
	// turn round mid-takedown. Gated on the ARMED window, not on volume overlap — an un-pinged pocket
	// hears an unsuppressed shot beside it like anyone else, which is what makes the ping+confirm
	// mechanic worth using. A sprint is blatant enough to pierce it (the player's own fault); a
	// level-wide Loud alert still wakes it via HandleGlobalAlertChanged.
	static const FName WeaponFireTag(TEXT("WeaponFire"));
	static const FName JogFootstepTag(TEXT("FootstepWalk"));
	static const FName SprintFootstepTag(TEXT("FootstepSprint"));
	if (IsOwnerTakedownHushed() && Stimulus.Tag != SprintFootstepTag) return;

	// Acoustic occlusion: walls/floors/locked doors silence the noise entirely — before the
	// track stamp, so a blocked shot leaves no memory at all. A closed-but-openable door lets
	// a muffled fraction through (the enemy can open it and investigate).
	const float AcousticMult = GetCachedAcousticMultiplier(Stimulus.StimulusLocation, Actor);
	if (AcousticMult <= KINDA_SMALL_NUMBER) return;

	FSuspicionTrack& Track = SuspicionTracks.FindOrAdd(Actor);
	StampTrack(Track, Stimulus.StimulusLocation);

	// During Combat, hearing the combat target himself fight (gunfire or a sprint — both already
	// acoustics-gated above) is live contact: it stamps the noise hold so a blind enemy
	// mid-firefight doesn't run its lost-contact clock while the player is audibly still there.
	// Otherwise track bookkeeping only — suspicion gain is irrelevant in Combat.
	if (CurrentState == EEnemyAwarenessState::Combat)
	{
		if (Actor == CombatTarget.Get()
			&& (Stimulus.Tag == WeaponFireTag || Stimulus.Tag == SprintFootstepTag))
		{
			if (const UWorld* W = GetWorld()) LastTargetNoiseTime = W->GetTimeSeconds();
		}
		return;
	}

	// Close-range Combat slam. A gunshot or a sprint heard from right beside you is not "something to
	// look into" — it is a fight already happening, so skip the meter and turn onto the source. Sits
	// below the Combat return above by design: an enemy already fighting has nothing left to slam.
	// The range scales with the noise's own strength AND the acoustic multiplier, so it degrades the
	// same way the noise does — a suppressor (~0.3) shrinks the 8 m gunshot slam to ~2.4 m, a closed
	// door (0.6) to ~4.8 m, and a fully occluded noise never reaches this line (returned above).
	// Hostility is re-tested cheaply here even though the perception handler already filtered it,
	// because slamming Combat onto a squadmate would be unrecoverable.
	const float SlamRange = Stimulus.Tag == WeaponFireTag
		? ArchetypeData->GunshotCombatSlamRange
		: (Stimulus.Tag == SprintFootstepTag ? ArchetypeData->SprintCombatSlamRange : 0.f);
	if (SlamRange > 0.f && IsHostile(Actor))
	{
		const AAIController* MyController = Cast<AAIController>(GetOwner());
		const APawn* MyPawn = MyController ? MyController->GetPawn() : nullptr;
		const float EffectiveSlam = SlamRange * Stimulus.Strength * AcousticMult;
		if (IsValid(MyPawn)
			&& FVector::DistSquared(MyPawn->GetActorLocation(), Stimulus.StimulusLocation) <= FMath::Square(EffectiveSlam))
		{
			EnterCombat(Actor, /*bConfirmedVisual=*/false);
			// EnterCombat can refuse (stealth-cloaked companion, DBNO player) — a refused slam must
			// fall through to the normal suspicion pipeline, not swallow the stimulus.
			if (CurrentState == EEnemyAwarenessState::Combat) return;
		}
	}

	const float Gain = Stimulus.Strength * ArchetypeData->NoiseSuspicionGain * AcousticMult;
	if (!TryApplyBreachSearchRoomStartle(Actor, Stimulus, Gain, Track))
		Track.Suspicion = FMath::Min(Track.Suspicion + Gain, NoiseSuspicionCap);

	// Movement-noise floors: loudness-scaled gain alone barely registers a footstep, so each audible
	// tier declares the minimum it is worth. Occlusion-scaled, so a door still kills it. Deliberately
	// a floor and not a state change — ApplySuspicionState picks it up on the next update, which reads
	// as a head-turn and a bark rather than a snap. Quiet/slow-walk steps have no floor at all: that
	// is the stealth tool, and it stays worth using.
	const float TierFloor = Stimulus.Tag == JogFootstepTag
		? ArchetypeData->JogFootstepSuspicionFloor
		: (Stimulus.Tag == SprintFootstepTag ? ArchetypeData->SprintFootstepSuspicionFloor : 0.f);
	if (TierFloor > 0.f)
		Track.Suspicion = FMath::Min(FMath::Max(Track.Suspicion, TierFloor * AcousticMult), NoiseSuspicionCap);

	// Investigate jump. Unsuppressed gunfire has always done this; a sprint joins it on a stricter
	// gate — gunfire is worth walking to from Suspicious upward, while a sprint has to have earned
	// SearchingThreshold through its own floor. An unoccluded sprint does (80); the same sprint heard
	// through a door does not (48), and stays a turn-and-look. A jog never gets here at all.
	const bool bFireInvestigate = Stimulus.Tag == WeaponFireTag && Track.Suspicion >= ArchetypeData->SuspiciousThreshold;
	const bool bSprintInvestigate = Stimulus.Tag == SprintFootstepTag && Track.Suspicion >= ArchetypeData->SearchingThreshold;
	if (bFireInvestigate || bSprintInvestigate)
	{
		Track.Suspicion = FMath::Max(Track.Suspicion, ArchetypeData->SearchingThreshold);
		SetInvestigateLocation(Stimulus.StimulusLocation);
		TimeSpentSearching = 0.f;
		if (CurrentState < EEnemyAwarenessState::Searching)
			Bark(EBarkType::SearchArea);
		SetState(EEnemyAwarenessState::Searching);
	}
}

float UEnemyAwarenessComponent::GetCachedAcousticMultiplier(const FVector& StimLoc, const AActor* Instigator)
{
	const AAIController* Controller = Cast<AAIController>(GetOwner());
	APawn* MyPawn = Controller ? Controller->GetPawn() : nullptr;
	UWorld* World = GetWorld();
	if (!MyPawn || !World) return 1.f;

	const float Now = World->GetTimeSeconds();
	const FIntVector Cell(
		FMath::FloorToInt(StimLoc.X / AcousticCellSize),
		FMath::FloorToInt(StimLoc.Y / AcousticCellSize),
		FMath::FloorToInt(StimLoc.Z / AcousticCellSize));

	for (int32 i = AcousticCache.Num() - 1; i >= 0; --i)
	{
		if (AcousticCache[i].ExpiryTime < Now)
		{
			AcousticCache.RemoveAtSwap(i);
			continue;
		}
		if (AcousticCache[i].Cell == Cell) return AcousticCache[i].Multiplier;
	}

	const float ThroughDoorMult = IsValid(ArchetypeData) ? ArchetypeData->ThroughDoorNoiseMultiplier : 1.f;
	const float Mult = AIAcoustics::ComputeMultiplier(World, MyPawn->GetPawnViewLocation(), StimLoc,
		MyPawn, Instigator, ThroughDoorMult);

	AcousticCache.Add({ Cell, Mult, Now + AcousticCacheTTL });
	return Mult;
}

void UEnemyAwarenessComponent::HandleAllyGunfireHeard(AEnemyCharacter* Shooter, const FAIStimulus& Stimulus)
{
	if (IsOwnerIsolatedEncounter()) return;
	if (!Stimulus.WasSuccessfullySensed()) return;
	if (!IsValid(ArchetypeData)) return;
	if (IsOwnerTakedownHushed()) return;
	if (!IsActorAlive(Shooter)) return;
	if (CurrentState == EEnemyAwarenessState::Combat) return;

	// Own gunfire echoes back through the friendly-hearing channel — never self-investigate
	// (the Combat gate above misses the Combat->Searching transition frame).
	const AAIController* MyController = Cast<AAIController>(GetOwner());
	if (MyController && Shooter == MyController->GetPawn()) return;

	// Ally gunfire obeys the same acoustics as hostile fire — a mate shooting two rooms away
	// behind solid walls doesn't coordinate this enemy.
	const float AcousticMult = GetCachedAcousticMultiplier(Stimulus.StimulusLocation, Shooter);
	if (AcousticMult <= KINDA_SMALL_NUMBER) return;

	// Suspicion points at what the mate is shooting at, not at the mate — the first heard shot turns
	// us toward his target (Suspicious), sustained fire accumulates into Searching via the normal
	// suspicion pipeline. If his target is unknown (or cloaked to us, which would purge the track),
	// face the muzzle instead.
	AActor* AimTarget = Shooter->GetAIAimTarget();
	const bool bAimKnown = IsValid(AimTarget) && IsHostile(AimTarget) && !IsCompanionSightCloaked(AimTarget);

	// Fight contagion: a mate audibly ENGAGED (his awareness is Combat, his target known) is combat
	// confirmation, not mere suspicion — join the fight instead of strolling through the middle of
	// it toward a body/investigate goal. Unoccluded earshot only: a muffled shot through a door
	// (AcousticMult < 1) says "fight somewhere", not "the target is THERE" — that stays on the
	// investigate path below. EnterCombat clears any body pin and stamps last-known itself.
	if (bAimKnown && AcousticMult >= 1.f - KINDA_SMALL_NUMBER)
	{
		const AEnemyAIController* ShooterCtrl = Cast<AEnemyAIController>(Shooter->GetController());
		const UEnemyAwarenessComponent* ShooterAware = ShooterCtrl ? ShooterCtrl->GetAwarenessComponent() : nullptr;
		if (IsValid(ShooterAware) && ShooterAware->GetAwarenessState() == EEnemyAwarenessState::Combat)
		{
			FSuspicionTrack& JoinTrack = SuspicionTracks.FindOrAdd(AimTarget);
			StampTrack(JoinTrack, AimTarget->GetActorLocation());

			EnterCombat(AimTarget, /*bConfirmedVisual=*/false);
			return;
		}
	}

	AActor* TrackKey = bAimKnown ? AimTarget : static_cast<AActor*>(Shooter);

	FSuspicionTrack& Track = SuspicionTracks.FindOrAdd(TrackKey);
	StampTrack(Track, bAimKnown ? AimTarget->GetActorLocation() : Stimulus.StimulusLocation);

	const float Gain = Stimulus.Strength * ArchetypeData->NoiseSuspicionGain * AcousticMult;
	Track.Suspicion = FMath::Min(
		FMath::Max(Track.Suspicion + Gain, ArchetypeData->SuspiciousThreshold), NoiseSuspicionCap);
}

// --- Damage Notification ---

void UEnemyAwarenessComponent::NotifyDamaged(AController* Instigator)
{
	if (bStopped) return;
	if (!IsValid(Instigator)) return;

	APawn* InstigatorPawn = Instigator->GetPawn();
	if (!IsValid(InstigatorPawn)) return;
	// Real damage is fight-on: only a stealth-active companion stays an invisible attacker (by
	// design); a cloaked Normal-mode companion that hurts us breaks its own cloak right here.
	if (const ACompanionCharacter* DmgCompanion = Cast<ACompanionCharacter>(InstigatorPawn))
		if (DmgCompanion->IsStealthActive()) return;
	if (!IsHostile(InstigatorPawn)) return;

	RecentDamageInstigatorPawn = InstigatorPawn;
	RecentDamageWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : -1e9f;
	DamageTimesByAttacker.Add(InstigatorPawn, RecentDamageWorldTime);

	// Ensure the instigator has a suspicion track so threat scoring can find it even when
	// perception never delivered a stimulus (suppressed weapon, out of hearing range — QA #6).
	FSuspicionTrack& Track = SuspicionTracks.FindOrAdd(InstigatorPawn);
	StampTrack(Track, InstigatorPawn->GetActorLocation());

	// Point-blank guard: a hit must not rip the target off a sighted hostile standing in our face —
	// that would flip-flop against ScoreAndSelectTarget's point-blank override every awareness tick.
	// The damage stamp above still feeds scoring. Holds when the damager is distant, and also when
	// both are point-blank and the damager is SIGHTED (in-set scoring + hysteresis arbitrate a close
	// brawl instead of hit-cadence ping-pong); an unsighted point-blank hit is an ambush — force-
	// switch as before so the enemy turns.
	bool bKeepPointBlankTarget = false;
	if (CurrentState == EEnemyAwarenessState::Combat && IsValid(ArchetypeData)
		&& ArchetypeData->PointBlankTargetRange > 0.f)
	{
		AActor* Current = CombatTarget.Get();
		const AAIController* MyController = Cast<AAIController>(GetOwner());
		const APawn* MyPawn = MyController ? MyController->GetPawn() : nullptr;
		if (IsValid(Current) && Current != InstigatorPawn && IsValid(MyPawn) && IsActorAlive(Current))
		{
			const FSuspicionTrack* CurrentTrack = SuspicionTracks.Find(Current);
			const float Range = ArchetypeData->PointBlankTargetRange;
			// Slack on the hold check so a target strafing across the boundary doesn't oscillate
			// between "guard holds" and "damage rips the target off" at hit cadence.
			constexpr float PointBlankHoldSlack = 1.15f;
			const FVector MyLoc = MyPawn->GetActorLocation();
			const bool bCurrentPointBlank = CurrentTrack && CurrentTrack->bSighted
				&& FVector::Dist(MyLoc, Current->GetActorLocation()) <= Range * PointBlankHoldSlack;
			if (bCurrentPointBlank)
			{
				const bool bInstigatorPointBlank =
					FVector::Dist(MyLoc, InstigatorPawn->GetActorLocation()) <= Range;
				bKeepPointBlankTarget = !bInstigatorPointBlank || Track.bSighted;
			}
		}
	}

	if (!bKeepPointBlankTarget)
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

// --- Debug Force-Engage ---

void UEnemyAwarenessComponent::DebugForceEngage(AActor* Target)
{
	if (!IsValid(Target)) return;

	// Direct state + target set: no bark, no squad broadcast, no companion-cloak seeding.
	// bDebugForcedCombat suppresses the Director report inside SetState.
	LastKnownLocation = Target->GetActorLocation();
	bHadLOS = true;
	TimeSinceLOSLost = 0.f;

	bDebugForcedCombat = true;
	SetCombatTarget(Target);
	SetState(EEnemyAwarenessState::Combat);
	bDebugForcedCombat = false;
}

void UEnemyAwarenessComponent::SetLastManHunting(bool bHunting)
{
	if (bLastManHunting == bHunting) return;
	bLastManHunting = bHunting;

	// Latching preserves whatever no-sight dwell has already accrued — a survivor who has been
	// hiding for a minute should not owe a fresh dwell before he starts hunting.
	if (!bHunting)
	{
		TimeWithoutSight = 0.f;
		return;
	}

	// The seed's approach-point contract is over the moment this enemy is the wave's last man:
	// its arrival quit runs off DirectorSeedArrivalAccum, a path the 1 Hz contact refresh cannot
	// intercept, and would drop him to Searching (which nulls the combat target) despite the latch.
	bDirectorSeeded = false;
}

void UEnemyAwarenessComponent::RefreshLastManContact(AActor* Target)
{
	if (bStopped) return;
	if (!IsValid(Target)) return;
	if (!IsActorAlive(Target)) return;
	if (Target == FindDownedPlayerPawn(this)) return;

	// Do not steal an existing combat lock on a different target (e.g. the companion).
	// ScoreAndSelectTarget runs at 0.15s; the director at 1s — overwriting would cause 1 Hz snap.
	// Only hold the lost-contact clock if real LOS to the other target is active; otherwise let the
	// grace expire — Searching nulls the target and the next refresh re-points at the player.
	if (CombatTarget.IsValid() && CombatTarget.Get() != Target)
	{
		if (bHadLOS) TimeSinceLOSLost = 0.f;
		return;
	}

	LastKnownLocation = Target->GetActorLocation();
	TimeSinceLOSLost = 0.f;

	if (CombatTarget.Get() != Target) SetCombatTarget(Target);
	else WriteBBVectors();

	const bool bWasCombat = (CurrentState == EEnemyAwarenessState::Combat);
	SetState(EEnemyAwarenessState::Combat);

	// Combat entry from a non-Combat state bypasses EnterCombat's SeedCompanionSightTracks (line
	// 1515). Without the seed a companion in direct view is sight-blind to this enemy.
	if (!bWasCombat && CurrentState == EEnemyAwarenessState::Combat)
		SeedCompanionSightTracks();
}

void UEnemyAwarenessComponent::ForceEngage(AActor* Target)
{
	if (bStopped) return;
	if (!IsValid(Target)) return;

	// The director force-engages every wave squad onto the player pawn the moment it spawns, and
	// re-issues it each tick for members that decayed to Unaware. With the player down, EnterCombat's
	// guard would refuse and the squad would idle at its spawn zone. Take the fight to the companion
	// instead; failing that, hunt toward the body (SetInvestigateLocation clamps to the DBNO standoff
	// ring, so they close in without crowding the downed player) and be in position on revive.
	if (Target == FindDownedPlayerPawn(this))
	{
		if (AActor* Handoff = FindDBNOHandoffCompanion())
		{
			EnterCombat(Handoff, /*bConfirmedVisual*/ false);
			return;
		}

		if (CurrentState < EEnemyAwarenessState::Searching)
		{
			SetInvestigateLocation(Target->GetActorLocation());
			TimeSpentSearching = 0.f;
			SetState(EEnemyAwarenessState::Searching);
		}
		return;
	}

	// Only arm the director seed when freshly entering combat — an already-fighting enemy
	// (wave re-engagement rally) must not inherit the close-range quit or suppression bypass.
	const bool bFreshSeed = (CurrentState != EEnemyAwarenessState::Combat);

	EnterCombat(Target, /*bConfirmedVisual*/ false);
	// EnterCombat unconditionally clears bDirectorSeeded; re-arm after if fresh.

	bWasDirectorSpawned = true;

	if (bFreshSeed && IsValid(ArchetypeData))
	{
		bDirectorSeeded = true;
		DirectorSeedArrivalAccum = 0.f;

		// Per-member approach dispersal: offset LastKnownLocation by a unique angle per
		// squad member so the squad fans out during the approach instead of travelling as
		// a column. The seed location captures the offset point for arrival detection.
		FVector Offset = FVector::ZeroVector;
		const AAIController* MyC = Cast<AAIController>(GetOwner());
		const AEnemyCharacter* MyEnemy = MyC ? Cast<AEnemyCharacter>(MyC->GetPawn()) : nullptr;
		if (IsValid(MyEnemy))
		{
			UEnemySquadSubsystem* SquadSS = SquadSubsystem.Get();
			UEnemySquad* Squad = IsValid(SquadSS) ? SquadSS->GetSquadFor(MyEnemy) : nullptr;
			if (IsValid(Squad))
			{
				// Dense living-member rank so dead entries don't collapse angles.
				const TArray<TWeakObjectPtr<AEnemyCharacter>>& Members = Squad->GetMembers();
				int32 LivingRank = 0;
				int32 LivingTotal = 0;
				for (const TWeakObjectPtr<AEnemyCharacter>& M : Members)
				{
					if (!M.IsValid()) continue;
					UHealthComponent* HP = M->GetHealthComponent();
					if (!IsValid(HP) || HP->IsDead()) continue;
					if (M.Get() == MyEnemy) LivingRank = LivingTotal;
					++LivingTotal;
				}

				if (LivingTotal > 1)
				{
					// Per-squad phase jitter so concurrent squads don't ring-overlap.
					// Frac wraps [0,4.29e6] to [0,1) — without it float32 ULP absorbs
					// the rank fraction and collapses most members onto one angle.
					const float PhaseJitter = FMath::Frac(static_cast<float>(Squad->GetSquadId().IsNone()
						? 0 : GetTypeHash(Squad->GetSquadId())) * 0.001f);
					const float Angle = ((static_cast<float>(LivingRank) / LivingTotal) + PhaseJitter) * 2.f * UE_PI;
					const float Radius = ArchetypeData->DirectorSeedSpreadRadius;
					Offset = FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.f);
				}
			}
		}

		// Nav-project the offset point; on failure retry at half radius rotated by pi
		// before falling back to the un-offset location.
		FVector OffsetTarget = LastKnownLocation + Offset;
		if (!Offset.IsNearlyZero())
		{
			UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
			if (IsValid(NavSys))
			{
				FNavLocation NavLoc;
				const FVector NavExtent(200.f, 200.f, EnemyNavProjectZExtent);
				if (NavSys->ProjectPointToNavigation(OffsetTarget, NavLoc, NavExtent))
				{
					OffsetTarget = NavLoc.Location;
				}
				else
				{
					const FVector Retry = LastKnownLocation - Offset * 0.5f;
					if (NavSys->ProjectPointToNavigation(Retry, NavLoc, NavExtent))
						OffsetTarget = NavLoc.Location;
					else
						OffsetTarget = LastKnownLocation;
				}
			}
		}

		LastKnownLocation = OffsetTarget;
		WriteBBVectors();
		DirectorSeedLocation = OffsetTarget;
	}
}

// --- Shot-At Notification ---

void UEnemyAwarenessComponent::NotifyShotAt(AActor* InstigatorPawn, const FVector& ShotOrigin)
{
	if (bStopped) return;
	if (!IsValid(InstigatorPawn)) return;
	// Same fight-on rule as NotifyDamaged: near-misses break a Normal cloak; stealth stays hidden.
	if (const ACompanionCharacter* ShotCompanion = Cast<ACompanionCharacter>(InstigatorPawn))
		if (ShotCompanion->IsStealthActive()) return;
	if (!IsHostile(InstigatorPawn)) return;
	if (!IsValid(ArchetypeData) || !ArchetypeData->bReactsToBeingShotAt) return;

	// Already in Combat — only refresh the track location; let the existing loop run.
	if (CurrentState == EEnemyAwarenessState::Combat)
	{
		FSuspicionTrack& Track = SuspicionTracks.FindOrAdd(InstigatorPawn);
		StampTrack(Track, ShotOrigin);
		return;
	}

	// Per-instigator rate-limit folded into FSuspicionTrack (fix #4 — no separate map).
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	FSuspicionTrack& Track = SuspicionTracks.FindOrAdd(InstigatorPawn);
	if ((Now - Track.LastShotAtTime) < ShotAtRateLimit) return;
	Track.LastShotAtTime = Now;

	// Clamp to at least SearchingThreshold so a mistuned DA can't decay out on the next tick (fix #7).
	StampTrack(Track, ShotOrigin);
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
	SetLastManHunting(false);

	if (const UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(UpdateTimerHandle);
}

// --- Update Timer ---

void UEnemyAwarenessComponent::UpdateAwareness()
{
	if (bStopped) return;
	if (!IsValid(ArchetypeData)) return;

	RefreshSearchRoomExposure();
	RefreshAlwaysCloakedCompanion();

	// Debug auto-engage: force Combat with the player pawn every tick while the flag is set.
	// Runs before the normal Combat/Suspicion branch so it re-asserts target and state even if
	// a previous tick decayed to Searching.
	{
		const AAIController* MyController = Cast<AAIController>(GetOwner());
		const AEnemyCharacter* MyChar = MyController ? Cast<AEnemyCharacter>(MyController->GetPawn()) : nullptr;
		const bool bWantDebugEngage = IsValid(MyChar) && MyChar->bDebugAutoEngagePlayer;

		if (bWantDebugEngage)
		{
			APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
			if (IsValid(PlayerPawn) && IsActorAlive(PlayerPawn))
			{
				bWasDebugEngaged = true;
				DebugForceEngage(PlayerPawn);
				return;
			}
		}

		// Flag-off edge: clear the synthetic bHadLOS so the geometric contact-hold check
		// re-establishes honestly or decays via LostContactGrace.
		if (!bWantDebugEngage && bWasDebugEngaged)
		{
			bWasDebugEngaged = false;
			bHadLOS = false;
		}
	}

	if (CurrentState == EEnemyAwarenessState::Combat)
	{
		UpdateCombat();
	}
	else
	{
		UpdateSuspicion();

		// Runs below Combat only: a corpse at this enemy's feet is outside the head-bone view cone,
		// so sight discovery never fires for the one standing right next to a fresh kill.
		UpdateProximityBodyNotice();

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

			// DBNO overwatch: a downed player generates no suspicion (reads as dead), so without
			// this hold the search timer runs dry and the shooter stands down to Unaware mid-down.
			if (bSearchHoldForDBNOPlayer && !FindDownedPlayerPawn(this))
				bSearchHoldForDBNOPlayer = false;

			// Hold the search timeout while actively investigating a body — the enemy must reach it
			// or lose it (hard-cap destroy nulls the weak ref) before reverting to Unaware.
			if (!CurrentInvestigateBody.IsValid() && !bSearchHoldForDBNOPlayer)
			{
				TimeSpentSearching += UpdateInterval;
				if (TimeSpentSearching >= ArchetypeData->SearchDuration)
				{
					// Sustained-pressure re-target: ONLY director-spawned, non-isolated
					// enemies whose search expires keep pressing. Hand-placed patrols,
					// isolated encounters, and corpse investigators decay normally.
					// Uses the effective config/phase via IsSustainedPressureActive
					// (folds in wave exclusion + punishment overrides).
					UEnemyDirectorSubsystem* Dir = Director.Get();
					const bool bShouldRetarget = bWasDirectorSpawned
						&& !IsOwnerIsolatedEncounter()
						&& IsValid(Dir)
						&& Dir->IsSustainedPressureActive();

					if (bShouldRetarget)
					{
						UWorld* SWorld = GetWorld();
						APawn* SPawn = IsValid(SWorld) ? UGameplayStatics::GetPlayerPawn(SWorld, 0) : nullptr;
						const AAIController* LogC = Cast<AAIController>(GetOwner());
						const APawn* LogP = LogC ? LogC->GetPawn() : nullptr;

						// Distance cap: a cross-map reinforcement decays instead of
						// trekking the whole level on a stale position.
						const float MaxRetargetDist = IsValid(ArchetypeData) ? ArchetypeData->SightRadius * 2.f : 5000.f;
						const bool bInRange = IsValid(SPawn) && IsValid(LogP)
							&& FVector::Dist(LogP->GetActorLocation(), SPawn->GetActorLocation()) <= MaxRetargetDist;

						if (bInRange)
						{
							SetInvestigateLocation(SPawn->GetActorLocation());
							TimeSpentSearching = 0.f;
							UE_LOG(LogEnemyAI, Log, TEXT("[AWARENESS] %s search expired under sustained pressure — re-targeting player"),
								IsValid(LogP) ? *LogP->GetName() : TEXT("?"));
						}
						else
						{
							ClearInvestigateBody();
							SetCombatTarget(nullptr);
							SetState(EEnemyAwarenessState::Unaware);
						}
					}
					else
					{
						ClearInvestigateBody();
						SetCombatTarget(nullptr);
						SetState(EEnemyAwarenessState::Unaware);
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

	// Cloak rules: a companion target that re-cloaked (went stealth mid-fight) counts as gone —
	// without this the ignore-gate swallows its lost-sight event, bHadLOS never drops, and the
	// enemy tracks and shoots an "invisible" target indefinitely.
	if (!bTargetGone)
		bTargetGone = IsCompanionSightCloaked(CombatTarget.Get());

	if (bTargetGone)
	{
		// Player-DBNO handoff: a downed player reads as dead (bIsDead holds until revive) but the
		// fight is NOT over — dropping straight to Searching made the shooter visibly stand down
		// the instant the player went DBNO. Seed companion sight tracks first (a companion in view
		// gets a sighted track immediately) so normal selection can hand off; if it still finds
		// nothing (companion occluded mid-fight), force the handoff to the in-range companion.
		// FindDownedPlayerPawn, not a bare interface cast — the companion implements the same
		// interface, and a DBNO companion dropping out of Combat must not trigger this path.
		const bool bDroppedDBNOPlayer = CombatTarget.IsValid()
			&& CombatTarget.Get() == FindDownedPlayerPawn(this);

		// Companion-DBNO mirror: a downed companion also reads as dead — hand the fight to the
		// player when normal selection has no track on them (player in cover the whole fight).
		const ACompanionCharacter* DroppedCompanion = Cast<ACompanionCharacter>(CombatTarget.Get());
		const bool bDroppedDBNOCompanion = IsValid(DroppedCompanion) && DroppedCompanion->GetIsCompanionDBNO();

		// Seed on EITHER drop, not just the player's: whoever is left standing on the player's side
		// is a companion either way — the primary once the player is down, the armed extraction VIP
		// once the primary is down too. Perception only fires on edges, so a companion in plain view
		// can hold no sighted track at all and normal selection would skip straight past him.
		if (bDroppedDBNOPlayer || bDroppedDBNOCompanion)
			SeedCompanionSightTracks();

		// Target died — try to immediately acquire a sighted candidate before dropping to Searching
		AActor* NextTarget = ScoreAndSelectTarget();
		if (!IsValid(NextTarget) && bDroppedDBNOCompanion)
			NextTarget = FindDBNOHandoffPlayer();
		// Nearest live companion, on either drop. Alive-gated inside, so the companion that just
		// went down can never be re-acquired here — with the player and the primary both DBNO this
		// is what puts the fight onto the armed VIP instead of standing the shooters down.
		if (!IsValid(NextTarget) && (bDroppedDBNOPlayer || bDroppedDBNOCompanion))
			NextTarget = FindDBNOHandoffCompanion();

		if (IsValid(NextTarget))
		{
			const FSuspicionTrack* Track = SuspicionTracks.Find(NextTarget);
			EnterCombat(NextTarget, Track && Track->bSighted);
		}
		else
		{
			// DBNO overwatch: the search timeout holds while the player stays down, so the
			// shooter never decays to Unaware mid-revive-window. Gated on a downed player
			// EXISTING, not on the player being the target that dropped — an enemy that only
			// ever fought the companion has no player lock to lose, and would otherwise stand
			// all the way down while the player lies bleeding. Set-only — a later non-player
			// target drop (e.g. handoff companion killed) must not wipe the hold; the Searching
			// tick clears it when the player revives or dies.
			if (FindDownedPlayerPawn(this))
				bSearchHoldForDBNOPlayer = true;
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
		TimeWithoutSight = 0.f;
		LastKnownLocation = CombatTarget->GetActorLocation();
		WriteBBVectors();
		BroadcastSightingToSquad();
	}
	else
	{
		// No perceived sight this tick. Zeroed again below if geometric LOS turns out to be clear —
		// contact held via recent damage or suppression is not sight and keeps the clock running.
		TimeWithoutSight += UpdateInterval;

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
			// Note: NotifyDamaged calls EnterCombat which unconditionally re-zeroes
			// TimeSinceLOSLost and re-writes LastKnownLocation. No skip needed here —
			// the re-seed happens at the EnterCombat site. Fixing that is out of scope
			// (it affects every enemy in the game, not just director-spawned ones).
			if (!bHoldContact)
			{
				const float WorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
				if ((WorldTime - RecentDamageWorldTime) < RecentDamageWindow)
					bHoldContact = true;
			}

			// 3) Currently suppressed — director-seeded enemies that never gained real LOS
			// skip this: near-miss suppression during a defend would zero TimeSinceLOSLost
			// every tick and keep them in Combat indefinitely at the stale position.
			// Geometric LOS (check 1 above) still holds — that's a real sighting.
			if (!bHoldContact && !(bDirectorSeeded && !bHadLOS))
			{
				const AEnemyCharacter* MyEnemy = Cast<AEnemyCharacter>(MyPawn);
				if (IsValid(MyEnemy))
				{
					USuppressionComponent* SupprComp = MyEnemy->GetSuppressionComponent();
					if (IsValid(SupprComp) && SupprComp->IsSuppressed())
						bHoldContact = true;
				}
			}

			// 4) A squadmate can currently see the target (sighting relay stamps this at 1 Hz
			// while any member holds sight). The relay also live-refreshes LastKnownLocation,
			// so a held member pursues real data — safe for director-seeded members too.
			if (!bHoldContact)
			{
				const float WorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
				if ((WorldTime - LastSquadSightTime) < SquadSightHoldWindow)
					bHoldContact = true;
			}

			// 5) We recently HEARD the target fight — gunfire or sprint, acoustics-gated at the
			// stimulus. This is what keeps a long drawn-out fight honest: an enemy pinned blind
			// behind cover doesn't "forget" a player who is audibly still shooting, yet a player
			// who goes quiet and slips away is searched for on the normal grace. Director-seeded
			// members that never gained real LOS skip it (same reason as suppression) so the
			// seed-arrival quit can still fire.
			if (!bHoldContact && !(bDirectorSeeded && !bHadLOS))
			{
				const float WorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
				if ((WorldTime - LastTargetNoiseTime) < TargetNoiseHoldWindow)
					bHoldContact = true;
			}
		}

		if (bHoldContact)
		{
			TimeSinceLOSLost = 0.f;
			// Only refresh last-known when we actually see the target (FOV LOS)
			if (bHoldViaFOVLOS)
			{
				// Geometric LOS is the honest "we can see them" signal — clear the seed
				// so normal combat behaviour takes over (even beyond SightRadius where
				// perception never delivers a stimulus edge).
				TimeWithoutSight = 0.f;
				bDirectorSeeded = false;
				LastKnownLocation = CombatTarget->GetActorLocation();
				WriteBBVectors();
				BroadcastSightingToSquad();
			}
		}
		else
		{
			TimeSinceLOSLost += UpdateInterval;

			// Director-seeded arrival: the enemy reached its per-member approach point and
			// found nobody. Measure against DirectorSeedLocation (the offset point captured
			// at ForceEngage), NOT LastKnownLocation (which is live-refreshed by
			// EnterCombat / squad relay and would track the player). Require a brief grace
			// so a momentary LOS break at close range doesn't read as "arrived and nobody".
			// The wave's last man is exempt: the seed quit nulls his combat target on a path the
			// director's contact refresh cannot intercept, which would eject the latch from Combat.
			if (bDirectorSeeded && !bLastManHunting && IsValid(MyPawn) && IsValid(ArchetypeData))
			{
				const float DistToSeed = FVector::Dist(MyPawn->GetActorLocation(), DirectorSeedLocation);
				if (DistToSeed < ArchetypeData->DirectorSeedArrivalRadius)
				{
					DirectorSeedArrivalAccum += UpdateInterval;
					if (DirectorSeedArrivalAccum >= ArchetypeData->DirectorSeedArrivalGrace)
					{
						UE_LOG(LogEnemyAI, Log, TEXT("[AWARENESS] %s director-seeded arrival (dist=%.0f, grace=%.1f) — searching"),
							*MyPawn->GetName(), DistToSeed, DirectorSeedArrivalAccum);
						TransitionToSearching(true);
						return;
					}
				}
				else
				{
					DirectorSeedArrivalAccum = 0.f;
				}
			}

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
	// Takedown-pocket enemies keep the pre-buff sneak-up profile: no point-blank auto-combat here
	// (and no near-fill boost, gated in ComputeSightFillRate) — the pocket exists to be crept on.
	// Volume overlap, NOT the armed-takedown hush: creeping up must work on an un-pinged pocket too.
	const bool bTakedownPocket = IsOwnerTakedownPocket();
	float MaxSuspicion = 0.f;
	FVector MaxLocation = FVector::ZeroVector;

	for (auto It = SuspicionTracks.CreateIterator(); It; ++It)
	{
		AActor* Actor = It.Key().Get();
		if (!Actor) { It.RemoveCurrent(); continue; }

		// Purge only when fully imperceptible. A sight-cloaked companion whose fire is audible
		// (unsuppressed stealth shots) keeps its track so noise suspicion accumulates across shots;
		// the cloak still forces the non-sighted branch below, so it can never sight-fill or
		// trigger the point-blank auto-combat.
		const bool bSightCloaked = IsCompanionSightCloaked(Actor);
		if (bSightCloaked && IsCompanionFireInaudible(Actor)) { It.RemoveCurrent(); continue; }

		FSuspicionTrack& Track = It.Value();
		if (!bSightCloaked && Track.bSighted && IsActorAlive(Actor))
		{
			Track.Suspicion += ComputeSightFillRate(MyPawn, Actor) * UpdateInterval;
			StampTrack(Track, Actor->GetActorLocation());

			const bool bPointBlank = !bTakedownPocket
				&& FVector::DistSquared(MyPawn->GetActorLocation(), Actor->GetActorLocation()) <= AutoCombatRangeSq;
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

	// Close-proximity boost: multiplies ON TOP of the band factor so it composes cleanly even if
	// NearFillBoostRange is tuned past FullFillRange. Point-blank staring fills fast; long sight
	// lines keep the slow burn (which also self-scales tight maps vs open ones).
	// Takedown-pocket enemies are exempt — the boost made sneaking up on them impossible.
	if (!IsOwnerTakedownPocket()
		&& ArchetypeData->NearFillBoostRange > 0.f && Dist < ArchetypeData->NearFillBoostRange)
		DistFactor *= FMath::Lerp(ArchetypeData->NearFillBoostMax, 1.f, Dist / ArchetypeData->NearFillBoostRange);

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
	// A stealth-active companion can never become a combat target (invisible by design — its
	// audible unsuppressed fire routes to Searching, never a lock-on). A cloaked NORMAL companion
	// reaching here means real damage/near-miss broke its cloak: entering Combat lifts the cloak
	// for this enemy, so the fight is coherent from this point on.
	if (ACompanionCharacter* TargetCompanion = Cast<ACompanionCharacter>(Target))
	{
		// A captive/unarmed VIP can never become a combat target, however the acquisition was raised.
		if (TargetCompanion->IsAlwaysSightCloaked()) return;

		if (TargetCompanion->IsStealthActive())
		{
			const AAIController* MyController = Cast<AAIController>(GetOwner());
			const APawn* MyPawn = MyController ? MyController->GetPawn() : nullptr;
			if (!IsValid(MyPawn)
				|| !TargetCompanion->IsSearchRoomExposureObserverInScope(MyPawn->GetActorLocation()))
			{
				return;
			}

			TargetCompanion->SetStealthBroken(true);
		}
	}

	// A downed player is never a valid combat target. UpdateCombat already releases the lock (DBNO
	// reads as not-alive), but EVERY acquisition funnels through here and re-locks faster than that
	// release runs — the director force-engages each new wave squad onto the player pawn, so the
	// player gets shot where they lie for as long as the wave keeps spawning. Refusing here is the
	// single choke point that covers NotifyDamaged / NotifyShotAt / sight fast-track alike.
	// FindDownedPlayerPawn, not a bare interface cast — the companion implements the same interface
	// and a DBNO COMPANION must keep flowing through (UpdateCombat's mirror owns that case).
	if (IsValid(Target) && Target == FindDownedPlayerPawn(this)) return;

	// Any non-ForceEngage entry clears the director seed by construction. ForceEngage
	// re-arms it AFTER this call returns, so no conditional logic is needed here.
	bDirectorSeeded = false;

	ClearInvestigateBody();

	LastKnownLocation = IsValid(Target) ? Target->GetActorLocation() : LastKnownLocation;
	bHadLOS = bConfirmedVisual;
	TimeSinceLOSLost = 0.f;
	// Only a confirmed visual is sight. A blind acquisition (ForceEngage, hearing, near-miss) must
	// not reset the no-sight dwell, or the last man's hunt gate could never mature.
	if (bConfirmedVisual) TimeWithoutSight = 0.f;

	for (auto& Pair : SuspicionTracks)
		Pair.Value.Suspicion = 0.f;

	// Fight-liveness stamps are per-target: a swap must not inherit the old target's holds.
	// Same-target re-entries (damage re-lock mid-fight) keep theirs — resetting those would
	// drop a live hold.
	if (CombatTarget.Get() != Target)
	{
		LastSquadSightTime = -1e9f;
		LastTargetNoiseTime = -1e9f;
	}

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

	// Combat entry lifts the Normal-mode companion cloak for this enemy, but sight events swallowed
	// while cloaked left no track and perception only fires on edges — seed from what's already in
	// view so the enemy isn't sight-blind to a now-perceivable companion.
	SeedCompanionSightTracks();

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

	TimeWithoutSight = 0.f;

	if (const UWorld* World = GetWorld())
		LastCombatExitWorldTime = World->GetTimeSeconds();

	// Search dispersal for director-seeded squads: spread members on a ring around the
	// investigate point using a dense living-member rank. Nav-projected so an offset into
	// a wall falls back to the raw location rather than failing pathfinding silently.
	FVector SearchTarget = LastKnownLocation;
	if (bDirectorSeeded && IsValid(MyPawn) && IsValid(ArchetypeData))
	{
		const AEnemyCharacter* MyEnemy = Cast<AEnemyCharacter>(MyPawn);
		if (IsValid(MyEnemy))
		{
			UEnemySquadSubsystem* SquadSS = SquadSubsystem.Get();
			UEnemySquad* Squad = IsValid(SquadSS) ? SquadSS->GetSquadFor(MyEnemy) : nullptr;
			if (IsValid(Squad))
			{
				// Dense rank among living members — dead entries don't collapse angles.
				const TArray<TWeakObjectPtr<AEnemyCharacter>>& Members = Squad->GetMembers();
				int32 LivingRank = 0;
				int32 LivingTotal = 0;
				for (const TWeakObjectPtr<AEnemyCharacter>& M : Members)
				{
					if (!M.IsValid()) continue;
					UHealthComponent* HP = M->GetHealthComponent();
					if (!IsValid(HP) || HP->IsDead()) continue;
					if (M.Get() == MyEnemy) LivingRank = LivingTotal;
					++LivingTotal;
				}

				if (LivingTotal > 1)
				{
					const float PhaseJitter = FMath::Frac(static_cast<float>(Squad->GetSquadId().IsNone()
						? 0 : GetTypeHash(Squad->GetSquadId())) * 0.001f);
					// +0.5 rank offset so the search ring is rotated half a slot from
					// the approach ring — the enemy searches a new bearing, not the
					// corridor it just walked and cleared.
					const float Angle = ((static_cast<float>(LivingRank) + 0.5f) / LivingTotal + PhaseJitter) * 2.f * UE_PI;
					const float Radius = ArchetypeData->DirectorSeedSpreadRadius;
					FVector Candidate = LastKnownLocation + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.f);

					// Nav-project; on failure retry at half radius and rotated by pi
					// before falling back to the un-offset location — thin navmesh at
					// the full ring (doorways, balcony edges) would otherwise re-clump
					// several members onto the centre.
					UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
					const FVector NavExtent(200.f, 200.f, EnemyNavProjectZExtent);
					bool bProjected = false;
					if (IsValid(NavSys))
					{
						FNavLocation NavLoc;
						if (NavSys->ProjectPointToNavigation(Candidate, NavLoc, NavExtent))
						{
							SearchTarget = NavLoc.Location;
							bProjected = true;
						}
						else
						{
							// Retry: half radius, rotated pi
							const FVector Retry = LastKnownLocation + FVector(
								FMath::Cos(Angle + UE_PI) * Radius * 0.5f,
								FMath::Sin(Angle + UE_PI) * Radius * 0.5f, 0.f);
							if (NavSys->ProjectPointToNavigation(Retry, NavLoc, NavExtent))
							{
								SearchTarget = NavLoc.Location;
								bProjected = true;
							}
						}
					}
					// If both projections failed (or no NavSys), SearchTarget stays
					// at the un-offset LastKnownLocation — at least it is reachable.
				}
			}
		}
	}

	SetInvestigateLocation(SearchTarget);
	SetCombatTarget(nullptr);
	TimeSpentSearching = 0.f;
	bDirectorSeeded = false;
	SetState(EEnemyAwarenessState::Searching);
}

// --- Global Alert ---

void UEnemyAwarenessComponent::HandleGlobalAlertChanged(EGlobalAlertLevel OldLevel, EGlobalAlertLevel NewLevel)
{
	if (bStopped) return;
	if (IsOwnerIsolatedEncounter()) return;
	if (NewLevel != EGlobalAlertLevel::Loud) return;

	// Loud lifts the Normal-mode companion cloak for every non-isolated enemy — seed tracks for
	// companions already in view regardless of our state (the Unaware gate below only guards the
	// wake-up sweep).
	SeedCompanionSightTracks();

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

	if (NewState == EEnemyAwarenessState::Combat)
	{
		if (const UWorld* World = GetWorld())
			LastCombatEnterTime = World->GetTimeSeconds();
	}

	if (const AAIController* C = Cast<AAIController>(GetOwner()))
		UE_LOG(LogEnemyAI, Verbose, TEXT("[AWARE] %s: %s -> %s"),
			C->GetPawn() ? *C->GetPawn()->GetName() : TEXT("<no pawn>"),
			*UEnum::GetValueAsString(OldState), *UEnum::GetValueAsString(NewState));

	UBlackboardComponent* BB = BlackboardComp.Get();
	if (IsValid(BB))
		BB->SetValueAsEnum(AEnemyAIController::BB_AwarenessState, static_cast<uint8>(CurrentState));

	if (!IsOwnerIsolatedEncounter() && !bDebugForcedCombat)
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
	if (!IsValid(BB)) return;

	// Downed-player standoff: investigate points never lead inside the ring around a DBNO player —
	// clamp to the ring edge on this enemy's side. Single choke point for every stimulus path
	// (target-drop search, hearing/perception stimuli, squad sighting relay).
	FVector ClampedLoc = Location;
	const float Standoff = IsValid(ArchetypeData) ? ArchetypeData->DBNOStandoffRadius : 0.f;
	if (Standoff > 0.f)
	{
		if (const APawn* Downed = FindDownedPlayerPawn(this))
		{
			const FVector DownedLoc = Downed->GetActorLocation();
			if (FVector::DistSquared2D(ClampedLoc, DownedLoc) < FMath::Square(Standoff))
			{
				const AAIController* MyController = Cast<AAIController>(GetOwner());
				const APawn* MyPawn = MyController ? MyController->GetPawn() : nullptr;
				FVector Away = IsValid(MyPawn)
					? MyPawn->GetActorLocation() - DownedLoc
					: -Downed->GetActorForwardVector();
				Away.Z = 0.f;
				if (!Away.Normalize()) Away = -Downed->GetActorForwardVector().GetSafeNormal2D();
				ClampedLoc = DownedLoc + Away * Standoff;
				if (IsValid(MyPawn)) ClampedLoc.Z = MyPawn->GetActorLocation().Z;
			}
		}
	}

	BB->SetValueAsVector(AEnemyAIController::BB_InvestigateLocation, ClampedLoc);
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

float UEnemyAwarenessComponent::GetTimeSinceDamagedBy(const AActor* Pawn) const
{
	if (!Pawn) return BIG_NUMBER;
	const float* Stamp = DamageTimesByAttacker.Find(Pawn);
	if (!Stamp) return BIG_NUMBER;
	const UWorld* World = GetWorld();
	if (!World) return BIG_NUMBER;
	return World->GetTimeSeconds() - *Stamp;
}

bool UEnemyAwarenessComponent::HasLiveKnowledgeOf(const AActor* Actor, float MemorySeconds) const
{
	if (!IsValid(Actor)) return false;

	// SuspicionTracks keys on TWeakObjectPtr<AActor> while DamageTimesByAttacker keys on
	// TWeakObjectPtr<const AActor> — the two maps disagree, and TWeakObjectPtr's pointer constructor
	// is constrained to convertible pointers, so a const AActor* simply will not form the key here.
	// The cast is for the lookup only; the track is read through a const pointer. Do not "fix" the
	// key type instead — 14 sites write these tracks through non-const actors.
	const FSuspicionTrack* Track = SuspicionTracks.Find(const_cast<AActor*>(Actor));
	if (!Track) return false;

	// A cloaked companion's track is stale by definition (GetExtraKnownThreats drops it the same way).
	if (IsCompanionSightCloaked(Actor)) return false;

	if (Track->bSighted) return true;
	if (MemorySeconds <= 0.f) return false;

	const UWorld* World = GetWorld();
	if (!World) return false;
	return (World->GetTimeSeconds() - Track->LastStimulusTime) <= MemorySeconds;
}

void UEnemyAwarenessComponent::StampTrack(FSuspicionTrack& Track, const FVector& Location) const
{
	Track.LastStimulusLocation = Location;
	const UWorld* World = GetWorld();
	Track.LastStimulusTime = World ? static_cast<float>(World->GetTimeSeconds()) : Track.LastStimulusTime;
}

void UEnemyAwarenessComponent::GetExtraKnownThreats(const AActor* ExcludeTarget, int32 MaxCount,
	float MemorySeconds, TArray<FEnemyKnownThreat>& OutThreats) const
{
	OutThreats.Reset();
	if (MaxCount <= 0) return;

	const AAIController* MyController = Cast<AAIController>(GetOwner());
	const APawn* MyPawn = MyController ? MyController->GetPawn() : nullptr;
	if (!IsValid(MyPawn)) return;

	const UWorld* World = GetWorld();
	const float Now = World ? static_cast<float>(World->GetTimeSeconds()) : 0.f;

	OutThreats.Reserve(MaxCount + 2);
	for (const auto& Pair : SuspicionTracks)
	{
		AActor* Candidate = Pair.Key.Get();
		if (!IsValid(Candidate) || Candidate == ExcludeTarget) continue;
		if (!IsActorAlive(Candidate)) continue;
		if (!IsHostile(Candidate)) continue;
		if (IsCompanionSightCloaked(Candidate)) continue;

		const FSuspicionTrack& Track = Pair.Value;
		FEnemyKnownThreat Known;
		Known.Actor = Candidate;
		Known.bSighted = Track.bSighted;
		if (Track.bSighted)
			Known.Location = Candidate->GetActorLocation();
		else if (MemorySeconds > 0.f && (Now - Track.LastStimulusTime) <= MemorySeconds)
			Known.Location = Track.LastStimulusLocation; // frozen — honest knowledge
		else
			continue;

		OutThreats.Add(Known);
	}

	const FVector PawnLoc = MyPawn->GetActorLocation();
	OutThreats.Sort([PawnLoc](const FEnemyKnownThreat& A, const FEnemyKnownThreat& B)
	{
		return FVector::DistSquared(PawnLoc, A.Location) < FVector::DistSquared(PawnLoc, B.Location);
	});

	if (OutThreats.Num() > MaxCount)
		OutThreats.SetNum(MaxCount, EAllowShrinking::No);
}

void UEnemyAwarenessComponent::Bark(EBarkType Type) const
{
	const AAIController* MyController = Cast<AAIController>(GetOwner());
	const AEnemyCharacter* MyPawn = MyController ? Cast<AEnemyCharacter>(MyController->GetPawn()) : nullptr;
	if (!IsValid(MyPawn) || !IsValid(ArchetypeData) || !IsValid(ArchetypeData->BarkSet)) return;

	const UWorld* World = GetWorld();
	UBarkSubsystem* Barks = IsValid(World) ? World->GetSubsystem<UBarkSubsystem>() : nullptr;
	if (Barks)
		Barks->RequestBark(MyPawn, ArchetypeData->BarkSet, Type);
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

bool UEnemyAwarenessComponent::IsOwnerTakedownPocket() const
{
	const AAIController* C = Cast<AAIController>(GetOwner());
	const AEnemyCharacter* E = C ? Cast<AEnemyCharacter>(C->GetPawn()) : nullptr;
	return E && E->IsInTakedownVolume();
}

bool UEnemyAwarenessComponent::IsOwnerTakedownHushed() const
{
	const AAIController* C = Cast<AAIController>(GetOwner());
	const AEnemyCharacter* E = C ? Cast<AEnemyCharacter>(C->GetPawn()) : nullptr;
	return E && E->IsInTakedownWindow();
}

bool UEnemyAwarenessComponent::IsHostile(AActor* Actor) const
{
	const AAIController* MyController = Cast<AAIController>(GetOwner());
	if (!MyController) return false;

	const ETeamAttitude::Type Attitude = MyController->GetTeamAttitudeTowards(*Actor);
	return Attitude == ETeamAttitude::Hostile;
}

APawn* UEnemyAwarenessComponent::FindDownedPlayerPawn(const UObject* WorldContext)
{
	const UWorld* World = IsValid(WorldContext) ? WorldContext->GetWorld() : nullptr;
	if (!World) return nullptr;

	APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0);
	const IExtractionPlayerInterface* Player = Cast<IExtractionPlayerInterface>(Pawn);
	return (Player && Player->GetIsDBNO()) ? Pawn : nullptr;
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

void UEnemyAwarenessComponent::RefreshSearchRoomExposure()
{
	ACompanionCharacter* Companion = CachedPerceivedCompanion.Get();
	if (!IsValid(Companion)) return;

	const uint32 ExposureGeneration = Companion->GetActiveSearchRoomExposureGeneration();
	if (ExposureGeneration == 0 || ExposureGeneration == LastSeededSearchRoomExposureGeneration) return;

	const AAIController* MyController = Cast<AAIController>(GetOwner());
	const APawn* MyPawn = MyController ? MyController->GetPawn() : nullptr;
	if (!IsValid(MyPawn)
		|| !Companion->IsSearchRoomExposureObserverInScope(MyPawn->GetActorLocation()))
	{
		return;
	}

	LastSeededSearchRoomExposureGeneration = ExposureGeneration;
	SeedCompanionSightTracks();
}

void UEnemyAwarenessComponent::RefreshAlwaysCloakedCompanion()
{
	const ACompanionCharacter* Companion = AlwaysCloakedCompanion.Get();
	if (!IsValid(Companion)) return;
	if (Companion->IsAlwaysSightCloaked()) return;

	// The cloak has lifted — the VIP just armed. Every sight stimulus he emitted while cloaked was
	// dropped and perception won't fire again without a fresh LOS edge, so an enemy already in
	// Combat holds no track for him. Clearing the slot means this seeds exactly once per lift.
	AlwaysCloakedCompanion.Reset();
	SeedCompanionSightTracks();
}

void UEnemyAwarenessComponent::ApplySilentSearchRoomStartle(ACompanionCharacter* Companion,
	FSuspicionTrack& Track)
{
	if (!IsValid(Companion) || !IsValid(ArchetypeData)) return;

	const uint32 ExposureGeneration = Companion->GetActiveSearchRoomExposureGeneration();
	if (!Companion->HasSearchRoomExposureSilentStartle(ExposureGeneration)
		|| LastStartledSearchRoomExposureGeneration == ExposureGeneration)
	{
		return;
	}

	const AAIController* MyController = Cast<AAIController>(GetOwner());
	const APawn* MyPawn = MyController ? MyController->GetPawn() : nullptr;
	if (!IsValid(MyPawn)
		|| !Companion->IsSearchRoomExposureObserverInScope(MyPawn->GetActorLocation()))
	{
		return;
	}

	Track.Suspicion = SearchRoomExposure::ApplyStartleSuspicion(Track.Suspicion, 0.f,
		ArchetypeData->BreachStartleSuspicionFloor, NoiseSuspicionCap);
	LastStartledSearchRoomExposureGeneration = ExposureGeneration;
	UE_LOG(LogEnemyAI, Log, TEXT("[SEARCH EXPOSURE] %s quiet visual startle gen=%u suspicion=%.0f"),
		*GetNameSafe(GetOwner()), ExposureGeneration, Track.Suspicion);
}

void UEnemyAwarenessComponent::SeedCompanionSightTracks()
{
	AAIController* MyController = Cast<AAIController>(GetOwner());
	UAIPerceptionComponent* Perception = MyController ? MyController->GetPerceptionComponent() : nullptr;
	if (!Perception) return;

	TArray<AActor*> Perceived;
	Perception->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), Perceived);
	for (AActor* Actor : Perceived)
	{
		if (!IsValid(Actor) || !IsCompanionActor(Actor)) continue;
		if (IsCompanionSightCloaked(Actor)) continue;

		FSuspicionTrack& Track = SuspicionTracks.FindOrAdd(Actor);
		Track.bSighted = true;
		StampTrack(Track, Actor->GetActorLocation());
		ApplySilentSearchRoomStartle(Cast<ACompanionCharacter>(Actor), Track);
	}
}

AActor* UEnemyAwarenessComponent::FindDBNOHandoffCompanion()
{
	const AAIController* MyController = Cast<AAIController>(GetOwner());
	const APawn* MyPawn = MyController ? MyController->GetPawn() : nullptr;
	UWorld* World = GetWorld();
	if (!IsValid(MyPawn) || !IsValid(ArchetypeData) || !World) return nullptr;

	// Respect the DA lever: 0 = companion deprioritised out of selection entirely.
	if (ArchetypeData->CompanionThreatScoreMultiplier <= 0.f) return nullptr;

	// Nearest, not first-iterated: with the primary companion and the armed extraction VIP both
	// live candidates, actor-iteration order would otherwise decide whether the shooter turns on
	// the one at his feet or the one across the room.
	const FVector MyLoc = MyPawn->GetActorLocation();
	ACompanionCharacter* Nearest = nullptr;
	float NearestDistSq = FMath::Square(ArchetypeData->SightRadius);

	for (TActorIterator<ACompanionCharacter> It(World); It; ++It)
	{
		ACompanionCharacter* Companion = *It;
		if (!IsValid(Companion) || !IsActorAlive(Companion)) continue;
		if (!IsHostile(Companion)) continue;
		if (IsCompanionSightCloaked(Companion)) continue;

		const float DistSq = FVector::DistSquared(MyLoc, Companion->GetActorLocation());
		if (DistSq > NearestDistSq) continue;

		Nearest = Companion;
		NearestDistSq = DistSq;
	}

	if (!Nearest) return nullptr;

	// Unsighted handoff: stamp the track at the companion's current position so EnterCombat's
	// last-known/contact-hold machinery drives the hunt (bSighted stays false until perception
	// genuinely sees it).
	FSuspicionTrack& Track = SuspicionTracks.FindOrAdd(Nearest);
	StampTrack(Track, Nearest->GetActorLocation());

	UE_LOG(LogEnemyAI, Log, TEXT("[AWARENESS] %s DBNO handoff -> %s (dist=%.0f sighted=%d)"),
		*MyPawn->GetName(), *Nearest->GetName(),
		FMath::Sqrt(NearestDistSq), (int32)Track.bSighted);
	return Nearest;
}

AActor* UEnemyAwarenessComponent::FindDBNOHandoffPlayer()
{
	const AAIController* MyController = Cast<AAIController>(GetOwner());
	const APawn* MyPawn = MyController ? MyController->GetPawn() : nullptr;
	UWorld* World = GetWorld();
	if (!IsValid(MyPawn) || !IsValid(ArchetypeData) || !World) return nullptr;

	// IsActorAlive is false for a DBNO player too — both down = no handoff (the DBNO search
	// hold keeps the enemy in overwatch instead).
	APawn* Player = UGameplayStatics::GetPlayerPawn(World, 0);
	if (!IsValid(Player) || !IsActorAlive(Player)) return nullptr;
	if (!IsHostile(Player)) return nullptr;
	if (FVector::DistSquared(MyPawn->GetActorLocation(), Player->GetActorLocation()) > FMath::Square(ArchetypeData->SightRadius)) return nullptr;

	// Unsighted handoff, same as the companion mirror: stamp the track at the player's current
	// position so the enemy hunts toward it (bSighted stays false until perception genuinely sees).
	FSuspicionTrack& Track = SuspicionTracks.FindOrAdd(Player);
	StampTrack(Track, Player->GetActorLocation());

	UE_LOG(LogEnemyAI, Log, TEXT("[AWARENESS] %s companion-DBNO handoff -> %s (dist=%.0f sighted=%d)"),
		*MyPawn->GetName(), *Player->GetName(),
		FVector::Dist(MyPawn->GetActorLocation(), Player->GetActorLocation()), (int32)Track.bSighted);
	return Player;
}

bool UEnemyAwarenessComponent::ShouldIgnoreCompanionStimulus(const AActor* Actor) const
{
	return IsCompanionSightCloaked(Actor);
}

bool UEnemyAwarenessComponent::IsCompanionSightCloaked(const AActor* Actor) const
{
	const ACompanionCharacter* Companion = Cast<const ACompanionCharacter>(Actor);
	if (!Companion) return false;

	// The captive/unarmed extraction VIP is never perceivable — no room exposure, alert level or
	// in-Combat state lifts this, so it sits above every rule below.
	if (Companion->IsAlwaysSightCloaked()) return true;

	const AAIController* MyController = Cast<AAIController>(GetOwner());
	const APawn* MyPawn = MyController ? MyController->GetPawn() : nullptr;
	if (IsValid(MyPawn)
		&& Companion->IsSearchRoomExposureObserverInScope(MyPawn->GetActorLocation()))
	{
		return false;
	}

	switch (Companion->GetMode())
	{
	case ECompanionMode::Combat:
		return false; // guns blazing — fully perceivable at all times
	case ECompanionMode::Stealth:
		if (Companion->IsStealthActive())
			return true; // invisible even to in-Combat enemies until stealth breaks
		break; // broken stealth falls through to the Normal fight-on rules
	default:
		break;
	}

	// Normal rules: cloaked until the fight is on — this enemy fighting, or the level gone Loud
	// (player spotted or firing raises the global alert). Isolated encounters ignore the alert.
	if (CurrentState == EEnemyAwarenessState::Combat) return false;
	if (!IsOwnerIsolatedEncounter())
		if (const UEnemyDirectorSubsystem* Dir = Director.Get())
			if (Dir->GetAlertLevel() == EGlobalAlertLevel::Loud) return false;
	return true;
}

bool UEnemyAwarenessComponent::IsCompanionFireInaudible(const AActor* Actor) const
{
	const ACompanionCharacter* Companion = Cast<const ACompanionCharacter>(Actor);
	if (!Companion) return false;

	// Stealth: gunfire is audible — a suppressor is what buys the silence.
	if (Companion->GetMode() == ECompanionMode::Stealth && Companion->IsStealthActive())
		return Companion->IsCurrentWeaponSuppressed();

	// Normal's pre-fight cloak silences fire too; Combat mode is always audible.
	return IsCompanionSightCloaked(Actor);
}

bool UEnemyAwarenessComponent::CanSelectCompanionTarget(const AActor* Candidate, const FSuspicionTrack& Track, float WorldTime) const
{
	if (!IsCompanionActor(Candidate)) return true;
	if (IsCompanionSightCloaked(Candidate)) return false;
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
	const FVector MyLoc = MyPawn->GetActorLocation();

	// Point-blank override (self-preservation beats orders): when any sighted, selectable hostile
	// stands inside PointBlankTargetRange, selection restricts to those candidates. Squad focus and
	// a distant attacker's recent-damage term can't hold the target on someone far away while a
	// hostile is in our face. Scoring stays normal INSIDE the set so two close hostiles don't
	// flicker; an incumbent outside the set never posts an IncumbentScore, so hysteresis can't
	// protect it either.
	const float PointBlankRange = ArchetypeData->PointBlankTargetRange;
	auto IsPointBlank = [&](const AActor* Candidate, const FSuspicionTrack& Track)
	{
		return PointBlankRange > 0.f && Track.bSighted
			&& FVector::Dist(MyLoc, Candidate->GetActorLocation()) <= PointBlankRange;
	};

	bool bRestrictToPointBlank = false;
	if (PointBlankRange > 0.f)
	{
		for (const auto& Pair : SuspicionTracks)
		{
			AActor* Candidate = Pair.Key.Get();
			if (!IsValid(Candidate)) continue;
			if (!IsActorAlive(Candidate)) continue;
			if (!IsHostile(Candidate)) continue;
			if (!CanSelectCompanionTarget(Candidate, Pair.Value, WorldTime)) continue;
			if (IsPointBlank(Candidate, Pair.Value)) { bRestrictToPointBlank = true; break; }
		}
	}

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
						&& CanSelectCompanionTarget(FocusTarget, *FocusTrack, WorldTime)
						&& (!bRestrictToPointBlank || IsPointBlank(FocusTarget, *FocusTrack)))
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
		if (bRestrictToPointBlank && !IsPointBlank(Candidate, Track)) continue;

		const float Dist = FVector::Dist(MyLoc, Candidate->GetActorLocation());
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

	Squad->ReportSighting(Target, LastKnownLocation, MyChar);
}

// --- Squad Sighting Ingress ---

void UEnemyAwarenessComponent::ReportSquadSighting(AActor* Target, const FVector& LastKnown)
{
	if (bStopped) return;
	if (!IsValid(Target)) return;
	if (ShouldIgnoreCompanionStimulus(Target)) return;
	// A hushed enemy ignores squad chatter too — a squadmate merely Searching must not wake the target
	// the companion is mid-takedown on. Only for the duration of the armed window: outside it a pocket
	// takes squad relay like anyone else. A real firefight raises the Loud alert, which still wakes it
	// via HandleGlobalAlertChanged.
	if (IsOwnerTakedownHushed()) return;

	// Guard: set flag to prevent re-broadcast from any combat-entry path this call triggers
	TGuardValue<bool> RelayGuard(bInSquadSightingRelay, true);

	if (CurrentState == EEnemyAwarenessState::Combat)
	{
		if (CombatTarget.Get() == Target)
		{
			LastKnownLocation = LastKnown;
			// A mate is live-sighting our target right now — stamp the fight-liveness hold so
			// our own lost-contact clock doesn't run while the squad still has him.
			if (const UWorld* W = GetWorld()) LastSquadSightTime = W->GetTimeSeconds();
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
