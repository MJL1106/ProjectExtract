// AI companion character — follows player, engages enemies, revives downed teammates.

#include "CompanionCharacter.h"
#include "AI/CompanionDiag.h"
#include "CompanionAIController.h"
#include "HealthComponent.h"
#include "WeaponBase.h"
#include "WeaponDataAsset.h"
#include "TraversalComponent.h"
#include "CompanionAnimInstance.h"
#include "ExtractionTypes.h"
#include "Components/CapsuleComponent.h"
#include "Components/WidgetComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY(LogCompanion);

ACompanionCharacter::ACompanionCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetNetUpdateFrequency(10.0f);
	SetMinNetUpdateFrequency(5.0f);

	GetCapsuleComponent()->SetCapsuleSize(34.0f, 88.0f);

	HealthComponent = CreateDefaultSubobject<UHealthComponent>(TEXT("HealthComponent"));
	TraversalComponent = CreateDefaultSubobject<UTraversalComponent>(TEXT("TraversalComponent"));

	HealthWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("HealthWidget"));
	HealthWidgetComponent->SetupAttachment(GetMesh(), TEXT("head"));
	HealthWidgetComponent->SetRelativeLocation(FVector(0.f, 0.f, 30.f));
	HealthWidgetComponent->SetWidgetSpace(EWidgetSpace::Screen);
	HealthWidgetComponent->SetDrawSize(FVector2D(150.f, 40.f));
	HealthWidgetComponent->SetPivot(FVector2D(0.5f, 0.5f));
	HealthWidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HealthWidgetComponent->SetTwoSided(false);
	HealthWidgetComponent->SetVisibility(false);

	// Configure inherited skeletal mesh — designer assigns mesh + anim class on BP_Companion
	GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, -88.f));
	GetMesh()->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));

	OwnedTags.AddTag(TAG_Character_Companion);

	// Movement defaults
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->bUseControllerDesiredRotation = true;
		MoveComp->bOrientRotationToMovement = false;
		MoveComp->MaxWalkSpeed = WalkSpeed;
		MoveComp->MaxWalkSpeedCrouched = CrouchedWalkSpeed;
		MoveComp->GetNavAgentPropertiesRef().bCanCrouch = true;
	}
	bUseControllerRotationYaw = false;
}

void ACompanionCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// Catch the most common BP misconfiguration: no weapon class assigned.
	// Runs once on spawn, server only (weapon is server-spawned).
	if (HasAuthority() && !WeaponClass)
		UE_LOG(LogCompanion, Warning, TEXT("%s has no WeaponClass assigned - companion will never fire"), *GetName());

	if (IsValid(TraversalComponent))
	{
		TraversalComponent->OnTraversalStarted.AddUObject(this, &ACompanionCharacter::HandleTraversalStarted);
		TraversalComponent->OnTraversalEnded.AddUObject(this, &ACompanionCharacter::HandleTraversalEnded);
	}
}

void ACompanionCharacter::BeginPlay()
{
	Super::BeginPlay();

	// The constructor's movement defaults run with C++ default values before BP CDO overrides
	// apply. Re-apply once BP-overridden values are live so speeds match what designers set.
	// WalkSpeed/SprintSpeed: re-applied via OnRep_IsSprinting below.
	// CrouchedWalkSpeed: must be pushed directly since no OnRep covers it.
	OnRep_IsSprinting();
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
		MoveComp->MaxWalkSpeedCrouched = CrouchedWalkSpeed;

	if (HealthComponent && !HealthComponent->OnDeath.IsAlreadyBound(this, &ACompanionCharacter::HandleDeath))
		HealthComponent->OnDeath.AddDynamic(this, &ACompanionCharacter::HandleDeath);

	// Spawn and attach weapon (server only — weapon replicates to clients)
	if (HasAuthority() && WeaponClass)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.Instigator = this;

		CurrentWeapon = GetWorld()->SpawnActor<AWeaponBase>(WeaponClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
		if (IsValid(CurrentWeapon))
		{
			CurrentWeapon->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, WeaponAttachSocket);
			CurrentWeapon->InitializeAmmo();

			// Drive the fire montage off the weapon's broadcast.
			if (!CurrentWeapon->OnWeaponFired.IsAlreadyBound(this, &ACompanionCharacter::OnWeaponFiredCallback))
				CurrentWeapon->OnWeaponFired.AddDynamic(this, &ACompanionCharacter::OnWeaponFiredCallback);

			UE_LOG(LogCompanion, Log, TEXT("%s equipped weapon %s"), *GetName(), *CurrentWeapon->GetName());
		}
	}

	if (HealthWidgetClass && HealthWidgetComponent)
	{
		HealthWidgetComponent->SetWidgetClass(HealthWidgetClass);
		HealthWidgetComponent->SetVisibility(true);
	}

	UE_LOG(LogCompanion, Log, TEXT("%s spawned with tag Character.Companion"), *GetName());
}

void ACompanionCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsValid(TraversalComponent))
	{
		TraversalComponent->OnTraversalStarted.RemoveAll(this);
		TraversalComponent->OnTraversalEnded.RemoveAll(this);
	}

	if (const UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(DestroyTimerHandle);

	if (HealthComponent)
		HealthComponent->OnDeath.RemoveDynamic(this, &ACompanionCharacter::HandleDeath);

	Super::EndPlay(EndPlayReason);
}

void ACompanionCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (CurrentAimTarget.IsValid())
		TimeAimingAtCurrentTarget += DeltaTime;
}

float ACompanionCharacter::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	const float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (IsValid(HealthComponent))
	{
		HealthComponent->TakeDamage(ActualDamage);
		UE_LOG(LogCompanion, Log, TEXT("Companion took %.1f damage — HP: %.0f / Shield: %.0f"),
			ActualDamage, HealthComponent->GetCurrentHealth(), HealthComponent->GetCurrentShield());
	}
	if (ActualDamage > 0.f && GetWorld())
		LastDamageWorldTime = GetWorld()->GetTimeSeconds();

	// Hit react — skip if dying (death path takes over).
	if (ActualDamage > 0.f && IsValid(HealthComponent) && HealthComponent->IsAlive())
	{
		if (USkeletalMeshComponent* MeshComp = GetMesh())
		{
			if (UCompanionAnimInstance* AnimInst = Cast<UCompanionAnimInstance>(MeshComp->GetAnimInstance()))
				AnimInst->PlayHitReactMontage(1.0f);
		}
	}

	return ActualDamage;
}

bool ACompanionCharacter::IsSuppressed(float Window) const
{
	if (Window <= 0.f || !GetWorld()) return false;
	return (GetWorld()->GetTimeSeconds() - LastDamageWorldTime) < Window;
}

float ACompanionCharacter::GetHealthFraction() const
{
	if (!IsValid(HealthComponent)) return 1.f;
	return HealthComponent->GetHealthPercent();
}

void ACompanionCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(ACompanionCharacter, bIsSprinting, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(ACompanionCharacter, Posture, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(ACompanionCharacter, bLowReadyAim, COND_SkipOwner);
}

// --- Crouch diagnostics ---

void ACompanionCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	if (HasAuthority())
		UE_LOG(LogCompanionDiag, Log, TEXT("%s: [OnStartCrouch] t=%.3f halfAdj=%.1f capsHalfH=%.1f meshRelZ=%.1f worldZ=%.1f"),
			*GetName(),
			GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f,
			HalfHeightAdjust,
			GetCapsuleComponent() ? GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() : 0.f,
			GetMesh() ? GetMesh()->GetRelativeLocation().Z : 0.f,
			GetActorLocation().Z);
}

void ACompanionCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	if (HasAuthority())
		UE_LOG(LogCompanionDiag, Log, TEXT("%s: [OnEndCrouch] t=%.3f halfAdj=%.1f capsHalfH=%.1f meshRelZ=%.1f worldZ=%.1f"),
			*GetName(),
			GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f,
			HalfHeightAdjust,
			GetCapsuleComponent() ? GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() : 0.f,
			GetMesh() ? GetMesh()->GetRelativeLocation().Z : 0.f,
			GetActorLocation().Z);
}

// --- Posture API ---

void ACompanionCharacter::SetPosture(ECompanionPosture NewPosture)
{
	if (!HasAuthority()) return;
	if (NewPosture == Posture) return;

	// Invariant: Posture MUST be assigned before OnRep_Posture broadcast.
	// Handlers reading GetPosture() during the broadcast must see the new value.
	// OnRep_Posture performs no state mutation — re-entry safe.
	Posture = NewPosture;
	UE_LOG(LogCompanion, Log, TEXT("Companion posture -> %s"), *UEnum::GetValueAsString(Posture));
	OnRep_Posture();
}

void ACompanionCharacter::OnRep_Posture()
{
	// No state mutation — broadcast only. See SetPosture for ordering invariant.
	OnPostureChanged.Broadcast(Posture);
}

// --- Low Ready Aim ---

void ACompanionCharacter::SetLowReadyAim(bool bNewLowReady)
{
	if (!HasAuthority()) return;
	if (bLowReadyAim == bNewLowReady) return;
	bLowReadyAim = bNewLowReady;
	OnRep_LowReadyAim();
}

void ACompanionCharacter::OnRep_LowReadyAim()
{
	OnLowReadyAimChanged.Broadcast(bLowReadyAim);
}

// --- Sprint API ---

void ACompanionCharacter::SetSprinting(bool bSprint)
{
	if (!HasAuthority()) return;
	if (bIsSprinting == bSprint) return;

	bIsSprinting = bSprint;
	OnRep_IsSprinting();
}

void ACompanionCharacter::OnRep_IsSprinting()
{
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
		MoveComp->MaxWalkSpeed = bIsSprinting ? SprintSpeed : WalkSpeed;
}

void ACompanionCharacter::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
{
	TagContainer.AppendTags(OwnedTags);
}

// --- Weapon Interface ---

void ACompanionCharacter::StartWeaponFire()
{
	if (IsValid(CurrentWeapon))
		CurrentWeapon->StartFiring();

	// Start the loop fire montage (idempotent — won't restart if already playing).
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		if (UCompanionAnimInstance* AnimInst = Cast<UCompanionAnimInstance>(MeshComp->GetAnimInstance()))
			AnimInst->PlayFireMontage(1.0f);
	}
}

void ACompanionCharacter::OnWeaponFiredCallback()
{
	// Per-shot callback hook — reserved for future use (recoil kicks, casing ejection, etc.).
	// Note: the loop fire montage is driven from StartWeaponFire / StopWeaponFire, not per-shot.
}

void ACompanionCharacter::StopWeaponFire()
{
	if (IsValid(CurrentWeapon))
		CurrentWeapon->StopFiring();

	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		if (UCompanionAnimInstance* AnimInst = Cast<UCompanionAnimInstance>(MeshComp->GetAnimInstance()))
			AnimInst->StopFireMontage(0.15f);
	}
}

void ACompanionCharacter::ReloadWeapon()
{
	if (UE_LOG_ACTIVE(LogCompanionDiag, Log))
	{
		const float Vel = GetVelocity().Size();
		const bool bIsMoving = Vel > KINDA_SMALL_NUMBER;
		const bool bAlreadyReloading = IsValid(CurrentWeapon) && CurrentWeapon->IsReloading();
		UE_LOG(LogCompanionDiag, Log, TEXT("%s: RELOAD-CALL vel=%.1f isMoving=%d alreadyReloading=%d"),
			*GetName(), Vel, (int32)bIsMoving, (int32)bAlreadyReloading);
	}
	if (IsValid(CurrentWeapon))
		CurrentWeapon->Reload();
}

bool ACompanionCharacter::CanFire() const
{
	return IsValid(CurrentWeapon) && CurrentWeapon->CanFire();
}

bool ACompanionCharacter::NeedsReload() const
{
	if (!IsValid(CurrentWeapon)) return false;
	return CurrentWeapon->GetCurrentAmmo() == 0 && CurrentWeapon->CanReload();
}

bool ACompanionCharacter::IsReloading() const
{
	return IsValid(CurrentWeapon) && CurrentWeapon->IsReloading();
}

bool ACompanionCharacter::CanReload() const
{
	return IsValid(CurrentWeapon) && CurrentWeapon->CanReload();
}

int32 ACompanionCharacter::GetCurrentAmmo() const
{
	return IsValid(CurrentWeapon) ? CurrentWeapon->GetCurrentAmmo() : 0;
}

float ACompanionCharacter::GetWeaponReloadTime() const
{
	if (!IsValid(CurrentWeapon)) return 0.f;
	const UWeaponDataAsset* Data = CurrentWeapon->GetWeaponData();
	return Data ? Data->ReloadTime : 0.f;
}

// --- Aim Inaccuracy ---

void ACompanionCharacter::SetAimTarget(AActor* NewTarget)
{
	if (NewTarget != CurrentAimTarget.Get())
	{
		CurrentAimTarget = NewTarget;
		TimeAimingAtCurrentTarget = 0.0f;
	}
}

float ACompanionCharacter::GetCurrentInaccuracy() const
{
	const float Alpha = FMath::Clamp(TimeAimingAtCurrentTarget / InaccuracySettleTime, 0.0f, 1.0f);
	return FMath::Lerp(MaxInaccuracyDegrees, MinInaccuracyDegrees, Alpha);
}

// --- Death ---

void ACompanionCharacter::HandleDeath()
{
	UE_LOG(LogCompanion, Log, TEXT("%s died"), *GetName());

	SetActorTickEnabled(false);

	if (IsValid(CurrentWeapon))
		CurrentWeapon->StopFiring();

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
		Movement->StopMovementImmediately();

	if (const UWorld* World = GetWorld())
		World->GetTimerManager().SetTimer(DestroyTimerHandle, this, &ACompanionCharacter::DestroyAfterDeath, DestroyDelay, false);
}

void ACompanionCharacter::DestroyAfterDeath()
{
	Destroy();
}

// --- Traversal ---

void ACompanionCharacter::HandleTraversalStarted(ETraversalType Type, float PlayRate, FVector /*ObstacleLocation*/, FVector /*LandingLocation*/)
{
	if (HasAuthority()) SetSprinting(false);

	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!MeshComp) return;
	if (UCompanionAnimInstance* Anim = Cast<UCompanionAnimInstance>(MeshComp->GetAnimInstance()))
	{
		Anim->PlayTraversalMontage(Type, PlayRate);

		FOnMontageEnded EndDelegate;
		EndDelegate.BindUObject(this, &ACompanionCharacter::OnTraversalMontageEnded);
		Anim->Montage_SetEndDelegate(EndDelegate, Anim->GetCurrentActiveMontage());
	}
}

void ACompanionCharacter::OnTraversalMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (IsValid(TraversalComponent))
		TraversalComponent->EndTraversal();
}

void ACompanionCharacter::HandleTraversalEnded()
{
	SetSprinting(false);
}
