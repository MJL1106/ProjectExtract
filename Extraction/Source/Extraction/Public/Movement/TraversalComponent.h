// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TraversalTypes.h"
#include "TraversalComponent.generated.h"

class ACharacter;
class UCharacterMovementComponent;
class UCapsuleComponent;

DECLARE_LOG_CATEGORY_EXTERN(LogTraversal, Log, All);

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnTraversalStarted, ETraversalType, float /*PlayRate*/);
DECLARE_MULTICAST_DELEGATE(FOnTraversalEnded);

UCLASS(ClassGroup=(Movement), meta=(BlueprintSpawnableComponent))
class EXTRACTION_API UTraversalComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UTraversalComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ---- Public API ----

	bool TryStartTraversal(bool bWasSprinting);
	bool DetectTraversalAhead(FVector& OutSnapTarget, ETraversalType& OutType);
	void ExecuteByType(ETraversalType Type, bool bWasSprinting);
	void CancelTraversal();

	UFUNCTION(BlueprintPure, Category = "Movement|Traversal")
	bool IsInTraversal() const { return ActiveTraversalType != ETraversalType::None; }

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
	bool TraceForwardForWall(FHitResult& OutHit) const;
	bool TraceDownForSurface(const FHitResult& WallHit, FHitResult& OutSurfaceHit) const;
	bool CheckClearance(const FVector& SurfaceLocation, float ForwardOffset) const;

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
};
