#include "EnemySquad.h"
#include "EnemyCharacter.h"
#include "EnemyArchetypeData.h"
#include "EnemyAIController.h"
#include "EnemyAwarenessComponent.h"
#include "EnemyMoraleComponent.h"
#include "EnemySquadSubsystem.h"
#include "HealthComponent.h"

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

	LastSightingRelayTime = Now;
	SquadTarget = Target;
	SquadLastKnown = LastKnown;

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

// ---------------------------------------------------------------------------
// Role tokens
// ---------------------------------------------------------------------------

bool UEnemySquad::TryClaimRole(EEnemySquadRole Role, AEnemyCharacter* Claimant)
{
	if (!IsValid(Claimant)) return false;

	TWeakObjectPtr<AEnemyCharacter>* Holder = RoleHolders.Find(Role);
	if (Holder)
	{
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
// Bark dedup
// ---------------------------------------------------------------------------

bool UEnemySquad::TryClaimSquadBark(EBarkType Type, float Window)
{
	UWorld* World = GetWorld();
	if (!IsValid(World)) return true;

	const float Now = World->GetTimeSeconds();
	float* LastTime = LastSquadBarkTime.Find(Type);
	if (LastTime && (Now - *LastTime) < Window) return false;

	LastSquadBarkTime.FindOrAdd(Type) = Now;
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
