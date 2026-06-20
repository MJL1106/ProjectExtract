// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExtractionPlayer.h"
#include "AI/AITargetingStatics.h"
#include "Perception/AISightTargetInterface.h"
#include "Perception/AISense_Sight.h"
#include "ExtractionAnimInstance.h"
#include "TraversalComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "Camera/CameraComponent.h"
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
#include "EnemyDebug.h"

AExtractionPlayer::AExtractionPlayer()
	: bIsDBNO(false)
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

void AExtractionPlayer::TakedownInput(const FInputActionValue& Value)
{
	UE_LOG(LogExtraction, Warning, TEXT("[Takedown] AExtractionPlayer::TakedownInput fired — HasAuthority=%d MontageActive=%d TakedownMontage=%s"),
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

	UE_LOG(LogExtraction, Warning, TEXT("[Takedown] scan complete: %d enemies, %d takeable, %d facing-rejected, Best=%s"),
		DbgTotal, DbgTakeable, DbgFacingRejected, *GetNameSafe(Best));

	if (!IsValid(Best))
	{
		UE_LOG(LogExtraction, Warning, TEXT("Takedown: no target (need Unaware enemy within range, behind it)"));
		return;
	}

	UE_LOG(LogExtraction, Warning, TEXT("Takedown: %s"), *GetNameSafe(Best));

	if (!IsValid(TakedownMontage))
	{
		// Instant path: enemy stays in place, snap/align is skipped entirely.
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

	if (!Victim->BeginTakedownHold(this, VictimLoc, SnapYaw, MontageDuration)) return;

	PendingTakedownVictim = Victim;
	bTakedownMontageActive = true;

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

	// End delegate fires on natural end AND interruption — fallback kill so no frozen enemy survives.
	FOnMontageEnded EndDelegate;
	EndDelegate.BindUObject(this, &AExtractionPlayer::OnTakedownMontageEnded);
	AnimInst->Montage_SetEndDelegate(EndDelegate, TakedownMontage);
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

	bAutoLeanActive = false;
	AutoLeanTargetAlpha = 0.f;

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
