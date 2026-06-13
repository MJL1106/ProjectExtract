// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "TraversalTypes.h"
#include "TraversalComponent.generated.h"

struct FCollisionQueryParams;

class ACharacter;
class UCharacterMovementComponent;
class UCapsuleComponent;

DECLARE_LOG_CATEGORY_EXTERN(LogTraversal, Log, All);

DECLARE_MULTICAST_DELEGATE_FourParams(FOnTraversalStarted,
	ETraversalType, float /*PlayRate*/,
	FVector /*ObstacleLocation*/, FVector /*LandingLocation*/);
DECLARE_MULTICAST_DELEGATE(FOnTraversalEnded);

UCLASS(ClassGroup=(Movement), meta=(BlueprintSpawnableComponent))
class EXTRACTION_API UTraversalComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UTraversalComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ---- Public API ----

	bool TryStartTraversal(bool bWasSprinting);
	bool DetectTraversalAhead(FVector& OutSnapTarget, ETraversalType& OutType);
	void ExecuteByType(ETraversalType Type, bool bWasSprinting);
	void CancelTraversal();

	/**
	 * Authored-traversal entry point — used by ATraversalNavLink smart-link callbacks.
	 * Skips the trace-based obstacle detection because Start/End are supplied by the
	 * nav-link. Enters an approach phase: drives the companion toward the wall via raw
	 * movement input until the wall enters trace range, then commits the vault.
	 * Returns false if pre-flight guards fail (no montage, bad clearance, etc.).
	 * bTeleportOnTimeout controls whether the approach-timeout path teleports to End.
	 */
	UFUNCTION(BlueprintCallable, Category = "Movement|Traversal")
	bool TryStartTraversalFromNavLink(ETraversalType Type, const FVector& Start, const FVector& End, float PlayRate = 1.f, bool bTeleportOnTimeout = true);

	UFUNCTION(BlueprintPure, Category = "Movement|Traversal")
	bool IsInTraversal() const { return ActiveTraversalType != ETraversalType::None; }

	/** True while the companion is in the approach phase OR mid-vault. Use this to gate external aborts/skips. */
	UFUNCTION(BlueprintPure, Category = "Movement|Traversal")
	bool IsBusy() const { return bNavLinkApproaching || IsInTraversal(); }

	UFUNCTION(BlueprintPure, Category = "Movement|Traversal")
	ETraversalType GetActiveType() const { return ActiveTraversalType; }

	UFUNCTION(BlueprintPure, Category = "Movement|Traversal")
	bool GetWasSprintingAtEntry() const { return bWasSprintingAtTraversalEntry; }

	UFUNCTION(BlueprintPure, Category = "Movement|Traversal")
	FVector GetVaultTargetLocation() const { return VaultTargetLocation; }

	UFUNCTION(BlueprintPure, Category = "Movement|Traversal")
	float GetVaultSurfaceHeight() const { return VaultSurfaceHeight; }

	// ---- Delegates ----

	FOnTraversalStarted OnTraversalStarted;
	FOnTraversalEnded OnTraversalEnded;

protected:

	// ---- Noise Config ----

	UPROPERTY(EditDefaultsOnly, Category = "Movement|Noise",
		meta = (ClampMin = "0.0",
			ToolTip = "AI-hearing loudness of starting a vault/climb/mantle."))
	float TraversalNoiseLoudness = 0.5f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement|Noise",
		meta = (ClampMin = "0.0",
			ToolTip = "Max range (cm) at which AI hearing registers a traversal. 0 disables."))
	float TraversalNoiseRange = 700.f;

	// ---- Vault Config ----

	UPROPERTY(EditDefaultsOnly, Category = "Movement|Vault",
		meta = (ClampMin = "10.0", ClampMax = "200.0",
			ToolTip = "How far ahead of the capsule edge the character checks for vaultable surfaces."))
	float VaultForwardTraceDistance = 80.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement|Vault",
		meta = (ClampMin = "1.0", ClampMax = "34.0",
			ToolTip = "Radius of the sphere sweep for forward wall detection."))
	float VaultForwardTraceRadius = 15.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement|Vault",
		meta = (ClampMin = "0.0", ClampMax = "200.0",
			ToolTip = "Height of the forward wall-detection trace above the character's feet."))
	float VaultForwardTraceHeight = 50.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement|Vault",
		meta = (ClampMin = "0.0",
			ToolTip = "Obstacles shorter than this are stepped over, not vaulted."))
	float VaultMinHeight = 50.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement|Vault",
		meta = (ClampMin = "0.0",
			ToolTip = "Obstacles taller than this cannot be vaulted."))
	float VaultMaxHeight = 90.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement|Vault",
		meta = (ClampMin = "0.0",
			ToolTip = "How far past the ledge edge the vault target is placed."))
	float VaultLandingForwardOffset = 45.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement|Vault",
		meta = (ClampMin = "0.5", ClampMax = "3.0",
			ToolTip = "Playback speed multiplier for sprint vaults."))
	float VaultSprintPlayRate = 1.3f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement|Vault",
		meta = (ClampMin = "0.5", ClampMax = "3.0",
			ToolTip = "Playback speed multiplier for walk vaults."))
	float VaultWalkPlayRate = 1.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement|Vault",
		meta = (ClampMin = "5.0", ClampMax = "100.0",
			ToolTip = "How far from the wall the character is placed at vault start."))
	float VaultSnapDistance = 50.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement|Vault",
		meta = (ClampMin = "5.0", ClampMax = "50.0",
			ToolTip = "Interpolation speed for the vault snap."))
	float VaultSnapInterpSpeed = 18.0f;

	// ---- Climb Config ----

	UPROPERTY(EditDefaultsOnly, Category = "Movement|Climb",
		meta = (ClampMin = "0.0",
			ToolTip = "Surfaces shorter than this won't trigger climb."))
	float ClimbMinHeight = 80.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement|Climb",
		meta = (ClampMin = "0.0",
			ToolTip = "Surfaces taller than this trigger mantle instead of climb."))
	float ClimbMaxHeight = 170.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement|Climb",
		meta = (ClampMin = "50.0", ClampMax = "300.0",
			ToolTip = "The obstacle height the climb montage's root motion was built for."))
	float ClimbAnimReferenceHeight = 100.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement|Climb",
		meta = (ClampMin = "0.5", ClampMax = "3.0"))
	float ClimbWalkPlayRate = 1.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement|Climb",
		meta = (ClampMin = "0.5", ClampMax = "3.0"))
	float ClimbSprintPlayRate = 1.0f;

	// ---- Mantle Config ----

	UPROPERTY(EditDefaultsOnly, Category = "Movement|Mantle",
		meta = (ClampMin = "0.0",
			ToolTip = "Surfaces taller than this cannot be mantled."))
	float MantleMaxHeight = 260.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement|Mantle",
		meta = (ClampMin = "50.0", ClampMax = "400.0",
			ToolTip = "The obstacle height the mantle montage's root motion was built for."))
	float MantleAnimReferenceHeight = 200.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement|Mantle",
		meta = (ClampMin = "0.5", ClampMax = "3.0"))
	float MantleWalkPlayRate = 1.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement|Mantle",
		meta = (ClampMin = "0.5", ClampMax = "3.0"))
	float MantleSprintPlayRate = 1.0f;

	// ---- Safety ----

	UPROPERTY(EditDefaultsOnly, Category = "Movement|Safety",
		meta = (ClampMin = "0.5", ClampMax = "10.0",
			ToolTip = "Worst-case time before the traversal is force-ended even if the montage end delegate never fires. Tune to longest expected montage + buffer."))
	float WorstCaseTraversalDuration = 3.0f;

	// ---- NavLink Approach Config ----

	/** Max seconds the companion drives toward a nav-link wall before giving up (then teleports to the link End). */
	UPROPERTY(EditDefaultsOnly, Category = "Traversal|NavLink", meta = (ClampMin = "0.5", ClampMax = "6.0"))
	float NavLinkApproachTimeout = 2.5f;

	// Vertical fine-tune added to the nav-link landing floor-snap (cm; +up). 0 = sit exactly on the floor.
	UPROPERTY(EditDefaultsOnly, Category = "Traversal|NavLink", meta = (ClampMin = "-100.0", ClampMax = "100.0"))
	float NavLinkLandingZOffset = 0.f;

	// ---- Debug ----

	UPROPERTY(EditAnywhere, Category = "Movement|Debug",
		meta = (ToolTip = "Draw the forward sweep, downward surface trace, and snap target on screen."))
	bool bDrawDebugTraces = false;

	UPROPERTY(EditAnywhere, Category = "Movement|Debug",
		meta = (ClampMin = "0.0", ClampMax = "10.0",
			ToolTip = "How long debug shapes persist (seconds)."))
	float DebugTraceDuration = 2.0f;

private:

	// ---- Detection Helpers ----

	ETraversalType PerformTraversalDetection();
	bool TraceForwardForWall(FHitResult& OutHit, const FCollisionQueryParams& IgnoreParams) const;
	bool TraceDownForSurface(const FHitResult& WallHit, FHitResult& OutSurfaceHit, const FCollisionQueryParams& IgnoreParams) const;
	bool CheckClearance(const FVector& SurfaceLocation, float ForwardOffset, const FCollisionQueryParams& IgnoreParams) const;
	void BuildPawnIgnoreParams(FCollisionQueryParams& OutParams) const;

	/** Returns true if the owning character's AnimInstance has a montage assigned for Type. */
	bool OwnerHasMontageForType(ETraversalType Type) const;

	/**
	 * Attempts to resolve real wall geometry for TryStartTraversalFromNavLink by aligning
	 * the actor's facing and running the standard forward + down traces.
	 * Populates VaultWall* / VaultSurface* / VaultSurfaceHeight on success.
	 * Returns true if real geometry was found; false means the caller should fall back to
	 * the endpoint-derived values (VaultTargetLocation is always left as End by the caller).
	 * Type is used only for a designer-facing height-mismatch warning (no behaviour change).
	 */
	bool ResolveNavLinkWallData(ETraversalType Type, const FVector& Start, const FVector& End, const FCollisionQueryParams& IgnoreParams);

	// ---- NavLink Approach Phase ----

	/** Drives the companion toward the wall each tick until the wall enters trace range, then commits the vault. */
	void UpdateNavLinkApproach(float DeltaTime);

	/** Ends the approach phase. Teleports to NavLinkEnd on timeout if bTeleportToEnd is true, then broadcasts OnTraversalEnded. */
	void EndNavLinkApproach(bool bTeleportToEnd);

	/** Floor-snap applied at EndTraversal when the traversal originated from a nav-link. */
	void SnapToFloorAfterNavLink();

	// ---- Traversal Execution ----

	void StartTraversal(ETraversalType Type);
	void UpdateTraversal(float DeltaTime);

public:
	/** Ends the active traversal. Called by the owner's montage-end callback. */
	void EndTraversal();

private:

	UFUNCTION()
	void OnRep_TraversalType();

	// ---- Cached References ----

	UPROPERTY()
	TObjectPtr<ACharacter> OwningCharacter;

	UPROPERTY()
	TObjectPtr<UCharacterMovementComponent> CachedMovement;

	UPROPERTY()
	TObjectPtr<UCapsuleComponent> CachedCapsule;

	// ---- Replicated State ----

	UPROPERTY(ReplicatedUsing = OnRep_TraversalType, BlueprintReadOnly, Category = "Movement|State", meta = (AllowPrivateAccess = "true"))
	ETraversalType ActiveTraversalType;

	UPROPERTY(Replicated)
	bool bWasSprintingAtTraversalEntry;

	// ---- NavLink Approach State ----

	bool bNavLinkApproaching = false;
	bool bNavLinkTeleportOnTimeout = true;
	/** Set true when a traversal is committed via the nav-link path; false for all trace/mirror paths. */
	bool bCurrentTraversalFromNavLink = false;
	ETraversalType NavLinkApproachType = ETraversalType::None;
	float NavLinkApproachPlayRate = 1.f;
	float NavLinkApproachElapsed = 0.f;
	FVector NavLinkApproachDir = FVector::ZeroVector;
	FVector NavLinkStart = FVector::ZeroVector;
	FVector NavLinkEnd = FVector::ZeroVector;

	/** Cached ignore params built once at approach entry — reused every poll tick. */
	FCollisionQueryParams NavLinkApproachIgnoreParams;

	/** Accumulates DeltaTime for wall-poll throttling. */
	float NavLinkPollAccumulator = 0.f;

	/** World time at the last teleport-abort (EndNavLinkApproach with bTeleportToEnd=true). Guards re-entry cooldown. */
	float NavLinkLastAbortTime = -100.f;

	/** Saved bUseControllerDesiredRotation from CachedMovement, restored when approach exits. */
	bool bSavedUseControllerDesiredRotation = false;

	// ---- Runtime State ----

	FVector VaultTargetLocation;
	FVector VaultSurfaceLocation;
	FVector VaultWallNormal;
	FVector VaultWallImpactPoint;
	float VaultSurfaceHeight;
	FVector VaultSnapTarget;
	bool bIsSnappingToVault;
	float VaultSnapTimeRemaining;
	FRotator VaultLockedRotation;

	/** Cached value of OwningCharacter->bUseControllerRotationYaw before traversal overrides it */
	bool bSavedUseControllerRotationYaw = false;

	/** Worst-case escape timer — force-ends traversal if the montage end delegate never fires. */
	FTimerHandle WorstCaseTraversalEndHandle;
};
