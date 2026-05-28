// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExtractionPlayer.h"
#include "ExtractionAnimInstance.h"
#include "TraversalComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "HealthComponent.h"
#include "WeaponComponent.h"
#include "WeaponBase.h"
#include "ExtractionDamageType.h"
#include "TimerManager.h"
#include "Engine/DamageEvents.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "Extraction.h"

AExtractionPlayer::AExtractionPlayer()
	: bIsDBNO(false)
	, BleedoutTimeRemaining(0.f)
	, ReviveElapsed(0.f)
	, bIsReviving(false)
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = true;

	HealthComponent   = CreateDefaultSubobject<UHealthComponent>(TEXT("HealthComponent"));
	WeaponComponent   = CreateDefaultSubobject<UWeaponComponent>(TEXT("WeaponComponent"));
	TraversalComponent = CreateDefaultSubobject<UTraversalComponent>(TEXT("TraversalComponent"));

	// Default bone-to-region map (UE5 mannequin skeleton)
	BoneToHitRegionMap.Reserve(25);
	BoneToHitRegionMap.Add(FName("head"),        EHitRegion::Head);
	BoneToHitRegionMap.Add(FName("neck_01"),     EHitRegion::Torso);
	BoneToHitRegionMap.Add(FName("neck_02"),     EHitRegion::Torso);
	BoneToHitRegionMap.Add(FName("spine_01"),    EHitRegion::Torso);
	BoneToHitRegionMap.Add(FName("spine_02"),    EHitRegion::Torso);
	BoneToHitRegionMap.Add(FName("spine_03"),    EHitRegion::Torso);
	BoneToHitRegionMap.Add(FName("spine_04"),    EHitRegion::Torso);
	BoneToHitRegionMap.Add(FName("spine_05"),    EHitRegion::Torso);
	BoneToHitRegionMap.Add(FName("pelvis"),      EHitRegion::Torso);
	BoneToHitRegionMap.Add(FName("clavicle_l"),  EHitRegion::Arms);
	BoneToHitRegionMap.Add(FName("upperarm_l"),  EHitRegion::Arms);
	BoneToHitRegionMap.Add(FName("lowerarm_l"),  EHitRegion::Arms);
	BoneToHitRegionMap.Add(FName("hand_l"),      EHitRegion::Arms);
	BoneToHitRegionMap.Add(FName("clavicle_r"),  EHitRegion::Arms);
	BoneToHitRegionMap.Add(FName("upperarm_r"),  EHitRegion::Arms);
	BoneToHitRegionMap.Add(FName("lowerarm_r"),  EHitRegion::Arms);
	BoneToHitRegionMap.Add(FName("hand_r"),      EHitRegion::Arms);
	BoneToHitRegionMap.Add(FName("thigh_l"),     EHitRegion::Legs);
	BoneToHitRegionMap.Add(FName("calf_l"),      EHitRegion::Legs);
	BoneToHitRegionMap.Add(FName("foot_l"),      EHitRegion::Legs);
	BoneToHitRegionMap.Add(FName("thigh_r"),     EHitRegion::Legs);
	BoneToHitRegionMap.Add(FName("calf_r"),      EHitRegion::Legs);
	BoneToHitRegionMap.Add(FName("foot_r"),      EHitRegion::Legs);
	BoneToHitRegionMap.Add(FName("ball_l"),      EHitRegion::Legs);
	BoneToHitRegionMap.Add(FName("ball_r"),      EHitRegion::Legs);
}

void AExtractionPlayer::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	if (IsValid(TraversalComponent))
	{
		TraversalComponent->OnTraversalStarted.AddUObject(this, &AExtractionPlayer::HandleTraversalStarted);
		TraversalComponent->OnTraversalEnded.AddUObject(this, &AExtractionPlayer::HandleTraversalEnded);
	}

	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		CachedAnimInstance = Cast<UExtractionAnimInstance>(MeshComp->GetAnimInstance());
		if (!IsValid(CachedAnimInstance))
			UE_LOG(LogExtraction, Warning, TEXT("'%s': AnimInstance is not UExtractionAnimInstance — check ABP parent class."), *GetNameSafe(this));
	}
}

void AExtractionPlayer::BeginPlay()
{
	Super::BeginPlay();

	if (IsValid(HealthComponent))
		HealthComponent->OnDeath.AddDynamic(this, &AExtractionPlayer::HandleDeath);

	// Late-join / standalone catch-up: re-fire OnWeaponEquipped if weapon already equipped
	if (IsLocallyControlled() && !GetIsDBNO() && IsValid(WeaponComponent))
	{
		AWeaponBase* ExistingWeapon = WeaponComponent->GetCurrentWeapon();
		if (IsValid(ExistingWeapon))
			OnWeaponEquipped(ExistingWeapon);
	}
}

void AExtractionPlayer::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsValid(TraversalComponent))
	{
		TraversalComponent->OnTraversalStarted.RemoveAll(this);
		TraversalComponent->OnTraversalEnded.RemoveAll(this);
	}

	if (IsValid(HealthComponent))
		HealthComponent->OnDeath.RemoveDynamic(this, &AExtractionPlayer::HandleDeath);

	if (const UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(BleedoutTimerHandle);

	Super::EndPlay(EndPlayReason);
}

void AExtractionPlayer::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bIsDBNO && BleedoutTimeRemaining > 0.f)
		BleedoutTimeRemaining = FMath::Max(BleedoutTimeRemaining - DeltaTime, 0.f);

	if (IsLocallyControlled() && bIsReviving)
		UpdateRevive(DeltaTime);

	if (IsLocallyControlled() && IsValid(WeaponComponent))
	{
		AWeaponBase* Weapon = WeaponComponent->GetCurrentWeapon();
		if (IsValid(Weapon))
			Weapon->UpdateRecoilRecovery(DeltaTime);
	}
}

void AExtractionPlayer::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AExtractionPlayer, bIsDBNO);
}

// ---- Input Binding ----

void AExtractionPlayer::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!IsValid(EnhancedInput))
	{
		UE_LOG(LogExtraction, Error, TEXT("'%s': Failed to find an Enhanced Input Component!"), *GetNameSafe(this));
		return;
	}

	if (MoveAction)
		EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AExtractionPlayer::MoveInput);
	else
		UE_LOG(LogExtraction, Warning, TEXT("'%s': MoveAction is null — assign in BP child class."), *GetNameSafe(this));

	if (LookAction)
		EnhancedInput->BindAction(LookAction, ETriggerEvent::Triggered, this, &AExtractionPlayer::LookInput);
	else
		UE_LOG(LogExtraction, Warning, TEXT("'%s': LookAction is null — assign in BP child class."), *GetNameSafe(this));

	if (MouseLookAction)
		EnhancedInput->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AExtractionPlayer::LookInput);
	else
		UE_LOG(LogExtraction, Warning, TEXT("'%s': MouseLookAction is null — assign in BP child class."), *GetNameSafe(this));

	if (FireAction)
	{
		EnhancedInput->BindAction(FireAction, ETriggerEvent::Started, this, &AExtractionPlayer::FireStart);
		EnhancedInput->BindAction(FireAction, ETriggerEvent::Completed, this, &AExtractionPlayer::FireStop);
	}

	if (ReloadAction)
		EnhancedInput->BindAction(ReloadAction, ETriggerEvent::Started, this, &AExtractionPlayer::ReloadStart);

	if (ADSAction)
	{
		EnhancedInput->BindAction(ADSAction, ETriggerEvent::Started, this, &AExtractionPlayer::ADSStart);
		EnhancedInput->BindAction(ADSAction, ETriggerEvent::Completed, this, &AExtractionPlayer::ADSStop);
	}

	if (VaultAction)
		EnhancedInput->BindAction(VaultAction, ETriggerEvent::Started, this, &AExtractionPlayer::VaultStart);
	else
		UE_LOG(LogExtraction, Warning, TEXT("'%s': VaultAction is null — assign in BP child class."), *GetNameSafe(this));

	if (InteractAction)
	{
		EnhancedInput->BindAction(InteractAction, ETriggerEvent::Started, this, &AExtractionPlayer::InteractStart);
		EnhancedInput->BindAction(InteractAction, ETriggerEvent::Completed, this, &AExtractionPlayer::InteractStop);
	}
	else
	{
		UE_LOG(LogExtraction, Warning, TEXT("'%s': InteractAction is null — assign in BP child class."), *GetNameSafe(this));
	}

	// Temp debug: H key applies 25 damage
	PlayerInputComponent->BindKey(EKeys::H, IE_Pressed, this, &AExtractionPlayer::DebugApplyDamage);
}

// ---- Core Input Handlers ----

void AExtractionPlayer::MoveInput(const FInputActionValue& Value)
{
	const FVector2D MovementVector = Value.Get<FVector2D>();
	DoMove(MovementVector.X, MovementVector.Y);
}

void AExtractionPlayer::LookInput(const FInputActionValue& Value)
{
	const FVector2D LookAxisVector = Value.Get<FVector2D>();

	// Cancel recoil recovery when player moves mouse
	if (IsValid(WeaponComponent))
	{
		AWeaponBase* Weapon = WeaponComponent->GetCurrentWeapon();
		if (IsValid(Weapon))
			Weapon->CancelRecoilRecovery();
	}

	DoAim(LookAxisVector.X, LookAxisVector.Y);
}

void AExtractionPlayer::DoAim(float Yaw, float Pitch)
{
	if (!IsValid(GetController())) return;

	AddControllerYawInput(Yaw);
	AddControllerPitchInput(Pitch);
}

void AExtractionPlayer::DoMove(float Right, float Forward)
{
	if (bIsDBNO) return;
	if (IsInTraversal()) return;
	if (!IsValid(GetController())) return;

	AddMovementInput(GetActorRightVector(), Right);
	AddMovementInput(GetActorForwardVector(), Forward);
}

// ---- Weapon Input ----

void AExtractionPlayer::FireStart(const FInputActionValue& Value)
{
	if (bIsDBNO) return;
	if (IsInTraversal()) return;
	if (!IsValid(WeaponComponent)) return;

	WeaponComponent->StartFire();
}

void AExtractionPlayer::FireStop(const FInputActionValue& Value)
{
	if (!IsValid(WeaponComponent)) return;
	WeaponComponent->StopFire();
}

void AExtractionPlayer::ReloadStart(const FInputActionValue& Value)
{
	if (bIsDBNO) return;
	if (IsInTraversal()) return;
	if (!IsValid(WeaponComponent)) return;

	WeaponComponent->StartReload();
}

void AExtractionPlayer::ADSStart(const FInputActionValue& Value)
{
	if (bIsDBNO) return;
	if (IsInTraversal()) return;
	if (!IsValid(WeaponComponent)) return;

	// TODO: notify kit BP to cancel sprint on ADS entry via BIE

	WeaponComponent->SetAiming(true);
	OnADSChanged(true);
}

void AExtractionPlayer::ADSStop(const FInputActionValue& Value)
{
	if (!IsValid(WeaponComponent)) return;

	WeaponComponent->SetAiming(false);
	OnADSChanged(false);
}

// ---- Traversal Input ----

void AExtractionPlayer::VaultStart(const FInputActionValue& Value)
{
	UE_LOG(LogExtraction, Warning, TEXT("[VAULT_DEBUG] VaultStart fired on '%s'"), *GetNameSafe(this));
	TryStartTraversal();
}

bool AExtractionPlayer::TryStartTraversal()
{
	if (bIsDBNO) return false;
	if (IsInTraversal()) return false;
	if (!IsValid(TraversalComponent)) return false;

	const UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!IsValid(MoveComp)) return false;
	if (!MoveComp->IsMovingOnGround()) return false;

	FVector SnapTarget;
	ETraversalType DetectedType;
	if (!TraversalComponent->DetectTraversalAhead(SnapTarget, DetectedType)) return false;

	TraversalComponent->ExecuteByType(DetectedType, false);
	return true;
}

// ---- Traversal Callbacks ----

void AExtractionPlayer::HandleTraversalStarted(ETraversalType Type, float PlayRate, FVector /*ObstacleLocation*/, FVector /*LandingLocation*/)
{
	if (bIsDBNO) return;

	UE_LOG(LogExtraction, Warning, TEXT("[VAULT_DEBUG] HandleTraversalStarted type=%d playRate=%.2f"), (int32)Type, PlayRate);

	UExtractionAnimInstance* AnimInst = CachedAnimInstance;
	UE_LOG(LogExtraction, Warning, TEXT("[VAULT_DEBUG] CachedAnimInstance is %s"), *GetNameSafe(AnimInst));
	if (!IsValid(AnimInst)) return;

	switch (Type)
	{
	case ETraversalType::Vault:
		AnimInst->PlayVaultMontage(PlayRate);
		UE_LOG(LogExtraction, Warning, TEXT("[VAULT_DEBUG] PlayVaultMontage called"));
		break;
	case ETraversalType::Climb:
		AnimInst->PlayClimbMontage(PlayRate);
		UE_LOG(LogExtraction, Warning, TEXT("[VAULT_DEBUG] PlayClimbMontage called"));
		break;
	case ETraversalType::Mantle:
		AnimInst->PlayMantleMontage(PlayRate);
		UE_LOG(LogExtraction, Warning, TEXT("[VAULT_DEBUG] PlayMantleMontage called"));
		break;
	default: break;
	}

	FOnMontageEnded EndDelegate;
	EndDelegate.BindUObject(this, &AExtractionPlayer::OnTraversalMontageEnded);
	AnimInst->Montage_SetEndDelegate(EndDelegate, AnimInst->GetCurrentActiveMontage());
	UE_LOG(LogExtraction, Warning, TEXT("[VAULT_DEBUG] End delegate bound to active montage=%s"),
		*GetNameSafe(AnimInst->GetCurrentActiveMontage()));
}

void AExtractionPlayer::OnTraversalMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (IsValid(TraversalComponent))
		TraversalComponent->EndTraversal();
}

void AExtractionPlayer::HandleTraversalEnded()
{
	// No sprint state to restore — kit BP owns sprint.
	UE_LOG(LogExtraction, Verbose, TEXT("'%s' traversal ended"), *GetNameSafe(this));
}

ETraversalType AExtractionPlayer::GetActiveTraversalType() const
{
	if (!IsValid(TraversalComponent)) return ETraversalType::None;
	return TraversalComponent->GetActiveType();
}

bool AExtractionPlayer::IsInTraversal() const
{
	if (!IsValid(TraversalComponent)) return false;
	return TraversalComponent->IsInTraversal();
}

bool AExtractionPlayer::GetIsVaulting() const
{
	return GetActiveTraversalType() == ETraversalType::Vault;
}

FVector AExtractionPlayer::GetVaultTargetLocation() const
{
	if (!IsValid(TraversalComponent)) return FVector::ZeroVector;
	return TraversalComponent->GetVaultTargetLocation();
}

float AExtractionPlayer::GetVaultSurfaceHeight() const
{
	if (!IsValid(TraversalComponent)) return 0.f;
	return TraversalComponent->GetVaultSurfaceHeight();
}

// ---- Interaction / Revive Input ----

void AExtractionPlayer::InteractStart(const FInputActionValue& Value)
{
	if (bIsDBNO) return;
	if (!IsLocallyControlled()) return;

	AExtractionPlayer* Target = FindReviveTarget();
	if (!IsValid(Target)) return;

	ReviveTarget = Target;
	ReviveElapsed = 0.f;
	bIsReviving = true;

	UE_LOG(LogExtraction, Verbose, TEXT("'%s' began reviving '%s'"), *GetNameSafe(this), *GetNameSafe(Target));
}

void AExtractionPlayer::InteractStop(const FInputActionValue& Value)
{
	if (!IsLocallyControlled()) return;
	if (bIsReviving) CancelRevive();
}

// ---- Controller Changed ----

void AExtractionPlayer::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();

	// Late-join catch-up: controller may arrive after OnRep_CurrentWeapon already fired
	if (!IsLocallyControlled() || GetIsDBNO() || !IsValid(WeaponComponent)) return;

	AWeaponBase* CurrentWeapon = WeaponComponent->GetCurrentWeapon();
	if (!IsValid(CurrentWeapon)) return;

	UE_LOG(LogExtraction, Verbose, TEXT("'%s': NotifyControllerChanged catch-up fired OnWeaponEquipped for '%s'."),
		*GetNameSafe(this), *GetNameSafe(CurrentWeapon));
	OnWeaponEquipped(CurrentWeapon);
}

// ---- Damage ----

float AExtractionPlayer::GetHitboxDamageMultiplier(const FDamageEvent& DamageEvent) const
{
	if (!DamageEvent.IsOfType(FPointDamageEvent::ClassID))
		return 1.0f;

	const FPointDamageEvent& PointDamage = static_cast<const FPointDamageEvent&>(DamageEvent);

	const EHitRegion* Region = BoneToHitRegionMap.Find(PointDamage.HitInfo.BoneName);
	if (!Region) return 1.0f;

	if (!PointDamage.DamageTypeClass) return 1.0f;

	const UExtractionDamageType* DmgType = Cast<UExtractionDamageType>(
		PointDamage.DamageTypeClass->GetDefaultObject());
	if (!IsValid(DmgType)) return 1.0f;

	return DmgType->GetMultiplierForRegion(*Region);
}

float AExtractionPlayer::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	const float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	const float FinalDamage = ActualDamage * GetHitboxDamageMultiplier(DamageEvent);

	if (IsValid(HealthComponent))
		HealthComponent->TakeDamage(FinalDamage);

	return FinalDamage;
}

// ---- Health / DBNO ----

void AExtractionPlayer::HandleDeath()
{
	EnterDBNO();
}

void AExtractionPlayer::EnterDBNO()
{
	if (bIsDBNO) return;
	bIsDBNO = true;

	// Cancel active revive (downed player can't finish reviving someone)
	if (bIsReviving) CancelRevive();

	if (IsValid(TraversalComponent) && TraversalComponent->IsInTraversal())
		TraversalComponent->CancelTraversal();

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (IsValid(MoveComp))
	{
		MoveComp->StopMovementImmediately();
		MoveComp->DisableMovement();
	}

	// Start bleedout timer (server only)
	if (HasAuthority())
	{
		BleedoutTimeRemaining = BleedoutDuration;

		UWorld* World = GetWorld();
		if (IsValid(World))
		{
			World->GetTimerManager().SetTimer(
				BleedoutTimerHandle, this,
				&AExtractionPlayer::OnBleedoutExpired,
				BleedoutDuration, false);
		}
	}

	OnDBNOStateChanged.Broadcast(true, BleedoutDuration);
	UE_LOG(LogExtraction, Log, TEXT("'%s' entered DBNO (%.0fs bleedout)"), *GetNameSafe(this), BleedoutDuration);
}

void AExtractionPlayer::ExitDBNO()
{
	if (!bIsDBNO) return;
	bIsDBNO = false;

	if (const UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(BleedoutTimerHandle);

	BleedoutTimeRemaining = 0.f;

	if (IsValid(HealthComponent))
		HealthComponent->Revive(ReviveHealthPercent);

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (IsValid(MoveComp))
		MoveComp->SetMovementMode(MOVE_Walking);

	OnDBNOStateChanged.Broadcast(false, 0.f);
	UE_LOG(LogExtraction, Log, TEXT("'%s' revived at %.0f%% health"), *GetNameSafe(this), ReviveHealthPercent * 100.f);
}

void AExtractionPlayer::OnBleedoutExpired()
{
	if (!bIsDBNO) return;

	UE_LOG(LogExtraction, Log, TEXT("'%s' bleedout expired — full death"), *GetNameSafe(this));
	FullDeath();
}

void AExtractionPlayer::FullDeath()
{
	bIsDBNO = false;
	BleedoutTimeRemaining = 0.f;

	if (const UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(BleedoutTimerHandle);

	// TODO: Ragdoll, drop loot, spectate camera
	UE_LOG(LogExtraction, Log, TEXT("'%s' is fully dead"), *GetNameSafe(this));
}

void AExtractionPlayer::OnRep_IsDBNO()
{
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (IsValid(MoveComp))
	{
		if (bIsDBNO)
		{
			MoveComp->StopMovementImmediately();
			MoveComp->DisableMovement();
		}
		else
		{
			MoveComp->SetMovementMode(MOVE_Walking);
		}
	}

	if (bIsDBNO)
		BleedoutTimeRemaining = BleedoutDuration;

	OnDBNOStateChanged.Broadcast(bIsDBNO, bIsDBNO ? BleedoutDuration : 0.f);
}

void AExtractionPlayer::DebugApplyDamage()
{
	if (!HasAuthority()) return;
	if (!IsValid(HealthComponent)) return;

	HealthComponent->TakeDamage(25.f);
	UE_LOG(LogExtraction, Log, TEXT("Debug: Applied 25 damage. Health=%.0f/%.0f Shield=%.0f/%.0f"),
		HealthComponent->GetCurrentHealth(), HealthComponent->GetMaxHealth(),
		HealthComponent->GetCurrentShield(), HealthComponent->GetMaxShield());
}

// ---- Revive ----

AExtractionPlayer* AExtractionPlayer::FindReviveTarget() const
{
	const UWorld* World = GetWorld();
	if (!IsValid(World)) return nullptr;

	// Use the active camera manager for view location/direction — works regardless of BP camera naming
	const APlayerCameraManager* CamManager = UGameplayStatics::GetPlayerCameraManager(this, 0);
	if (!IsValid(CamManager)) return nullptr;

	const FVector TraceStart = CamManager->GetCameraLocation();
	const FVector TraceEnd = TraceStart + CamManager->GetCameraRotation().Vector() * ReviveTraceDistance;

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	FHitResult HitResult;
	const FCollisionShape SweepShape = FCollisionShape::MakeSphere(ReviveTraceSphereRadius);

	const bool bHit = World->SweepSingleByChannel(
		HitResult, TraceStart, TraceEnd, FQuat::Identity,
		ECC_Pawn, SweepShape, QueryParams);

	if (!bHit) return nullptr;

	AExtractionPlayer* HitPlayer = Cast<AExtractionPlayer>(HitResult.GetActor());
	if (!IsValid(HitPlayer)) return nullptr;
	if (!HitPlayer->GetIsDBNO()) return nullptr;

	// TODO: Validate team membership when team system exists
	return HitPlayer;
}

void AExtractionPlayer::UpdateRevive(float DeltaTime)
{
	if (!IsValid(ReviveTarget) || !ReviveTarget->GetIsDBNO())
	{
		CancelRevive();
		return;
	}

	const float DistSq = FVector::DistSquared(GetActorLocation(), ReviveTarget->GetActorLocation());
	if (DistSq > FMath::Square(ReviveProximityRadius))
	{
		CancelRevive();
		return;
	}

	ReviveElapsed += DeltaTime;
	if (ReviveElapsed >= ReviveDuration) CompleteRevive();
}

void AExtractionPlayer::CancelRevive()
{
	if (!bIsReviving) return;

	UE_LOG(LogExtraction, Verbose, TEXT("'%s' cancelled revive on '%s'"),
		*GetNameSafe(this), *GetNameSafe(ReviveTarget));

	bIsReviving = false;
	ReviveElapsed = 0.f;
	ReviveTarget = nullptr;
}

void AExtractionPlayer::CompleteRevive()
{
	if (!IsValid(ReviveTarget))
	{
		CancelRevive();
		return;
	}

	UE_LOG(LogExtraction, Log, TEXT("'%s' completed revive on '%s'"),
		*GetNameSafe(this), *GetNameSafe(ReviveTarget));

	Server_CompleteRevive(ReviveTarget);

	bIsReviving = false;
	ReviveElapsed = 0.f;
	ReviveTarget = nullptr;
}

void AExtractionPlayer::Server_CompleteRevive_Implementation(AExtractionPlayer* Target)
{
	if (!IsValid(Target)) return;
	if (!Target->GetIsDBNO()) return;
	if (bIsDBNO) return;

	const float DistSq = FVector::DistSquared(GetActorLocation(), Target->GetActorLocation());
	if (DistSq > FMath::Square(ReviveProximityRadius))
	{
		UE_LOG(LogExtraction, Warning, TEXT("Server_CompleteRevive: '%s' too far from '%s'"),
			*GetNameSafe(this), *GetNameSafe(Target));
		return;
	}

	// TODO: Validate team membership when team system exists
	Target->ExitDBNO();
}
