// Anim instance for enemy characters — locomotion, aim offset, combat montages, and delegate-driven reactions.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "ExtractionTypes.h"
#include "EnemyAnimInstance.generated.h"

class AEnemyCharacter;
class UCharacterMovementComponent;
class UHealthComponent;
class UAnimMontage;

UCLASS()
class EXTRACTION_API UEnemyAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUninitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	// --- Montage Helpers ---

	UFUNCTION(BlueprintCallable, Category = "Enemy|Animation")
	void PlayFireMontage(float PlayRate = 1.f);

	UFUNCTION(BlueprintCallable, Category = "Enemy|Animation")
	void StopFireMontage(float BlendOutTime = 0.2f);

	UFUNCTION(BlueprintCallable, Category = "Enemy|Animation")
	void PlayReloadMontage(float PlayRate = 1.f);

	UFUNCTION(BlueprintCallable, Category = "Enemy|Animation")
	void PlayHitReactMontage(float PlayRate = 1.f);

	UFUNCTION(BlueprintCallable, Category = "Enemy|Animation")
	void PlayDeathMontage(float PlayRate = 1.f);

	UFUNCTION(BlueprintCallable, Category = "Enemy|Animation")
	void PlayMeleeMontage(float PlayRate = 1.f);

protected:
	// --- Cached Refs ---

	UPROPERTY(BlueprintReadOnly, Category = "Enemy|Animation")
	TObjectPtr<AEnemyCharacter> OwningEnemy;

	UPROPERTY(BlueprintReadOnly, Category = "Enemy|Animation")
	TObjectPtr<UCharacterMovementComponent> MovementComponent;

	UPROPERTY(BlueprintReadOnly, Category = "Enemy|Animation")
	TObjectPtr<UHealthComponent> HealthComponent;

	// --- Locomotion ---

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Locomotion")
	float Speed = 0.f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Locomotion")
	float Direction = 0.f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Locomotion")
	float NormalizedSpeed = 0.f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Locomotion")
	bool bIsInAir = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Locomotion")
	bool bIsFalling = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Locomotion")
	bool bHasVelocity = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Locomotion")
	bool bIsAccelerating = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Locomotion")
	bool bIsCrouched = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Locomotion")
	bool bIsAlive = true;

	// --- Aim Offset ---

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|AimOffset")
	float AimPitch = 0.f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|AimOffset")
	float AimYaw = 0.f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|AimOffset")
	bool bIsAiming = false;

	// --- Combat ---

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Combat")
	bool bIsFiring = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Combat")
	bool bIsReloading = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Combat")
	bool bInCombat = false;

	// --- Montage Assets (designer-assigned on the ABP) ---

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Montages")
	TObjectPtr<UAnimMontage> FireMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Montages")
	TObjectPtr<UAnimMontage> ReloadMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Montages")
	TObjectPtr<UAnimMontage> HitReactMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Montages")
	TObjectPtr<UAnimMontage> DeathMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Montages")
	TObjectPtr<UAnimMontage> MeleeMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Montages")
	TObjectPtr<UAnimMontage> TakedownReactionMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Montages")
	FName FireMontageLoopSection = TEXT("Default");

	// --- Aim Offset Helper ---

	void UpdateAimOffset(const FVector& ToTarget, const FRotator& ActorRot);

private:
	// --- Delegate handlers ---

	UFUNCTION()
	void HandleHitReact(EHitRegion Region);

	UFUNCTION()
	void HandleMeleePerformed();

	UFUNCTION()
	void HandleTakedown(AActor* Instigator);

	// --- Auto-trigger tracking ---

	bool bPrevIsFiring = false;
	bool bPrevIsReloading = false;
};
