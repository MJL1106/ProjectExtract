// UEnemySquad -- thin coordinator per squad: relays sightings, hands out role tokens, never puppets pawns (design SS6).

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "EnemyTypes.h"
#include "EnemySquad.generated.h"

class AEnemyCharacter;
class AEnemyAIController;
class UEnemyAwarenessComponent;
class UBarkSubsystem;

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

	// --- Bounding overwatch (Phase 7, officer-gated) ---

	/** Starts a bounding overwatch maneuver. Officer must be alive with bHasCommandAura, squad
	 *  must have a valid target and enough grunt members. Returns true if the maneuver started. */
	bool StartBounding(AEnemyCharacter* Officer);

	/** Stops any active bounding overwatch. Idempotent; clears roles and BB on both participants. */
	void StopBounding(const TCHAR* Reason);

	/** True if a bounding overwatch maneuver is currently active. Lazily validates stale state. */
	bool IsBoundingActive();

	/** True if the suppressor is actively engaged (task flag set), alive, and not reloading.
	 *  The flanker's decorator polls this. */
	bool IsSuppressionLive() const;

	/** True if suppressor is engaged, alive, and weapon is reloading. The flanker holds position
	 *  during reloads rather than aborting — suppression resumes when the mag refills. */
	bool IsSuppressionHolding() const;

	/** Called by BTTask_EnemySuppressFire: true in ExecuteTask, false in CleanUp.
	 *  Identity-gated: ignored when Who != current BoundingSuppressor (prevents stale CleanUp
	 *  from clearing the new suppressor's flag after a swap). */
	void SetSuppressorEngaged(AEnemyCharacter* Who, bool bEngaged);

	/** Called by the flanker on arrival: swaps suppressor and flanker roles, barks CoveringGo. */
	void NotifyFlankerArrived(AEnemyCharacter* Flanker);

	/** Called when the flanker aborts (suppressed, low HP, no path): stops the entire maneuver. */
	void NotifyManeuverMemberBlocked(AEnemyCharacter* Member);

	// --- Bounding attempt cooldown ---

	/** World time of the last bounding attempt (success or failure). -1e9 if never attempted. */
	float GetLastBoundingAttemptTime() const { return LastBoundingAttemptTime; }

	/** Records the current world time as the last bounding attempt. Called by BTTask_OfficerCommand. */
	void RecordBoundingAttempt();

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

	// --- Bounding overwatch state ---

	bool bBoundingActive = false;
	bool bSuppressorEngaged = false;
	bool bSwapInProgress = false;
	float LastSwapTime = -1e9f;
	float LastBoundingAttemptTime = -1e9f;
	static constexpr float MinSwapInterval = 2.f;
	TWeakObjectPtr<AEnemyCharacter> BoundingOfficer;
	TWeakObjectPtr<AEnemyCharacter> BoundingSuppressor;
	TWeakObjectPtr<AEnemyCharacter> BoundingFlanker;

	/** Pushes ManeuverRole onto a member's BB via its controller. Null-safe. */
	void PushManeuverRoleToBB(AEnemyCharacter* Member, EEnemyManeuverRole Role);

	/** Stamps ManeuverHoldUntil on a member's BB so the post-maneuver engage-in-place decorator holds.
	 *  Uses the member's DA ManeuverDisengageHoldTime; 0 or missing DA = no stamp. */
	void StampManeuverHold(AEnemyCharacter* Member);

	/** Picks the best grunt member for a role. Suppressor prefers LOS to target; flanker prefers health > 0.5. */
	AEnemyCharacter* PickBoundingCandidate(bool bPreferLOS, AEnemyCharacter* Exclude) const;

	/** Returns true if Member is a living grunt archetype (eligible for maneuver roles). */
	bool IsEligibleForManeuver(const TWeakObjectPtr<AEnemyCharacter>& Member) const;

	/** Returns true if the member is valid and alive. */
	static bool IsMemberAlive(const TWeakObjectPtr<AEnemyCharacter>& Member);

	/** Returns true if any living member (excluding Exclude) has awareness >= MinState. */
	bool AnyMemberAwareAtOrAbove(EEnemyAwarenessState MinState, const AEnemyCharacter* Exclude = nullptr) const;

	// Flank attempt tracking (keyed by member, world time)
	mutable TMap<TWeakObjectPtr<const AEnemyCharacter>, float> FlankAttemptTimes;
};
