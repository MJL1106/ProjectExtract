// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExtractionPlayer.h"
#include "ExtractionPlayerMovement.h"
#include "Components/CompanionCommandComponent.h"
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
#include "HAL/IConsoleManager.h"

// Live revive-arrangement tuning (paired with revive.* CVars in CompanionCharacter.cpp): rotates the
// downed player's aligned body relative to the reviver. Tweak in PIE, bake the winner, leave at 0.
static float GRevivePlayerYawOffset = 0.f;
static FAutoConsoleVariableRef CVarRevivePlayerYawOffset(
	TEXT("revive.PlayerYawOffset"), GRevivePlayerYawOffset,
	TEXT("Extra yaw (deg) added to the downed player's revive alignment. 0 = face the reviver."));

// Per-frame camera trace while DBNO/being-revived and for 2s after the revive — for pinning down
// which rotation source jumps on the snap frame. `revive.CameraDebug 1` to enable.
static int32 GReviveCameraDebug = 0;
static FAutoConsoleVariableRef CVarReviveCameraDebug(
	TEXT("revive.CameraDebug"), GReviveCameraDebug,
	TEXT("1 = per-frame camera/control/body rotation trace around the revive (very chatty)."));

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

	if (const UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(BleedoutTimerHandle);

	// Takedown was committed — kill the frozen victim so player despawn can't leave them stuck alive.
	if (AEnemyCharacter* Victim = PendingTakedownVictim.Get())
		Victim->FinishTakedownKill(this);
	PendingTakedownVictim.Reset();
	bTakedownMontageActive = false;

	Super::EndPlay(EndPlayReason);
}

void AExtractionPlayer::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Snap-hunt trace: one line per frame while downed/being revived and for 2s after the revive.
	// Whichever column jumps on the snap frame is the source.
	if (GReviveCameraDebug && IsLocallyControlled())
	{
		const UWorld* World = GetWorld();
		const float SinceRevive = World ? World->GetTimeSeconds() - LastReviveWorldTime : 1e9f;
		if (bIsDBNO || bBeingRevivedAnimActive || SinceRevive < 2.f)
		{
			const APlayerController* PC = Cast<APlayerController>(GetController());
			const FRotator CamRot = PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraRotation() : FRotator::ZeroRotator;
			const FVector CamLoc = PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
			const FRotator CtrlRot = PC ? PC->GetControlRotation() : FRotator::ZeroRotator;
			const USkeletalMeshComponent* MeshComp = GetMesh();
			const float HeadYaw = MeshComp ? MeshComp->GetSocketRotation(TEXT("head")).Yaw : 0.f;
			const USpringArmComponent* Arm = CachedSpringArm.Get();
			const UAnimInstance* AnimInst = MeshComp ? MeshComp->GetAnimInstance() : nullptr;
			const UAnimMontage* ActiveMon = IsValid(AnimInst) ? AnimInst->GetCurrentActiveMontage() : nullptr;

			UE_LOG(LogExtraction, Log,
				TEXT("CAMTRACE t=%.3f cam=(%.1f,%.1f) camLoc=%s ctrl=(%.1f,%.1f) actorYaw=%.1f headYaw=%.1f armPCR=%d yawFollow=%d DBNO=%d beingRev=%d montage=%s"),
				World ? World->GetTimeSeconds() : 0.f,
				CamRot.Yaw, CamRot.Pitch, *CamLoc.ToCompactString(),
				CtrlRot.Yaw, CtrlRot.Pitch,
				GetActorRotation().Yaw, HeadYaw,
				Arm ? (int32)Arm->bUsePawnControlRotation : -1,
				(int32)bUseControllerRotationYaw, (int32)bIsDBNO, (int32)bBeingRevivedAnimActive,
				*GetNameSafe(ActiveMon));
		}
	}

	// Deferred DBNO free-look restore: hand the camera back to head-bone inheritance only once the
	// head has converged with the view yaw (get-up montage fully blended out). Restoring the instant
	// DBNO ends inherited a head still posed ~45° off — a hard snap + quarter-second swing-back.
	// 1s cap = safety net (also covers FullDeath, which never restored the flag before).
	if (bDBNOFreeLookActive && !bIsDBNO && !bBeingRevivedAnimActive && IsLocallyControlled())
	{
		const UWorld* World = GetWorld();
		const float SinceRevive = World ? World->GetTimeSeconds() - LastReviveWorldTime : 1e9f;
		const USkeletalMeshComponent* MeshComp = GetMesh();
		const float HeadYaw = MeshComp ? MeshComp->GetSocketRotation(TEXT("head")).Yaw : GetActorRotation().Yaw;
		const float CtrlYaw = IsValid(GetController()) ? GetController()->GetControlRotation().Yaw : HeadYaw;
		if (FMath::Abs(FMath::FindDeltaAngleDegrees(HeadYaw, CtrlYaw)) < 5.f || SinceRevive > 1.f)
			SetDBNOCameraFreeLook(false);
	}

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
	}

	if (IsLocallyControlled() && bIsReviving)
		UpdateRevive(DeltaTime);

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

	if (TakedownAction)
		EnhancedInput->BindAction(TakedownAction, ETriggerEvent::Started, this, &AExtractionPlayer::TakedownInput);

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
	if (bTakedownMontageActive) return;
	// Being-revived does NOT lock look: yaw-follow is suspended (SetBeingRevived), so the camera
	// spins freely while the animated body stays put. Only movement stays locked (DoMove).

	AddControllerYawInput(Yaw);
	AddControllerPitchInput(Pitch);
}

void AExtractionPlayer::DoMove(float Right, float Forward)
{
	if (IsInTraversal()) return;
	if (bBeingRevivedAnimActive) return;
	if (!IsValid(GetController())) return;

	AddMovementInput(GetActorRightVector(), Right);
	AddMovementInput(GetActorForwardVector(), Forward);
}

void AExtractionPlayer::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	Super::CalcCamera(DeltaTime, OutResult);
	if (bTakedownMontageActive && TakedownNearClipPlane > 0.f)
		OutResult.PerspectiveNearClipPlane = TakedownNearClipPlane;
}

// ---- Weapon Input ----

void AExtractionPlayer::FireStart(const FInputActionValue& Value)
{
	if (bIsDBNO) return;
	if (IsInTraversal()) return;
	if (!IsValid(WeaponComponent)) return;

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
	bAutoLeanActive = true;
}

void AExtractionPlayer::ADSStop(const FInputActionValue& Value)
{
	if (!IsValid(WeaponComponent)) return;

	WeaponComponent->SetAiming(false);
	OnADSChanged(false);
	bAutoLeanActive = false;
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

	UE_LOG(LogExtraction, Verbose, TEXT("[VAULT_DEBUG] HandleTraversalStarted type=%d playRate=%.2f"), (int32)Type, PlayRate);

	UExtractionAnimInstance* AnimInst = CachedAnimInstance;
	UE_LOG(LogExtraction, Verbose, TEXT("[VAULT_DEBUG] CachedAnimInstance is %s"), *GetNameSafe(AnimInst));
	if (!IsValid(AnimInst)) return;

	switch (Type)
	{
	case ETraversalType::Vault:
		AnimInst->PlayVaultMontage(PlayRate);
		UE_LOG(LogExtraction, Verbose, TEXT("[VAULT_DEBUG] PlayVaultMontage called"));
		break;
	case ETraversalType::Climb:
		AnimInst->PlayClimbMontage(PlayRate);
		UE_LOG(LogExtraction, Verbose, TEXT("[VAULT_DEBUG] PlayClimbMontage called"));
		break;
	case ETraversalType::Mantle:
		AnimInst->PlayMantleMontage(PlayRate);
		UE_LOG(LogExtraction, Verbose, TEXT("[VAULT_DEBUG] PlayMantleMontage called"));
		break;
	default: break;
	}

	FOnMontageEnded EndDelegate;
	EndDelegate.BindUObject(this, &AExtractionPlayer::OnTraversalMontageEnded);
	AnimInst->Montage_SetEndDelegate(EndDelegate, AnimInst->GetCurrentActiveMontage());
	UE_LOG(LogExtraction, Verbose, TEXT("[VAULT_DEBUG] End delegate bound to active montage=%s"),
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
}

void AExtractionPlayer::UpdateCompanionSoftCollision()
{
	if (GetIsDBNO() || bIsReviving) return;
	if (IsValid(TraversalComponent) && TraversalComponent->IsBusy()) return;

	if (!IsValid(CompanionCommandComponent)) return;
	ACompanionCharacter* Companion = CompanionCommandComponent->GetCompanion();
	if (!IsValid(Companion)) return;

	UCapsuleComponent* CompCapsule = Companion->GetCapsuleComponent();
	if (!IsValid(CompCapsule) || CompCapsule->GetCollisionEnabled() == ECollisionEnabled::NoCollision) return;

	if (WiredCompanion.Get() != Companion)
	{
		if (ACompanionCharacter* Old = WiredCompanion.Get())
		{
			if (UCapsuleComponent* OldCap = Old->GetCapsuleComponent())
				OldCap->IgnoreActorWhenMoving(this, false);
			GetCapsuleComponent()->IgnoreActorWhenMoving(Old, false);
		}
		GetCapsuleComponent()->IgnoreActorWhenMoving(Companion, true);
		CompCapsule->IgnoreActorWhenMoving(this, true);
		WiredCompanion = Companion;
	}

	FVector Delta = GetActorLocation() - Companion->GetActorLocation();
	Delta.Z = 0.f;

	const float CombinedRadius = GetCapsuleComponent()->GetScaledCapsuleRadius()
		+ CompCapsule->GetScaledCapsuleRadius()
		+ CompanionPushPadding;

	const float Dist = Delta.Size();
	if (Dist >= CombinedRadius) return;

	FVector PushDir = (Dist > KINDA_SMALL_NUMBER) ? (Delta / Dist) : GetActorRightVector();
	PushDir.Z = 0.f;
	PushDir = PushDir.GetSafeNormal();

	const float DepthFraction = 1.f - (Dist / CombinedRadius);
	const FVector Push = StripOpposingPush(PushDir * (CompanionPushStrength * DepthFraction), GetLastMovementInputVector());
	AddMovementInput(Push, 1.f);
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
	if (!IsLocallyControlled()) return;

	// World interactions (loot container / keycard door) win over the revive hold.
	if (TryWorldInteract()) return;

	AExtractionPlayer* Target = FindReviveTarget();
	if (!IsValid(Target)) return;

	ReviveTarget = Target;
	ReviveElapsed = 0.f;
	bIsReviving = true;
	Target->SetBeingRevived(true, ReviveDuration);
	Target->AlignForRevive(GetActorLocation());

	UE_LOG(LogExtraction, Verbose, TEXT("'%s' began reviving '%s'"), *GetNameSafe(this), *GetNameSafe(Target));
}

void AExtractionPlayer::InteractStop(const FInputActionValue& Value)
{
	if (!IsLocallyControlled()) return;
	if (bIsReviving) CancelRevive();
}

bool AExtractionPlayer::TryWorldInteract()
{
	UWorld* World = GetWorld();
	if (!World) return false;

	// Controller view point tracks CalcCamera (kit-driven FP camera) — more accurate than eyes.
	FVector ViewLoc; FRotator ViewRot;
	GetActorEyesViewPoint(ViewLoc, ViewRot);
	if (const AController* C = GetController())
		C->GetPlayerViewPoint(ViewLoc, ViewRot);

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PlayerInteract), false, this);
	const FVector TraceEnd = ViewLoc + ViewRot.Vector() * InteractTraceRange;
	if (!World->LineTraceSingleByChannel(Hit, ViewLoc, TraceEnd, ECC_Visibility, Params)) return false;

	AActor* HitActor = Hit.GetActor();
	if (!IsValid(HitActor)) return false;

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

void AExtractionPlayer::TakedownInput(const FInputActionValue& Value)
{
	UE_LOG(LogExtraction, Verbose, TEXT("[Takedown] AExtractionPlayer::TakedownInput fired — HasAuthority=%d MontageActive=%d TakedownMontage=%s"),
		HasAuthority(), bTakedownMontageActive, *GetNameSafe(TakedownMontage));

	if (!HasAuthority()) return;
	// Re-entrancy guard: a second press during an active montage takedown would orphan the frozen victim.
	if (bTakedownMontageActive) return;

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
	if (CompanionCommandComponent->GetPendingCommand() == ECompanionCommand::Loot)
		CompanionCommandComponent->ConfirmLoot();
	else
		CompanionCommandComponent->ConfirmBreach();
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

	SetDBNOCameraFreeLook(true);

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
	{
		MoveComp->SetMovementMode(MOVE_Walking);
		MoveComp->MaxWalkSpeedCrouched = SavedMaxWalkSpeedCrouched;
		if (const UCharacterMovementComponent* Archetype = Cast<UCharacterMovementComponent>(MoveComp->GetArchetype()))
			MoveComp->MaxWalkSpeed = Archetype->MaxWalkSpeed;
		else
			MoveComp->MaxWalkSpeed = GetClass()->GetDefaultObject<AExtractionPlayer>()->GetCharacterMovement()->MaxWalkSpeed;
	}
	UnCrouch();

	SetBeingRevived(false);
	// Free-look restore is DEFERRED to Tick: flipping the spring arm back to bone-inheritance here,
	// while the get-up montage still poses the head ~45° off the view, snapped the camera 47° and
	// swung it back over the blend-out (CAMTRACE-verified). Tick restores once the head converges.

	if (const UWorld* World = GetWorld())
		LastReviveWorldTime = World->GetTimeSeconds();

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

	bAutoLeanActive = false;
	AutoLeanTargetAlpha = 0.f;

	if (const UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(BleedoutTimerHandle);

	// TODO: Ragdoll, drop loot, spectate camera
	UE_LOG(LogExtraction, Log, TEXT("'%s' is fully dead"), *GetNameSafe(this));
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
	if (bBeingRevived == bBeingRevivedAnimActive)
	{
		// Redundant false→false calls are normal teardown (montage-driven exit already cleared the
		// state); only a stale TRUE at hold start is a real problem (player would stay flat).
		if (bBeingRevived)
			UE_LOG(LogExtraction, Warning, TEXT("SetBeingRevived(true): flag already true — montage NOT retriggered, player will stay flat"));
		return;
	}
	bBeingRevivedAnimActive = bBeingRevived;

	// Yaw-follow suspension: controller yaw re-slaves the actor every frame, which would stomp
	// AlignForRevive's facing and let look input drag the animated body. Input locks live in
	// DoAim/DoMove (gated on bBeingRevivedAnimActive).
	if (bBeingRevived)
	{
		bSavedUseControllerRotationYaw = bUseControllerRotationYaw;
		bUseControllerRotationYaw = false;
	}
	else
	{
		bUseControllerRotationYaw = bSavedUseControllerRotationYaw;

		// Align the body to the view the player held during the revive in the SAME frame yaw-follow
		// resumes — otherwise the head-bone-inherited camera shows one frame of the old body yaw and
		// then swings as the body re-slaves: the end-of-revive camera snap. Skip while still DBNO
		// (abort path): the downed body must not spin to the free-look camera.
		if (!bIsDBNO && bUseControllerRotationYaw)
		{
			if (AController* PC = GetController())
				SetActorRotation(FRotator(0.f, PC->GetControlRotation().Yaw, 0.f));
		}
	}

	// Hide the held weapon while the paired revive anims play (the clips are authored empty-handed);
	// restored on every exit path since all of them come back through here with false.
	// SetWeaponHidden, not SetActorHiddenInGame: the visible gun is a separate visual actor.
	if (IsValid(WeaponComponent))
	{
		if (AWeaponBase* Weapon = WeaponComponent->GetCurrentWeapon())
			Weapon->SetWeaponHidden(bBeingRevived);
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
			Attached->SetActorHiddenInGame(bBeingRevived);
	}

	USkeletalMeshComponent* MeshComp = GetMesh();
	UAnimInstance* AnimInst = IsValid(MeshComp) ? MeshComp->GetAnimInstance() : nullptr;
	if (!IsValid(AnimInst) || !BeingRevivedMontage)
	{
		UE_LOG(LogExtraction, Warning, TEXT("SetBeingRevived(%d): no anim instance or no BeingRevivedMontage assigned — player stays flat"),
			bBeingRevived);
		return;
	}

	if (bBeingRevived && bIsDBNO)
	{
		if (!AnimInst->Montage_IsPlaying(BeingRevivedMontage))
		{
			// Rate-scale so one montage cycle spans the reviver's hold exactly — the get-up motion
			// lands on the frame the revive completes, whatever the hold duration is tuned to.
			const float MontageLength = BeingRevivedMontage->GetPlayLength();
			const float PlayRate = (ExpectedDuration > 0.f && MontageLength > 0.f)
				? MontageLength / ExpectedDuration : 1.f;
			const float PlayLength = AnimInst->Montage_Play(BeingRevivedMontage, PlayRate);
			UE_LOG(LogExtraction, Log, TEXT("SetBeingRevived: playing '%s' rate=%.3f -> playLength=%.2f (bIsDBNO=%d expectDur=%.2f)"),
				*GetNameSafe(BeingRevivedMontage), PlayRate, PlayLength, bIsDBNO, ExpectedDuration);
			if (PlayLength <= 0.f)
			{
				UE_LOG(LogExtraction, Warning, TEXT("%s: BeingRevivedMontage '%s' failed to play (slot/skeleton mismatch?)"),
					*GetName(), *GetNameSafe(BeingRevivedMontage));
			}
			else
			{
				// The get-up montage owns revive completion: its natural blend-out fires ExitDBNO at
				// the frame the character is upright. The reviver's timer stays as fallback only.
				FOnMontageBlendingOutStarted BlendOutDelegate;
				BlendOutDelegate.BindUObject(this, &AExtractionPlayer::OnBeingRevivedMontageBlendOut);
				AnimInst->Montage_SetBlendingOutDelegate(BlendOutDelegate, BeingRevivedMontage);
			}
		}
	}
	else if (bBeingRevived && !bIsDBNO)
	{
		UE_LOG(LogExtraction, Warning, TEXT("SetBeingRevived(true) but bIsDBNO=false — being-revived montage skipped, player will not pose"));
	}
	else if (AnimInst->Montage_IsPlaying(BeingRevivedMontage))
		AnimInst->Montage_Stop(0.25f, BeingRevivedMontage);
}

void AExtractionPlayer::AlignForRevive(const FVector& ReviverLocation)
{
	if (!bIsDBNO) return;

	// The downed player's body IS the arrangement anchor: keep its current yaw (the reviver snaps
	// into the player's frame instead), plus the live-tuning offset. ReviverLocation is now only
	// logged for diagnostics.
	const float PreYaw = GetActorRotation().Yaw;
	const float AlignYaw = PreYaw + GRevivePlayerYawOffset;
	if (!FMath::IsNearlyZero(GRevivePlayerYawOffset))
		SetActorRotation(FRotator(0.f, AlignYaw, 0.f));
	UE_LOG(LogExtraction, Log,
		TEXT("REVIVE ALIGN(player): loc=%s yaw %.1f -> %.1f (yawOffset=%.1f) reviverLoc=%s dist2D=%.1f meshRelYaw=%.1f"),
		*GetActorLocation().ToCompactString(), PreYaw, AlignYaw, GRevivePlayerYawOffset,
		*ReviverLocation.ToCompactString(),
		FVector::Dist2D(GetActorLocation(), ReviverLocation),
		GetMesh() ? GetMesh()->GetRelativeRotation().Yaw : 0.f);

	// No control-rotation sync: the player looks around freely during the revive (yaw-follow is
	// suspended, so the view can't drag the body). On get-up the body aligns to wherever they're
	// looking — normal FPS control resume.
}

void AExtractionPlayer::OnBeingRevivedMontageBlendOut(UAnimMontage* Montage, bool bInterrupted)
{
	// Interrupted = manual stop (task abort/finish) or a same-slot stomp — the active-montage name
	// identifies a stomper when the get-up visibly cuts out mid-hold.
	if (bInterrupted)
	{
		const USkeletalMeshComponent* MeshComp = GetMesh();
		const UAnimInstance* AnimInst = IsValid(MeshComp) ? MeshComp->GetAnimInstance() : nullptr;
		const UAnimMontage* Active = IsValid(AnimInst) ? AnimInst->GetCurrentActiveMontage() : nullptr;
		UE_LOG(LogExtraction, Log, TEXT("Being-revived montage INTERRUPTED (bIsDBNO=%d beingRevived=%d activeMontage=%s)"),
			bIsDBNO, bBeingRevivedAnimActive, *GetNameSafe(Active));
		return;
	}

	if (!bIsDBNO || !bBeingRevivedAnimActive || !HasAuthority()) return;

	UE_LOG(LogExtraction, Log, TEXT("Being-revived montage completed get-up — montage-driven ExitDBNO"));
	ExitDBNO();
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

	if (!bIsDBNO) SetBeingRevived(false);
	// Enable-only here: the restore is deferred to Tick until the head bone converges with the view
	// (same camera-snap avoidance as ExitDBNO).
	if (bIsDBNO) SetDBNOCameraFreeLook(true);

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

	if (IsValid(ReviveTarget)) ReviveTarget->SetBeingRevived(false);

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

	if (IsValid(ReviveTarget)) ReviveTarget->SetBeingRevived(false);

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

// ---- Companion Debug Exec Commands ----

ACompanionCharacter* AExtractionPlayer::ResolveDebugCompanion() const
{
	if (WiredCompanion.IsValid()) return WiredCompanion.Get();

	for (TActorIterator<ACompanionCharacter> It(GetWorld()); It; ++It) return *It;

	return nullptr;
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
