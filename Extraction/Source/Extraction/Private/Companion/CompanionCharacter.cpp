// AI companion character — follows player, engages enemies, revives downed teammates.

#include "CompanionCharacter.h"
#include "AI/AITargetingStatics.h"
#include "AI/CompanionDiag.h"
#include "AI/CompanionTuningDataAsset.h"
#include "CompanionAIController.h"
#include "HealthComponent.h"
#include "WeaponBase.h"
#include "WeaponDataAsset.h"
#include "TraversalComponent.h"
#include "CompanionAnimInstance.h"
#include "SuppressionComponent.h"
#include "CoverPoseComponent.h"
#include "ExtractionTypes.h"
#include "Character/ExtractionPlayer.h"
#include "EnemyCharacter.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "UI/OverheadWidgetComponent.h"
#include "UI/CompanionModeIndicatorWidget.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "HAL/IConsoleManager.h" // companion.AimLog diagnostics

DEFINE_LOG_CATEGORY(LogCompanion);

ACompanionCharacter::ACompanionCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetNetUpdateFrequency(10.0f);
	SetMinNetUpdateFrequency(5.0f);

	GetCapsuleComponent()->SetCapsuleSize(34.0f, 88.0f);

	HealthComponent = CreateDefaultSubobject<UHealthComponent>(TEXT("HealthComponent"));
	SuppressionComponent = CreateDefaultSubobject<USuppressionComponent>(TEXT("SuppressionComponent"));
	CoverPoseComponent = CreateDefaultSubobject<UCoverPoseComponent>(TEXT("CoverPoseComponent"));
	TraversalComponent = CreateDefaultSubobject<UTraversalComponent>(TEXT("TraversalComponent"));

	HealthWidgetComponent = CreateDefaultSubobject<UOverheadWidgetComponent>(TEXT("HealthWidget"));
	HealthWidgetComponent->SetupAttachment(GetMesh(), TEXT("head"));
	HealthWidgetComponent->SetRelativeLocation(FVector(0.f, 0.f, 30.f));
	HealthWidgetComponent->SetWidgetSpace(EWidgetSpace::Screen);
	HealthWidgetComponent->SetDrawSize(FVector2D(150.f, 40.f));
	HealthWidgetComponent->SetPivot(FVector2D(0.5f, 0.5f));
	HealthWidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HealthWidgetComponent->SetTwoSided(false);
	HealthWidgetComponent->SetVisibility(false);

	// Sits above the health bar so the two never overlap.
	ModeWidgetComponent = CreateDefaultSubobject<UOverheadWidgetComponent>(TEXT("ModeWidget"));
	ModeWidgetComponent->SetupAttachment(GetMesh(), TEXT("head"));
	ModeWidgetComponent->SetRelativeLocation(FVector(0.f, 0.f, 60.f));
	ModeWidgetComponent->SetWidgetSpace(EWidgetSpace::Screen);
	ModeWidgetComponent->SetDrawSize(FVector2D(64.f, 64.f));
	ModeWidgetComponent->SetPivot(FVector2D(0.5f, 0.5f));
	ModeWidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ModeWidgetComponent->SetTwoSided(false);
	ModeWidgetComponent->SetVisibility(false);

	// Configure inherited skeletal mesh — designer assigns mesh + anim class on BP_Companion
	GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, -88.f));
	GetMesh()->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));

	// Bug 6: weapon hitscan traces ECC_Visibility, which the inherited CharacterMesh profile ignores —
	// block it on the mesh so enemy fire actually registers on the companion.
	GetMesh()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	TakedownKnifeMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("TakedownKnifeMesh"));
	TakedownKnifeMesh->SetupAttachment(GetMesh(), KnifeAttachSocket);
	TakedownKnifeMesh->SetHiddenInGame(true);
	TakedownKnifeMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TakedownKnifeMesh->SetCastShadow(false);
	TakedownKnifeMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	TakedownKnifeMesh->SetComponentTickEnabled(false);

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

void ACompanionCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// Tuning-driven speeds become reachable only once the companion AI controller is attached.
	ApplyMovementSpeeds();
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
	// PossessedBy re-applies again once the AI controller (and its tuning asset) is attached.
	ApplyMovementSpeeds();

	if (HealthComponent && !HealthComponent->OnDeath.IsAlreadyBound(this, &ACompanionCharacter::HandleDeath))
		HealthComponent->OnDeath.AddDynamic(this, &ACompanionCharacter::HandleDeath);

	if (HealthComponent && !HealthComponent->OnRevive.IsAlreadyBound(this, &ACompanionCharacter::HandleRevive))
		HealthComponent->OnRevive.AddDynamic(this, &ACompanionCharacter::HandleRevive);

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

	if (ModeWidgetClass && ModeWidgetComponent)
	{
		ModeWidgetComponent->SetWidgetClass(ModeWidgetClass);
		ModeWidgetComponent->SetVisibility(true);
		TryLinkModeWidget();
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

	DisarmCommandedTakedown();

	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DestroyTimerHandle);
		World->GetTimerManager().ClearTimer(ShootDelayTimerHandle);
		World->GetTimerManager().ClearTimer(ModeWidgetLinkTimerHandle);
	}

	if (HealthComponent)
	{
		HealthComponent->OnDeath.RemoveDynamic(this, &ACompanionCharacter::HandleDeath);
		HealthComponent->OnRevive.RemoveDynamic(this, &ACompanionCharacter::HandleRevive);
	}

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
		UE_LOG(LogCompanion, Verbose, TEXT("Companion took %.1f damage — HP: %.0f / Shield: %.0f"),
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
	if (IsValid(SuppressionComponent) && SuppressionComponent->IsSuppressed()) return true;
	if (Window <= 0.f || !GetWorld()) return false;
	return (GetWorld()->GetTimeSeconds() - LastDamageWorldTime) < Window;
}

float ACompanionCharacter::GetHealthFraction() const
{
	if (!IsValid(HealthComponent)) return 1.f;
	return HealthComponent->GetHealthPercent();
}

float ACompanionCharacter::GetAmmoFraction() const
{
	if (!IsValid(CurrentWeapon)) return 1.f;
	const UWeaponDataAsset* Data = CurrentWeapon->GetWeaponData();
	if (!Data || Data->MagazineSize <= 0) return 1.f;
	return static_cast<float>(CurrentWeapon->GetCurrentAmmo()) / static_cast<float>(Data->MagazineSize);
}

void ACompanionCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(ACompanionCharacter, bIsSprinting, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(ACompanionCharacter, Posture, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(ACompanionCharacter, bLowReadyAim, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(ACompanionCharacter, Mode, COND_SkipOwner);
}

// --- Crouch diagnostics ---

void ACompanionCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	if (HasAuthority())
		UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: [OnStartCrouch] t=%.3f halfAdj=%.1f capsHalfH=%.1f meshRelZ=%.1f worldZ=%.1f"),
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
		UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: [OnEndCrouch] t=%.3f halfAdj=%.1f capsHalfH=%.1f meshRelZ=%.1f worldZ=%.1f"),
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

// --- Mode API ---

void ACompanionCharacter::SetMode(ECompanionMode NewMode)
{
	if (!HasAuthority()) return;
	if (NewMode == Mode) return;

	// Same ordering invariant as SetPosture: Mode assigned before broadcast.
	const bool bWasStealthActive = IsStealthActive();
	Mode = NewMode;

	// A fresh Stealth order always starts unbroken; leaving Stealth clears the flag so a later
	// return to Stealth doesn't inherit a stale broken state.
	bStealthBroken = false;

	// Clamps only on a stealth-active transition — an unconditional call would UnCrouch on every
	// non-stealth mode cycle, popping the companion out of crouch cover mid-fight. A stale catch-up
	// stage from the last stealth stint would sprint-pop the clamps; follow re-evaluates next tick.
	if (IsStealthActive() != bWasStealthActive)
	{
		StealthCatchupStage = EStealthCatchup::None;
		ApplyStealthMovementClamps();
	}

	UE_LOG(LogCompanion, Log, TEXT("Companion mode -> %s"), *UEnum::GetValueAsString(Mode));
	OnRep_Mode();
}

void ACompanionCharacter::OnRep_Mode()
{
	// No state mutation — broadcast only. See SetMode for ordering invariant.
	OnModeChanged.Broadcast(Mode);
}

void ACompanionCharacter::SetStealthBroken(bool bBroken)
{
	if (!HasAuthority()) return;
	if (bStealthBroken == bBroken) return;

	bStealthBroken = bBroken;
	// Stale catch-up stage from before the break would make the re-pin clamps sprint-pop.
	StealthCatchupStage = EStealthCatchup::None;
	UE_LOG(LogCompanion, Log, TEXT("Companion stealth %s"), bBroken ? TEXT("BROKEN") : TEXT("re-pinned"));
	ApplyStealthMovementClamps();
}

void ACompanionCharacter::TryLinkModeWidget()
{
	if (!IsValid(ModeWidgetComponent)) return;

	if (UCompanionModeIndicatorWidget* ModeWidget = Cast<UCompanionModeIndicatorWidget>(ModeWidgetComponent->GetUserWidgetObject()))
	{
		ModeWidget->SetCompanion(this);
		return;
	}

	// Widget not constructed yet (screen-space widgets build on first render) — retry shortly.
	if (UWorld* World = GetWorld())
		World->GetTimerManager().SetTimer(ModeWidgetLinkTimerHandle, this, &ACompanionCharacter::TryLinkModeWidget, 0.25f, false);
}

void ACompanionCharacter::ApplyStealthMovementClamps()
{
	// Stance enforcement yields to systems that own stance/aim right now (mirrors the BT service's
	// enforcement-yield set): takedowns own the crouch-approach and the authored knife pose,
	// traversal resizes the capsule mid-vault, route legs set their own stances. A stealth break /
	// mode keypress landing mid-vault must not Crouch/UnCrouch under them. Speeds always apply.
	bool bStanceOwnedElsewhere = bTakedownArmed || bTakedownExecuting || bTakedownMontagePlaying
		|| (IsValid(TraversalComponent) && TraversalComponent->IsBusy());
	if (!bStanceOwnedElsewhere)
		if (const AAIController* AIC = Cast<AAIController>(GetController()))
			if (const UBlackboardComponent* BB = AIC->GetBlackboardComponent())
				bStanceOwnedElsewhere = BB->GetValueAsBool(ACompanionAIController::BB_RouteActive);

	if (!bStanceOwnedElsewhere)
	{
		if (IsStealthActive())
		{
			if (StealthCatchupStage == EStealthCatchup::Sprint)
			{
				// Clearly losing the player — break the low profile and sprint to close the gap.
				// SetSprinting's stealth veto passes only in this stage.
				UnCrouch();
				SetSprinting(true);
			}
			else
			{
				SetSprinting(false);
				if (CanCrouch()) Crouch();
			}
		}
		else
		{
			// Combat tasks own crouch during a fight; releasing here just returns the follow/explore
			// paths to standing. Cover tasks re-crouch as needed.
			UnCrouch();
		}
	}
	ApplyMovementSpeeds();
}

void ACompanionCharacter::SetStealthCatchup(EStealthCatchup NewStage)
{
	if (!HasAuthority()) return;
	if (StealthCatchupStage == NewStage) return;

	StealthCatchupStage = NewStage;
	if (IsStealthActive()) ApplyStealthMovementClamps();
	else ApplyMovementSpeeds();
}

// --- Low Ready Aim ---

void ACompanionCharacter::SetLowReadyAim(bool bNewLowReady)
{
	if (!HasAuthority()) return;
	if (bLowReadyAim == bNewLowReady) return;
	bLowReadyAim = bNewLowReady;

	// AimLog diagnostics (companion.AimLog 1): change-only, so cost is the edge, not per call.
	if (const IConsoleVariable* AimLogCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("companion.AimLog"));
		AimLogCVar && AimLogCVar->GetInt() != 0)
		UE_LOG(LogCompanion, Display, TEXT("[AimLog] SetLowReadyAim -> %d"), (int32)bNewLowReady);

	OnRep_LowReadyAim();
}

void ACompanionCharacter::OnRep_LowReadyAim()
{
	OnLowReadyAimChanged.Broadcast(bLowReadyAim);
}

// --- Scripted Aim ---

void ACompanionCharacter::SetScriptedAim(bool bNewScriptedAim)
{
	bScriptedAim = bNewScriptedAim;
}

// --- Sprint API ---

void ACompanionCharacter::SetSprinting(bool bSprint)
{
	if (!HasAuthority()) return;
	// Stealth rules: no sprinting — except the Sprint catch-up stage (clearly losing the player).
	if (bSprint && IsStealthActive() && StealthCatchupStage != EStealthCatchup::Sprint) return;
	if (bIsSprinting == bSprint) return;

	bIsSprinting = bSprint;
	OnRep_IsSprinting();
}

void ACompanionCharacter::OnRep_IsSprinting()
{
	ApplyMovementSpeeds();
}

const UCompanionTuningDataAsset* ACompanionCharacter::GetTuning() const
{
	const ACompanionAIController* AIC = Cast<ACompanionAIController>(GetController());
	return AIC ? AIC->GetTuning() : nullptr;
}

float ACompanionCharacter::TunedWalkSpeed() const
{
	const UCompanionTuningDataAsset* T = GetTuning();
	return T ? T->WalkSpeed : WalkSpeed;
}

float ACompanionCharacter::TunedSprintSpeed() const
{
	const UCompanionTuningDataAsset* T = GetTuning();
	return T ? T->SprintSpeed : SprintSpeed;
}

float ACompanionCharacter::TunedCrouchedWalkSpeed() const
{
	const UCompanionTuningDataAsset* T = GetTuning();
	const bool bFastCrouch = IsStealthActive() && StealthCatchupStage == EStealthCatchup::FastCrouch;
	if (bFastCrouch && T) return T->StealthCrouchCatchupSpeed;
	return T ? T->CrouchedWalkSpeed : CrouchedWalkSpeed;
}

void ACompanionCharacter::ApplyMovementSpeeds()
{
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!MoveComp) return;

	MoveComp->MaxWalkSpeed = bIsSprinting ? TunedSprintSpeed() : TunedWalkSpeed();
	MoveComp->MaxWalkSpeedCrouched = TunedCrouchedWalkSpeed();
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
	if (USkeletalMeshComponent* M = GetMesh())
		if (UCompanionAnimInstance* A = Cast<UCompanionAnimInstance>(M->GetAnimInstance()))
			A->AddRecoilImpulse();
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

bool ACompanionCharacter::IsCurrentWeaponSuppressed() const
{
	if (!IsValid(CurrentWeapon)) return false;
	const UWeaponDataAsset* Data = CurrentWeapon->GetWeaponData();
	return Data && Data->bSuppressed;
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
		// AimLog diagnostics (companion.AimLog 1): change-only.
		if (const IConsoleVariable* AimLogCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("companion.AimLog"));
			AimLogCVar && AimLogCVar->GetInt() != 0)
			UE_LOG(LogCompanion, Display, TEXT("[AimLog] SetAimTarget %s -> %s"),
				*GetNameSafe(CurrentAimTarget.Get()), *GetNameSafe(NewTarget));

		CurrentAimTarget = NewTarget;
		TimeAimingAtCurrentTarget = 0.0f;
	}
}

float ACompanionCharacter::GetCurrentInaccuracy() const
{
	const float Alpha = FMath::Clamp(TimeAimingAtCurrentTarget / InaccuracySettleTime, 0.0f, 1.0f);
	return FMath::Lerp(MaxInaccuracyDegrees, MinInaccuracyDegrees, Alpha);
}

FVector ACompanionCharacter::GetAimPointForTarget(const AActor* Target) const
{
	return AITargeting::GetSightLocation(Target);
}

// --- Death ---

void ACompanionCharacter::HandleDeath()
{
	UE_LOG(LogCompanion, Log, TEXT("%s died"), *GetName());

	// FIX 1: Tear down any armed takedown immediately on death
	DisarmCommandedTakedown();
	if (ACompanionAIController* CompAIC = Cast<ACompanionAIController>(GetController()))
		CompAIC->ClearActiveCommand();

	SetActorTickEnabled(false);

	if (IsValid(TraversalComponent))
		TraversalComponent->CancelTraversal();

	if (IsValid(CurrentWeapon))
	{
		CurrentWeapon->StopFiring();
		CurrentWeapon->CancelReload();
	}

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

void ACompanionCharacter::HandleRevive()
{
	UE_LOG(LogCompanion, Log, TEXT("%s revived"), *GetName());

	GetWorldTimerManager().ClearTimer(DestroyTimerHandle);

	SetActorTickEnabled(true);

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
		Movement->SetMovementMode(MOVE_Walking);
}

// --- Traversal ---

void ACompanionCharacter::HandleTraversalStarted(ETraversalType Type, float PlayRate, FVector /*ObstacleLocation*/, FVector /*LandingLocation*/)
{
	if (HasAuthority()) SetSprinting(false);

	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!MeshComp) return;
	UCompanionAnimInstance* Anim = Cast<UCompanionAnimInstance>(MeshComp->GetAnimInstance());
	if (!Anim) return;

	// Resolve the exact montage asset BEFORE playing it so the end-delegate is bound to
	// the correct montage even when another montage (e.g. fire loop) is currently active.
	// Binding to GetCurrentActiveMontage() after Play would race against any montage that
	// the play call itself interrupted or that was already occupying the slot.
	UAnimMontage* Played = Anim->GetMontageForType(Type);
	Anim->PlayTraversalMontage(Type, PlayRate);

	if (IsValid(Played))
	{
		FOnMontageEnded EndDelegate;
		EndDelegate.BindUObject(this, &ACompanionCharacter::OnTraversalMontageEnded);
		Anim->Montage_SetEndDelegate(EndDelegate, Played);
	}
	else if (IsValid(TraversalComponent))
	{
		// No montage configured — end traversal immediately so the companion is never
		// stranded in MOVE_Flying + no collision waiting for a delegate that won't fire.
		TraversalComponent->EndTraversal();
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

// --- Commanded Takedown ---

void ACompanionCharacter::ArmCommandedTakedown(AActor* Victim, ETakedownMethod Method)
{
	if (bTakedownArmed) DisarmCommandedTakedown();
	if (!IsValid(Victim)) return;

	TakedownVictim = Victim;
	TakedownActiveMethod = Method;
	bTakedownArmed = true;
	bTakedownPlayerCommitted = false;
	bTakedownCommandedInstant = false;
	bTakedownKillRegistered = false;
	bTakedownInPosition = false;
	bTakedownExecuting = false;
	bTakedownMontagePlaying = false;

	// Aim at the victim
	SetAimTarget(Victim);

	// Shoot: raise weapon + face immediately so the companion lines up the instant it's commanded
	// (and stays aimed through the autonomous 2-4s wait / until the player's synced shot).
	if (Method == ETakedownMethod::Shoot)
	{
		SetLowReadyAim(false);
		if (AAIController* AIC = Cast<AAIController>(GetController()))
			AIC->SetFocus(Victim, EAIFocusPriority::Gameplay);
	}

	// Bind to the player's commit delegate — method-specific
	ACompanionAIController* CompAIC = Cast<ACompanionAIController>(GetController());
	APawn* PlayerPawn = IsValid(CompAIC) ? CompAIC->GetPlayerCharacter() : nullptr;
	AExtractionPlayer* Player = Cast<AExtractionPlayer>(PlayerPawn);
	if (IsValid(Player))
	{
		TakedownPlayerRef = Player;
		if (Method == ETakedownMethod::Shoot)
			Player->OnPlayerFiredWeapon.AddDynamic(this, &ACompanionCharacter::OnPlayerFiredWeaponHandler);
		else
			Player->OnPlayerTakedownCommitted.AddDynamic(this, &ACompanionCharacter::OnPlayerTakedownCommittedHandler);
	}

	UE_LOG(LogCompanion, Log, TEXT("Takedown armed: victim=%s method=%s"),
		*GetNameSafe(Victim), Method == ETakedownMethod::Knife ? TEXT("Knife") : TEXT("Shoot"));
}

void ACompanionCharacter::DisarmCommandedTakedown()
{
	if (!bTakedownArmed && !bTakedownMontagePlaying) return;

	StopWeaponFire();

	// Unbind both delegates — safe even if only one was bound
	AExtractionPlayer* Player = TakedownPlayerRef.Get();
	if (IsValid(Player))
	{
		Player->OnPlayerTakedownCommitted.RemoveDynamic(this, &ACompanionCharacter::OnPlayerTakedownCommittedHandler);
		Player->OnPlayerFiredWeapon.RemoveDynamic(this, &ACompanionCharacter::OnPlayerFiredWeaponHandler);
	}

	if (const UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(ShootDelayTimerHandle);

	if (AActor* IgnoredVictim = TakedownVictim.Get())
	{
		if (UCapsuleComponent* Capsule = GetCapsuleComponent())
			Capsule->IgnoreActorWhenMoving(IgnoredVictim, false);
		if (ACharacter* VictimChar = Cast<ACharacter>(IgnoredVictim))
			if (UCapsuleComponent* VictimCapsule = VictimChar->GetCapsuleComponent())
				VictimCapsule->IgnoreActorWhenMoving(this, false);
	}

	// If we tear down mid-stab (companion death / BT abort), kill the frozen victim now — attributed to
	// us — instead of leaving it frozen-alive until the enemy's own watchdog fires with no instigator.
	if (bTakedownMontagePlaying)
		if (AEnemyCharacter* DyingVictim = Cast<AEnemyCharacter>(TakedownVictim.Get()))
			DyingVictim->FinishTakedownKill(this);

	TakedownVictim.Reset();
	TakedownPlayerRef.Reset();
	bTakedownArmed = false;
	bTakedownPlayerCommitted = false;
	bTakedownCommandedInstant = false;
	bTakedownKillRegistered = false;
	bTakedownInPosition = false;
	bTakedownExecuting = false;
	bTakedownCrouchApproach = false;
	bTakedownMontagePlaying = false;
	SetAimTarget(nullptr);
	SetLowReadyAim(true);
	if (IsValid(CurrentWeapon)) CurrentWeapon->SetWeaponHidden(false);
	if (IsValid(TakedownKnifeMesh)) TakedownKnifeMesh->SetHiddenInGame(true);
	if (AAIController* AIC = Cast<AAIController>(GetController()))
		AIC->ClearFocus(EAIFocusPriority::Gameplay);

	UE_LOG(LogCompanion, Log, TEXT("Takedown disarmed — broadcasting finished"));
	OnCommandedTakedownFinished.Broadcast();
}

void ACompanionCharacter::OnPlayerTakedownCommittedHandler()
{
	UE_LOG(LogCompanion, Warning, TEXT("[Takedown-Companion] player-commit signal received (armed=%d inPosition=%d)"),
		(int32)bTakedownArmed, (int32)bTakedownInPosition);
	if (!bTakedownArmed) return;

	bTakedownPlayerCommitted = true;
	if (bTakedownInPosition) ExecuteCommandedTakedown();
	else UE_LOG(LogCompanion, Warning, TEXT("[Takedown-Companion] committed but NOT in position yet — will execute on arrival"));
}

void ACompanionCharacter::OnPlayerFiredWeaponHandler()
{
	UE_LOG(LogCompanion, Log, TEXT("[Takedown-Companion] player-fired signal received (armed=%d inPosition=%d)"),
		(int32)bTakedownArmed, (int32)bTakedownInPosition);
	// bTakedownExecuting guard: a follow-up player shot mid-chain (autonomous commit already in
	// flight) must not convert the remaining timers to the instant cadence.
	if (!bTakedownArmed || bTakedownExecuting) return;

	// Player pulled the trigger — the synced kill must land this frame, not on the phased cadence.
	bTakedownCommandedInstant = true;
	bTakedownPlayerCommitted = true;
	if (bTakedownInPosition) ExecuteCommandedTakedown();
}

void ACompanionCharacter::SetTakedownInPosition(bool bInPos)
{
	bTakedownInPosition = bInPos;

	if (bInPos && bTakedownPlayerCommitted && bTakedownArmed)
	{
		UE_LOG(LogCompanion, Log, TEXT("Takedown: in position with pending player commit — executing now"));
		ExecuteCommandedTakedown();
	}
}

void ACompanionCharacter::CommitTakedownNow()
{
	if (!bTakedownArmed || bTakedownExecuting) return;
	bTakedownCommandedInstant = false; // autonomous solo — keep the phased cadence
	bTakedownPlayerCommitted = true;
	if (bTakedownInPosition) ExecuteCommandedTakedown();
	// If not yet in position, SetTakedownInPosition(true) will fire it (committed already set).
}

void ACompanionCharacter::ExecuteCommandedTakedown()
{
	if (bTakedownExecuting) return;   // re-entry guard

	UE_LOG(LogCompanion, Warning, TEXT("[Takedown-Companion] ExecuteCommandedTakedown: committed=%d inPos=%d armed=%d method=%d"),
		(int32)bTakedownPlayerCommitted, (int32)bTakedownInPosition, (int32)bTakedownArmed, (int32)TakedownActiveMethod);
	if (!bTakedownPlayerCommitted || !bTakedownInPosition || !bTakedownArmed) return;

	bTakedownExecuting = true;

	AActor* Victim = TakedownVictim.Get();
	if (!IsValid(Victim))
	{
		FinishCommandedTakedown();
		return;
	}

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Victim);
	if (!IsValid(Enemy))
	{
		FinishCommandedTakedown();
		return;
	}

	if (TakedownActiveMethod == ETakedownMethod::Knife)
	{
		// No montage assigned → instant kill BEFORE we freeze/teleport anyone (a paired hold with no
		// attacker animation would leave the victim reacting to a T-posing companion).
		if (!IsValid(KnifeTakedownMontage))
		{
			UE_LOG(LogCompanion, Warning, TEXT("Takedown: no KnifeTakedownMontage assigned — instant-kill fallback"));
			Enemy->FinishTakedownKill(this);
			FinishCommandedTakedown();
			return;
		}

		OnCommandedTakedownStarted.Broadcast();
		if (IsValid(CurrentWeapon)) CurrentWeapon->SetWeaponHidden(true);
		if (IsValid(TakedownKnifeMesh)) TakedownKnifeMesh->SetHiddenInGame(false);

		// Authored placement, measured from the StealthFinishers pack's own demo map: for
		// Paired_Knife_Stealth_ClavicleStabDown the victim sits 115cm along the attacker MESH's +Y,
		// same facing (dYaw 0). Both characters carry a -90 mesh yaw, which maps that mesh-+Y onto
		// actor FORWARD — so the layout is: the companion stands CommandedTakedownOffset BEHIND the
		// (stationary) victim, both sharing the victim's facing, and the stab lands as the Att pose
		// lunges in. Offset is victim-relative-to-attacker in that shared frame (X fwd, Y right, Z up).
		UnCrouch();
		SetTakedownCrouchApproach(false);

		const float SnapYaw = Enemy->GetActorRotation().Yaw;
		const FVector SnapLoc = Enemy->GetActorLocation();   // victim stays where it stands
		const float WatchdogTimeout = KnifeTakedownMontage->GetPlayLength() + 1.f;

		// Hold the victim FIRST (freeze in place + play its Vic reaction, broadcasts OnTakedownExecuted);
		// relocate ourselves only once that succeeds, so a failed hold never strands the companion.
		if (!Enemy->BeginTakedownHold(this, SnapLoc, SnapYaw, WatchdogTimeout))
		{
			UE_LOG(LogCompanion, Warning, TEXT("Takedown: BeginTakedownHold failed — aborting"));
			FinishCommandedTakedown();
			return;
		}

		// Companion stands behind the victim (victim − offset). Keep our OWN grounded Z (capsule
		// half-heights can differ), and sweep from the victim toward the spot so an enemy backed
		// against a wall can't embed us in geometry — mirrors AExtractionPlayer::StartMontageDeferred.
		const FVector OffsetWorld = FRotator(0.f, SnapYaw, 0.f).RotateVector(CommandedTakedownOffset);
		FVector CompLoc(SnapLoc.X - OffsetWorld.X, SnapLoc.Y - OffsetWorld.Y, GetActorLocation().Z);
		if (const UWorld* World = GetWorld())
		{
			static constexpr float SweepRadius = 30.f;
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(CompanionTakedownPlace), false, this);
			Params.AddIgnoredActor(Victim);
			if (World->SweepSingleByChannel(Hit, FVector(SnapLoc.X, SnapLoc.Y, CompLoc.Z), CompLoc,
				FQuat::Identity, ECC_WorldStatic, FCollisionShape::MakeSphere(SweepRadius), Params))
				CompLoc = Hit.Location;
		}
		SetActorLocation(CompLoc, false, nullptr, ETeleportType::TeleportPhysics);
		SetActorRotation(FRotator(0.f, SnapYaw, 0.f));

		// Mutually ignore capsule collision so the two never shove apart — lets the bodies sit as close
		// as CommandedTakedownOffset asks, even when the gap is below the summed capsule radii. Both
		// directions are cleared in FinishCommandedTakedown / DisarmCommandedTakedown.
		if (UCapsuleComponent* Capsule = GetCapsuleComponent())
			Capsule->IgnoreActorWhenMoving(Victim, true);
		if (UCapsuleComponent* VictimCapsule = Enemy->GetCapsuleComponent())
			VictimCapsule->IgnoreActorWhenMoving(this, true);

		// Kill at the stab/montage end, NOT instantly (instant ragdolls the victim mid-grapple).
		UAnimInstance* AnimInst = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
		const float MontageLen = IsValid(AnimInst) ? AnimInst->Montage_Play(KnifeTakedownMontage) : 0.f;
		if (MontageLen <= 0.f)
		{
			UE_LOG(LogCompanion, Warning, TEXT("Takedown: knife montage did not play — instant-kill fallback"));
			Enemy->FinishTakedownKill(this);   // never leave the victim frozen-alive
			FinishCommandedTakedown();
			return;
		}

		Enemy->SetTakedownWasMontageDriven(true);
		bTakedownMontagePlaying = true;
		FOnMontageEnded EndDelegate;
		EndDelegate.BindUObject(this, &ACompanionCharacter::OnTakedownMontageEnded);
		AnimInst->Montage_SetEndDelegate(EndDelegate, KnifeTakedownMontage);
		UE_LOG(LogCompanion, Warning, TEXT("[Takedown-Companion] knife executed (behind victim) on victim=%s"), *GetNameSafe(Victim));
	}
	else // Shoot — phased aim-in → cosmetic fire → kill → lower
	{
		if (!CanFire())
		{
			UE_LOG(LogCompanion, Warning, TEXT("Takedown: shoot aborted — cannot fire (no ammo or weapon)"));
			FinishCommandedTakedown();
			return;
		}

		// LoS trace from companion eyes to victim
		const UWorld* World = GetWorld();
		if (!World)
		{
			FinishCommandedTakedown();
			return;
		}

		FVector EyesLoc;
		FRotator EyesRot;
		GetActorEyesViewPoint(EyesLoc, EyesRot);

		FHitResult Hit;
		FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(CompanionShootTakedown), false, this);
		TraceParams.AddIgnoredActor(Victim);
		if (IsValid(CurrentWeapon)) TraceParams.AddIgnoredActor(CurrentWeapon);

		const FVector TraceEnd = GetAimPointForTarget(Victim);
		if (World->LineTraceSingleByChannel(Hit, EyesLoc, TraceEnd, ECC_Visibility, TraceParams))
		{
			UE_LOG(LogCompanion, Warning, TEXT("Takedown: shoot aborted — LoS blocked by %s"), *GetNameSafe(Hit.GetActor()));
			FinishCommandedTakedown();
			return;
		}

		// Phase 0: aim in — face + raise weapon
		SetAimTarget(Victim);
		SetLowReadyAim(false);
		if (AAIController* AIC = Cast<AAIController>(GetController()))
			AIC->SetFocus(Victim, EAIFocusPriority::Gameplay);

		TakedownShotsRemaining = FMath::Max(ShootShotCount, 0);

		// Player-synced commit: the enemy must die on the player's shot frame. Register the kill
		// FIRST — the player's own (unsuppressed) shot can alert the victim out of Unaware inside
		// even the double-tap window, and ExecuteTakedown's reject would turn the synced takedown
		// into a visible whiff. The double-tap then plays over the first ragdoll frames, which
		// reads as the killing burst. The autonomous path keeps the phased aim-in cadence below.
		if (bTakedownCommandedInstant)
		{
			if (!Enemy->ExecuteTakedown(this, /*bIgnoreRangeAndArc=*/true))
			{
				UE_LOG(LogCompanion, Warning, TEXT("Takedown: instant shoot kill rejected (enemy no longer Unaware)"));
				FinishCommandedTakedown();
				return;
			}
			bTakedownKillRegistered = true;

			if (TakedownShotsRemaining <= 0) HandleTakedownKill();
			else HandleTakedownAimedIn();
		}
		else if (TakedownShotsRemaining <= 0)
		{
			// Rate <= 0 silently clears a timer instead of firing — call zero-delay steps directly.
			if (ShootAimInDuration > 0.f)
				World->GetTimerManager().SetTimer(ShootDelayTimerHandle,
					FTimerDelegate::CreateUObject(this, &ACompanionCharacter::HandleTakedownKill),
					ShootAimInDuration, false);
			else
				HandleTakedownKill();
		}
		else
		{
			if (ShootAimInDuration > 0.f)
				World->GetTimerManager().SetTimer(ShootDelayTimerHandle,
					FTimerDelegate::CreateUObject(this, &ACompanionCharacter::HandleTakedownAimedIn),
					ShootAimInDuration, false);
			else
				HandleTakedownAimedIn();
		}
	}
}

void ACompanionCharacter::OnTakedownMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	bTakedownMontagePlaying = false;
	// Montage-driven death: the kill lands at the END of the stab montage, not mid-grapple.
	if (AEnemyCharacter* DeadVictim = Cast<AEnemyCharacter>(TakedownVictim.Get()))
		DeadVictim->FinishTakedownKill(this);
	FinishCommandedTakedown();
}

void ACompanionCharacter::FireCosmeticShotAt(const FVector& AimEndPoint)
{
	if (!IsValid(CurrentWeapon)) return;

	// Ensure the fire arms montage is running (idempotent — won't restart if already playing)
	if (USkeletalMeshComponent* MeshComp = GetMesh())
		if (UCompanionAnimInstance* AnimInst = Cast<UCompanionAnimInstance>(MeshComp->GetAnimInstance()))
			AnimInst->PlayFireMontage(1.0f);

	CurrentWeapon->FireCosmetic(AimEndPoint);
}

void ACompanionCharacter::HandleTakedownAimedIn()
{
	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(TakedownVictim.Get());
	if (!IsValid(Enemy))
	{
		FinishCommandedTakedown();
		return;
	}

	// Fire one cosmetic shot
	FireCosmeticShotAt(GetAimPointForTarget(Enemy));
	--TakedownShotsRemaining;

	const UWorld* World = GetWorld();
	if (!World) { FinishCommandedTakedown(); return; }

	// Commanded (player-synced) chain runs on near-zero intervals; a rate <= 0 would silently
	// clear the timer instead of firing, so zero-delay steps are called directly.
	if (TakedownShotsRemaining > 0)
	{
		const float ShotGap = bTakedownCommandedInstant ? ShootCommandedShotInterval : ShootShotInterval;
		if (ShotGap > 0.f)
			World->GetTimerManager().SetTimer(ShootDelayTimerHandle,
				FTimerDelegate::CreateUObject(this, &ACompanionCharacter::HandleTakedownAimedIn),
				ShotGap, false);
		else
			HandleTakedownAimedIn();
	}
	else
	{
		const float KillDelay = bTakedownCommandedInstant ? ShootCommandedKillDelay : ShootShotInterval;
		if (KillDelay > 0.f)
			World->GetTimerManager().SetTimer(ShootDelayTimerHandle,
				FTimerDelegate::CreateUObject(this, &ACompanionCharacter::HandleTakedownKill),
				KillDelay, false);
		else
			HandleTakedownKill();
	}
}

void ACompanionCharacter::HandleTakedownKill()
{
	StopWeaponFire();

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(TakedownVictim.Get());
	if (!IsValid(Enemy))
	{
		FinishCommandedTakedown();
		return;
	}

	// Instant chain already registered the kill before the cosmetic shots — don't re-execute on
	// the corpse (it would reject and mislabel a successful takedown as whiffed).
	if (!bTakedownKillRegistered && !Enemy->ExecuteTakedown(this, /*bIgnoreRangeAndArc=*/true))
	{
		UE_LOG(LogCompanion, Warning, TEXT("Takedown: shoot kill rejected (enemy no longer Unaware)"));
		FinishCommandedTakedown();
		return;
	}

	UE_LOG(LogCompanion, Log, TEXT("Takedown: shoot executed"));

	const UWorld* World = GetWorld();
	if (!World) { FinishCommandedTakedown(); return; }

	// Rate <= 0 silently clears a timer instead of firing — ShootLowerDelay=0 must still finish
	// the chain (a hang here leaves bTakedownExecuting stuck and the BT takedown branch waiting).
	if (ShootLowerDelay > 0.f)
		World->GetTimerManager().SetTimer(ShootDelayTimerHandle,
			FTimerDelegate::CreateUObject(this, &ACompanionCharacter::HandleTakedownLower),
			ShootLowerDelay, false);
	else
		HandleTakedownLower();
}

void ACompanionCharacter::HandleTakedownLower()
{
	SetLowReadyAim(true);
	SetAimTarget(nullptr);
	if (AAIController* AIC = Cast<AAIController>(GetController()))
		AIC->ClearFocus(EAIFocusPriority::Gameplay);

	FinishCommandedTakedown();
}

void ACompanionCharacter::FinishCommandedTakedown()
{
	const bool bWasArmed = bTakedownArmed;

	StopWeaponFire();

	// Unbind both delegates before broadcast to avoid re-entry
	AExtractionPlayer* Player = TakedownPlayerRef.Get();
	if (IsValid(Player))
	{
		Player->OnPlayerTakedownCommitted.RemoveDynamic(this, &ACompanionCharacter::OnPlayerTakedownCommittedHandler);
		Player->OnPlayerFiredWeapon.RemoveDynamic(this, &ACompanionCharacter::OnPlayerFiredWeaponHandler);
	}

	if (const UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(ShootDelayTimerHandle);

	if (AActor* IgnoredVictim = TakedownVictim.Get())
	{
		if (UCapsuleComponent* Capsule = GetCapsuleComponent())
			Capsule->IgnoreActorWhenMoving(IgnoredVictim, false);
		if (ACharacter* VictimChar = Cast<ACharacter>(IgnoredVictim))
			if (UCapsuleComponent* VictimCapsule = VictimChar->GetCapsuleComponent())
				VictimCapsule->IgnoreActorWhenMoving(this, false);
	}

	TakedownVictim.Reset();
	TakedownPlayerRef.Reset();
	bTakedownArmed = false;
	bTakedownPlayerCommitted = false;
	bTakedownCommandedInstant = false;
	bTakedownKillRegistered = false;
	bTakedownInPosition = false;
	bTakedownExecuting = false;
	bTakedownCrouchApproach = false;
	bTakedownMontagePlaying = false;

	SetAimTarget(nullptr);
	if (IsValid(CurrentWeapon)) CurrentWeapon->SetWeaponHidden(false);
	if (IsValid(TakedownKnifeMesh)) TakedownKnifeMesh->SetHiddenInGame(true);
	if (AAIController* AIC = Cast<AAIController>(GetController()))
		AIC->ClearFocus(EAIFocusPriority::Gameplay);

	if (bWasArmed)
	{
		UE_LOG(LogCompanion, Log, TEXT("Takedown: finished, broadcasting completion"));
		OnCommandedTakedownFinished.Broadcast();
	}
}
