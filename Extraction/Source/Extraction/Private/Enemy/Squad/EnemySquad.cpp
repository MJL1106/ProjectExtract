#include "EnemySquad.h"
#include "EnemyCharacter.h"
#include "EnemyArchetypeData.h"
#include "EnemyAIController.h"
#include "EnemyAwarenessComponent.h"
#include "EnemyMoraleComponent.h"
#include "EnemySquadSubsystem.h"
#include "HealthComponent.h"
#include "BarkSubsystem.h"
#include "EnemyDebug.h"
#include "WeaponBase.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Engine/Engine.h"

// ---------------------------------------------------------------------------
// Membership
// ---------------------------------------------------------------------------

void UEnemySquad::AddMember(AEnemyCharacter* Member)
{
	if (!IsValid(Member)) return;

	for (const TWeakObjectPtr<AEnemyCharacter>& Existing : Members)
	{
		if (Existing.Get() == Member) return;
	}

	Members.Add(Member);
	UE_LOG(LogEnemySquad, Verbose, TEXT("[%s] AddMember: %s (count=%d)"), *SquadId.ToString(), *Member->GetName(), Members.Num());
}

void UEnemySquad::RemoveMember(AEnemyCharacter* Member)
{
	if (!Member) return;

	Members.RemoveAll([Member](const TWeakObjectPtr<AEnemyCharacter>& W)
	{
		return !W.IsValid() || W.Get() == Member;
	});
}

int32 UEnemySquad::NumAlive() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<AEnemyCharacter>& M : Members)
	{
		if (IsMemberAlive(M)) ++Count;
	}
	return Count;
}

bool UEnemySquad::HasLivingOfficer() const
{
	return IsValid(GetOfficer());
}

AEnemyCharacter* UEnemySquad::GetOfficer() const
{
	for (const TWeakObjectPtr<AEnemyCharacter>& M : Members)
	{
		if (!IsMemberAlive(M)) continue;

		const UEnemyArchetypeData* DA = M->GetArchetypeData();
		if (IsValid(DA) && DA->bHasCommandAura) return M.Get();
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// Shared sightings
// ---------------------------------------------------------------------------

void UEnemySquad::ReportSighting(AActor* Target, const FVector& LastKnown)
{
	if (!IsValid(Target)) return;

	UWorld* World = GetWorld();
	if (!IsValid(World)) return;

	const float Now = World->GetTimeSeconds();
	if (Now - LastSightingRelayTime < SightingRelayInterval) return;

	// First squad-wide alert of this engagement (no prior shared target) — the officer (else the
	// first alive member) calls the contact for the whole squad. Content-gated: only bark sets
	// carrying CallingContact lines actually speak.
	const bool bFirstAlert = !SquadTarget.IsValid();

	LastSightingRelayTime = Now;
	SquadTarget = Target;
	SquadLastKnown = LastKnown;

	if (bFirstAlert)
	{
		AEnemyCharacter* Caller = GetOfficer();
		if (!IsValid(Caller))
			for (const TWeakObjectPtr<AEnemyCharacter>& M : Members)
				if (IsMemberAlive(M)) { Caller = M.Get(); break; }

		const UEnemyArchetypeData* CallerDA = IsValid(Caller) ? Caller->GetArchetypeData() : nullptr;
		UBarkSubsystem* Barks = World->GetSubsystem<UBarkSubsystem>();
		if (IsValid(CallerDA) && IsValid(CallerDA->BarkSet) && Barks && TryClaimSquadBark(EBarkType::CallingContact))
			Barks->RequestBark(Caller, CallerDA->BarkSet, EBarkType::CallingContact);
	}

	for (const TWeakObjectPtr<AEnemyCharacter>& M : Members)
	{
		if (!IsMemberAlive(M)) continue;

		AEnemyAIController* AIC = Cast<AEnemyAIController>(M->GetController());
		if (!IsValid(AIC)) continue;

		UEnemyAwarenessComponent* Awareness = AIC->GetAwarenessComponent();
		if (!IsValid(Awareness)) continue;

		Awareness->ReportSquadSighting(Target, LastKnown);
	}
}

void UEnemySquad::ForceEngage(AActor* Target, const FVector& LastKnown)
{
	if (!IsValid(Target)) return;

	SquadTarget = Target;
	SquadLastKnown = LastKnown;

	for (const TWeakObjectPtr<AEnemyCharacter>& M : Members)
	{
		if (!IsMemberAlive(M)) continue;

		AEnemyAIController* AIC = Cast<AEnemyAIController>(M->GetController());
		if (!IsValid(AIC)) continue;

		UEnemyAwarenessComponent* Awareness = AIC->GetAwarenessComponent();
		if (!IsValid(Awareness)) continue;

		Awareness->ForceEngage(Target);
	}
}

// ---------------------------------------------------------------------------
// Role tokens
// ---------------------------------------------------------------------------

bool UEnemySquad::TryClaimRole(EEnemySquadRole Role, AEnemyCharacter* Claimant)
{
	if (!IsValid(Claimant)) return false;

	TWeakObjectPtr<AEnemyCharacter>* Holder = RoleHolders.Find(Role);
	if (Holder)
	{
		if (Holder->Get() == Claimant) return true;
		if (IsMemberAlive(*Holder)) return false;
		RoleHolders.Remove(Role);
	}

	RoleHolders.Add(Role, Claimant);
	UE_LOG(LogEnemySquad, Verbose, TEXT("[%s] %s claimed role %d"), *SquadId.ToString(), *Claimant->GetName(), static_cast<int32>(Role));
	return true;
}

void UEnemySquad::ReleaseRole(EEnemySquadRole Role, AEnemyCharacter* Claimant)
{
	TWeakObjectPtr<AEnemyCharacter>* Holder = RoleHolders.Find(Role);
	if (!Holder) return;
	if (Holder->Get() != Claimant) return;

	RoleHolders.Remove(Role);
	UE_LOG(LogEnemySquad, Verbose, TEXT("[%s] %s released role %d"), *SquadId.ToString(), *GetNameSafe(Claimant), static_cast<int32>(Role));
}

AEnemyCharacter* UEnemySquad::GetRoleHolder(EEnemySquadRole Role) const
{
	const TWeakObjectPtr<AEnemyCharacter>* Holder = RoleHolders.Find(Role);
	if (!Holder) return nullptr;

	if (!IsMemberAlive(*Holder))
	{
		RoleHolders.Remove(Role);
		return nullptr;
	}

	return Holder->Get();
}

// ---------------------------------------------------------------------------
// Rush tokens
// ---------------------------------------------------------------------------

void UEnemySquad::PruneRushTokens() const
{
	for (TWeakObjectPtr<AEnemyCharacter>& Holder : RushTokenHolders)
	{
		if (Holder.IsValid() && !IsMemberAlive(Holder)) Holder.Reset();
	}
	while (RushTokenHolders.Num() > 0 && !RushTokenHolders.Last().IsValid())
		RushTokenHolders.Pop();
}

bool UEnemySquad::TryClaimRushToken(AEnemyCharacter* Claimant, int32 MaxTokens)
{
	if (!IsValid(Claimant)) return false;

	PruneRushTokens();

	if (RushTokenHolders.Contains(Claimant)) return true;

	int32 HeldCount = 0;
	int32 FreeSlot = INDEX_NONE;
	for (int32 i = 0; i < RushTokenHolders.Num(); ++i)
	{
		if (RushTokenHolders[i].IsValid()) ++HeldCount;
		else if (FreeSlot == INDEX_NONE) FreeSlot = i;
	}
	if (HeldCount >= MaxTokens) return false;

	if (FreeSlot != INDEX_NONE) RushTokenHolders[FreeSlot] = Claimant;
	else RushTokenHolders.Add(Claimant);

	UE_LOG(LogEnemySquad, Verbose, TEXT("[%s] %s claimed rush token (%d/%d held)"),
		*SquadId.ToString(), *Claimant->GetName(), HeldCount + 1, MaxTokens);
	return true;
}

void UEnemySquad::ReleaseRushToken(AEnemyCharacter* Claimant)
{
	const int32 Slot = RushTokenHolders.IndexOfByKey(Claimant);
	if (Slot == INDEX_NONE) return;

	RushTokenHolders[Slot].Reset();
	PruneRushTokens();
	UE_LOG(LogEnemySquad, Verbose, TEXT("[%s] %s released rush token"), *SquadId.ToString(), *GetNameSafe(Claimant));
}

bool UEnemySquad::IsRushTokenAvailable(const AEnemyCharacter* Claimant, int32 MaxTokens) const
{
	PruneRushTokens();

	int32 HeldCount = 0;
	for (const TWeakObjectPtr<AEnemyCharacter>& Holder : RushTokenHolders)
	{
		if (!Holder.IsValid()) continue;
		if (Holder.Get() == Claimant) return true;
		++HeldCount;
	}
	return HeldCount < MaxTokens;
}

int32 UEnemySquad::GetRushSlotIndex(const AEnemyCharacter* Claimant) const
{
	PruneRushTokens();
	return RushTokenHolders.IndexOfByKey(Claimant);
}

// ---------------------------------------------------------------------------
// Focus fire
// ---------------------------------------------------------------------------

void UEnemySquad::SetFocusTarget(AActor* Target, AEnemyCharacter* Caller, bool bOfficerCommand)
{
	if (!IsValid(Target)) return;

	if (bOfficerCommand)
	{
		FocusTarget = Target;
		bFocusSetByOfficer = true;
		UE_LOG(LogEnemySquad, Verbose, TEXT("[%s] FocusTarget set to %s (officer command)"), *SquadId.ToString(), *Target->GetName());
		return;
	}

	if (FocusTarget.IsValid()) return;

	FocusTarget = Target;
	bFocusSetByOfficer = false;
	UE_LOG(LogEnemySquad, Verbose, TEXT("[%s] FocusTarget set to %s (non-officer)"), *SquadId.ToString(), *Target->GetName());
}

AActor* UEnemySquad::GetFocusTarget() const
{
	if (!FocusTarget.IsValid())
	{
		bFocusSetByOfficer = false;
		return nullptr;
	}

	AActor* Target = FocusTarget.Get();
	if (!IsValid(Target))
	{
		bFocusSetByOfficer = false;
		return nullptr;
	}

	return Target;
}

void UEnemySquad::ClearFocusTarget()
{
	FocusTarget.Reset();
	bFocusSetByOfficer = false;
}

// ---------------------------------------------------------------------------
// Morale relay
// ---------------------------------------------------------------------------

void UEnemySquad::NotifyMemberDied(AEnemyCharacter* Dead, bool bWasOfficer)
{
	if (bWasOfficer) ClearFocusTarget();

	if (bBoundingActive)
	{
		if (bWasOfficer || Dead == BoundingSuppressor.Get() || Dead == BoundingFlanker.Get())
		{
			StopBounding(bWasOfficer ? TEXT("OfficerDied") : TEXT("ManeuverMemberDied"));
		}
	}

	if (!AnyMemberAwareAtOrAbove(EEnemyAwarenessState::Searching, Dead)) return;

	for (const TWeakObjectPtr<AEnemyCharacter>& M : Members)
	{
		if (!IsMemberAlive(M)) continue;
		if (M.Get() == Dead) continue;

		UEnemyMoraleComponent* Morale = M->GetMoraleComponent();
		if (IsValid(Morale))
		{
			Morale->NotifySquadAllyDied(bWasOfficer);
		}
	}
}

void UEnemySquad::Rally(AEnemyCharacter* Officer)
{
	if (!IsValid(Officer)) return;

	const UEnemyArchetypeData* DA = Officer->GetArchetypeData();
	if (!IsValid(DA)) return;

	const float MoraleBoost = DA->RallyMoraleBoost;
	const float FloorRaise = DA->RallyFloorRaise;

	for (const TWeakObjectPtr<AEnemyCharacter>& M : Members)
	{
		if (!IsMemberAlive(M)) continue;
		if (M.Get() == Officer) continue;

		UEnemyMoraleComponent* Morale = M->GetMoraleComponent();
		if (IsValid(Morale))
		{
			Morale->NotifyRally(MoraleBoost, FloorRaise);
		}
	}

	UE_LOG(LogEnemySquad, Log, TEXT("[%s] Officer %s rallied squad (boost=%.0f, floor+=%.0f)"), *SquadId.ToString(), *Officer->GetName(), MoraleBoost, FloorRaise);
}

// ---------------------------------------------------------------------------
// Flank attempt tracking
// ---------------------------------------------------------------------------

float UEnemySquad::GetLastFlankAttemptTime(const AEnemyCharacter* Member) const
{
	if (!Member) return -1e9f;

	const float* Time = FlankAttemptTimes.Find(Member);
	return Time ? *Time : -1e9f;
}

void UEnemySquad::RecordFlankAttempt(const AEnemyCharacter* Member)
{
	if (!Member) return;

	UWorld* World = GetWorld();
	if (!IsValid(World)) return;

	FlankAttemptTimes.FindOrAdd(Member) = World->GetTimeSeconds();

	// Opportunistic prune: drop stale entries
	if (FlankAttemptTimes.Num() > Members.Num() * 2)
	{
		for (auto It = FlankAttemptTimes.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid()) It.RemoveCurrent();
		}
	}
}

// ---------------------------------------------------------------------------
// Awareness gate
// ---------------------------------------------------------------------------

bool UEnemySquad::AnyMemberAwareAtOrAbove(EEnemyAwarenessState MinState, const AEnemyCharacter* Exclude) const
{
	for (const TWeakObjectPtr<AEnemyCharacter>& M : Members)
	{
		if (!IsMemberAlive(M)) continue;
		if (M.Get() == Exclude) continue;

		AEnemyAIController* AIC = Cast<AEnemyAIController>(M->GetController());
		if (!IsValid(AIC)) continue;

		UEnemyAwarenessComponent* Awareness = AIC->GetAwarenessComponent();
		if (!IsValid(Awareness)) continue;

		if (Awareness->GetAwarenessState() >= MinState) return true;
	}
	return false;
}

// ---------------------------------------------------------------------------
// Advance staggering
// ---------------------------------------------------------------------------

bool UEnemySquad::CanClaimAdvanceWindow() const
{
	const UWorld* World = GetWorld();
	if (!IsValid(World)) return true;
	return (World->GetTimeSeconds() - LastAdvanceWorldTime) >= AdvanceWindowSeconds;
}

void UEnemySquad::RecordAdvance()
{
	const UWorld* World = GetWorld();
	if (IsValid(World)) LastAdvanceWorldTime = World->GetTimeSeconds();
}

// ---------------------------------------------------------------------------
// Bark dedup
// ---------------------------------------------------------------------------

bool UEnemySquad::TryClaimSquadBark(EBarkType Type, float Window)
{
	UWorld* World = GetWorld();
	if (!IsValid(World)) return true;

	const float Now = World->GetTimeSeconds();
	float* LastTime = LastSquadBarkTime.Find(Type);
	if (LastTime && (Now - *LastTime) < Window)
	{
		if (IsEnemyBarkDebugEnabled())
		{
			const float Since = Now - *LastTime;
			const FString TypeStr = UEnum::GetValueAsString(Type);
			UE_LOG(LogEnemyBark, Log, TEXT("DROP SquadClaim type=%s (since=%.2fs < window=%.2fs)"), *TypeStr, Since, Window);
			if (GEngine)
			{
				const FString Msg = FString::Printf(TEXT("DROP SquadClaim %s (%.1fs<%.1fs)"), *TypeStr, Since, Window);
				GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Silver, Msg);
			}
		}
		return false;
	}

	LastSquadBarkTime.FindOrAdd(Type) = Now;
	return true;
}

// ---------------------------------------------------------------------------
// Bounding overwatch (Phase 7)
// ---------------------------------------------------------------------------

bool UEnemySquad::StartBounding(AEnemyCharacter* Officer)
{
	if (!IsValid(Officer)) return false;
	if (bBoundingActive) return false;

	const UEnemyArchetypeData* DA = Officer->GetArchetypeData();
	if (!IsValid(DA) || !DA->bHasCommandAura) return false;

	if (!SquadTarget.IsValid()) return false;

	const int32 MinOtherMembers = FMath::Max(2, DA->BoundingMinSquadSize - 1);
	int32 EligibleCount = 0;
	for (const TWeakObjectPtr<AEnemyCharacter>& M : Members)
	{
		if (M.Get() == Officer) continue;
		if (IsEligibleForManeuver(M)) ++EligibleCount;
	}
	if (EligibleCount < MinOtherMembers) return false;

	AEnemyCharacter* Suppressor = PickBoundingCandidate(true, nullptr);
	if (!IsValid(Suppressor)) return false;

	AEnemyCharacter* Flanker = PickBoundingCandidate(false, Suppressor);
	if (!IsValid(Flanker)) return false;

	if (!TryClaimRole(EEnemySquadRole::Suppressor, Suppressor)) return false;
	if (!TryClaimRole(EEnemySquadRole::Flanker, Flanker))
	{
		ReleaseRole(EEnemySquadRole::Suppressor, Suppressor);
		return false;
	}

	bBoundingActive = true;
	BoundingOfficer = Officer;
	BoundingSuppressor = Suppressor;
	BoundingFlanker = Flanker;

	PushManeuverRoleToBB(Suppressor, EEnemyManeuverRole::Suppressor);
	PushManeuverRoleToBB(Flanker, EEnemyManeuverRole::Flanker);

	UE_LOG(LogEnemySquad, Log, TEXT("[%s] Bounding started — Suppressor=%s, Flanker=%s"),
		*SquadId.ToString(), *Suppressor->GetName(), *Flanker->GetName());

	return true;
}

void UEnemySquad::StopBounding(const TCHAR* Reason)
{
	if (!bBoundingActive) return;

	AEnemyCharacter* Supp = BoundingSuppressor.Get();
	AEnemyCharacter* Flank = BoundingFlanker.Get();

	if (IsValid(Supp))
	{
		StampManeuverHold(Supp);
		ReleaseRole(EEnemySquadRole::Suppressor, Supp);
		PushManeuverRoleToBB(Supp, EEnemyManeuverRole::None);
	}
	if (IsValid(Flank))
	{
		StampManeuverHold(Flank);
		ReleaseRole(EEnemySquadRole::Flanker, Flank);
		PushManeuverRoleToBB(Flank, EEnemyManeuverRole::None);
	}

	bBoundingActive = false;
	bSuppressorEngaged = false;
	BoundingOfficer.Reset();
	BoundingSuppressor.Reset();
	BoundingFlanker.Reset();

	UE_LOG(LogEnemySquad, Log, TEXT("[%s] Bounding stopped — Reason=%s"), *SquadId.ToString(), Reason);
}

bool UEnemySquad::IsBoundingActive()
{
	if (!bBoundingActive) return false;

	if (!SquadTarget.IsValid() || !BoundingSuppressor.IsValid() || !BoundingFlanker.IsValid())
	{
		StopBounding(TEXT("StaleState"));
		return false;
	}

	return true;
}

bool UEnemySquad::IsSuppressionLive() const
{
	if (!bBoundingActive) return false;
	if (!bSuppressorEngaged) return false;
	if (!BoundingSuppressor.IsValid()) return false;

	AEnemyCharacter* Supp = BoundingSuppressor.Get();
	if (!IsValid(Supp)) return false;

	UHealthComponent* Health = Supp->GetHealthComponent();
	if (!IsValid(Health) || Health->IsDead()) return false;

	AWeaponBase* Weapon = Supp->GetCurrentWeapon();
	if (!IsValid(Weapon)) return false;
	if (Weapon->IsReloading()) return false;

	return true;
}

bool UEnemySquad::IsSuppressionHolding() const
{
	if (!bBoundingActive) return false;
	if (!bSuppressorEngaged) return false;
	if (!BoundingSuppressor.IsValid()) return false;

	AEnemyCharacter* Supp = BoundingSuppressor.Get();
	if (!IsValid(Supp)) return false;

	UHealthComponent* Health = Supp->GetHealthComponent();
	if (!IsValid(Health) || Health->IsDead()) return false;

	AWeaponBase* Weapon = Supp->GetCurrentWeapon();
	return IsValid(Weapon) && Weapon->IsReloading();
}

void UEnemySquad::SetSuppressorEngaged(AEnemyCharacter* Who, bool bEngaged)
{
	if (Who != BoundingSuppressor.Get()) return;
	bSuppressorEngaged = bEngaged;
}

void UEnemySquad::NotifyFlankerArrived(AEnemyCharacter* Flanker)
{
	if (!bBoundingActive) return;
	if (bSwapInProgress) return;
	if (Flanker != BoundingFlanker.Get()) return;

	UWorld* World = GetWorld();
	if (!World) return;

	const float Now = World->GetTimeSeconds();
	if (Now - LastSwapTime < MinSwapInterval) return;

	bSwapInProgress = true;

	AEnemyCharacter* OldSupp = BoundingSuppressor.Get();
	AEnemyCharacter* OldFlank = BoundingFlanker.Get();

	if (!IsValid(OldSupp) || !IsValid(OldFlank))
	{
		bSwapInProgress = false;
		StopBounding(TEXT("SwapFailed_InvalidMember"));
		return;
	}

	ReleaseRole(EEnemySquadRole::Suppressor, OldSupp);
	ReleaseRole(EEnemySquadRole::Flanker, OldFlank);

	if (!TryClaimRole(EEnemySquadRole::Suppressor, OldFlank) ||
		!TryClaimRole(EEnemySquadRole::Flanker, OldSupp))
	{
		bSwapInProgress = false;
		StopBounding(TEXT("SwapFailed_TokenConflict"));
		return;
	}

	BoundingSuppressor = OldFlank;
	BoundingFlanker = OldSupp;
	bSuppressorEngaged = false;
	LastSwapTime = Now;

	PushManeuverRoleToBB(OldFlank, EEnemyManeuverRole::Suppressor);
	PushManeuverRoleToBB(OldSupp, EEnemyManeuverRole::Flanker);

	UBarkSubsystem* BarkSub = World->GetSubsystem<UBarkSubsystem>();
	if (IsValid(BarkSub))
	{
		const UEnemyArchetypeData* SuppDA = OldFlank->GetArchetypeData();
		if (IsValid(SuppDA) && SuppDA->BarkSet != nullptr && TryClaimSquadBark(EBarkType::CoveringGo))
		{
			BarkSub->RequestBark(OldFlank, SuppDA->BarkSet, EBarkType::CoveringGo);
		}
	}

	UE_LOG(LogEnemySquad, Log, TEXT("[%s] Bounding swapped — Suppressor=%s, Flanker=%s"),
		*SquadId.ToString(), *OldFlank->GetName(), *OldSupp->GetName());

	bSwapInProgress = false;
}

void UEnemySquad::RecordBoundingAttempt()
{
	UWorld* World = GetWorld();
	if (World) LastBoundingAttemptTime = World->GetTimeSeconds();
}

void UEnemySquad::NotifyManeuverMemberBlocked(AEnemyCharacter* Member)
{
	if (!bBoundingActive) return;
	if (Member != BoundingFlanker.Get() && Member != BoundingSuppressor.Get()) return;

	StopBounding(TEXT("MemberBlocked"));
}

void UEnemySquad::PushManeuverRoleToBB(AEnemyCharacter* Member, EEnemyManeuverRole Role)
{
	if (!IsValid(Member)) return;

	AEnemyAIController* AIC = Cast<AEnemyAIController>(Member->GetController());
	if (!IsValid(AIC)) return;

	AIC->SetManeuverRole(Role);
}

void UEnemySquad::StampManeuverHold(AEnemyCharacter* Member)
{
	if (!IsValid(Member)) return;

	const UEnemyArchetypeData* DA = Member->GetArchetypeData();
	if (!IsValid(DA) || DA->ManeuverDisengageHoldTime <= 0.f) return;

	AEnemyAIController* AIC = Cast<AEnemyAIController>(Member->GetController());
	if (!IsValid(AIC)) return;

	UBlackboardComponent* BB = AIC->GetBlackboardComponent();
	if (!IsValid(BB)) return;

	UWorld* World = GetWorld();
	if (!World) return;

	BB->SetValueAsFloat(AEnemyAIController::BB_ManeuverHoldUntil,
		World->GetTimeSeconds() + DA->ManeuverDisengageHoldTime);
}

AEnemyCharacter* UEnemySquad::PickBoundingCandidate(bool bPreferLOS, AEnemyCharacter* Exclude) const
{
	AEnemyCharacter* Best = nullptr;
	float BestScore = -1.f;
	const FVector TargetLoc = SquadLastKnown;
	AEnemyCharacter* Officer = GetOfficer();

	for (const TWeakObjectPtr<AEnemyCharacter>& M : Members)
	{
		if (!IsEligibleForManeuver(M)) continue;

		AEnemyCharacter* Candidate = M.Get();
		if (Candidate == Exclude) continue;
		if (Candidate == Officer) continue;

		float Score = 0.f;

		if (bPreferLOS)
		{
			AEnemyAIController* AIC = Cast<AEnemyAIController>(Candidate->GetController());
			if (IsValid(AIC))
			{
				UBlackboardComponent* BB = AIC->GetBlackboardComponent();
				if (IsValid(BB) && BB->GetValueAsBool(AEnemyAIController::BB_HasLineOfSight))
				{
					Score += 100.f;
				}
			}
			float Dist = FVector::Dist(Candidate->GetActorLocation(), TargetLoc);
			Score += FMath::Max(0.f, 5000.f - Dist);
		}
		else
		{
			UHealthComponent* Health = Candidate->GetHealthComponent();
			if (IsValid(Health)) Score += Health->GetHealthPercent() * 100.f;
		}

		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Candidate;
		}
	}

	return Best;
}

bool UEnemySquad::IsEligibleForManeuver(const TWeakObjectPtr<AEnemyCharacter>& Member) const
{
	if (!IsMemberAlive(Member)) return false;

	const UEnemyArchetypeData* DA = Member->GetArchetypeData();
	if (!IsValid(DA)) return false;
	if (DA->Archetype != EEnemyArchetype::Grunt && DA->Archetype != EEnemyArchetype::Pistol) return false;

	AEnemyAIController* AIC = Cast<AEnemyAIController>(Member->GetController());
	if (!IsValid(AIC)) return false;

	UEnemyAwarenessComponent* Awareness = AIC->GetAwarenessComponent();
	if (!IsValid(Awareness) || Awareness->GetAwarenessState() != EEnemyAwarenessState::Combat) return false;

	UBlackboardComponent* BB = AIC->GetBlackboardComponent();
	if (!IsValid(BB) || !BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget)) return false;

	// Morale gate: only Confident enemies are eligible for bounding maneuvers.
	UEnemyMoraleComponent* MoraleComp = Member->GetMoraleComponent();
	if (!IsValid(MoraleComp) || MoraleComp->GetMoraleState() != EMoraleState::Confident) return false;

	return true;
}

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------

bool UEnemySquad::IsMemberAlive(const TWeakObjectPtr<AEnemyCharacter>& Member)
{
	if (!Member.IsValid()) return false;

	AEnemyCharacter* Enemy = Member.Get();
	if (!IsValid(Enemy)) return false;

	UHealthComponent* Health = Enemy->GetHealthComponent();
	return IsValid(Health) && !Health->IsDead();
}
