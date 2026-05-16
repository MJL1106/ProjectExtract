// Copyright Epic Games, Inc. All Rights Reserved.

#include "WeaponBase.h"
#include "AI/CompanionDiag.h"
#include "WeaponDataAsset.h"
#include "ExtractionCharacter.h"
#include "CompanionCharacter.h"
#include "EnemyBase.h"
#include "ExtractionDamageType.h"
#include "HealthComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"
#include "Extraction.h"

namespace WeaponConstants
{
	static const FName MuzzleSocketName(TEXT("Muzzle"));
}

static TAutoConsoleVariable<int32> CVarAIWeaponTraceDebug(
	TEXT("companion.WeaponTraceDebug"),
	0,
	TEXT("If non-zero, log AI weapon hitscan trace details (start, end, hit actor, distance)."),
	ECVF_Cheat);

AWeaponBase::AWeaponBase()
	: CurrentState(EWeaponState::Idle)
	, CurrentAmmo(0)
	, ReserveAmmo(0)
	, RecoilIndex(0)
	, bWantsToFire(false)
	, bOwnerIsAiming(false)
	, AccumulatedRecoilPitch(0.f)
	, AccumulatedRecoilYaw(0.f)
	, bIsRecoveringRecoil(false)
	, RecoilRecoveryElapsed(0.f)
	, RecoilRecoveryPitchTotal(0.f)
	, RecoilRecoveryYawTotal(0.f)
	, RecoilRecoveryPitchApplied(0.f)
	, RecoilRecoveryYawApplied(0.f)
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = false;

	WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	SetRootComponent(WeaponMesh);
	WeaponMesh->SetCollisionProfileName(FName("NoCollision"));
}

void AWeaponBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(AWeaponBase, CurrentState, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AWeaponBase, CurrentAmmo, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AWeaponBase, ReserveAmmo, COND_OwnerOnly);
}

void AWeaponBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const UWorld* World = GetWorld())
	{
		FTimerManager& TM = World->GetTimerManager();
		TM.ClearTimer(AutoFireTimerHandle);
		TM.ClearTimer(ReloadTimerHandle);
		TM.ClearTimer(RecoilResetTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

FVector AWeaponBase::GetMuzzleLocation() const
{
	if (IsValid(WeaponMesh) && WeaponMesh->DoesSocketExist(WeaponConstants::MuzzleSocketName))
		return WeaponMesh->GetSocketLocation(WeaponConstants::MuzzleSocketName);
	return GetActorLocation();
}

// ---- Fire Control ----

bool AWeaponBase::CanFire() const
{
	return (CurrentState == EWeaponState::Idle || CurrentState == EWeaponState::Firing)
		&& CurrentAmmo > 0
		&& IsValid(WeaponData);
}

void AWeaponBase::StartFiring()
{
	bWantsToFire = true;

	// Cancel recoil recovery when firing resumes
	bIsRecoveringRecoil = false;

	if (!CanFire()) return;

	if (HasAuthority())
		CurrentState = EWeaponState::Firing;

	FireShot();

	if (!IsValid(WeaponData)) return;

	// Auto weapons: set looping timer
	if (WeaponData->bIsAutomatic)
	{
		const float FireInterval = 1.0f / WeaponData->FireRate;
		if (const UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				AutoFireTimerHandle,
				this,
				&AWeaponBase::OnAutoFireTimer,
				FireInterval,
				true
			);
		}
	}
	else if (HasAuthority())
	{
		// Semi-auto: return to idle after single shot
		CurrentState = EWeaponState::Idle;
	}
}

void AWeaponBase::StopFiring()
{
	bWantsToFire = false;

	if (const UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(AutoFireTimerHandle);

	if (HasAuthority() && CurrentState == EWeaponState::Firing)
		CurrentState = EWeaponState::Idle;

	// Begin recoil recovery if we accumulated any
	if ((AccumulatedRecoilPitch != 0.f || AccumulatedRecoilYaw != 0.f) && IsValid(WeaponData))
	{
		bIsRecoveringRecoil = true;
		RecoilRecoveryElapsed = 0.f;
		RecoilRecoveryPitchTotal = AccumulatedRecoilPitch;
		RecoilRecoveryYawTotal = AccumulatedRecoilYaw;
		RecoilRecoveryPitchApplied = 0.f;
		RecoilRecoveryYawApplied = 0.f;
	}

	// Auto-reload if magazine empty and we have reserve
	if (CurrentAmmo <= 0 && CanReload())
		Reload();
}

void AWeaponBase::OnAutoFireTimer()
{
	if (!bWantsToFire || !CanFire())
	{
		if (const UWorld* World = GetWorld())
			World->GetTimerManager().ClearTimer(AutoFireTimerHandle);

		if (HasAuthority())
			CurrentState = EWeaponState::Idle;

		if (bWantsToFire && CurrentAmmo <= 0 && CanReload())
			Reload();
		return;
	}

	FireShot();
}

void AWeaponBase::FireShot()
{
	if (!IsValid(WeaponData)) return;

	// Only server modifies replicated ammo state
	if (HasAuthority())
	{
		CurrentAmmo = FMath::Max(CurrentAmmo - 1, 0);
		OnAmmoChanged.Broadcast(CurrentAmmo, ReserveAmmo);
	}

	// Hitscan on server
	if (HasAuthority())
		PerformHitscan();

	// Recoil on owning client
	AExtractionCharacter* OwnerChar = Cast<AExtractionCharacter>(GetOwner());
	if (IsValid(OwnerChar) && OwnerChar->IsLocallyControlled())
	{
		if (IsValid(Cast<APlayerController>(OwnerChar->GetController())))
			ApplyRecoil();
	}

	OnWeaponFired.Broadcast();
}

void AWeaponBase::PerformHitscan()
{
	if (!IsValid(WeaponData)) return;

	ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());
	if (!IsValid(OwnerChar)) return;

	FVector TraceStart;
	FVector TraceEnd;

	// Player: trace from camera. AI/enemy: trace from muzzle along actor forward.
	APlayerController* PC = Cast<APlayerController>(OwnerChar->GetController());
	if (IsValid(PC))
	{
		FVector CameraLoc;
		FRotator CameraRot;
		PC->GetPlayerViewPoint(CameraLoc, CameraRot);
		TraceStart = CameraLoc;
		TraceEnd = CameraLoc + CameraRot.Vector() * WeaponData->MaxRange;
	}
	else
	{
		// AI path: trace from muzzle socket. Aim directly at the AI's current target location
		// (not ActorForward) so vertical offsets and yaw interp lag don't cause misses.
		// BP-serialization can leave WeaponMesh null after C++ component type changes — fall back to any static mesh on the actor.
		UStaticMeshComponent* EffectiveMesh = IsValid(WeaponMesh) ? WeaponMesh.Get() : FindComponentByClass<UStaticMeshComponent>();
		TraceStart = IsValid(EffectiveMesh)
			? EffectiveMesh->GetSocketLocation(WeaponConstants::MuzzleSocketName)
			: OwnerChar->GetActorLocation();

		FVector AimDirection = OwnerChar->GetActorForwardVector(); // fallback
		float InaccuracyDeg = 0.0f;
		AActor* AimTarget = nullptr;

		if (const ACompanionCharacter* Companion = Cast<ACompanionCharacter>(OwnerChar))
		{
			AimTarget = Companion->GetAimTarget();
			InaccuracyDeg = Companion->GetCurrentInaccuracy();
		}
		else if (const AEnemyBase* Enemy = Cast<AEnemyBase>(OwnerChar))
		{
			AimTarget = Enemy->GetCurrentTarget();
			InaccuracyDeg = Enemy->GetAimInaccuracyDegrees();
		}

		if (IsValid(AimTarget))
		{
			const FVector ToTarget = AimTarget->GetActorLocation() - TraceStart;
			if (!ToTarget.IsNearlyZero())
				AimDirection = ToTarget.GetSafeNormal();
		}

		// Apply spread to the fire direction (not the actor rotation — body stays upright).
		if (InaccuracyDeg > 0.0f)
		{
			FRotator SpreadRot = AimDirection.Rotation();
			SpreadRot.Yaw += FMath::RandRange(-InaccuracyDeg, InaccuracyDeg);
			SpreadRot.Pitch += FMath::RandRange(-InaccuracyDeg, InaccuracyDeg);
			AimDirection = SpreadRot.Vector();
		}

		TraceEnd = TraceStart + AimDirection * WeaponData->MaxRange;
	}

	FHitResult HitResult;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(OwnerChar);
	QueryParams.bReturnPhysicalMaterial = false;

	// Friendly-fire prevention: AI-owned weapons ignore the player + all companions.
	// (Player-fired shots still trace normally — only AI uses teammate filtering.)
	const bool bAIOwned = !IsValid(PC);
	if (bAIOwned)
	{
		if (UWorld* QueryWorld = GetWorld())
		{
			if (ACharacter* PlayerChar = UGameplayStatics::GetPlayerCharacter(QueryWorld, 0))
			{
				if (PlayerChar != OwnerChar)
					QueryParams.AddIgnoredActor(PlayerChar);
			}

			for (TActorIterator<ACompanionCharacter> It(QueryWorld); It; ++It)
			{
				ACompanionCharacter* Teammate = *It;
				if (IsValid(Teammate) && Teammate != OwnerChar)
					QueryParams.AddIgnoredActor(Teammate);
			}
		}
	}

	if (UWorld* World = GetWorld())
	{
		const bool bHit = World->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECC_Visibility, QueryParams);

		if (bAIOwned && CVarAIWeaponTraceDebug.GetValueOnGameThread() != 0)
		{
			const AActor* AILogAimTarget = IsValid(Cast<ACompanionCharacter>(OwnerChar))
				? Cast<ACompanionCharacter>(OwnerChar)->GetAimTarget()
				: (IsValid(Cast<AEnemyBase>(OwnerChar)) ? Cast<AEnemyBase>(OwnerChar)->GetCurrentTarget() : nullptr);
			UE_LOG(LogExtraction, Verbose,
				TEXT("AI-FIRE owner=%s muzzle=%s end=%s aimTarget=%s bHit=%d hitActor=%s hitDist=%.0f"),
				*GetNameSafe(OwnerChar), *TraceStart.ToCompactString(), *TraceEnd.ToCompactString(),
				*GetNameSafe(AILogAimTarget), (int32)bHit,
				*GetNameSafe(bHit ? HitResult.GetActor() : nullptr),
				bHit ? HitResult.Distance : 0.f);
		}

#if ENABLE_DRAW_DEBUG
		if (bHit)
			DrawDebugLine(World, TraceStart, HitResult.ImpactPoint, FColor::Red, false, 0.5f, 0, 1.0f);
		else
			DrawDebugLine(World, TraceStart, TraceEnd, FColor::Yellow, false, 0.5f, 0, 1.0f);
#endif

		if (bHit)
		{
			AActor* HitActor = HitResult.GetActor();
			if (IsValid(HitActor))
			{
				FPointDamageEvent DamageEvent;
				DamageEvent.Damage = WeaponData->BaseDamage;
				DamageEvent.HitInfo = HitResult;
				DamageEvent.ShotDirection = (TraceEnd - TraceStart).GetSafeNormal();

				if (WeaponData->DamageTypeClass)
					DamageEvent.DamageTypeClass = WeaponData->DamageTypeClass;
				else
					UE_LOG(LogExtraction, Warning, TEXT("'%s': WeaponData has no DamageTypeClass — hitbox multipliers won't apply."), *GetNameSafe(this));

				UE_LOG(LogExtraction, Log, TEXT("%s hit %s for %.1f damage"),
					*GetNameSafe(OwnerChar), *GetNameSafe(HitActor), WeaponData->BaseDamage);

				HitActor->TakeDamage(
					WeaponData->BaseDamage,
					DamageEvent,
					OwnerChar->GetController(),
					this
				);
			}
		}
	}
}

// ---- Reload ----

bool AWeaponBase::CanReload() const
{
	return CurrentState == EWeaponState::Idle
		&& IsValid(WeaponData)
		&& CurrentAmmo < WeaponData->MagazineSize
		&& ReserveAmmo > 0;
}

void AWeaponBase::Reload()
{
	if (!CanReload()) return;

	if (const UWorld* World = GetWorld())
	{
		ReloadStartTimeSeconds = World->GetTimeSeconds();
		if (UE_LOG_ACTIVE(LogCompanionDiag, Log))
		{
			const float OwnerVel = IsValid(GetOwner()) ? GetOwner()->GetVelocity().Size() : 0.f;
			UE_LOG(LogCompanionDiag, Log, TEXT("%s: RELOAD-START ammo=%d/%d reserve=%d vel=%.1f reloadTime=%.2f"),
				IsValid(GetOwner()) ? *GetOwner()->GetName() : TEXT("Unknown"),
				CurrentAmmo,
				IsValid(WeaponData) ? WeaponData->MagazineSize : -1,
				ReserveAmmo,
				OwnerVel,
				IsValid(WeaponData) ? WeaponData->ReloadTime : -1.f);
		}
	}

	if (HasAuthority())
		CurrentState = EWeaponState::Reloading;

	// Stop firing
	bWantsToFire = false;
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoFireTimerHandle);

		World->GetTimerManager().SetTimer(
			ReloadTimerHandle,
			this,
			&AWeaponBase::OnReloadFinished,
			WeaponData->ReloadTime,
			false
		);
	}
}

void AWeaponBase::OnReloadFinished()
{
	if (!IsValid(WeaponData)) return;

	if (HasAuthority())
	{
		const int32 AmmoNeeded = WeaponData->MagazineSize - CurrentAmmo;
		const int32 AmmoToLoad = FMath::Min(AmmoNeeded, ReserveAmmo);

		CurrentAmmo += AmmoToLoad;
		ReserveAmmo -= AmmoToLoad;
		CurrentState = EWeaponState::Idle;

		OnAmmoChanged.Broadcast(CurrentAmmo, ReserveAmmo);
	}

	if (HasAuthority() && UE_LOG_ACTIVE(LogCompanionDiag, Log))
	{
		const float ElapsedReal = GetWorld() ? (GetWorld()->GetTimeSeconds() - ReloadStartTimeSeconds) : -1.f;
		const float OwnerVel = IsValid(GetOwner()) ? GetOwner()->GetVelocity().Size() : 0.f;
		UE_LOG(LogCompanionDiag, Log, TEXT("%s: RELOAD-FINISH ammo=%d/%d reserve=%d elapsedReal=%.2f vel=%.1f bWantsToFire=%d"),
			IsValid(GetOwner()) ? *GetOwner()->GetName() : TEXT("Unknown"),
			CurrentAmmo,
			IsValid(WeaponData) ? WeaponData->MagazineSize : -1,
			ReserveAmmo,
			ElapsedReal,
			OwnerVel,
			(int32)bWantsToFire);
	}

	OnReloadComplete.Broadcast();

	// Resume firing if input is still held
	if (bWantsToFire)
		StartFiring();
}

// ---- Recoil ----

void AWeaponBase::ApplyRecoil()
{
	if (!IsValid(WeaponData)) return;
	const FRecoilPattern& Pattern = WeaponData->RecoilPattern;
	if (Pattern.Points.Num() == 0) return;

	AExtractionCharacter* OwnerChar = Cast<AExtractionCharacter>(GetOwner());
	if (!IsValid(OwnerChar)) return;

	// Get current recoil point
	const int32 PatternIndex = FMath::Min(RecoilIndex, Pattern.Points.Num() - 1);
	FVector2D RecoilOffset = Pattern.Points[PatternIndex];

	RecoilOffset *= (bOwnerIsAiming ? Pattern.ADSMultiplier : 1.0f);

	// Apply to camera
	OwnerChar->DoAim(RecoilOffset.X, RecoilOffset.Y);

	// Track accumulated recoil for recovery
	AccumulatedRecoilPitch += RecoilOffset.Y;
	AccumulatedRecoilYaw += RecoilOffset.X;

	// Advance pattern index
	RecoilIndex = FMath::Min(RecoilIndex + 1, Pattern.Points.Num() - 1);

	// Reset timer — if no shots fired within ResetDelay, pattern resets
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			RecoilResetTimerHandle,
			this,
			&AWeaponBase::OnRecoilResetTimer,
			Pattern.ResetDelay,
			false
		);
	}
}

void AWeaponBase::OnRecoilResetTimer()
{
	RecoilIndex = 0;
	AccumulatedRecoilPitch = 0.f;
	AccumulatedRecoilYaw = 0.f;
}

void AWeaponBase::UpdateRecoilRecovery(float DeltaTime)
{
	if (!bIsRecoveringRecoil || !IsValid(WeaponData)) return;

	const float RecoveryTime = WeaponData->RecoilPattern.RecoveryTime;
	if (RecoveryTime <= 0.f)
	{
		bIsRecoveringRecoil = false;
		return;
	}

	RecoilRecoveryElapsed += DeltaTime;
	const float Alpha = FMath::Clamp(RecoilRecoveryElapsed / RecoveryTime, 0.f, 1.f);

	// Calculate how much recovery we should have applied by now
	const float TargetPitch = RecoilRecoveryPitchTotal * Alpha;
	const float TargetYaw = RecoilRecoveryYawTotal * Alpha;

	// Apply only the delta since last frame
	const float DeltaPitch = TargetPitch - RecoilRecoveryPitchApplied;
	const float DeltaYaw = TargetYaw - RecoilRecoveryYawApplied;

	AExtractionCharacter* OwnerChar = Cast<AExtractionCharacter>(GetOwner());
	if (IsValid(OwnerChar))
		OwnerChar->DoAim(-DeltaYaw, -DeltaPitch);

	RecoilRecoveryPitchApplied = TargetPitch;
	RecoilRecoveryYawApplied = TargetYaw;

	if (Alpha >= 1.0f)
	{
		bIsRecoveringRecoil = false;
		AccumulatedRecoilPitch = 0.f;
		AccumulatedRecoilYaw = 0.f;
	}
}

void AWeaponBase::CancelRecoilRecovery()
{
	if (!bIsRecoveringRecoil) return;

	bIsRecoveringRecoil = false;
	AccumulatedRecoilPitch = 0.f;
	AccumulatedRecoilYaw = 0.f;
}

// ---- Initialization ----

void AWeaponBase::InitializeAmmo()
{
	if (!IsValid(WeaponData)) return;
	if (!HasAuthority()) return;

	CurrentAmmo = WeaponData->MagazineSize;
	ReserveAmmo = WeaponData->DefaultReserveAmmo;
	OnAmmoChanged.Broadcast(CurrentAmmo, ReserveAmmo);
}

// ---- RepNotify ----

void AWeaponBase::OnRep_CurrentState()
{
	// Proxies can trigger animations based on state change here
}

void AWeaponBase::OnRep_CurrentAmmo()
{
	OnAmmoChanged.Broadcast(CurrentAmmo, ReserveAmmo);
}
