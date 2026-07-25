// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExtractionPlayer.h"
#include "ExtractionPlayerMovement.h"
#include "Components/CompanionCommandComponent.h"
#include "Components/ConsumableInventoryComponent.h"
#include "AI/AITargetingStatics.h"
#include "Perception/AISightTargetInterface.h"
#include "Perception/AISense_Sight.h"
#include "ExtractionAnimInstance.h"
#include "TraversalComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "HealthComponent.h"
#include "FootstepNoiseComponent.h"
#include "EnemyCharacter.h"
#include "Companion/CompanionCharacter.h"
#include "Extractee/ExtracteeCompanion.h"
#include "EngineUtils.h"
#include "WeaponComponent.h"
#include "WeaponBase.h"
#include "ExtractionDamageType.h"
#include "TimerManager.h"
#include "Engine/DamageEvents.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "DrawDebugHelpers.h"
#include "ExtractionTypes.h"
#include "Extraction.h"
#include "AnimNotify_TakedownKill.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "EnemyDebug.h"
#include "World/Lootable.h"
#include "World/BreachableDoor.h"
#include "Audio/GameAudioSubsystem.h"
#include "Audio/SurfaceAudioBank.h"
#include "World/WorldInteractable.h"
#include "Game/ExtractionGameMode.h"

AExtractionPlayer::AExtractionPlayer(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UExtractionPlayerMovement>(
		ACharacter::CharacterMovementComponentName))
	, bIsDBNO(false)
	, BleedoutTimeRemaining(0.f)
	, ReviveElapsed(0.f)
	, bIsReviving(false)
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = true;

	HealthComponent   = CreateDefaultSubobject<UHealthComponent>(TEXT("HealthComponent"));
	FootstepNoiseComponent = CreateDefaultSubobject<UFootstepNoiseComponent>(TEXT("FootstepNoiseComponent"));
	WeaponComponent   = CreateDefaultSubobject<UWeaponComponent>(TEXT("WeaponComponent"));
	TraversalComponent = CreateDefaultSubobject<UTraversalComponent>(TEXT("TraversalComponent"));
	CompanionCommandComponent = CreateDefaultSubobject<UCompanionCommandComponent>(TEXT("CompanionCommandComponent"));
	ConsumableInventoryComponent = CreateDefaultSubobject<UConsumableInventoryComponent>(TEXT("ConsumableInventoryComponent"));

	// Bug 6: weapon hitscan traces ECC_Visibility, which the inherited CharacterMesh profile ignores —
	// block it on the mesh so enemy fire registers on the player.
	if (USkeletalMeshComponent* MeshComp = GetMesh()) MeshComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

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

	OwnedTags.AddTag(TAG_Character_Player);
}

void AExtractionPlayer::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
{
	TagContainer.AppendTags(OwnedTags);
}

// --- IAISightTargetInterface ---

UAISense_Sight::EVisibilityResult AExtractionPlayer::CanBeSeenFrom(
	const FCanBeSeenFromContext& Context,
	FVector& OutSeenLocation,
	int32& OutNumberOfLoSChecksPerformed,
	int32& OutNumberOfAsyncLosCheckRequested,
	float& OutSightStrength,
	int32* UserData,
	const FOnPendingVisibilityQueryProcessedDelegate* Delegate)
{
	OutNumberOfAsyncLosCheckRequested = 0;
	OutNumberOfLoSChecksPerformed = 1;
	OutSightStrength = 0.f;

	const bool bAllowHead = AITargeting::ShouldIncludeHeadForObserver(Context.IgnoreActor, this);
	const bool bVisible = AITargeting::GetVisibleBodyPoint(this, Context.ObserverLocation, Context.IgnoreActor, OutSeenLocation, bAllowHead);

	if (bVisible)
		OutSightStrength = 1.f;

#if !UE_BUILD_SHIPPING
	if (GetDetectionLogLevel() > 0)
	{
		TWeakObjectPtr<const AActor> ObsKey(Context.IgnoreActor);
		const bool* LastResult = DebugLastCanBeSeenResult.Find(ObsKey);
		if (!LastResult || *LastResult != bVisible)
		{
			DebugLastCanBeSeenResult.Add(ObsKey, bVisible);
			UE_LOG(LogTemp, Warning, TEXT("[DETECTDBG] CanBeSeenFrom obs=%s result=%s seenZ=%.0f playerZ=%.0f crouched=%d"),
				*GetNameSafe(Context.IgnoreActor),
				bVisible ? TEXT("VISIBLE") : TEXT("NOT-VISIBLE"),
				bVisible ? OutSeenLocation.Z : -1.f,
				GetActorLocation().Z,
				bIsCrouched ? 1 : 0);
		}
	}
#endif

	return bVisible ? UAISense_Sight::EVisibilityResult::Visible : UAISense_Sight::EVisibilityResult::NotVisible;
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
		// Null on the shipped setup: the pristine kit ABP_Manny is a plain UAnimInstance.
		// Montage playback (traversal, being-revived) goes through the generic instance.
		CachedAnimInstance = Cast<UExtractionAnimInstance>(MeshComp->GetAnimInstance());
		if (!IsValid(CachedAnimInstance))
			UE_LOG(LogExtraction, Verbose, TEXT("'%s': AnimInstance is not UExtractionAnimInstance — montages play on the generic instance."), *GetNameSafe(this));
	}
}

void AExtractionPlayer::BeginPlay()
{
	Super::BeginPlay();

	if (IsValid(HealthComponent))
		HealthComponent->OnDeath.AddDynamic(this, &AExtractionPlayer::HandleDeath);

	if (IsValid(ConsumableInventoryComponent))
		ConsumableInventoryComponent->OnStimUsedNative.AddUObject(this, &AExtractionPlayer::HandleStimUsed);

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
#if !UE_BUILD_SHIPPING
	DebugLastCanBeSeenResult.Empty();
#endif

	if (IsValid(TraversalComponent))
	{
		TraversalComponent->OnTraversalStarted.RemoveAll(this);
		TraversalComponent->OnTraversalEnded.RemoveAll(this);
	}

	if (IsValid(HealthComponent))
		HealthComponent->OnDeath.RemoveDynamic(this, &AExtractionPlayer::HandleDeath);

	if (IsValid(ConsumableInventoryComponent))
		ConsumableInventoryComponent->OnStimUsedNative.RemoveAll(this);

	if (const UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(BleedoutTimerHandle);

	// Takedown was committed — kill the frozen victim so player despawn can't leave them stuck alive.
	if (AEnemyCharacter* Victim = PendingTakedownVictim.Get())
		Victim->FinishTakedownKill(this);
	PendingTakedownVictim.Reset();
	bTakedownMontageActive = false;

	CancelRevive();          // reviver-side teardown (idempotent no-op when not reviving)
	CancelInteractHold();    // idempotent no-op when no hold is running
	SetBeingRevived(false);  // patient-side teardown (companion-revives-player direction)

	Super::EndPlay(EndPlayReason);
}

void AExtractionPlayer::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bIsDBNO && BleedoutTimeRemaining > 0.f)
		BleedoutTimeRemaining = FMath::Max(BleedoutTimeRemaining - DeltaTime, 0.f);

	if (bIsDBNO)
	{
		UCharacterMovementComponent* MoveComp = GetCharacterMovement();
		if (IsValid(MoveComp))
		{
			MoveComp->MaxWalkSpeed = DBNOCrawlSpeed;
			MoveComp->MaxWalkSpeedCrouched = DBNOCrawlSpeed;
			if (!bIsCrouched && MoveComp->GetNavAgentPropertiesRef().bCanCrouch) Crouch();
		}

		// Not during being-revived: look input is locked and AlignForRevive owns the body's facing.
		if (IsLocallyControlled() && !bBeingRevivedAnimActive)
		{
			// Body drifts toward the camera yaw (rate-limited) instead of snapping or orienting
			// to velocity — keeps the crawl blendspace's Direction input meaningful so the
			// sideways/backward downed anims actually play.
			if (const AController* C = GetController())
			{
				FRotator ActorRot = GetActorRotation();
				const float NewYaw = FMath::FixedTurn(ActorRot.Yaw, C->GetControlRotation().Yaw, DBNOCrawlRotationRate * DeltaTime);
				if (!FMath::IsNearlyEqual(NewYaw, ActorRot.Yaw))
				{
					ActorRot.Yaw = NewYaw;
					SetActorRotation(ActorRot);
				}
			}

			if (bDBNOFreeLookActive) ClampDBNOFreeLook();
		}
	}

	// Yaw-follow watchdog: three systems save/restore bUseControllerRotationYaw and a cross-
	// clobbered restore leaves it stuck off — the body stops tracking the camera and the kit
	// ABP's aim-offset layers contort the pose (the recurring "float at doors"). Nothing holds
	// yaw-follow off outside these states (the kit BP's freelook rows are unwired), so a stuck
	// flag here is always the latch: heal it and name the moment.
	if (!bUseControllerRotationYaw && !bIsReviving && !bBeingRevivedAnimActive && !bIsDBNO
		&& !bTakedownMontageActive && !IsInTraversal())
	{
		const UAnimInstance* WatchdogAnim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
		UE_LOG(LogExtraction, Warning,
			TEXT("'%s': yaw-follow stuck off with no owning system — restoring (montage=%s)"),
			*GetName(), *GetNameSafe(WatchdogAnim ? WatchdogAnim->GetCurrentActiveMontage() : nullptr));
		bUseControllerRotationYaw = true;
	}

	if (IsLocallyControlled() && bIsReviving)
		UpdateRevive(DeltaTime);

	if (IsLocallyControlled() && bIsInteractHolding)
		UpdateInteractHold(DeltaTime);

	if (IsLocallyControlled())
	{
		UpdateReviveCandidateScan(DeltaTime);
		UpdateInteractCandidateScan(DeltaTime);
	}

	if (IsLocallyControlled() && IsValid(WeaponComponent))
	{
		AWeaponBase* Weapon = WeaponComponent->GetCurrentWeapon();
		if (IsValid(Weapon))
			Weapon->UpdateRecoilRecovery(DeltaTime);
	}

	if (IsLocallyControlled())
	{
		if (bIsDBNO)
		{
			AutoLeanTargetAlpha = 0.f;
		}
		else if (bAutoLeanActive)
		{
			LeanProbeAccumulator += DeltaTime;
			if (LeanProbeAccumulator >= LeanProbeInterval)
			{
				UpdateAutoLean(DeltaTime);
				LeanProbeAccumulator = 0.f;
			}
		}
		else
		{
			AutoLeanTargetAlpha = 0.f;
		}

		AutoLeanAlpha = FMath::FInterpTo(AutoLeanAlpha, AutoLeanTargetAlpha, DeltaTime, AutoLeanInterpSpeed);
	}

	if (IsLocallyControlled()) UpdateCompanionSoftCollision();
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

	if (EquipPrimaryAction)
		EnhancedInput->BindAction(EquipPrimaryAction, ETriggerEvent::Started, this, &AExtractionPlayer::EquipPrimaryInput);

	if (EquipSecondaryAction)
		EnhancedInput->BindAction(EquipSecondaryAction, ETriggerEvent::Started, this, &AExtractionPlayer::EquipSecondaryInput);

	if (ADSAction)
	{
		EnhancedInput->BindAction(ADSAction, ETriggerEvent::Started, this, &AExtractionPlayer::ADSStart);
		EnhancedInput->BindAction(ADSAction, ETriggerEvent::Completed, this, &AExtractionPlayer::ADSStop);
	}

	if (VaultAction)
		EnhancedInput->BindAction(VaultAction, ETriggerEvent::Started, this, &AExtractionPlayer::VaultStart);
	else
		UE_LOG(LogExtraction, Warning, TEXT("'%s': VaultAction is null — assign in BP child class."), *GetNameSafe(this));

	if (WalkAction)
	{
		EnhancedInput->BindAction(WalkAction, ETriggerEvent::Started, this, &AExtractionPlayer::WalkStart);
		EnhancedInput->BindAction(WalkAction, ETriggerEvent::Completed, this, &AExtractionPlayer::WalkStop);
		EnhancedInput->BindAction(WalkAction, ETriggerEvent::Canceled, this, &AExtractionPlayer::WalkStop);
	}
	else
		UE_LOG(LogExtraction, Warning, TEXT("'%s': WalkAction is null — assign in BP child class."), *GetNameSafe(this));

	if (InteractAction)
	{
		EnhancedInput->BindAction(InteractAction, ETriggerEvent::Started, this, &AExtractionPlayer::InteractStart);
		EnhancedInput->BindAction(InteractAction, ETriggerEvent::Completed, this, &AExtractionPlayer::InteractStop);
	}
	else
	{
		UE_LOG(LogExtraction, Warning, TEXT("'%s': InteractAction is null — assign in BP child class."), *GetNameSafe(this));
	}

	if (TakedownAction)
		EnhancedInput->BindAction(TakedownAction, ETriggerEvent::Started, this, &AExtractionPlayer::TakedownInput);

	if (UseStimAction)
		EnhancedInput->BindAction(UseStimAction, ETriggerEvent::Started, this, &AExtractionPlayer::UseStimInput);

	if (IA_CompanionPing)
		EnhancedInput->BindAction(IA_CompanionPing, ETriggerEvent::Started, this, &AExtractionPlayer::CompanionPingInput);

	if (IA_CompanionTakedownKnife)
		EnhancedInput->BindAction(IA_CompanionTakedownKnife, ETriggerEvent::Started, this, &AExtractionPlayer::CompanionConfirmTakedownKnifeInput);

	if (IA_CompanionTakedownShoot)
		EnhancedInput->BindAction(IA_CompanionTakedownShoot, ETriggerEvent::Started, this, &AExtractionPlayer::CompanionConfirmTakedownShootInput);

	if (IA_CompanionBreach)
		EnhancedInput->BindAction(IA_CompanionBreach, ETriggerEvent::Started, this, &AExtractionPlayer::CompanionConfirmBreachInput);

	if (IA_CompanionModeToggle)
		EnhancedInput->BindAction(IA_CompanionModeToggle, ETriggerEvent::Started, this, &AExtractionPlayer::CompanionModeToggleInput);

	if (IA_CompanionModeStealth)
		EnhancedInput->BindAction(IA_CompanionModeStealth, ETriggerEvent::Started, this, &AExtractionPlayer::CompanionModeSelectStealthInput);

	if (IA_CompanionModeNormal)
		EnhancedInput->BindAction(IA_CompanionModeNormal, ETriggerEvent::Started, this, &AExtractionPlayer::CompanionModeSelectNormalInput);

	if (IA_CompanionModeCombat)
		EnhancedInput->BindAction(IA_CompanionModeCombat, ETriggerEvent::Started, this, &AExtractionPlayer::CompanionModeSelectCombatInput);

	// Temp debug: H key applies 25 damage
	PlayerInputComponent->BindKey(EKeys::H, IE_Pressed, this, &AExtractionPlayer::DebugApplyDamage);
}

void AExtractionPlayer::PawnClientRestart()
{
	Super::PawnClientRestart();

	if (!IsValid(DefaultMappingContext)) return;

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!IsValid(PC)) return;

	ULocalPlayer* LP = PC->GetLocalPlayer();
	if (!IsValid(LP)) return;

	UEnhancedInputLocalPlayerSubsystem* Subsystem = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (!IsValid(Subsystem)) return;

	Subsystem->AddMappingContext(DefaultMappingContext, 0);
}

// ---- Core Input Handlers ----

void AExtractionPlayer::MoveInput(const FInputActionValue& Value)
{
	const FVector2D MovementVector = Value.Get<FVector2D>();
	DoMove(MovementVector.X, MovementVector.Y);
}

void AExtractionPlayer::LookInput(const FInputActionValue& Value)
{
	if (bIsReviving || bBeingRevivedAnimActive) return;

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
	if (bTakedownMontageActive || bIsReviving || bBeingRevivedAnimActive) return;
	AddControllerYawInput(Yaw);
	AddControllerPitchInput(Pitch);
}

void AExtractionPlayer::DoMove(float Right, float Forward)
{
	if (IsInTraversal()) return;
	if (bBeingRevivedAnimActive) return;
	if (bIsReviving) return;
	if (!IsValid(GetController())) return;

	// While downed the body no longer yaw-follows the camera (free look + orient-to-movement),
	// so actor axes drift from where the player is looking — steer camera-relative instead.
	if (bIsDBNO)
	{
		const FRotator YawRot(0.f, GetControlRotation().Yaw, 0.f);
		AddMovementInput(FRotationMatrix(YawRot).GetScaledAxis(EAxis::Y), Right);
		AddMovementInput(FRotationMatrix(YawRot).GetScaledAxis(EAxis::X), Forward);
		return;
	}

	AddMovementInput(GetActorRightVector(), Right);
	AddMovementInput(GetActorForwardVector(), Forward);
}

void AExtractionPlayer::AddMovementInput(FVector WorldDirection, float ScaleValue, bool bForce)
{
	if (bIsReviving || bBeingRevivedAnimActive) return;
	Super::AddMovementInput(WorldDirection, ScaleValue, bForce);
}

void AExtractionPlayer::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	Super::CalcCamera(DeltaTime, OutResult);
	// Revive kneel shares the takedown's geometry problem: head-mounted camera riding a montage
	// right up against another body.
	if ((bTakedownMontageActive || bIsReviving) && TakedownNearClipPlane > 0.f)
		OutResult.PerspectiveNearClipPlane = TakedownNearClipPlane;

	// No hold-camera manipulation: like traversal, the camera rides the head bone exactly as
	// the kneel montage animates it (look input is ignored for the hold). The historical spin
	// sources are fixed at THEIR sinks — intro skipped, zero blend-in, aim layers zeroed,
	// turn-in-place synced — so the head-following camera is clean.
}

// ---- Weapon Input ----

void AExtractionPlayer::FireStart(const FInputActionValue& Value)
{
	if (bIsDBNO) return;
	if (bIsReviving || bBeingRevivedAnimActive) return;
	if (IsInTraversal()) return;
	if (!IsValid(WeaponComponent)) return;

	// Kit throwable equipped: route the press to the kit grenade item and skip the hitscan path —
	// a grenade throw must not broadcast OnPlayerFiredWeapon (companion shoot-takedown sync).
	if (WeaponComponent->IsThrowableEquipped())
	{
		NotifyThrowableFirePressed();
		if (UGameAudioSubsystem* AudioSys = GetWorld()->GetSubsystem<UGameAudioSubsystem>())
			AudioSys->PlayThrowFoley();
		return;
	}

	// Snapshot the companion shoot-takedown state BEFORE broadcasting: the synchronous
	// takedown listener disarms before the actual weapon shot lands.
	bool bTakedownSnapshot = false;
	if (HasAuthority() && IsValid(CompanionCommandComponent))
	{
		ACompanionCharacter* Companion = CompanionCommandComponent->GetCompanion();
		if (IsValid(Companion))
			bTakedownSnapshot = Companion->IsShootTakedownArmed();
	}

	// Broadcast BEFORE the shot: StartFire's hitscan/kill/alert chain runs synchronously, and a
	// synced companion shoot-takedown listening on this delegate must land its kill while the
	// victim is still Unaware -- after StartFire, the player's own gunshot has already alerted it
	// and the takedown whiffs.
	OnPlayerFiredWeapon.Broadcast();
	WeaponComponent->StartFire(bTakedownSnapshot);
}

void AExtractionPlayer::FireStop(const FInputActionValue& Value)
{
	if (!IsValid(WeaponComponent)) return;
	WeaponComponent->StopFire();
}

void AExtractionPlayer::ReloadStart(const FInputActionValue& Value)
{
	if (bIsDBNO) return;
	if (bIsReviving || bBeingRevivedAnimActive) return;
	if (IsInTraversal()) return;
	if (!IsValid(WeaponComponent)) return;

	WeaponComponent->StartReload();
}

void AExtractionPlayer::EquipPrimaryInput(const FInputActionValue& Value)
{
	if (bIsDBNO) return;
	if (bIsReviving || bBeingRevivedAnimActive) return;
	if (IsInTraversal()) return;
	if (!IsValid(WeaponComponent)) return;

	WeaponComponent->SwitchToPrimary();
}

void AExtractionPlayer::EquipSecondaryInput(const FInputActionValue& Value)
{
	if (bIsDBNO) return;
	if (bIsReviving || bBeingRevivedAnimActive) return;
	if (IsInTraversal()) return;
	if (!IsValid(WeaponComponent)) return;

	WeaponComponent->SwitchToSecondary();
}

void AExtractionPlayer::ADSStart(const FInputActionValue& Value)
{
	if (bIsDBNO) return;
	if (bIsReviving || bBeingRevivedAnimActive) return;
	if (IsInTraversal()) return;
	if (!IsValid(WeaponComponent)) return;

	// TODO: notify kit BP to cancel sprint on ADS entry via BIE

	WeaponComponent->SetAiming(true);
	OnADSChanged(true);
	bAutoLeanActive = true;
}

void AExtractionPlayer::ADSStop(const FInputActionValue& Value)
{
	if (!IsValid(WeaponComponent)) return;

	WeaponComponent->SetAiming(false);
	OnADSChanged(false);
	bAutoLeanActive = false;
}

// ---- Walk Input ----

void AExtractionPlayer::WalkStart(const FInputActionValue& Value)
{
	if (bWalkHeld) return;
	bWalkHeld = true;
	OnWalkHeldChanged(true);
}

void AExtractionPlayer::WalkStop(const FInputActionValue& Value)
{
	if (!bWalkHeld) return;
	bWalkHeld = false;
	OnWalkHeldChanged(false);
}

// ---- Traversal Input ----

void AExtractionPlayer::VaultStart(const FInputActionValue& Value)
{
	UE_LOG(LogExtraction, Verbose, TEXT("[VAULT_DEBUG] VaultStart fired on '%s'"), *GetNameSafe(this));
	TryStartTraversal();
}

bool AExtractionPlayer::TryStartTraversal()
{
	if (bIsDBNO) return false;
	// Mid-revive-hold: a vault would displace the kneeling reviver AND cross-clobber the shared
	// yaw-follow save with the traversal component's own (leaves yaw-follow stuck off).
	if (bIsReviving) return false;
	// Traversal montages stop-all on the body instance — a vault input mid-takedown would
	// interrupt the finisher montage mid-kill.
	if (bTakedownMontageActive) return false;
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

UAnimMontage* AExtractionPlayer::GetTraversalMontage(ETraversalType Type) const
{
	switch (Type)
	{
	case ETraversalType::Vault:  return VaultMontage;
	case ETraversalType::Climb:  return ClimbMontage;
	case ETraversalType::Mantle: return MantleMontage;
	default: return nullptr;
	}
}

bool AExtractionPlayer::HasTraversalMontage(ETraversalType Type) const
{
	return GetTraversalMontage(Type) != nullptr;
}

void AExtractionPlayer::HandleTraversalStarted(ETraversalType Type, float PlayRate, FVector /*ObstacleLocation*/, FVector /*LandingLocation*/)
{
	if (bIsDBNO) return;

	UE_LOG(LogExtraction, Verbose, TEXT("[VAULT_DEBUG] HandleTraversalStarted type=%d playRate=%.2f"), (int32)Type, PlayRate);

	// The body mesh runs the pristine kit ABP_Manny (plain UAnimInstance) — montages come from
	// the designer-assigned properties, played on the generic instance's TraversalSlot.
	UAnimMontage* Montage = GetTraversalMontage(Type);
	USkeletalMeshComponent* MeshComp = GetMesh();
	UAnimInstance* AnimInst = IsValid(MeshComp) ? MeshComp->GetAnimInstance() : nullptr;

	// StartTraversal already switched to MOVE_Flying/no-collision, and only the montage end
	// delegate restores it promptly (the worst-case timer is seconds away) — if the montage
	// can't play, end the traversal on the spot instead of stranding the player.
	if (!IsValid(AnimInst) || !Montage || AnimInst->Montage_Play(Montage, PlayRate) <= 0.f)
	{
		UE_LOG(LogExtraction, Warning, TEXT("[VAULT_DEBUG] traversal montage unavailable (type=%d montage=%s animinst=%s) — ending traversal"),
			(int32)Type, *GetNameSafe(Montage), *GetNameSafe(AnimInst));
		if (IsValid(TraversalComponent))
			TraversalComponent->EndTraversal();
		return;
	}

	FOnMontageEnded EndDelegate;
	EndDelegate.BindUObject(this, &AExtractionPlayer::OnTraversalMontageEnded);
	AnimInst->Montage_SetEndDelegate(EndDelegate, Montage);
	UE_LOG(LogExtraction, Verbose, TEXT("[VAULT_DEBUG] End delegate bound to montage=%s"), *GetNameSafe(Montage));
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

	// Abnormal end (DBNO cancel, worst-case timer): stop any still-playing traversal montage
	// so its root motion can't drag the walking character, and so its end delegate can't fire
	// late into a NEW traversal (it lands while ActiveTraversalType is None and no-ops).
	USkeletalMeshComponent* MeshComp = GetMesh();
	UAnimInstance* AnimInst = IsValid(MeshComp) ? MeshComp->GetAnimInstance() : nullptr;
	if (!IsValid(AnimInst)) return;
	for (UAnimMontage* Montage : { VaultMontage.Get(), ClimbMontage.Get(), MantleMontage.Get() })
	{
		if (Montage && AnimInst->Montage_IsPlaying(Montage))
			AnimInst->Montage_Stop(0.25f, Montage);
	}
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

// ---- Companion Soft Collision ----

namespace
{
	/** Strip the component of Push that opposes the player's input so they can walk through. */
	FVector StripOpposingPush(const FVector& Push, const FVector& RawInput)
	{
		FVector InputDir = RawInput;
		InputDir.Z = 0.f;
		if (InputDir.IsNearlyZero()) return Push;
		InputDir = InputDir.GetSafeNormal();
		const float Opposing = FVector::DotProduct(Push, InputDir);
		return (Opposing < 0.f) ? (Push - InputDir * Opposing) : Push;
	}

	// A companion can appear after the last scan (the VIP is placed, but a respawn is a spawn), and
	// TActorIterator walks the whole level — far too heavy for the player's per-frame tick.
	// File-distinct names: anonymous namespaces merge inside a unity blob, and ExtracteeCharacter.cpp
	// declares its own CompanionRescanInterval/ExpectedCompanionCount pair.
	constexpr float SoftCollisionRescanInterval = 2.f;

	// Primary + armed VIP. Sized once on the first build; Reset() then keeps the slack, so the
	// Reserve is a deliberate no-op on every rebuild after that and none of them reallocate.
	constexpr int32 ExpectedSoftCollisionCompanionCount = 2;
}

void AExtractionPlayer::UpdateCompanionSoftCollision()
{
	if (GetIsDBNO() || bIsReviving) return;
	if (IsValid(TraversalComponent) && TraversalComponent->IsBusy()) return;

	// Two passes rather than one loop: only the primary carries the respawn re-wire, and a bail-out
	// on the primary (no command component, capsule gone) must not take the VIP's wiring with it.
	// Both return their push instead of applying it so they sum into ONE AddMovementInput: two
	// saturated pushes at CompanionPushStrength each swamp the player's own unit input vector, and
	// CMC's ScaleInputAcceleration clamps the total — the player would keep a fraction of their
	// intended direction and get shoved sideways at full walk speed in exactly the corridor this
	// separation work targets.
	FVector Push = UpdatePrimaryCompanionSoftCollision();
	Push += UpdateAllyCompanionSoftCollision();
	if (Push.IsNearlyZero()) return;

	AddMovementInput(Push.GetClampedToMaxSize(CompanionPushStrength), 1.f);
}

FVector AExtractionPlayer::UpdatePrimaryCompanionSoftCollision()
{
	if (!IsValid(CompanionCommandComponent)) return FVector::ZeroVector;
	ACompanionCharacter* Companion = CompanionCommandComponent->GetCompanion();
	if (!IsValid(Companion)) return FVector::ZeroVector;

	UCapsuleComponent* CompCapsule = Companion->GetCapsuleComponent();
	if (!IsValid(CompCapsule) || CompCapsule->GetCollisionEnabled() == ECollisionEnabled::NoCollision) return FVector::ZeroVector;

	// Old-companion-instance cleanup only (respawn swap) — the live pair's ignore flags are asserted
	// unconditionally by ApplyCompanionSoftCollision, every tick, instead of latch-gated on this. A
	// latch-gated one-time wire never noticed BTTask_RevivePlayer's SetReviveAnimsActive(false)
	// stripping the player's ignore of the companion at hold teardown (MoveIgnoreActorRemove on both
	// capsules) — pass-through stayed broken for the rest of the level after the first
	// companion-revives-player.
	if (WiredCompanion.Get() != Companion)
	{
		if (ACompanionCharacter* Old = WiredCompanion.Get())
		{
			if (UCapsuleComponent* OldCap = Old->GetCapsuleComponent())
				OldCap->IgnoreActorWhenMoving(this, false);
			GetCapsuleComponent()->IgnoreActorWhenMoving(Old, false);
		}
		WiredCompanion = Companion;
	}

	return ApplyCompanionSoftCollision(*Companion, *CompCapsule);
}

FVector AExtractionPlayer::UpdateAllyCompanionSoftCollision()
{
	RefreshSoftCollisionCompanions();

	FVector Push = FVector::ZeroVector;
	for (const TWeakObjectPtr<ACompanionCharacter>& Entry : SoftCollisionCompanions)
	{
		ACompanionCharacter* Ally = Entry.Get();
		if (!IsValid(Ally)) continue;
		if (Ally->IsPrimaryCompanion()) continue; // the pass above owns it, via the command component

		// Unpossessed = the VIP still captive at his placed spot. He is set dressing until rescued,
		// so leave him solid: pass-through would let the player stand inside a kneeling hostage for
		// the whole rescue hold. He starts blocking-free the frame the rescue possesses him.
		if (!Ally->GetController()) continue;

		UCapsuleComponent* AllyCapsule = Ally->GetCapsuleComponent();
		if (!IsValid(AllyCapsule) || AllyCapsule->GetCollisionEnabled() == ECollisionEnabled::NoCollision) continue;

		Push += ApplyCompanionSoftCollision(*Ally, *AllyCapsule);
	}

	return Push;
}

void AExtractionPlayer::RefreshSoftCollisionCompanions()
{
	const UWorld* World = GetWorld();
	if (!IsValid(World)) return;

	// Rebuild on the interval, plus immediately when an entry goes stale. Interval-gated rather
	// than empty-gated: before any companion exists an empty-list trigger would re-walk every actor
	// in the level on every frame of the player's tick.
	const float Now = World->GetTimeSeconds();
	bool bRebuild = (Now - LastCompanionScanTime) >= SoftCollisionRescanInterval;
	if (!bRebuild)
	{
		for (const TWeakObjectPtr<ACompanionCharacter>& Entry : SoftCollisionCompanions)
		{
			if (Entry.IsValid()) continue;
			bRebuild = true;
			break;
		}
	}
	if (!bRebuild) return;

	LastCompanionScanTime = Now;
	SoftCollisionCompanions.Reset();
	SoftCollisionCompanions.Reserve(ExpectedSoftCollisionCompanionCount);
	for (TActorIterator<ACompanionCharacter> It(World); It; ++It)
	{
		ACompanionCharacter* Companion = *It;
		if (IsValid(Companion)) SoftCollisionCompanions.Add(Companion);
	}
}

FVector AExtractionPlayer::ApplyCompanionSoftCollision(ACompanionCharacter& Companion, UCapsuleComponent& CompanionCapsule)
{
	// Idempotent per-tick assert (self-heals any external clear, e.g. BTTask_RevivePlayer's hold
	// teardown): the player still ignores the companion (existing pass-through feel); the companion
	// no longer ignores the player, so ITS movement sweeps block against the player's capsule — CMC
	// wall-slide walks it around instead of shoving through (asymmetric blocking, F2).
	GetCapsuleComponent()->IgnoreActorWhenMoving(&Companion, true);
	CompanionCapsule.IgnoreActorWhenMoving(this, false);

	FVector Delta = GetActorLocation() - Companion.GetActorLocation();
	Delta.Z = 0.f;

	const float CombinedRadius = GetCapsuleComponent()->GetScaledCapsuleRadius()
		+ CompanionCapsule.GetScaledCapsuleRadius()
		+ CompanionPushPadding;

	const float Dist = Delta.Size();
	if (Dist >= CombinedRadius) return FVector::ZeroVector;

	FVector PushDir = (Dist > KINDA_SMALL_NUMBER) ? (Delta / Dist) : GetActorRightVector();
	PushDir.Z = 0.f;
	PushDir = PushDir.GetSafeNormal();

	const float DepthFraction = 1.f - (Dist / CombinedRadius);
	return StripOpposingPush(PushDir * (CompanionPushStrength * DepthFraction), GetLastMovementInputVector());
}

// ---- Auto-Lean ----

void AExtractionPlayer::UpdateAutoLean(float DeltaTime)
{
	const APlayerCameraManager* CamManager = UGameplayStatics::GetPlayerCameraManager(this, 0);
	if (!IsValid(CamManager)) return;

	const UWorld* World = GetWorld();
	if (!IsValid(World)) return;

	const FVector Origin = CamManager->GetCameraLocation() + FVector(0.f, 0.f, LeanProbeVerticalOffset);
	const FVector Right = GetActorRightVector();

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);
	const FCollisionShape ProbeShape = FCollisionShape::MakeSphere(LeanProbeRadius);

	// One sweep per side, both of length LeanGapClearance from the camera origin.
	// A side is a WALL you're hugging if the sweep hits within LeanProbeDistance.
	// A side is OPEN if the sweep finds nothing within LeanGapClearance (a miss).
	FHitResult RightHit, LeftHit;
	const bool bRightHit = World->SweepSingleByChannel(
		RightHit, Origin, Origin + Right * LeanGapClearance,
		FQuat::Identity, ECC_Visibility, ProbeShape, Params);

	const bool bLeftHit = World->SweepSingleByChannel(
		LeftHit, Origin, Origin - Right * LeanGapClearance,
		FQuat::Identity, ECC_Visibility, ProbeShape, Params);

	const bool bRightWall = bRightHit && RightHit.Distance <= LeanProbeDistance;
	const bool bLeftWall  = bLeftHit  && LeftHit.Distance  <= LeanProbeDistance;
	const bool bRightOpen = !bRightHit;
	const bool bLeftOpen  = !bLeftHit;

	if (bRightWall && bLeftOpen)
		AutoLeanTargetAlpha = -MaxAutoLeanMagnitude;  // wall on right → lean left toward the open side
	else if (bLeftWall && bRightOpen)
		AutoLeanTargetAlpha = MaxAutoLeanMagnitude;   // wall on left → lean right toward the open side
	else
		AutoLeanTargetAlpha = 0.f;

#if ENABLE_DRAW_DEBUG
	if (bDrawLeanDebug)
	{
		const FVector RightEnd = Origin + Right * LeanGapClearance;
		const FVector LeftEnd  = Origin - Right * LeanGapClearance;
		DrawDebugLine(World, Origin, RightEnd, bRightWall ? FColor::Red   : FColor::Green, false, LeanProbeInterval * 2.f, 0, 1.f);
		DrawDebugLine(World, Origin, LeftEnd,  bLeftWall  ? FColor::Red   : FColor::Green, false, LeanProbeInterval * 2.f, 0, 1.f);
		DrawDebugSphere(World, RightEnd, LeanProbeRadius, 8, bRightWall ? FColor::Red : FColor::Green, false, LeanProbeInterval * 2.f, 0, 1.f);
		DrawDebugSphere(World, LeftEnd,  LeanProbeRadius, 8, bLeftWall  ? FColor::Red : FColor::Green, false, LeanProbeInterval * 2.f, 0, 1.f);
	}
#endif
}

// ---- Interaction / Revive Input ----

void AExtractionPlayer::InteractStart(const FInputActionValue& Value)
{
	if (bIsDBNO) return;
	// Mid-takedown E would start the revive: the kneel's stop-all Montage_Play kills the takedown
	// montage (orphaning the victim) and the seat snap flips the capsule mid-kill.
	if (bTakedownMontageActive) return;
	// Mid-traversal E is the reverse of TryStartTraversal's bIsReviving guard: the revive hold
	// would snapshot bUseControllerRotationYaw while traversal already holds it false, and
	// its restore then leaves yaw-follow stuck off — the body stops tracking the camera and the
	// kit's turn/aim layers contort the pose (the "player floats at doors" latch).
	if (IsInTraversal()) return;
	if (!IsLocallyControlled()) return;

	if (bIsReviving) return;
	// Repeat-fire on a held key must not restart the clock from zero.
	if (bIsInteractHolding) return;

	// World interactions (loot container / keycard door) win over the revive hold.
	if (TryWorldInteract()) return;

	AActor* Target = FindReviveTarget();
	if (!IsValid(Target)) return;

	BeginReviveHold(Target);
}

void AExtractionPlayer::InteractStop(const FInputActionValue& Value)
{
	if (!IsLocallyControlled()) return;

	if (bIsInteractHolding)
	{
		UE_LOG(LogExtraction, Log, TEXT("Interact hold cancel: E released at %.2fs / %.2fs on '%s'"),
			InteractHoldElapsed, InteractHoldDuration, *GetNameSafe(InteractHoldTarget));
		CancelInteractHold();
		return;
	}

	if (!bIsReviving) return;

	UE_LOG(LogExtraction, Log, TEXT("Revive cancel: E released at %.2fs / %.2fs"), ReviveElapsed, ReviveDuration);
	CancelRevive();
}

bool AExtractionPlayer::TraceInteractHit(FHitResult& OutHit) const
{
	const UWorld* World = GetWorld();
	if (!World) return false;

	// Controller view point tracks CalcCamera (kit-driven FP camera) — more accurate than eyes.
	FVector ViewLoc; FRotator ViewRot;
	GetActorEyesViewPoint(ViewLoc, ViewRot);
	if (const AController* C = GetController())
		C->GetPlayerViewPoint(ViewLoc, ViewRot);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(PlayerInteract), false, this);
	const FVector TraceEnd = ViewLoc + ViewRot.Vector() * InteractTraceRange;
	return World->LineTraceSingleByChannel(OutHit, ViewLoc, TraceEnd, ECC_Visibility, Params);
}

AActor* AExtractionPlayer::TraceInteractableUnderCrosshair() const
{
	FHitResult Hit;
	if (!TraceInteractHit(Hit)) return nullptr;

	AActor* HitActor = Hit.GetActor();
	if (!IsValid(HitActor) || !HitActor->Implements<UWorldInteractable>()) return nullptr;

	// const_cast: the interface Execute_ wrappers take a non-const interactor, and the scan path
	// that needs this helper is itself const. The call is a pure query.
	return IWorldInteractable::Execute_CanWorldInteract(HitActor, const_cast<AExtractionPlayer*>(this))
		? HitActor : nullptr;
}

bool AExtractionPlayer::TryWorldInteract()
{
	FHitResult Hit;
	if (!TraceInteractHit(Hit)) return false;

	AActor* HitActor = Hit.GetActor();
	if (!IsValid(HitActor)) return false;

	// Generic world interactable — extraction targets, the captive VIP, terminals, etc.
	if (HitActor->Implements<UWorldInteractable>() && IWorldInteractable::Execute_CanWorldInteract(HitActor, this))
	{
		// Hold-gated interactables open the hold instead of firing; the press is consumed either way.
		const float HoldSeconds = IWorldInteractable::Execute_GetWorldInteractHoldSeconds(HitActor, this);
		if (HoldSeconds > 0.f)
		{
			InteractHoldTarget = HitActor;
			InteractHoldDuration = HoldSeconds;
			InteractHoldElapsed = 0.f;
			bIsInteractHolding = true;

			// The under-the-hold beat (scripted VO) is authority-side, like the interaction itself.
			if (HasAuthority())
				IWorldInteractable::Execute_OnWorldInteractHoldStarted(HitActor, this);
			else
				Server_BeginWorldInteractHold(HitActor);

			UE_LOG(LogExtraction, Log, TEXT("Interact hold start: '%s' (%.2fs)"),
				*GetNameSafe(HitActor), HoldSeconds);
			return true;
		}

		if (HasAuthority())
		{
			IWorldInteractable::Execute_WorldInteract(HitActor, this);
		}
		else
		{
			Server_WorldInteract(HitActor);
		}
		return true;
	}

	// Lootable container — instant press-to-loot; the container animates itself (no player anim yet).
	if (HitActor->Implements<ULootable>() && ILootable::Execute_CanLoot(HitActor))
	{
		ILootable::Execute_Loot(HitActor, this);
		return true;
	}

	// Locked door — keycard unlock (opens on success, "requires keycard" toast otherwise).
	if (ABreachableDoor* Door = Cast<ABreachableDoor>(HitActor))
	{
		if (Door->IsLocked())
		{
			Door->TryUnlock(this);
			return true;
		}
	}

	return false;
}

void AExtractionPlayer::Server_WorldInteract_Implementation(AActor* Target)
{
	if (!IsValid(Target)) return;
	if (!Target->Implements<UWorldInteractable>()) return;
	if (!IWorldInteractable::Execute_CanWorldInteract(Target, this)) return;

	const float MaxDistSq = FMath::Square(InteractTraceRange + WorldInteractDistanceSlack);
	const float BoundsDistSq = Target->GetComponentsBoundingBox().ComputeSquaredDistanceToPoint(GetActorLocation());
	if (BoundsDistSq > MaxDistSq)
	{
		UE_LOG(LogExtraction, Warning, TEXT("Server_WorldInteract: '%s' too far from '%s'"),
			*GetNameSafe(this), *GetNameSafe(Target));
		return;
	}

	IWorldInteractable::Execute_WorldInteract(Target, this);
}

void AExtractionPlayer::Server_BeginWorldInteractHold_Implementation(AActor* Target)
{
	if (!IsValid(Target)) return;
	if (!Target->Implements<UWorldInteractable>()) return;
	if (!IWorldInteractable::Execute_CanWorldInteract(Target, this)) return;
	if (IWorldInteractable::Execute_GetWorldInteractHoldSeconds(Target, this) <= 0.f) return;

	const float MaxDistSq = FMath::Square(InteractTraceRange + WorldInteractDistanceSlack);
	if (Target->GetComponentsBoundingBox().ComputeSquaredDistanceToPoint(GetActorLocation()) > MaxDistSq)
	{
		UE_LOG(LogExtraction, Warning, TEXT("Server_BeginWorldInteractHold: '%s' too far from '%s'"),
			*GetNameSafe(this), *GetNameSafe(Target));
		return;
	}

	IWorldInteractable::Execute_OnWorldInteractHoldStarted(Target, this);
}

void AExtractionPlayer::UpdateInteractHold(float DeltaTime)
{
	// Same guards that block a start, re-checked every frame — a hold that outlives its
	// preconditions must not commit the interaction when the clock runs out.
	auto AbortIf = [this](bool bCondition, const TCHAR* Reason)
	{
		if (!bCondition) return false;
		UE_LOG(LogExtraction, Log, TEXT("Interact hold cancel: %s at %.2fs / %.2fs on '%s'"),
			Reason, InteractHoldElapsed, InteractHoldDuration, *GetNameSafe(InteractHoldTarget));
		CancelInteractHold();
		return true;
	};

	if (AbortIf(bIsDBNO, TEXT("player went DBNO"))) return;
	if (AbortIf(bTakedownMontageActive, TEXT("takedown started"))) return;
	if (AbortIf(IsInTraversal(), TEXT("traversal started"))) return;
	if (AbortIf(!IsValid(InteractHoldTarget), TEXT("target destroyed"))) return;
	if (AbortIf(!IWorldInteractable::Execute_CanWorldInteract(InteractHoldTarget, this),
		TEXT("target refused interaction"))) return;

	// Walking away cancels; looking away does not — a five-second hold that breaks on every
	// stray mouse twitch is unusable.
	const float MaxDistSq = FMath::Square(InteractTraceRange + WorldInteractDistanceSlack);
	if (AbortIf(InteractHoldTarget->GetComponentsBoundingBox().ComputeSquaredDistanceToPoint(GetActorLocation()) > MaxDistSq,
		TEXT("walked out of range"))) return;

	InteractHoldElapsed += DeltaTime;
	if (InteractHoldElapsed < InteractHoldDuration) return;

	AActor* Target = InteractHoldTarget;
	UE_LOG(LogExtraction, Log, TEXT("Interact hold complete: '%s' (%.2fs)"), *GetNameSafe(Target), InteractHoldDuration);

	// State resets BEFORE the commit — WorldInteract can destroy or re-configure the target.
	CancelInteractHold();

	if (HasAuthority())
		IWorldInteractable::Execute_WorldInteract(Target, this);
	else
		Server_WorldInteract(Target);
}

void AExtractionPlayer::CancelInteractHold()
{
	if (!bIsInteractHolding) return;

	bIsInteractHolding = false;
	InteractHoldElapsed = 0.f;
	InteractHoldDuration = 0.f;
	InteractHoldTarget = nullptr;
}

void AExtractionPlayer::UpdateInteractCandidateScan(float DeltaTime)
{
	InteractCandidateScanAccumulator += DeltaTime;
	if (InteractCandidateScanAccumulator < 0.1f) return;
	InteractCandidateScanAccumulator = 0.f;

	// While the hold runs the prompt stays pinned to the held target (progress bar); a downed
	// player interacts with nothing.
	AActor* Candidate = nullptr;
	if (bIsInteractHolding) Candidate = InteractHoldTarget;
	else if (!bIsDBNO && !bIsReviving) Candidate = TraceInteractableUnderCrosshair();

	InteractCandidate = Candidate;
	InteractCandidatePrompt = IsValid(Candidate)
		? IWorldInteractable::Execute_GetWorldInteractionPrompt(Candidate, this)
		: FText::GetEmpty();
}

void AExtractionPlayer::TakedownInput(const FInputActionValue& Value)
{
	UE_LOG(LogExtraction, Verbose, TEXT("[Takedown] AExtractionPlayer::TakedownInput fired — HasAuthority=%d MontageActive=%d TakedownMontage=%s"),
		HasAuthority(), bTakedownMontageActive, *GetNameSafe(TakedownMontage));

	if (!HasAuthority()) return;
	// Re-entrancy guard: a second press during an active montage takedown would orphan the frozen victim.
	if (bTakedownMontageActive) return;
	// Mid-revive-hold: the takedown align/montage would stomp the kneel and teleport the reviver.
	if (bIsReviving) return;
	// Mid-traversal: the takedown montage would interrupt the vault montage and force-end the
	// traversal mid-obstacle while the seat snap fights the traversal rotation lock.
	if (IsInTraversal()) return;

	static constexpr float FacingDotMin = 0.3f;
	AEnemyCharacter* Best = nullptr;
	float BestDistSq = MAX_FLT;

	int32 DbgTotal = 0;
	int32 DbgTakeable = 0;
	int32 DbgFacingRejected = 0;

	for (TActorIterator<AEnemyCharacter> It(GetWorld()); It; ++It)
	{
		AEnemyCharacter* Enemy = *It;
		if (!IsValid(Enemy)) continue;
		++DbgTotal;
		if (!Enemy->CanBeTakenDown(this)) continue;
		++DbgTakeable;

		const FVector ToEnemy = Enemy->GetActorLocation() - GetActorLocation();
		if (FVector::DotProduct(GetActorForwardVector(), ToEnemy.GetSafeNormal2D()) < FacingDotMin)
		{
			++DbgFacingRejected;
			continue;
		}

		const float DistSq = ToEnemy.SizeSquared();
		if (DistSq < BestDistSq) { BestDistSq = DistSq; Best = Enemy; }
	}

	UE_LOG(LogExtraction, Verbose, TEXT("[Takedown] scan complete: %d enemies, %d takeable, %d facing-rejected, Best=%s"),
		DbgTotal, DbgTakeable, DbgFacingRejected, *GetNameSafe(Best));

	if (!IsValid(Best))
	{
		UE_LOG(LogExtraction, Warning, TEXT("[Takedown-Player] T pressed but NO victim found (need an Unaware enemy you're BEHIND, in range) — companion will NOT sync"));
		return;
	}

	UE_LOG(LogExtraction, Warning, TEXT("[Takedown-Player] victim=%s -> committing"), *GetNameSafe(Best));

	if (!IsValid(TakedownMontage))
	{
		// Instant path: enemy stays in place, snap/align is skipped entirely.
		UE_LOG(LogExtraction, Warning, TEXT("[Takedown-Player] COMMIT (instant path) -> broadcast OnPlayerTakedownCommitted"));
		OnPlayerTakedownCommitted.Broadcast();
		Best->ExecuteTakedown(this);
		return;
	}

	StartMontageDeferred(Best);
}

void AExtractionPlayer::StartMontageDeferred(AEnemyCharacter* Victim)
{
	// Compute a collision-safe snap location for the victim.
	const FVector PlayerLoc = GetActorLocation();
	const FVector PlayerFwd = GetActorForwardVector();
	const float SnapYaw = GetActorRotation().Yaw;

	FVector VictimLoc = Victim->GetActorLocation();
	if (bAlignTakedownVictim)
	{
		const FVector IdealXY = PlayerLoc + PlayerFwd * TakedownVictimForwardOffset;

		// Sweep from player to the ideal snap point and clamp on first blocking hit.
		static constexpr float SweepRadius = 20.f;
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TakedownSnap), false, this);
		Params.AddIgnoredActor(Victim);
		const bool bBlocked = GetWorld()->SweepSingleByChannel(
			Hit, PlayerLoc, FVector(IdealXY.X, IdealXY.Y, PlayerLoc.Z),
			FQuat::Identity, ECC_WorldStatic, FCollisionShape::MakeSphere(SweepRadius), Params);

		const FVector SafeXY = bBlocked ? Hit.Location : IdealXY;
		VictimLoc = FVector(SafeXY.X, SafeXY.Y, Victim->GetActorLocation().Z);
	}

	// Montage length drives the watchdog timeout; add a small buffer for blend-out.
	const float MontageDuration = TakedownMontage->GetPlayLength() + 1.f;

#if !UE_BUILD_SHIPPING
	{
		const TArray<FAnimNotifyEvent>& Notifies = TakedownMontage->Notifies;
		const bool bHasKillNotify = Notifies.ContainsByPredicate([](const FAnimNotifyEvent& N)
			{ return N.Notify && N.Notify->IsA<UAnimNotify_TakedownKill>(); });
		if (!bHasKillNotify)
			UE_LOG(LogExtraction, Warning, TEXT("TakedownMontage '%s' has no UAnimNotify_TakedownKill — death will fire from the montage-end fallback, not at the intended frame."),
				*GetNameSafe(TakedownMontage));
	}
#endif

	if (!Victim->BeginTakedownHold(this, VictimLoc, SnapYaw, MontageDuration)) { UE_LOG(LogExtraction, Warning, TEXT("[Takedown-Player] BeginTakedownHold FAILED on %s — no broadcast"), *GetNameSafe(Victim)); return; }

	PendingTakedownVictim = Victim;
	bTakedownMontageActive = true;
	UE_LOG(LogExtraction, Warning, TEXT("[Takedown-Player] COMMIT (montage path) -> broadcast OnPlayerTakedownCommitted"));
	OnPlayerTakedownCommitted.Broadcast();

	UAnimInstance* AnimInst = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!IsValid(AnimInst))
	{
		FinishPendingTakedown();
		return;
	}

	const float PlayedLength = AnimInst->Montage_Play(TakedownMontage);
	if (PlayedLength <= 0.f)
	{
		UE_LOG(LogExtraction, Warning, TEXT("TakedownInput: Montage_Play failed for '%s', killing immediately."),
			*GetNameSafe(TakedownMontage));
		FinishPendingTakedown();
		return;
	}

	Victim->SetTakedownWasMontageDriven(true);

	UE_LOG(LogExtraction, Warning, TEXT("[Takedown-Player] montage PLAYING len=%.2f on victim=%s (mesh=%s)"),
		PlayedLength, *GetNameSafe(Victim), *GetNameSafe(GetMesh()));

	// End delegate fires on natural end AND interruption — fallback kill so no frozen enemy survives.
	FOnMontageEnded EndDelegate;
	EndDelegate.BindUObject(this, &AExtractionPlayer::OnTakedownMontageEnded);
	AnimInst->Montage_SetEndDelegate(EndDelegate, TakedownMontage);

	// Notify BP now that the montage is confirmed running and the end delegate is bound.
	// Must NOT be called on the early-return failure paths above (no montage = no restore needed).
	OnTakedownStarted(Victim);
}

// ---- Companion command input handlers ----

void AExtractionPlayer::CompanionPingInput(const FInputActionValue& /*Value*/)
{
	UE_LOG(LogExtraction, Warning, TEXT("[CompanionPing] MMB input received; cmdComp valid=%d"), IsValid(CompanionCommandComponent));
	if (!IsValid(CompanionCommandComponent)) return;
	CompanionCommandComponent->IssuePing();
}

void AExtractionPlayer::CompanionConfirmTakedownKnifeInput(const FInputActionValue& /*Value*/)
{
	UE_LOG(LogExtraction, Warning, TEXT("[CompanionConfirm] KNIFE (Y) input received; cmdComp valid=%d"), IsValid(CompanionCommandComponent));
	if (!IsValid(CompanionCommandComponent)) return;
	CompanionCommandComponent->ConfirmTakedownKnife();
}

void AExtractionPlayer::CompanionConfirmTakedownShootInput(const FInputActionValue& /*Value*/)
{
	if (!IsValid(CompanionCommandComponent)) return;
	CompanionCommandComponent->ConfirmTakedownShoot();
}

void AExtractionPlayer::CompanionConfirmBreachInput(const FInputActionValue& /*Value*/)
{
	if (!IsValid(CompanionCommandComponent)) return;

	// One confirm key — routes to whatever the ping prompt is showing.
	switch (CompanionCommandComponent->GetPendingCommand())
	{
	case ECompanionCommand::Loot:    CompanionCommandComponent->ConfirmLoot();    break;
	case ECompanionCommand::Explore: CompanionCommandComponent->ConfirmExplore(); break;
	default:                         CompanionCommandComponent->ConfirmBreach();  break;
	}
}

void AExtractionPlayer::CompanionModeToggleInput(const FInputActionValue& /*Value*/)
{
	if (!IsValid(CompanionCommandComponent)) return;
	CompanionCommandComponent->ToggleModeMenu();
}

void AExtractionPlayer::CompanionModeSelectStealthInput(const FInputActionValue& /*Value*/)
{
	if (!IsValid(CompanionCommandComponent)) return;
	CompanionCommandComponent->SelectCompanionMode(ECompanionMode::Stealth);
}

void AExtractionPlayer::CompanionModeSelectNormalInput(const FInputActionValue& /*Value*/)
{
	if (!IsValid(CompanionCommandComponent)) return;
	CompanionCommandComponent->SelectCompanionMode(ECompanionMode::Normal);
}

void AExtractionPlayer::CompanionModeSelectCombatInput(const FInputActionValue& /*Value*/)
{
	if (!IsValid(CompanionCommandComponent)) return;
	CompanionCommandComponent->SelectCompanionMode(ECompanionMode::Combat);
}

void AExtractionPlayer::UseStimInput(const FInputActionValue& /*Value*/)
{
	if (IsValid(ConsumableInventoryComponent))
		ConsumableInventoryComponent->TryUseStim();
}

void AExtractionPlayer::HandleStimUsed()
{
	OnStimUsed();
}

void AExtractionPlayer::FinishPendingTakedown()
{
	AEnemyCharacter* Victim = PendingTakedownVictim.Get();
	PendingTakedownVictim.Reset();
	bTakedownMontageActive = false;

	if (IsValid(Victim)) Victim->FinishTakedownKill(this);
}

void AExtractionPlayer::OnTakedownMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	UE_LOG(LogExtraction, Warning, TEXT("[Takedown-Player] montage ENDED interrupted=%d montage=%s"),
		(int32)bInterrupted, *GetNameSafe(Montage));

	// Restore camera/gun/knife regardless of whether the kill already fired via notify.
	// The started/finished pair is always balanced: this delegate only binds after OnTakedownStarted fires.
	OnTakedownFinished();

	// Fallback: if the notify didn't fire before the montage ended/was interrupted, apply the kill now.
	if (bTakedownMontageActive) FinishPendingTakedown();
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

	// Headshots deal no bonus to the player — head lethality is enemy-class-driven, not weapon-driven,
	// and the player is out of scope for it. Limb (Arms/Legs) scaling below is unchanged.
	if (*Region == EHitRegion::Head) return 1.0f;

	if (!PointDamage.DamageTypeClass) return 1.0f;

	const UExtractionDamageType* DmgType = Cast<UExtractionDamageType>(
		PointDamage.DamageTypeClass->GetDefaultObject());
	if (!IsValid(DmgType)) return 1.0f;

	return DmgType->GetMultiplierForRegion(*Region);
}

float AExtractionPlayer::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// Post-revive grace: full immunity for a beat after standing up, or a mid-burst enemy re-downs
	// the player the frame the revive completes.
	if (!bIsDBNO && GetWorld()
		&& (GetWorld()->GetTimeSeconds() - LastReviveWorldTime) < PostReviveDamageGraceSeconds)
		return 0.f;

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

void AExtractionPlayer::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	PlayCrouchFoley();
}

void AExtractionPlayer::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	PlayCrouchFoley();
}

void AExtractionPlayer::PlayCrouchFoley() const
{
	// Stance-change cloth only for deliberate crouch toggles — slides, prone shuffles and the
	// DBNO capsule shrink all route through Crouch/UnCrouch too and must stay silent here.
	if (GetIsDBNO() || GetIsProne() || GetIsSliding()) return;

	const UWorld* World = GetWorld();
	UGameAudioSubsystem* AudioSys = World ? World->GetSubsystem<UGameAudioSubsystem>() : nullptr;
	if (AudioSys && AudioSys->GetBank())
		AudioSys->Play2D(AudioSys->GetBank()->CrouchFoley);
}

void AExtractionPlayer::EnterDBNO()
{
	if (bIsDBNO) return;
	bIsDBNO = true;
	// Stop auto-lean: ADSStop may never fire if the player is downed mid-ADS, which would leave the probe
	// driving lean on a downed pawn. The per-frame FInterpTo eases AutoLeanAlpha back to 0.
	bAutoLeanActive = false;
	AutoLeanTargetAlpha = 0.f;

	// Cancel active revive (downed player can't finish reviving someone)
	if (bIsReviving) CancelRevive();
	CancelInteractHold();

	// Fresh down, fresh arbitration — a claim left over from a previous DBNO would lock the wrong
	// ally out until its capability test happened to fail.
	ReviveClaimant.Reset();

	// IsBusy covers approach phase + mid-vault
	if (IsValid(TraversalComponent) && TraversalComponent->IsBusy())
		TraversalComponent->CancelTraversal();

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (IsValid(MoveComp))
	{
		MoveComp->StopMovementImmediately();
		MoveComp->SetMovementMode(MOVE_Walking);
		SavedMaxWalkSpeedCrouched = MoveComp->MaxWalkSpeedCrouched;
		MoveComp->MaxWalkSpeed = DBNOCrawlSpeed;
		MoveComp->MaxWalkSpeedCrouched = DBNOCrawlSpeed;
		if (MoveComp->GetNavAgentPropertiesRef().bCanCrouch) Crouch();
	}

	// Drop weapon state before hiding: the ADS/pose events can re-show the hand-socket item
	// if they fire after the hide (same ordering rule as SetReviveAnimsActive).
	if (IsValid(WeaponComponent))
	{
		WeaponComponent->StopFire();
		WeaponComponent->SetAiming(false);
		OnADSChanged(false);
		if (AWeaponBase* Weapon = WeaponComponent->GetCurrentWeapon())
		{
			Weapon->CancelRecoilRecovery();
			Weapon->CancelReload();
		}
	}
	SetHeldWeaponHidden(true);
	SetDBNOMovementProfile(true);

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

		// Squad-wipe check: fail only when NO companion can still pick the squad up (with the
		// armed extractee in play, one downed ally plus a standing one is not a wipe).
		if (!ACompanionCharacter::IsAnyCompanionReviveCapable(GetWorld(), nullptr))
		{
			if (AExtractionGameMode* GM = GetWorld()->GetAuthGameMode<AExtractionGameMode>())
				GM->FailLevel(NSLOCTEXT("Extraction", "BothDownReason", "Your squad was wiped out."));
		}
	}

	SetDBNOCameraFreeLook(true);

	if (UGameAudioSubsystem* AudioSys = GetWorld()->GetSubsystem<UGameAudioSubsystem>())
		AudioSys->StartDBNOAudio();

	OnDBNOStateChanged.Broadcast(true, BleedoutDuration);
	UE_LOG(LogExtraction, Log, TEXT("'%s' entered DBNO (%.0fs bleedout)"), *GetNameSafe(this), BleedoutDuration);
}

void AExtractionPlayer::ExitDBNO()
{
	if (!bIsDBNO) return;
	// bIsDBNO stays set until after UnCrouch() below — OnEndCrouch's foley guard reads it, and
	// clearing first made every revive stand-up play crouch cloth.

	if (const UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(BleedoutTimerHandle);

	BleedoutTimeRemaining = 0.f;

	// The revive is done — drop the hold unconditionally (not holder-scoped: whoever finished it,
	// nobody should still own a claim on a player who is back up).
	ReviveClaimant.Reset();

	if (IsValid(HealthComponent))
		HealthComponent->Revive(ReviveHealthPercent);

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (IsValid(MoveComp))
	{
		MoveComp->SetMovementMode(MOVE_Walking);
		MoveComp->MaxWalkSpeedCrouched = SavedMaxWalkSpeedCrouched;
		if (const UCharacterMovementComponent* Archetype = Cast<UCharacterMovementComponent>(MoveComp->GetArchetype()))
			MoveComp->MaxWalkSpeed = Archetype->MaxWalkSpeed;
		else
			MoveComp->MaxWalkSpeed = GetClass()->GetDefaultObject<AExtractionPlayer>()->GetCharacterMovement()->MaxWalkSpeed;
	}
	UnCrouch();
	bIsDBNO = false;

	SetBeingRevived(false);
	SetDBNOCameraFreeLook(false);
	SetHeldWeaponHidden(false);
	SetDBNOMovementProfile(false);

	if (const UWorld* World = GetWorld())
		LastReviveWorldTime = World->GetTimeSeconds();

	if (UGameAudioSubsystem* AudioSys = GetWorld()->GetSubsystem<UGameAudioSubsystem>())
		AudioSys->StopDBNOAudio();

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

	if (UGameAudioSubsystem* AudioSys = GetWorld()->GetSubsystem<UGameAudioSubsystem>())
		AudioSys->StopDBNOAudio();

	bAutoLeanActive = false;
	AutoLeanTargetAlpha = 0.f;
	SetBeingRevived(false);
	SetDBNOCameraFreeLook(false);
	// Weapon stays hidden — the level-fail flow owns the screen from here.
	SetDBNOMovementProfile(false);

	if (const UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(BleedoutTimerHandle);

	UE_LOG(LogExtraction, Log, TEXT("'%s' is fully dead"), *GetNameSafe(this));

	if (HasAuthority())
	{
		if (AExtractionGameMode* GM = GetWorld()->GetAuthGameMode<AExtractionGameMode>())
			GM->FailLevel(NSLOCTEXT("Extraction", "PlayerDiedReason", "You died."));
	}
}

void AExtractionPlayer::ClampDBNOFreeLook()
{
	AController* C = GetController();
	if (!IsValid(C)) return;

	FRotator ControlRot = C->GetControlRotation();
	const float BodyYaw = GetActorRotation().Yaw;
	const float YawDelta = FRotator::NormalizeAxis(ControlRot.Yaw - BodyYaw);
	const float ClampedYawDelta = FMath::Clamp(YawDelta, -DBNOFreeLookYawLimit, DBNOFreeLookYawLimit);
	const float Pitch = FRotator::NormalizeAxis(ControlRot.Pitch);
	const float ClampedPitch = FMath::Clamp(Pitch, DBNOFreeLookPitchMin, DBNOFreeLookPitchMax);

	if (FMath::IsNearlyEqual(YawDelta, ClampedYawDelta) && FMath::IsNearlyEqual(Pitch, ClampedPitch))
		return;

	ControlRot.Yaw = BodyYaw + ClampedYawDelta;
	ControlRot.Pitch = ClampedPitch;
	C->SetControlRotation(ControlRot);
}

void AExtractionPlayer::SetDBNOMovementProfile(bool bEnable)
{
	if (bDBNOMovementProfileActive == bEnable) return;

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!IsValid(MoveComp)) return;
	bDBNOMovementProfileActive = bEnable;

	if (bEnable)
	{
		SavedMaxAcceleration = MoveComp->MaxAcceleration;
		SavedBrakingDecelerationWalking = MoveComp->BrakingDecelerationWalking;
		SavedGroundFriction = MoveComp->GroundFriction;
		bSavedDBNOUseControllerRotationYaw = bUseControllerRotationYaw;

		MoveComp->MaxAcceleration = DBNOCrawlAcceleration;
		MoveComp->BrakingDecelerationWalking = DBNOCrawlBrakingDeceleration;
		MoveComp->GroundFriction = DBNOCrawlGroundFriction;
		bUseControllerRotationYaw = false;
	}
	else
	{
		MoveComp->MaxAcceleration = SavedMaxAcceleration;
		MoveComp->BrakingDecelerationWalking = SavedBrakingDecelerationWalking;
		MoveComp->GroundFriction = SavedGroundFriction;
		bUseControllerRotationYaw = bSavedDBNOUseControllerRotationYaw;
	}
}

void AExtractionPlayer::SetDBNOCameraFreeLook(bool bEnable)
{
	if (!IsLocallyControlled()) return;
	if (bEnable == bDBNOFreeLookActive) return;

	USpringArmComponent* Arm = CachedSpringArm.Get();
	if (!IsValid(Arm))
	{
		Arm = FindComponentByClass<USpringArmComponent>();
		CachedSpringArm = Arm;
	}
	if (!IsValid(Arm)) return;

	bDBNOFreeLookActive = bEnable;
	if (bEnable)
	{
		bSavedSpringArmUsePawnControlRotation = Arm->bUsePawnControlRotation;
		Arm->bUsePawnControlRotation = true;
	}
	else
		Arm->bUsePawnControlRotation = bSavedSpringArmUsePawnControlRotation;
}

void AExtractionPlayer::SetBeingRevived(bool bBeingRevived, float ExpectedDuration)
{
	if (bBeingRevivedAnimActive == bBeingRevived) return;
	bBeingRevivedAnimActive = bBeingRevived;
	if (bBeingRevived)
	{
		bSavedUseControllerRotationYaw = bUseControllerRotationYaw;
		bUseControllerRotationYaw = false;
		SetBeingRevivedCameraAnimationControl(true);
	}
	else
	{
		bUseControllerRotationYaw = bSavedUseControllerRotationYaw;
		SetBeingRevivedCameraAnimationControl(false);
	}

	if (bBeingRevived && IsValid(WeaponComponent))
	{
		WeaponComponent->StopFire();
		WeaponComponent->SetAiming(false);
		OnADSChanged(false);
		bAutoLeanActive = false;
		AutoLeanTargetAlpha = 0.f;
		if (AWeaponBase* Weapon = WeaponComponent->GetCurrentWeapon()) Weapon->CancelReload();
	}
	// After the ADS/pose drops: OnADSChanged drives the kit's NewHandPose, which can re-show
	// the hand-socket item a hide-first ordering just hid.
	SetHeldWeaponHidden(bBeingRevived);

	USkeletalMeshComponent* MeshComp = GetMesh();
	UAnimInstance* AnimInst = IsValid(MeshComp) ? MeshComp->GetAnimInstance() : nullptr;
	if (!IsValid(AnimInst) || !BeingRevivedMontage) return;
	if (bBeingRevived && bIsDBNO)
	{
		AnimInst->SetRootMotionMode(ERootMotionMode::RootMotionFromMontagesOnly);
		const float Length = BeingRevivedMontage->GetPlayLength();
		const float Rate = ExpectedDuration > 0.f && Length > 0.f ? Length / ExpectedDuration : 1.f;
		AnimInst->Montage_Play(BeingRevivedMontage, Rate);
	}
	else if (AnimInst->Montage_IsPlaying(BeingRevivedMontage))
	{
		AnimInst->Montage_Stop(0.25f, BeingRevivedMontage);
	}
}

void AExtractionPlayer::SetBeingRevivedCameraAnimationControl(bool bActive)
{
	if (!IsLocallyControlled() || bBeingRevivedCameraOverrideActive == bActive) return;
	USpringArmComponent* Arm = CachedSpringArm.Get();
	if (!IsValid(Arm))
	{
		Arm = FindComponentByClass<USpringArmComponent>();
		CachedSpringArm = Arm;
	}
	if (!IsValid(Arm)) return;

	bBeingRevivedCameraOverrideActive = bActive;
	if (bActive)
	{
		bBeingRevivedSavedSpringArmUsePawnControlRotation = Arm->bUsePawnControlRotation;
		Arm->bUsePawnControlRotation = false;
	}
	else
	{
		Arm->bUsePawnControlRotation = bBeingRevivedSavedSpringArmUsePawnControlRotation;
	}
}

void AExtractionPlayer::SetHeldWeaponHidden(bool bHideWeapon)
{
	// SetWeaponHidden, not SetActorHiddenInGame: the visible gun is a separate visual actor.
	if (IsValid(WeaponComponent))
	{
		if (AWeaponBase* Weapon = WeaponComponent->GetCurrentWeapon())
			Weapon->SetWeaponHidden(bHideWeapon);
	}

	// The kit's VISIBLE third-person gun is a separate BP-spawned actor (BP_Item_Base's Item_Mesh)
	// attached to a hand socket — C++ has no direct ref (BP-only SpawnedItemRef), so hide any actor
	// hanging off the weapon-hand sockets. Covers the gun body + its attachment meshes in one call.
	TArray<AActor*> AttachedActors;
	GetAttachedActors(AttachedActors);
	for (AActor* Attached : AttachedActors)
	{
		const USceneComponent* AttachedRoot = IsValid(Attached) ? Attached->GetRootComponent() : nullptr;
		const FName Socket = AttachedRoot ? AttachedRoot->GetAttachSocketName() : NAME_None;
		if (Socket == TEXT("ik_hand_gun") || Socket == TEXT("ItemHand_R") || Socket == TEXT("ItemHand_L"))
			Attached->SetActorHiddenInGame(bHideWeapon);
	}
}

// Reads a float-ish BP variable off an anim instance (UE5 BP floats are doubles).
static float ReadAnimFloat(const UAnimInstance* AnimInst, const TCHAR* Name)
{
	if (!AnimInst) return 0.f;
	const FProperty* Prop = AnimInst->GetClass()->FindPropertyByName(Name);
	if (const FDoubleProperty* D = CastField<FDoubleProperty>(Prop))
		return static_cast<float>(D->GetPropertyValue_InContainer(AnimInst));
	if (const FFloatProperty* F = CastField<FFloatProperty>(Prop))
		return F->GetPropertyValue_InContainer(AnimInst);
	if (const FBoolProperty* B = CastField<FBoolProperty>(Prop))
		return B->GetPropertyValue_InContainer(AnimInst) ? 1.f : 0.f;
	return 0.f;
}

// One-line dump of every rotation participating in the revive pair — playtest logs name the
// system that moves when the head misbehaves, instead of another guess.
static void LogReviveDebugState(const TCHAR* Phase, AExtractionPlayer& Player, AActor* TargetActor)
{
	const USkeletalMeshComponent* PMesh = Player.GetMesh();
	const UAnimInstance* AnimInst = PMesh ? PMesh->GetAnimInstance() : nullptr;
	const AController* C = Player.GetController();
	const APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(&Player, 0);

	float CompYaw = 0.f, CompMeshYaw = 0.f, CompPelvisYaw = 0.f, Bearing = 0.f, Dist = 0.f, PlayerLocalAngle = 0.f;
	if (const ACharacter* Comp = Cast<ACharacter>(TargetActor))
	{
		CompYaw = Comp->GetActorRotation().Yaw;
		if (const USkeletalMeshComponent* CMesh = Comp->GetMesh())
		{
			CompMeshYaw = CMesh->GetComponentRotation().Yaw;
			CompPelvisYaw = CMesh->GetSocketRotation(TEXT("pelvis")).Yaw;
		}
		const FVector ToPlayer = Player.GetActorLocation() - Comp->GetActorLocation();
		Bearing = ToPlayer.GetSafeNormal2D().Rotation().Yaw;
		Dist = ToPlayer.Size2D();
		PlayerLocalAngle = FRotator::NormalizeAxis(Bearing - CompYaw);
	}

	UE_LOG(LogExtraction, Log,
		TEXT("[REVIVE DBG %s] comp: actorYaw=%.1f meshYaw=%.1f pelvisYaw=%.1f | player: actorYaw=%.1f meshRelYaw=%.1f meshWorldYaw=%.1f headYaw=%.1f | ctrlYaw=%.1f camYaw=%.1f camPitch=%.1f | AimYaw=%.1f AimPitch=%.1f UpperYaw=%.1f IsTurn=%.0f | bearingC2P=%.1f dist=%.0f playerLocalAngle=%.1f (authored -47.1) relCapYaw=%.1f (authored -46.0)"),
		Phase,
		CompYaw, CompMeshYaw, CompPelvisYaw,
		Player.GetActorRotation().Yaw,
		PMesh ? PMesh->GetRelativeRotation().Yaw : 0.f,
		PMesh ? PMesh->GetComponentRotation().Yaw : 0.f,
		PMesh ? PMesh->GetSocketRotation(TEXT("head")).Yaw : 0.f,
		C ? C->GetControlRotation().Yaw : 0.f,
		Cam ? Cam->GetCameraRotation().Yaw : 0.f,
		Cam ? Cam->GetCameraRotation().Pitch : 0.f,
		ReadAnimFloat(AnimInst, TEXT("AimYaw")),
		ReadAnimFloat(AnimInst, TEXT("AimPitch")),
		ReadAnimFloat(AnimInst, TEXT("UpperYaw")),
		ReadAnimFloat(AnimInst, TEXT("IsTurn")),
		Bearing, Dist, PlayerLocalAngle,
		FRotator::NormalizeAxis(Player.GetActorRotation().Yaw - CompYaw));
}

void AExtractionPlayer::BeginReviveHold(AActor* Target)
{
	if (bIsReviving) return;
	IExtractionPlayerInterface* TargetIface = Cast<IExtractionPlayerInterface>(Target);
	if (!IsValid(Target) || !TargetIface || !TargetIface->GetIsDBNO()) return;
	if (FVector::DistSquared(GetActorLocation(), Target->GetActorLocation()) > FMath::Square(ReviveProximityRadius)) return;
	// Root motion needs a grounded CMC mode; an airborne E would kneel in mid-air.
	if (GetCharacterMovement() && GetCharacterMovement()->IsFalling()) return;

	ReviveTarget = Target;
	ReviveElapsed = 0.f;
	bIsReviving = true;

	// Clear approach velocity, but keep a grounded movement mode so CMC consumes the revive
	// montage's authored root motion. Input remains locked for the full hold.
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->StopMovementImmediately();
		Move->SetMovementMode(MOVE_Walking);
	}

	// Patient first: SetBeingRevived stops its AI movement, so the rotation-only align below
	// can't be fought by the downed crawl's orient-to-movement.
	TargetIface->SetBeingRevived(true, ReviveDuration);
	TargetIface->AlignForRevive(GetActorLocation());

	bReviverSavedUseControllerRotationYaw = bUseControllerRotationYaw;
	bUseControllerRotationYaw = false;
	if (AController* C = GetController())
		ReviverSavedControlRotation = C->GetControlRotation();

	// The kit BP pumps IA_Look straight into AddControllerYaw/PitchInput, bypassing the C++
	// gates — SetIgnoreLookInput blocks that path at the controller (AddYaw/PitchInput check it).
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->SetIgnoreLookInput(true);
		ReviverLookIgnoredPC = PC;
	}

	// Montage-less hold (companion's bPlayPlayerReviveMontages off): skip the kneel seat and
	// montages — the seat's mesh yaw exists to line up the kneel clip, and with no kneel pose
	// to own it the yaw would spin the head-mounted FP camera on a standing body.
	if (const ACompanionCharacter* Companion = Cast<ACompanionCharacter>(Target))
	{
		if (Companion->ShouldPlayPlayerReviveMontages())
		{
			SeatReviverForHold(*Companion);
			SetReviveAnimsActive(true);
		}
	}

	LogReviveDebugState(TEXT("START"), *this, Target);
}

void AExtractionPlayer::SeatReviverForHold(const ACompanionCharacter& Companion)
{
	// Mirror of BTTask_RevivePlayer's pair snap with roles swapped: the reviver steps to the
	// authored offset anchored on the patient's (post-align) yaw. The offset lies along the
	// original approach bearing by construction, so this is a purely radial correction to the
	// authored 88cm — direction is untouched.
	// Anchor on the UNTRIMMED patient frame: the patient-yaw trim rotates only the companion's
	// body; the seat and the kneel facing must not swing with it.
	const float CompanionYaw = Companion.GetActorRotation().Yaw - Companion.PlayerRevivePatientYawTrimDeg;
	// PLAYER-direction pair constants — same values as the companion reviver's tuned constants
	// (both directions play the one authored clip pair), kept separate so this side trims live
	// without touching the working direction.
	const FVector AuthoredOffset(Companion.PlayerRevivePairOffset.X, Companion.PlayerRevivePairOffset.Y, 0.f);
	FVector SeatLocation = Companion.GetActorLocation()
		+ FRotator(0.f, CompanionYaw, 0.f).RotateVector(AuthoredOffset);
	SeatLocation.Z = GetActorLocation().Z;

	// Sweep the initial radial correction so a downed companion beside tight cover cannot snap
	// the player capsule or camera into geometry before root motion takes ownership.
	if (const UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		FCollisionQueryParams SeatParams;
		SeatParams.AddIgnoredActor(this);
		SeatParams.AddIgnoredActor(&Companion);
		FHitResult SeatHit;
		if (GetWorld()->SweepSingleByChannel(SeatHit, GetActorLocation(), SeatLocation, FQuat::Identity, ECC_Pawn,
				FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), SeatParams))
			SeatLocation = SeatHit.Location;
	}
	SetActorLocation(SeatLocation, false, nullptr, ETeleportType::TeleportPhysics);

	// Rotate the MESH to the seat, never the capsule: a capsule snap feeds the kit ABP's
	// turn-in-place chain (TurnDir/UpperYaw = clamped delta ×10) and spins the head at hold
	// start and end. The mesh-only rotation is invisible to every actor-rotation reader.
	// SeatYaw points the kneel clip's visual (authored ~180° from forward, plus ABP_Manny's
	// static Rotate Root Bone 45 — the correction) at the patient.
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!IsValid(MeshComp)) return;
	// Authored pair yaw plus the side-agnostic ABP Rotate-Root-Bone correction.
	const float SeatYaw = CompanionYaw + Companion.PlayerRevivePairYawOffset + ReviverSeatYawCorrectionDeg;
	const float MeshYawDelta = FRotator::NormalizeAxis(SeatYaw - GetActorRotation().Yaw);
	ReviverSavedMeshRelativeRotation = MeshComp->GetRelativeRotation();
	MeshComp->SetRelativeRotation(ReviverSavedMeshRelativeRotation + FRotator(0.f, MeshYawDelta, 0.f));
	bReviverSeated = true;

	// Sync the kit ABP's turn-in-place snapshot to the body: UpperYaw = clamped
	// delta(rotation, TurnDir) recomputes every anim update, and whatever it froze at
	// pre-hold (28° in the diagnostic run) stays baked into the kneel as a torso twist.
	// With TurnDir == the current rotation the chain computes zero for the whole hold.
	if (UAnimInstance* AnimInst = MeshComp->GetAnimInstance())
	{
		const FRotator ActorRot = GetActorRotation();
		for (const TCHAR* VarName : { TEXT("TurnDir"), TEXT("TurnDirEnd") })
		{
			const FStructProperty* Prop = CastField<FStructProperty>(AnimInst->GetClass()->FindPropertyByName(VarName));
			if (Prop && Prop->Struct == TBaseStructure<FRotator>::Get())
				*Prop->ContainerPtrToValuePtr<FRotator>(AnimInst) = ActorRot;
		}
	}
}


void AExtractionPlayer::SetReviveAnimsActive(bool bActive)
{
	if (bActive && IsValid(WeaponComponent))
	{
		WeaponComponent->StopFire();
		WeaponComponent->SetAiming(false);
		OnADSChanged(false);
		bAutoLeanActive = false;
		AutoLeanTargetAlpha = 0.f;
		if (AWeaponBase* Weapon = WeaponComponent->GetCurrentWeapon())
		{
			Weapon->CancelRecoilRecovery();
			Weapon->CancelReload();
		}
	}
	// After the ADS/pose drops: OnADSChanged drives the kit's NewHandPose, which can re-show
	// the hand-socket item a hide-first ordering just hid.
	SetHeldWeaponHidden(bActive);

	if (ACharacter* Target = Cast<ACharacter>(ReviveTarget))
	{
		if (bActive)
		{
			MoveIgnoreActorAdd(Target);
			Target->MoveIgnoreActorAdd(this);
		}
		else
		{
			MoveIgnoreActorRemove(Target);
			if (IsValid(Target)) Target->MoveIgnoreActorRemove(this);
		}
	}

	USkeletalMeshComponent* MeshComp = GetMesh();
	UAnimInstance* AnimInst = IsValid(MeshComp) ? MeshComp->GetAnimInstance() : nullptr;
	if (!IsValid(AnimInst) || !ReviverMontage) return;

	if (bActive)
	{
		AnimInst->SetRootMotionMode(ERootMotionMode::RootMotionFromMontagesOnly);
		// Skip the authored stand-turn-kneel intro AND keep the auto blend-out region off the
		// timeline: playable = [offset, length - blendout], rate-scaled to span the full hold
		// so the clip never starts blending back to standing while the player is still holding.
		// Zero blend-in: the standing and kneel poses differ ~145° in facing, so any blend slerps
		// the whole body (and its visible FP arms/shadow) the long way round.
		const float Length = ReviverMontage->GetPlayLength();
		const float StartAt = FMath::Min(ReviverKneelStartOffsetSeconds, Length);
		const float Playable = Length - StartAt - ReviverMontage->BlendOut.GetBlendTime();
		const float Rate = (ReviveDuration > 0.f && Playable > 0.f) ? Playable / ReviveDuration : 1.f;
		const FMontageBlendSettings BlendIn(0.f);
		if (AnimInst->Montage_PlayWithBlendSettings(ReviverMontage, BlendIn, Rate, EMontagePlayReturnType::MontageLength, StartAt) <= 0.f)
			UE_LOG(LogExtraction, Warning, TEXT("SetReviveAnimsActive: Montage_Play failed for '%s' — hold continues without the kneel."),
				*GetNameSafe(ReviverMontage));
	}
	else if (AnimInst->Montage_IsPlaying(ReviverMontage))
	{
		// Zero blend: the capsule un-seats the same frame — a blended stop would swing the
		// head-mounted camera through the body while the capsule flips back to control yaw.
		AnimInst->Montage_Stop(0.f, ReviverMontage);
	}
}

void AExtractionPlayer::AlignForRevive(const FVector& ReviverLocation)
{
	// Legacy plain look-at, currently unreachable: the companion-revives-player task positions
	// itself (never calls this), and BeginReviveHold only ever targets the companion, whose
	// override carries the authored-angle math.
	if (!bIsDBNO) return;
	const FVector ToReviver = (ReviverLocation - GetActorLocation()).GetSafeNormal2D();
	if (!ToReviver.IsNearlyZero())
		SetActorRotation(FRotator(0.f, ToReviver.Rotation().Yaw, 0.f));
}

bool AExtractionPlayer::IsBeingRevivedMontagePlaying() const
{
	if (!BeingRevivedMontage) return false;
	const USkeletalMeshComponent* MeshComp = GetMesh();
	const UAnimInstance* AnimInst = IsValid(MeshComp) ? MeshComp->GetAnimInstance() : nullptr;
	return IsValid(AnimInst) && AnimInst->Montage_IsPlaying(BeingRevivedMontage);
}

void AExtractionPlayer::OnRep_IsDBNO()
{
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (IsValid(MoveComp))
	{
		if (bIsDBNO)
		{
			MoveComp->StopMovementImmediately();
			MoveComp->SetMovementMode(MOVE_Walking);
			SavedMaxWalkSpeedCrouched = MoveComp->MaxWalkSpeedCrouched;
			MoveComp->MaxWalkSpeed = DBNOCrawlSpeed;
			MoveComp->MaxWalkSpeedCrouched = DBNOCrawlSpeed;
			if (MoveComp->GetNavAgentPropertiesRef().bCanCrouch) Crouch();
		}
		else
		{
			MoveComp->SetMovementMode(MOVE_Walking);
			MoveComp->MaxWalkSpeedCrouched = SavedMaxWalkSpeedCrouched;
			if (const UCharacterMovementComponent* Archetype = Cast<UCharacterMovementComponent>(MoveComp->GetArchetype()))
				MoveComp->MaxWalkSpeed = Archetype->MaxWalkSpeed;
			else
				MoveComp->MaxWalkSpeed = GetClass()->GetDefaultObject<AExtractionPlayer>()->GetCharacterMovement()->MaxWalkSpeed;
			UnCrouch();
		}
	}

	if (bIsDBNO)
		BleedoutTimeRemaining = BleedoutDuration;

	if (!bIsDBNO)
	{
		SetBeingRevived(false);
		SetDBNOCameraFreeLook(false);
	}
	else if (!bBeingRevivedAnimActive)
	{
		SetDBNOCameraFreeLook(true);
	}

	// Restore order matters when leaving DBNO: SetBeingRevived above ran first, so the
	// profile's saved bUseControllerRotationYaw wins (see SetDBNOMovementProfile).
	SetHeldWeaponHidden(bIsDBNO);
	SetDBNOMovementProfile(bIsDBNO);

	OnDBNOStateChanged.Broadcast(bIsDBNO, bIsDBNO ? BleedoutDuration : 0.f);
}

void AExtractionPlayer::DebugApplyDamage()
{
	if (!HasAuthority()) return;
	if (!IsValid(HealthComponent)) return;

	HealthComponent->TakeDamage(25.f);
	UE_LOG(LogExtraction, Verbose, TEXT("Debug: Applied 25 damage. Health=%.0f/%.0f Shield=%.0f/%.0f"),
		HealthComponent->GetCurrentHealth(), HealthComponent->GetMaxHealth(),
		HealthComponent->GetCurrentShield(), HealthComponent->GetMaxShield());
}

// ---- Revive ----

AActor* AExtractionPlayer::FindReviveTarget() const
{
	const UWorld* World = GetWorld();
	if (!IsValid(World)) return nullptr;

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

	AActor* HitActor = HitResult.GetActor();
	if (!IsValid(HitActor)) return nullptr;

	IExtractionPlayerInterface* Iface = Cast<IExtractionPlayerInterface>(HitActor);
	if (!Iface || !Iface->GetIsDBNO()) return nullptr;

	// The camera trace reaches further than the hold allows — only offer targets where
	// BeginReviveHold's actor-distance gate will actually accept the press.
	if (FVector::DistSquared(GetActorLocation(), HitActor->GetActorLocation()) > FMath::Square(ReviveProximityRadius))
		return nullptr;

	return HitActor;
}

void AExtractionPlayer::UpdateReviveCandidateScan(float DeltaTime)
{
	ReviveCandidateScanAccumulator += DeltaTime;
	if (ReviveCandidateScanAccumulator < 0.1f) return;
	ReviveCandidateScanAccumulator = 0.f;

	// While the hold runs, the prompt tracks the held target (progress bar); a downed player
	// can't revive anyone.
	if (bIsReviving) { ReviveCandidate = ReviveTarget; return; }
	if (bIsDBNO) { ReviveCandidate = nullptr; return; }
	ReviveCandidate = FindReviveTarget();
}

void AExtractionPlayer::UpdateRevive(float DeltaTime)
{
	// Shot down mid-hold: tear the lock down cleanly instead of leaving a kneeling DBNO body.
	if (bIsDBNO)
	{
		UE_LOG(LogExtraction, Log, TEXT("Revive cancel: reviver went DBNO at %.2fs"), ReviveElapsed);
		CancelRevive();
		return;
	}

	IExtractionPlayerInterface* TargetIface = Cast<IExtractionPlayerInterface>(ReviveTarget);
	if (!IsValid(ReviveTarget) || !TargetIface || !TargetIface->GetIsDBNO())
	{
		UE_LOG(LogExtraction, Log, TEXT("Revive cancel: target '%s' invalid or no longer DBNO at %.2fs"),
			*GetNameSafe(ReviveTarget), ReviveElapsed);
		CancelRevive();
		return;
	}

	const float DistSq = FVector::DistSquared(GetActorLocation(), ReviveTarget->GetActorLocation());
	if (DistSq > FMath::Square(ReviveProximityRadius))
	{
		UE_LOG(LogExtraction, Log, TEXT("Revive cancel: target %.0fcm away (limit %.0f) at %.2fs"),
			FMath::Sqrt(DistSq), ReviveProximityRadius, ReviveElapsed);
		CancelRevive();
		return;
	}

	ReviveElapsed += DeltaTime;

	// Per-frame dump through the spin window (montage blend-in + pose-branch blend), 4Hz after.
	if (ReviveElapsed < 0.6f
		|| FMath::FloorToInt(ReviveElapsed * 4.f) != FMath::FloorToInt((ReviveElapsed - DeltaTime) * 4.f))
		LogReviveDebugState(TEXT("TICK"), *this, ReviveTarget);

	if (ReviveElapsed < ReviveDuration) return;

	TargetIface->ExitDBNO();
	CancelRevive();
}

void AExtractionPlayer::CancelRevive()
{
	if (!bIsReviving) return;

	LogReviveDebugState(TEXT("END-PRE"), *this, ReviveTarget);

	// Mirror of BTTask_RevivePlayer::CleanupRevive — single teardown for every exit
	// (E-release, completion, reviver DBNO, target dead/invalid, EndPlay). Reads
	// ReviveTarget, so state resets last.
	SetReviveAnimsActive(false);
	if (IsValid(ReviveTarget))
	{
		if (IExtractionPlayerInterface* TargetIface = Cast<IExtractionPlayerInterface>(ReviveTarget))
			TargetIface->SetBeingRevived(false);
	}

	if (bReviverSeated)
	{
		if (USkeletalMeshComponent* MeshComp = GetMesh())
		{
			MeshComp->SetRelativeRotation(ReviverSavedMeshRelativeRotation);
			MeshComp->TickPose(0.f, false);
			MeshComp->RefreshBoneTransforms();
		}
		bReviverSeated = false;
	}
	if (APlayerController* IgnoredPC = ReviverLookIgnoredPC.Get())
		IgnoredPC->SetIgnoreLookInput(false);
	ReviverLookIgnoredPC.Reset();
	if (AController* C = GetController())
		C->SetControlRotation(ReviverSavedControlRotation);
	bUseControllerRotationYaw = bReviverSavedUseControllerRotationYaw;
	bReviverSavedUseControllerRotationYaw = false;

	// Reassert normal grounded movement after completion or interruption. The hold rejects
	// starts from falling and blocks all movement input while active.
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
		Move->SetMovementMode(MOVE_Walking);

	LogReviveDebugState(TEXT("END-POST"), *this, ReviveTarget);

	bIsReviving = false;
	ReviveElapsed = 0.f;
	ReviveTarget = nullptr;
}

// ---- Companion Debug Exec Commands ----

ACompanionCharacter* AExtractionPlayer::ResolveDebugCompanion() const
{
	if (WiredCompanion.IsValid()) return WiredCompanion.Get();

	return ACompanionCharacter::GetPrimaryCompanion(GetWorld());
}

void AExtractionPlayer::CompAim(bool bEnable)
{
	ACompanionCharacter* Comp = ResolveDebugCompanion();
	if (!Comp) { UE_LOG(LogCompanion, Warning, TEXT("CompAim: no companion found")); return; }

	Comp->SetScriptedAim(bEnable);
	UE_LOG(LogCompanion, Log, TEXT("CompAim -> %d"), bEnable);
}

void AExtractionPlayer::CompFire(bool bEnable)
{
	ACompanionCharacter* Comp = ResolveDebugCompanion();
	if (!Comp) { UE_LOG(LogCompanion, Warning, TEXT("CompFire: no companion found")); return; }

	bEnable ? Comp->StartWeaponFire() : Comp->StopWeaponFire();
	UE_LOG(LogCompanion, Log, TEXT("CompFire -> %d"), bEnable);
}

void AExtractionPlayer::CompReload()
{
	ACompanionCharacter* Comp = ResolveDebugCompanion();
	if (!Comp) { UE_LOG(LogCompanion, Warning, TEXT("CompReload: no companion found")); return; }

	Comp->ReloadWeapon();
	UE_LOG(LogCompanion, Log, TEXT("CompReload triggered"));
}

void AExtractionPlayer::CompLowReady(bool bEnable)
{
	ACompanionCharacter* Comp = ResolveDebugCompanion();
	if (!Comp) { UE_LOG(LogCompanion, Warning, TEXT("CompLowReady: no companion found")); return; }

	Comp->SetLowReadyAim(bEnable);
	UE_LOG(LogCompanion, Log, TEXT("CompLowReady -> %d"), bEnable);
}

void AExtractionPlayer::CompDebug(bool bFreeze)
{
	ACompanionCharacter* Comp = ResolveDebugCompanion();
	if (!Comp) { UE_LOG(LogCompanion, Warning, TEXT("CompDebug: no companion found")); return; }

	AAIController* AIC = Cast<AAIController>(Comp->GetController());
	UBrainComponent* Brain = AIC ? AIC->GetBrainComponent() : nullptr;
	if (!Brain) { UE_LOG(LogCompanion, Warning, TEXT("CompDebug: no brain component")); return; }

	if (bFreeze) Brain->StopLogic(TEXT("CompDebug"));
	else Brain->RestartLogic();

	UE_LOG(LogCompanion, Log, TEXT("CompDebug freeze -> %d"), bFreeze);
}

void AExtractionPlayer::PlayerDown()
{
	if (!HasAuthority()) { UE_LOG(LogExtraction, Warning, TEXT("PlayerDown: no authority")); return; }
	if (bIsDBNO) { UE_LOG(LogExtraction, Warning, TEXT("PlayerDown: already DBNO")); return; }
	if (!IsValid(HealthComponent)) { UE_LOG(LogExtraction, Warning, TEXT("PlayerDown: no health component")); return; }
	// After a full death HealthComponent stays bIsDead; Die() would early-return, so the exec would
	// silently no-op while logging success. Revive resets bIsDead, so this only blocks post-bleedout.
	if (HealthComponent->IsDead()) { UE_LOG(LogExtraction, Warning, TEXT("PlayerDown: already fully dead — revive/respawn first")); return; }

	UE_LOG(LogExtraction, Log, TEXT("PlayerDown: forcing DBNO via console"));
	HealthComponent->Die();
}

void AExtractionPlayer::CompDown()
{
	if (!HasAuthority()) { UE_LOG(LogCompanion, Warning, TEXT("CompDown: no authority")); return; }

	ACompanionCharacter* Comp = ResolveDebugCompanion();
	if (!Comp) { UE_LOG(LogCompanion, Warning, TEXT("CompDown: no companion found")); return; }
	if (Comp->GetIsDBNO()) { UE_LOG(LogCompanion, Warning, TEXT("CompDown: already DBNO")); return; }

	UHealthComponent* CompHealth = Comp->GetHealthComponent();
	if (!IsValid(CompHealth)) { UE_LOG(LogCompanion, Warning, TEXT("CompDown: no health component")); return; }
	if (CompHealth->IsDead()) { UE_LOG(LogCompanion, Warning, TEXT("CompDown: already dead")); return; }

	UE_LOG(LogCompanion, Log, TEXT("CompDown: forcing companion DBNO via console"));
	CompHealth->Die();
}

ACompanionCharacter* AExtractionPlayer::ResolveDebugExtractee() const
{
	const UWorld* World = GetWorld();
	if (!World) return nullptr;

	for (TActorIterator<AExtracteeCompanion> It(World); It; ++It)
		if (IsValid(*It)) return *It;

	return nullptr;
}

void AExtractionPlayer::VipReload()
{
	ACompanionCharacter* Vip = ResolveDebugExtractee();
	if (!Vip) { UE_LOG(LogCompanion, Warning, TEXT("VipReload: no extraction VIP found")); return; }

	// Force it even on a full magazine. CanReload() requires CurrentAmmo < MagazineSize, so on a
	// freshly-spawned VIP that has not fired a shot the command would otherwise silently no-op —
	// which is exactly the case you want to inspect the animation in.
	if (AWeaponBase* Weapon = Vip->GetCurrentWeapon())
	{
		if (!Weapon->IsReloading() && !Weapon->CanReload())
			Weapon->DebugDrainMagazine(0);
	}

	// Reload is inherited straight from ACompanionCharacter — the VIP overrides only the fire gate —
	// so this is the exact call the BT combat task makes, not a debug-only shortcut.
	Vip->ReloadWeapon();
	UE_LOG(LogCompanion, Log, TEXT("VipReload triggered on %s"), *Vip->GetName());
}

// --- Single-reviver claim ---

bool AExtractionPlayer::TryClaimRevive(AActor* Claimant)
{
	if (!IsValid(Claimant)) return false;

	AActor* Holder = ReviveClaimant.Get();
	if (Holder == Claimant) return true; // idempotent re-claim by the current holder

	// A hold whose owner has gone down, died or lost its controller is stale — steal it, or the
	// surviving ally could never take over a revive the first one can no longer finish.
	if (IsValid(Holder) && ACompanionCharacter::IsReviveClaimantCapable(Holder)) return false;

	ReviveClaimant = Claimant;
	return true;
}

void AExtractionPlayer::ReleaseReviveClaim(AActor* Claimant)
{
	// Holder-only: a losing bidder releasing must never free the winner's hold.
	if (ReviveClaimant.Get() != Claimant) return;
	ReviveClaimant.Reset();
}
