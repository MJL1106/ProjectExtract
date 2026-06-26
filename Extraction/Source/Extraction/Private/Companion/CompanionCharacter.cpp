// AI companion character — follows player, engages enemies, revives downed teammates.

#include "CompanionCharacter.h"
#include "AI/AITargetingStatics.h"
#include "AI/CompanionDiag.h"
#include "CompanionAIController.h"
#include "HealthComponent.h"
#include "WeaponBase.h"
#include "WeaponDataAsset.h"
#include "TraversalComponent.h"
#include "CompanionAnimInstance.h"
#include "SuppressionComponent.h"
#include "ExtractionTypes.h"
#include "Character/ExtractionPlayer.h"
#include "EnemyCharacter.h"
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
	SuppressionComponent = CreateDefaultSubobject<USuppressionComponent>(TEXT("SuppressionComponent"));
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

	// Bug 6: weapon hitscan traces ECC_Visibility, which the inherited CharacterMesh profile ignores —
	// block it on the mesh so enemy fire actually registers on the companion.
	GetMesh()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

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

// --- Scripted Aim ---

void ACompanionCharacter::SetScriptedAim(bool bNewScriptedAim)
{
	bScriptedAim = bNewScriptedAim;
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
	bTakedownInPosition = false;
	bTakedownExecuting = false;
	bTakedownMontagePlaying = false;

	// Aim at the victim
	SetAimTarget(Victim);

	// Bind to the player's commit delegate
	ACompanionAIController* CompAIC = Cast<ACompanionAIController>(GetController());
	APawn* PlayerPawn = IsValid(CompAIC) ? CompAIC->GetPlayerCharacter() : nullptr;
	AExtractionPlayer* Player = Cast<AExtractionPlayer>(PlayerPawn);
	if (IsValid(Player))
	{
		TakedownPlayerRef = Player;
		Player->OnPlayerTakedownCommitted.AddDynamic(this, &ACompanionCharacter::OnPlayerTakedownCommittedHandler);
	}

	UE_LOG(LogCompanion, Log, TEXT("Takedown armed: victim=%s method=%s"),
		*GetNameSafe(Victim), Method == ETakedownMethod::Knife ? TEXT("Knife") : TEXT("Shoot"));
}

void ACompanionCharacter::DisarmCommandedTakedown()
{
	if (!bTakedownArmed && !bTakedownMontagePlaying) return;

	// Unbind delegate
	AExtractionPlayer* Player = TakedownPlayerRef.Get();
	if (IsValid(Player))
		Player->OnPlayerTakedownCommitted.RemoveDynamic(this, &ACompanionCharacter::OnPlayerTakedownCommittedHandler);

	if (const UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(ShootDelayTimerHandle);

	TakedownVictim.Reset();
	TakedownPlayerRef.Reset();
	bTakedownArmed = false;
	bTakedownPlayerCommitted = false;
	bTakedownInPosition = false;
	bTakedownExecuting = false;
	bTakedownCrouchApproach = false;
	bTakedownMontagePlaying = false;

	UE_LOG(LogCompanion, Log, TEXT("Takedown disarmed"));
}

void ACompanionCharacter::OnPlayerTakedownCommittedHandler()
{
	if (!bTakedownArmed) return;

	bTakedownPlayerCommitted = true;
	UE_LOG(LogCompanion, Log, TEXT("Takedown: player committed signal received (inPosition=%d)"),
		(int32)bTakedownInPosition);

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

void ACompanionCharacter::ExecuteCommandedTakedown()
{
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
		// Face the victim
		const FVector ToVictim = Victim->GetActorLocation() - GetActorLocation();
		SetActorRotation(FRotator(0.f, ToVictim.Rotation().Yaw, 0.f));

		// FIX 4: Kill first, only play montage if kill succeeded (no stab-on-air)
		if (!Enemy->ExecuteTakedown(this))
		{
			UE_LOG(LogCompanion, Warning, TEXT("Takedown: knife ExecuteTakedown returned false — aborting"));
			FinishCommandedTakedown();
			return;
		}

		UAnimInstance* AnimInst = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
		const float MontageLen = IsValid(AnimInst) ? AnimInst->Montage_Play(KnifeTakedownMontage) : 0.f;
		if (MontageLen <= 0.f)
		{
			UE_LOG(LogCompanion, Warning, TEXT("Takedown: knife montage did not play (null or skeleton-incompatible) — instant-kill fallback"));
			FinishCommandedTakedown();
			return;
		}

		bTakedownMontagePlaying = true;
		FOnMontageEnded EndDelegate;
		EndDelegate.BindUObject(this, &ACompanionCharacter::OnTakedownMontageEnded);
		AnimInst->Montage_SetEndDelegate(EndDelegate, KnifeTakedownMontage);
		UE_LOG(LogCompanion, Log, TEXT("Takedown: knife montage playing, victim killed"));
	}
	else // Shoot
	{
		SetAimTarget(Victim);

		// FIX 5: Gate on ammo + LoS before committing the shot
		if (!CanFire())
		{
			UE_LOG(LogCompanion, Warning, TEXT("Takedown: shoot aborted — cannot fire (no ammo or weapon)"));
			FinishCommandedTakedown();
			return;
		}

		// LoS trace from companion eyes to victim
		if (const UWorld* World = GetWorld())
		{
			FVector EyesLoc;
			FRotator EyesRot;
			GetActorEyesViewPoint(EyesLoc, EyesRot);

			FHitResult Hit;
			FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(CompanionShootTakedown), false, this);
			TraceParams.AddIgnoredActor(Victim);
			if (IsValid(CurrentWeapon)) TraceParams.AddIgnoredActor(CurrentWeapon);

			const FVector TraceEnd = GetAimPointForTarget(Victim);
			const bool bBlocked = World->LineTraceSingleByChannel(
				Hit, EyesLoc, TraceEnd, ECC_Visibility, TraceParams);

			if (bBlocked)
			{
				UE_LOG(LogCompanion, Warning, TEXT("Takedown: shoot aborted — LoS blocked by %s"), *GetNameSafe(Hit.GetActor()));
				FinishCommandedTakedown();
				return;
			}

			World->GetTimerManager().SetTimer(ShootDelayTimerHandle, FTimerDelegate::CreateWeakLambda(this,
				[this, Enemy]()
				{
					if (!IsValid(Enemy))
					{
						FinishCommandedTakedown();
						return;
					}
					StartWeaponFire();
					Enemy->ExecuteTakedown(this);
					StopWeaponFire();
					UE_LOG(LogCompanion, Log, TEXT("Takedown: shoot executed"));
					FinishCommandedTakedown();
				}), ShootAimSettleDelay, false);
		}
		else
		{
			FinishCommandedTakedown();
		}
	}
}

void ACompanionCharacter::OnTakedownMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	bTakedownMontagePlaying = false;
	FinishCommandedTakedown();
}

void ACompanionCharacter::FinishCommandedTakedown()
{
	const bool bWasArmed = bTakedownArmed;

	// Unbind before broadcast to avoid re-entry
	AExtractionPlayer* Player = TakedownPlayerRef.Get();
	if (IsValid(Player))
		Player->OnPlayerTakedownCommitted.RemoveDynamic(this, &ACompanionCharacter::OnPlayerTakedownCommittedHandler);

	if (const UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(ShootDelayTimerHandle);

	TakedownVictim.Reset();
	TakedownPlayerRef.Reset();
	bTakedownArmed = false;
	bTakedownPlayerCommitted = false;
	bTakedownInPosition = false;
	bTakedownExecuting = false;
	bTakedownCrouchApproach = false;
	bTakedownMontagePlaying = false;

	SetAimTarget(nullptr);

	if (bWasArmed)
	{
		UE_LOG(LogCompanion, Log, TEXT("Takedown: finished, broadcasting completion"));
		OnCommandedTakedownFinished.Broadcast();
	}
}
