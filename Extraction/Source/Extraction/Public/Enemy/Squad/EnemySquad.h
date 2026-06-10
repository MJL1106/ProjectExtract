// UEnemySquad -- thin coordinator per squad: relays sightings, hands out role tokens, never puppets pawns (design SS6).

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "EnemyTypes.h"
#include "EnemySquad.generated.h"

class AEnemyCharacter;
class UEnemyAwarenessComponent;

UENUM(BlueprintType)
enum class EEnemySquadRole : uint8
{
	Flanker		UMETA(DisplayName = "Flanker"),
	Suppressor	UMETA(DisplayName = "Suppressor"),
};

UCLASS()
class EXTRACTION_API UEnemySquad : public UObject
{
	GENERATED_BODY()

public:

	void SetSquadId(FName InId) { SquadId = InId; }
	FName GetSquadId() const { return SquadId; }

	// --- Membership ---

	void AddMember(AEnemyCharacter* Member);
	void RemoveMember(AEnemyCharacter* Member);
	const TArray<TWeakObjectPtr<AEnemyCharacter>>& GetMembers() const { return Members; }
	int32 NumAlive() const;
	bool HasLivingOfficer() const;
	AEnemyCharacter* GetOfficer() const;

	// --- Shared sightings ---

	/** Called by awareness component on Combat entry + LOS updates. Rate-limited per squad. */
	void ReportSighting(AActor* Target, const FVector& LastKnown);

	// --- Role tokens ---

	bool TryClaimRole(EEnemySquadRole Role, AEnemyCharacter* Claimant);
	void ReleaseRole(EEnemySquadRole Role, AEnemyCharacter* Claimant);
	AEnemyCharacter* GetRoleHolder(EEnemySquadRole Role) const;

	// --- Focus fire ---

	void SetFocusTarget(AActor* Target, AEnemyCharacter* Caller, bool bOfficerCommand = false);
	AActor* GetFocusTarget() const;
	void ClearFocusTarget();

	// --- Morale relay ---

	void NotifyMemberDied(AEnemyCharacter* Dead, bool bWasOfficer);
	void Rally(AEnemyCharacter* Officer);

	// --- Flank attempt tracking ---

	/** Returns world time of the last flank attempt by this member, or -1e9 if none. */
	float GetLastFlankAttemptTime(const AEnemyCharacter* Member) const;

	/** Records the current world time as this member's last flank attempt. */
	void RecordFlankAttempt(const AEnemyCharacter* Member);

	// --- Bark dedup ---

	/** Squad-level bark rate limiter. Returns true if the bark is allowed. */
	bool TryClaimSquadBark(EBarkType Type, float Window = 3.f);

private:

	FName SquadId = NAME_None;

	UPROPERTY()
	TArray<TWeakObjectPtr<AEnemyCharacter>> Members;

	// Shared sighting state
	TWeakObjectPtr<AActor> SquadTarget;
	FVector SquadLastKnown = FVector::ZeroVector;
	float LastSightingRelayTime = -1e9f;
	static constexpr float SightingRelayInterval = 1.f;

	// Role tokens (mutable: GetRoleHolder lazily clears dead holders)
	mutable TMap<EEnemySquadRole, TWeakObjectPtr<AEnemyCharacter>> RoleHolders;

	// Focus fire (mutable: GetFocusTarget resets officer flag when weak ptr goes stale)
	mutable TWeakObjectPtr<AActor> FocusTarget;
	mutable bool bFocusSetByOfficer = false;

	// Bark dedup
	TMap<EBarkType, float> LastSquadBarkTime;

	/** Returns true if the member is valid and alive. */
	static bool IsMemberAlive(const TWeakObjectPtr<AEnemyCharacter>& Member);

	/** Returns true if any living member (excluding Exclude) has awareness >= MinState. */
	bool AnyMemberAwareAtOrAbove(EEnemyAwarenessState MinState, const AEnemyCharacter* Exclude = nullptr) const;

	// Flank attempt tracking (keyed by member, world time)
	mutable TMap<TWeakObjectPtr<const AEnemyCharacter>, float> FlankAttemptTimes;
};
