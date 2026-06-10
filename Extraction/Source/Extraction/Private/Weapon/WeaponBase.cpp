// Copyright Epic Games, Inc. All Rights Reserved.

#include "WeaponBase.h"
#include "AI/CompanionDiag.h"
#include "WeaponDataAsset.h"
#include "Character/ExtractionPlayerInterface.h"
#include "AIShooterInterface.h"
#include "ExtractionDamageType.h"
#include "HealthComponent.h"
#include "SuppressionComponent.h"
#include "EnemyCharacter.h"
#include "EnemyMoraleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/MeshComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GenericTeamAgentInterface.h"
#include "Perception/AISense_Hearing.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Engine/DamageEvents.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "Extraction.h"

namespace WeaponConstants
{
	static const FName MuzzleSocketName(TEXT("Muzzle"));
}

static TAutoConsoleVariable<int32> CVarShowBulletTracers(
	TEXT("weapon.ShowTracers"),
	1,
	TEXT("If non-zero, draw a tracer from muzzle to impact and an impact marker for every shot (player + AI)."),
	ECVF_Cheat);

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

	WeaponMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponMesh"));
	SetRootComponent(WeaponMesh);
	WeaponMesh->SetCollisionProfileName(FName("NoCollision"));
	// Kit spawns a BP_Item_Base FP visual weapon for the owning player — hide this mesh on
	// the owner to prevent a double-weapon. Third-person clients still see it.
	WeaponMesh->SetOwnerNoSee(true);

	// Weapon follows the hand via attachment to ik_hand_gun; its own skeletal pose only needs
	// to evaluate when on-screen (e.g. weapon-local reload/bolt anim). Avoids per-frame off-screen
	// pose refresh for every armed pawn's weapon.
	WeaponMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	// NoCollision weapon must never simulate, even if a designer-assigned skeletal asset ships a physics asset.
	WeaponMesh->SetSimulatePhysics(false);
}

void AWeaponBase::BeginPlay()
{
	Super::BeginPlay();

	if (IsValid(WeaponMesh))
	{
		CachedEffectiveMesh = WeaponMesh.Get();
	}
	else
	{
		CachedEffectiveMesh = FindComponentByClass<USkeletalMeshComponent>();
		if (HasAuthority() && IsValid(WeaponData))
			UE_LOG(LogTemp, Warning, TEXT("WeaponBase %s: WeaponMesh is null — BP misconfig, falling back to FindComponentByClass per shot"), *GetName());
	}

	if (IsValid(WeaponData) && !WeaponData->KitWeaponPoseAsset)
		UE_LOG(LogExtraction, Warning, TEXT("[KitWeapon] %s shipping without KitWeaponPoseAsset — kit procedural arms will receive nullptr"), *GetNameSafe(this));
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
	if (bWantsToFire) return;

	bWantsToFire = true;
	bDryFireLogged = false;

	// Cancel recoil recovery when firing resumes
	bIsRecoveringRecoil = false;

	const ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());

	// AI path: rebuild the friendly-fire ignore list once per burst so PerformHitscan doesn't
	// iterate all pawns every shot. Player-owned weapons skip this (PC check in PerformHitscan).
	if (IsValid(OwnerChar) && !IsValid(Cast<APlayerController>(OwnerChar->GetController())))
		RebuildFFIgnoreList();

	// All shooters: rebuild the suppression-target cache for near-miss reporting.
	if (IsValid(OwnerChar))
		RebuildSuppressionTargets();

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

	// Auto-reload if magazine empty and we have reserve (player UX — AI weapons set bAutoReloadOnEmpty=false to defer to BT).
	if (bAutoReloadOnEmpty && CurrentAmmo <= 0 && CanReload())
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

		if (bWantsToFire && CurrentAmmo <= 0)
		{
			if (!bDryFireLogged)
			{
				bDryFireLogged = true;
				UE_LOG(LogCompanionDiag, Warning, TEXT("%s: WEAPON-DRY ammo=%d reserve=%d"),
					*GetNameSafe(GetOwner()), CurrentAmmo, ReserveAmmo);
			}
			if (bAutoReloadOnEmpty && CanReload()) Reload();
		}
		return;
	}

	FireShot();
}

void AWeaponBase::RebuildFFIgnoreList()
{
	CachedFFIgnoreList.Reset();

	ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());
	if (!IsValid(OwnerChar)) return;

	const IGenericTeamAgentInterface* TeamAgent = Cast<IGenericTeamAgentInterface>(OwnerChar);
	if (!TeamAgent) return;

	const FGenericTeamId OwnerTeam = TeamAgent->GetGenericTeamId();

	UWorld* World = GetWorld();
	if (!World) return;

	CachedFFIgnoreList.Reserve(32);
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		APawn* OtherPawn = *It;
		if (!IsValid(OtherPawn) || OtherPawn == OwnerChar) continue;
		const IGenericTeamAgentInterface* OtherTeam = Cast<IGenericTeamAgentInterface>(OtherPawn);
		if (OtherTeam && OtherTeam->GetGenericTeamId() == OwnerTeam)
			CachedFFIgnoreList.Add(OtherPawn);
	}

	FFIgnoreListBuiltTime = World->GetTimeSeconds();
}

void AWeaponBase::RebuildSuppressionTargets()
{
	CachedSuppressionTargets.Reset();

	ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());
	if (!IsValid(OwnerChar)) return;

	const IGenericTeamAgentInterface* TeamAgent = Cast<IGenericTeamAgentInterface>(OwnerChar);
	if (!TeamAgent) return;

	const FGenericTeamId OwnerTeam = TeamAgent->GetGenericTeamId();

	UWorld* World = GetWorld();
	if (!World) return;

	CachedSuppressionTargets.Reserve(32);
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		APawn* OtherPawn = *It;
		if (!IsValid(OtherPawn) || OtherPawn == OwnerChar) continue;
		const IGenericTeamAgentInterface* OtherTeam = Cast<IGenericTeamAgentInterface>(OtherPawn);
		if (!OtherTeam) continue;
		const FGenericTeamId OtherTeamId = OtherTeam->GetGenericTeamId();
		if (OtherTeamId == OwnerTeam || OtherTeamId == FGenericTeamId::NoTeam) continue;
		USuppressionComponent* SuppComp = OtherPawn->FindComponentByClass<USuppressionComponent>();
		if (!SuppComp) continue;
		CachedSuppressionTargets.Add({ OtherPawn, SuppComp });
	}

	SuppressionTargetsBuiltTime = World->GetTimeSeconds();
}

void AWeaponBase::ReportNearMisses(const FVector& TraceStart, const FVector& TraceEnd, AActor* HitActor)
{
	if (NearMissRadius <= 0.f) return;

	const UWorld* World = GetWorld();
	if (!World) return;

	if ((World->GetTimeSeconds() - SuppressionTargetsBuiltTime) > 1.f)
		RebuildSuppressionTargets();

	const FVector Segment = TraceEnd - TraceStart;
	const float SegmentLenSq = Segment.SizeSquared();
	if (SegmentLenSq < 1.f) return;

	const float NearMissRadiusSq = NearMissRadius * NearMissRadius;

	for (const FSuppressionTarget& Target : CachedSuppressionTargets)
	{
		APawn* Pawn = Target.Pawn.Get();
		USuppressionComponent* Comp = Target.Component.Get();
		if (!IsValid(Pawn) || !IsValid(Comp)) continue;
		if (Pawn == HitActor) continue;

		const FVector ToPawn = Pawn->GetActorLocation() - TraceStart;
		const float T = FMath::Clamp(FVector::DotProduct(ToPawn, Segment) / SegmentLenSq, 0.f, 1.f);
		const FVector ClosestPoint = TraceStart + Segment * T;
		const float DistSq = FVector::DistSquared(ClosestPoint, Pawn->GetActorLocation());

		if (DistSq <= NearMissRadiusSq)
			Comp->RegisterNearMiss();
	}
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
	IExtractionPlayerInterface* OwnerIface = IsValid(GetOwner()) ? Cast<IExtractionPlayerInterface>(GetOwner()) : nullptr;
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (OwnerIface && IsValid(OwnerPawn) && OwnerPawn->IsLocallyControlled())
	{
		if (IsValid(Cast<APlayerController>(OwnerPawn->GetController())))
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
		// CachedEffectiveMesh resolved in BeginPlay: WeaponMesh if valid, else FindComponentByClass result (avoids per-shot scan).
		UMeshComponent* EffectiveMesh = CachedEffectiveMesh.Get();
		TraceStart = IsValid(EffectiveMesh)
			? EffectiveMesh->GetSocketLocation(WeaponConstants::MuzzleSocketName)
			: OwnerChar->GetActorLocation();

		FVector AimDirection = OwnerChar->GetActorForwardVector(); // fallback
		float InaccuracyDeg = 0.0f;
		AActor* AimTarget = nullptr;

		const IAIShooterInterface* Shooter = Cast<IAIShooterInterface>(OwnerChar);
		if (Shooter)
		{
			AimTarget = Shooter->GetAIAimTarget();
			InaccuracyDeg = Shooter->GetAIAimSpreadDegrees();
		}

		if (IsValid(AimTarget))
		{
			const FVector ToTarget = AimTarget->GetActorLocation() - TraceStart;
			if (!ToTarget.IsNearlyZero())
				AimDirection = ToTarget.GetSafeNormal();
		}
		else if (Shooter)
		{
			// Target is gone but an aim-location override may exist (e.g. heavy suppressing last-known).
			FVector AimOverride;
			if (Shooter->GetAIAimLocation(AimOverride))
			{
				const FVector ToOverride = AimOverride - TraceStart;
				if (!ToOverride.IsNearlyZero())
					AimDirection = ToOverride.GetSafeNormal();
			}
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

	// Friendly-fire prevention: AI-owned weapons ignore pawns on the same team (via IGenericTeamAgentInterface).
	// Player-fired shots (bAIOwned false) remain untouched.
	// The ignore list is built once in StartFiring and refreshed every ~1s during sustained fire so we
	// don't iterate all pawns per shot (~20 pawns * fire rate = significant per-frame cost).
	const bool bAIOwned = !IsValid(PC);
	if (bAIOwned)
	{
		// Refresh if stale (sustained auto fire running longer than 1s since last build).
		const UWorld* QueryWorld = GetWorld();
		if (QueryWorld && (QueryWorld->GetTimeSeconds() - FFIgnoreListBuiltTime) > 1.f)
			RebuildFFIgnoreList();

		QueryParams.AddIgnoredActors(CachedFFIgnoreList);
	}

	if (UWorld* World = GetWorld())
	{
		const bool bHit = World->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECC_Visibility, QueryParams);

		if (bAIOwned && CVarAIWeaponTraceDebug.GetValueOnGameThread() != 0)
		{
			const IAIShooterInterface* DebugShooter = Cast<IAIShooterInterface>(OwnerChar);
			const AActor* AILogAimTarget = DebugShooter ? DebugShooter->GetAIAimTarget() : nullptr;
			UE_LOG(LogExtraction, Verbose,
				TEXT("AI-FIRE owner=%s muzzle=%s end=%s aimTarget=%s bHit=%d hitActor=%s hitDist=%.0f"),
				*GetNameSafe(OwnerChar), *TraceStart.ToCompactString(), *TraceEnd.ToCompactString(),
				*GetNameSafe(AILogAimTarget), (int32)bHit,
				*GetNameSafe(bHit ? HitResult.GetActor() : nullptr),
				bHit ? HitResult.Distance : 0.f);
		}

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

				UHealthComponent* VictimHealth = HitActor->FindComponentByClass<UHealthComponent>();
				const bool bVictimWasAlive = VictimHealth && VictimHealth->IsAlive();

				HitActor->TakeDamage(
					WeaponData->BaseDamage,
					DamageEvent,
					OwnerChar->GetController(),
					this
				);

				if (bVictimWasAlive)
				{
					AEnemyCharacter* OwnerEnemy = Cast<AEnemyCharacter>(OwnerChar);
					if (IsValid(OwnerEnemy))
					{
						if (UEnemyMoraleComponent* Morale = OwnerEnemy->GetMoraleComponent())
						{
							Morale->NotifyDamagedTarget();
							if (VictimHealth->IsDead())
								Morale->NotifyTargetDowned();
						}
					}
				}
			}
		}

		// Near-miss suppression: report to hostile AI pawns close to the bullet segment.
		ReportNearMisses(TraceStart, bHit ? HitResult.ImpactPoint : TraceEnd, bHit ? HitResult.GetActor() : nullptr);

		Multicast_PlayFireFX(GetMuzzleLocation(), bHit ? HitResult.ImpactPoint : TraceEnd, bHit);

		// AI hearing: every shot is a noise event (suppressed weapons set low loudness/range on their data asset)
		if (WeaponData->NoiseRange > 0.f)
			UAISense_Hearing::ReportNoiseEvent(World, GetMuzzleLocation(), WeaponData->NoiseLoudness, OwnerChar, WeaponData->NoiseRange, TEXT("WeaponFire"));
	}
}

// ---- FX RPCs ----

void AWeaponBase::Multicast_PlayFireFX_Implementation(const FVector& MuzzleLocation, const FVector& EndPoint, bool bHit)
{
#if ENABLE_DRAW_DEBUG
	if (CVarShowBulletTracers.GetValueOnGameThread() == 0) return;
	UWorld* World = GetWorld();
	if (!IsValid(World)) return;

	static constexpr float TracerLifetime = 0.4f;
	static constexpr float TracerThickness = 1.5f;
	DrawDebugLine(World, MuzzleLocation, EndPoint, FColor::Yellow, false, TracerLifetime, 0, TracerThickness);
	if (bHit)
		DrawDebugPoint(World, EndPoint, 10.f, FColor::Red, false, TracerLifetime, 0);
#endif
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
	{
		CurrentState = EWeaponState::Reloading;

		if (IsValid(WeaponData) && WeaponData->ReloadNoiseRange > 0.f)
			UAISense_Hearing::ReportNoiseEvent(GetWorld(), GetActorLocation(), WeaponData->ReloadNoiseLoudness, GetOwner(), WeaponData->ReloadNoiseRange, TEXT("Reload"));
	}

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
	bDryFireLogged = false;

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

	IExtractionPlayerInterface* OwnerIface = IsValid(GetOwner()) ? Cast<IExtractionPlayerInterface>(GetOwner()) : nullptr;
	if (!OwnerIface) return;

	// Get current recoil point
	const int32 PatternIndex = FMath::Min(RecoilIndex, Pattern.Points.Num() - 1);
	FVector2D RecoilOffset = Pattern.Points[PatternIndex];

	RecoilOffset *= (bOwnerIsAiming ? Pattern.ADSMultiplier : 1.0f);

	// Apply to camera
	OwnerIface->DoAim(RecoilOffset.X, RecoilOffset.Y);

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

	IExtractionPlayerInterface* OwnerIface = IsValid(GetOwner()) ? Cast<IExtractionPlayerInterface>(GetOwner()) : nullptr;
	if (OwnerIface)
		OwnerIface->DoAim(-DeltaYaw, -DeltaPitch);

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

// ---- IKitWeaponInterface ----
// Bridge dispatch from kit's BP_FPCharacter into AWeaponBase. Methods we have map
// to existing logic; gameplay surfaces we don't yet implement are Verbose no-op stubs.

void AWeaponBase::KitReload_Implementation()
{
	if (!CanReload()) return;
	Reload();
}

void AWeaponBase::KitBeginFire_Implementation()
{
	// Kit owns fire cadence — do NOT arm AutoFireTimer here. Just clear the stop-fire
	// flag so the subsequent KitFire_HitScan dispatches succeed via CanFire().
	bWantsToFire = true;
	bDryFireLogged = false;
	bIsRecoveringRecoil = false;

	if (IsValid(Cast<ACharacter>(GetOwner())))
		RebuildSuppressionTargets();
}

void AWeaponBase::KitStopFire_Implementation()
{
	// Kit drives cadence — no AutoFireTimer was armed by KitBeginFire, so just clear
	// fire intent. Avoid StopFiring()'s timer-clear path which would also run recoil
	// recovery setup on every release.
	bWantsToFire = false;
	if (HasAuthority() && CurrentState == EWeaponState::Firing)
		CurrentState = EWeaponState::Idle;
}

void AWeaponBase::KitFire_HitScan_Implementation()
{
	// One shot per kit dispatch — kit calls this on its own fire-rate cadence.
	if (!CanFire()) return;

	if (HasAuthority())
		CurrentState = EWeaponState::Firing;

	FireShot();

	if (HasAuthority())
		CurrentState = EWeaponState::Idle;
}

void AWeaponBase::KitInspect_Implementation()
{
	UE_LOG(LogExtraction, Log, TEXT("[KitWeapon] %s KitInspect — no-op stub"), *GetNameSafe(this));
}

void AWeaponBase::KitMelee_Implementation()
{
	UE_LOG(LogExtraction, Log, TEXT("[KitWeapon] %s KitMelee — no-op stub"), *GetNameSafe(this));
}

void AWeaponBase::KitChangeFireMode_Implementation()
{
	UE_LOG(LogExtraction, Log, TEXT("[KitWeapon] %s KitChangeFireMode — no-op stub"), *GetNameSafe(this));
}

void AWeaponBase::KitBurstFire_Implementation()
{
	UE_LOG(LogExtraction, Log, TEXT("[KitWeapon] %s KitBurstFire — no-op stub"), *GetNameSafe(this));
}

void AWeaponBase::KitFinishFire_Implementation()
{
	bWantsToFire = false;
	if (HasAuthority() && CurrentState == EWeaponState::Firing)
		CurrentState = EWeaponState::Idle;
}

void AWeaponBase::KitTrigger_Implementation()
{
	UE_LOG(LogExtraction, Log, TEXT("[KitWeapon] %s KitTrigger — no-op stub"), *GetNameSafe(this));
}

void AWeaponBase::KitSpawnAttachments_Implementation()
{
	UE_LOG(LogExtraction, Log, TEXT("[KitWeapon] %s KitSpawnAttachments — no-op stub"), *GetNameSafe(this));
}

void AWeaponBase::KitUnequip_Implementation()
{
	if (const UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(ReloadTimerHandle);
	CancelRecoilRecovery();
	if (HasAuthority())
		CurrentState = EWeaponState::Idle;
	bWantsToFire = false;
	StopFiring();
}

UDataAsset* AWeaponBase::GetKitProceduralValues_Implementation() const
{
	if (!IsValid(WeaponData))
	{
		UE_LOG(LogExtraction, Warning, TEXT("[KitWeapon] %s has no WeaponData — returning nullptr"), *GetNameSafe(this));
		return nullptr;
	}
	if (!WeaponData->KitWeaponPoseAsset)
	{
		UE_LOG(LogExtraction, Warning, TEXT("[KitWeapon] %s WeaponData->KitWeaponPoseAsset is unassigned — kit IK will receive nullptr"), *GetNameSafe(this));
		return nullptr;
	}
	return WeaponData->KitWeaponPoseAsset;
}

FTransform AWeaponBase::GetKitIK_HandGunSocketOffset_Implementation() const
{
	return FTransform::Identity;
}

FTransform AWeaponBase::GetKitIK_HandRSocketOffset_Implementation() const
{
	return FTransform::Identity;
}

FTransform AWeaponBase::GetKitIK_HandLSocketOffset_Implementation() const
{
	return FTransform::Identity;
}

TSubclassOf<AActor> AWeaponBase::GetKitVisualWeaponClass_Implementation() const
{
	return IsValid(WeaponData) ? WeaponData->KitVisualWeaponClass : nullptr;
}

float AWeaponBase::GetKitAimDistanceFromCamera_Implementation() const
{
	return 30.f;
}

FVector AWeaponBase::GetKitMuzzleRingScale_Implementation() const
{
	return FVector(1.f);
}

bool AWeaponBase::GetKitReloading_Implementation() const
{
	return IsReloading();
}

bool AWeaponBase::GetKitIsFire_Implementation() const
{
	return IsFiring();
}

void AWeaponBase::KitSetAmmo_Implementation(int32 AmmoCount, int32 MaxAmmo)
{
	if (!HasAuthority()) return;

	// (0,0) sentinel: no loadout override — use the weapon's own data-driven defaults.
	if (MaxAmmo <= 0)
	{
		InitializeAmmo();
		return;
	}

	// Cancel an in-flight reload so OnReloadFinished can't stack ammo on top of what we set.
	if (CurrentState == EWeaponState::Reloading)
	{
		if (const UWorld* World = GetWorld())
			World->GetTimerManager().ClearTimer(ReloadTimerHandle);
		CurrentState = EWeaponState::Idle;
	}

	// ST_Item carries no reserve figure, so seed reserve from our data — otherwise the
	// loadout weapon spawns with 0 reserve and CanReload() is false forever.
	if (IsValid(WeaponData)) ReserveAmmo = WeaponData->DefaultReserveAmmo;

	// Clamp to our real magazine size (data-driven), not the caller's MaxAmmo hint.
	const int32 MagCeiling = IsValid(WeaponData) ? WeaponData->MagazineSize : MaxAmmo;
	CurrentAmmo = FMath::Clamp(AmmoCount, 0, MagCeiling);

	bDryFireLogged = false;
	OnAmmoChanged.Broadcast(CurrentAmmo, ReserveAmmo);
}
