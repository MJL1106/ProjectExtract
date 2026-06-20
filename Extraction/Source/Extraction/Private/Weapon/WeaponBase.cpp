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
#include "EnemyAIController.h"
#include "EnemyAwarenessComponent.h"
#include "EnemyMoraleComponent.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/MeshComponent.h"
#include "Components/PrimitiveComponent.h"
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

static TAutoConsoleVariable<int32> CVarPlayerTraceDebug(
	TEXT("weapon.PlayerTraceDebug"),
	0,
	TEXT("If non-zero, log player weapon hitscan trace details (start, end, hit actor, component, distance, health check)."),
	ECVF_Cheat);

static TAutoConsoleVariable<int32> CVarFireAlignDebug(
	TEXT("weapon.FireAlignDebug"),
	0,
	TEXT("If non-zero, log enemy weapon fire-align: SetupFireAlign captures (rest/fire relative, sockets, fire offset) and SetFireAlignAlpha (alpha + resulting WeaponMesh relative/world transform). Diagnoses misalignment from the WeaponSocket_Fire blend. Default 0 = no logging, no behavior change."),
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

	// Bug 6a: safe ammo seed — if the weapon spawned server-side with a valid DA but no ammo
	// (kit path not yet called KitSetAmmo), seed the magazine so CanFire() succeeds.
	if (HasAuthority() && IsValid(WeaponData) && CurrentAmmo == 0)
		InitializeAmmo();

	// Spawn the pre-assembled visual weapon actor (e.g. Infima _Default_Example BP) if configured.
	// The visual actor replaces the bare skeletal WeaponMesh for third-person display while
	// WeaponMesh stays active as root + muzzle-socket source.
	// Guards: need a valid class, a valid WeaponMesh to attach to, no existing visual (re-entrancy),
	// and a valid world to spawn into.
	if (ThirdPersonVisualActorClass && IsValid(WeaponMesh) && !IsValid(SpawnedVisualActor) && IsValid(GetWorld()))
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.Instigator = GetInstigator();
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		SpawnedVisualActor = GetWorld()->SpawnActor<AActor>(ThirdPersonVisualActorClass, GetActorTransform(), SpawnParams);
		if (IsValid(SpawnedVisualActor))
		{
			SpawnedVisualActor->AttachToComponent(WeaponMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
			SpawnedVisualActor->SetActorTickEnabled(false);

			// Disable collision on every primitive in the visual actor so it can't shove/block its owner.
			TInlineComponentArray<UPrimitiveComponent*> Primitives;
			SpawnedVisualActor->GetComponents(Primitives);
			for (UPrimitiveComponent* Prim : Primitives)
			{
				if (IsValid(Prim))
					Prim->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}

			// Only hide the bare frame once the visual is confirmed attached.
			// On attach failure the enemy keeps the skeletal mesh as a visible fallback.
			if (SpawnedVisualActor->GetAttachParentActor() == this)
			{
				WeaponMesh->SetVisibility(false, false);
			}
			else
			{
				UE_LOG(LogExtraction, Warning, TEXT("%s: ThirdPersonVisualActor spawned but attach failed — keeping WeaponMesh visible"),
					*GetName());
			}

			// Resolve magazine component for reload swap (enemy weapons only).
			// When MagazineComponentName is NAME_None (player kit weapons) skip entirely.
			if (MagazineComponentName != NAME_None)
			{
				USceneComponent* FoundMag = nullptr;
				TInlineComponentArray<USceneComponent*> SceneComps;
				SpawnedVisualActor->GetComponents(SceneComps);
				for (USceneComponent* Comp : SceneComps)
				{
					if (!IsValid(Comp)) continue;
					if (Comp->GetFName() == MagazineComponentName || Comp->ComponentHasTag(MagazineComponentName))
					{
						FoundMag = Comp;
						break;
					}
				}

				if (IsValid(FoundMag))
				{
					CachedMagazineComp = FoundMag;
					MagazineHomeParent = FoundMag->GetAttachParent();
					MagazineHomeSocket = FoundMag->GetAttachSocketName();
					MagazineHomeRelativeTransform = FoundMag->GetRelativeTransform();
				}
				else
				{
					UE_LOG(LogExtraction, Warning,
						TEXT("%s: MagazineComponentName '%s' not found in visual actor '%s' — magazine swap disabled"),
						*GetName(), *MagazineComponentName.ToString(), *GetNameSafe(SpawnedVisualActor));
				}
			}

			// Resolve the gun body's skeletal mesh component for weapon reload montage playback.
			if (WeaponVisualMeshName != NAME_None)
			{
				TInlineComponentArray<USkeletalMeshComponent*> SkelComps;
				SpawnedVisualActor->GetComponents(SkelComps);
				USkeletalMeshComponent* FoundVisualMesh = nullptr;
				for (USkeletalMeshComponent* Comp : SkelComps)
				{
					if (!IsValid(Comp)) continue;
					if (Comp->GetFName() == WeaponVisualMeshName || Comp->ComponentHasTag(WeaponVisualMeshName))
					{
						FoundVisualMesh = Comp;
						break;
					}
				}

				if (IsValid(FoundVisualMesh))
					CachedWeaponVisualMesh = FoundVisualMesh;
				else
					UE_LOG(LogExtraction, Warning,
						TEXT("%s: WeaponVisualMeshName '%s' not found in visual actor '%s' — weapon reload montage disabled"),
						*GetName(), *WeaponVisualMeshName.ToString(), *GetNameSafe(SpawnedVisualActor));
			}
		}
		else
		{
			UE_LOG(LogExtraction, Warning, TEXT("%s: failed to spawn ThirdPersonVisualActor of class %s"),
				*GetName(), *GetNameSafe(ThirdPersonVisualActorClass));
		}
	}
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
	SetFireAlignAlpha(0.f);
	SetMeleeAlignAlpha(0.f);

	if (IsValid(SpawnedVisualActor))
	{
		SpawnedVisualActor->Destroy();
		SpawnedVisualActor = nullptr;
	}

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

USkeletalMeshComponent* AWeaponBase::GetThirdPersonGripMesh() const
{
	// Prefer the visual actor's skeletal mesh — it is the visible geometry with authored sockets.
	// WeaponMesh is hidden whenever a visual actor is present (see BeginPlay).
	if (IsValid(SpawnedVisualActor))
	{
		if (USkeletalMeshComponent* VisualMesh = SpawnedVisualActor->FindComponentByClass<USkeletalMeshComponent>())
			return VisualMesh;
	}
	return IsValid(WeaponMesh) ? WeaponMesh.Get() : nullptr;
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
		{
			Comp->RegisterNearMiss();

			// Part A: notify enemy awareness so near-misses escalate alertness.
			if (const AEnemyCharacter* EnemyChar = Cast<AEnemyCharacter>(Pawn))
			{
				if (const AEnemyAIController* AIC = Cast<AEnemyAIController>(EnemyChar->GetController()))
				{
					if (UEnemyAwarenessComponent* Awareness = AIC->GetAwarenessComponent())
					{
						APawn* ShooterPawn = Cast<APawn>(GetOwner());
						if (IsValid(ShooterPawn))
							Awareness->NotifyShotAt(ShooterPawn, TraceStart);
					}
				}
			}
		}
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
	PlayVisualWeaponFire();
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
		// AI path: trace from eye height (GetPawnViewLocation) so damage agrees with LOS checks
		// in BTService_EnemyCombat and EnemyAwarenessComponent. Muzzle socket is kept for FX/noise only.
		TraceStart = OwnerChar->GetPawnViewLocation();

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
			// GetAimPointForTarget: companion returns head/eye; enemy default returns actor centre.
			const FVector AimPoint = Shooter->GetAimPointForTarget(AimTarget);
			const FVector ToTarget = AimPoint - TraceStart;
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

		// Bug 5b: uniform cone spread — single random angle + magnitude within InaccuracyDeg.
		// The old per-axis approach doubled the effective cone (independent Yaw+Pitch).
		if (InaccuracyDeg > 0.0f)
		{
			const float RandAngle = FMath::RandRange(0.f, 360.f);
			const float RandMagnitude = FMath::RandRange(0.f, InaccuracyDeg);
			FRotator SpreadRot = AimDirection.Rotation();
			SpreadRot.Yaw += FMath::Cos(FMath::DegreesToRadians(RandAngle)) * RandMagnitude;
			SpreadRot.Pitch += FMath::Sin(FMath::DegreesToRadians(RandAngle)) * RandMagnitude;
			AimDirection = SpreadRot.Vector();
		}

		TraceEnd = TraceStart + AimDirection * WeaponData->MaxRange;
	}

	FHitResult HitResult;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(OwnerChar);
	QueryParams.bReturnPhysicalMaterial = false;

	// Friendly-fire prevention for ALL shooters (player + AI): ignore pawns on the SAME team as the
	// shooter (via IGenericTeamAgentInterface), so the bullet passes through allies to whatever is
	// behind. Player (team 0) won't hit the companion; an enemy (team 1) won't hit other enemies.
	// The ignore list is team-based and rebuilt at most once per ~1s (iterating all pawns per shot is costly).
	// NOTE: this is intentionally weapon-trace-only — grenades stay team-blind by design.
	const bool bAIOwned = !IsValid(PC);
	{
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
				TEXT("AI-FIRE owner=%s eye=%s end=%s aimTarget=%s bHit=%d hitActor=%s hitDist=%.0f"),
				*GetNameSafe(OwnerChar), *TraceStart.ToCompactString(), *TraceEnd.ToCompactString(),
				*GetNameSafe(AILogAimTarget), (int32)bHit,
				*GetNameSafe(bHit ? HitResult.GetActor() : nullptr),
				bHit ? HitResult.Distance : 0.f);
		}

		// Bug 6b: player trace diagnostics
		if (!bAIOwned && CVarPlayerTraceDebug.GetValueOnGameThread() != 0)
		{
			AActor* DbgHitActor = bHit ? HitResult.GetActor() : nullptr;
			UE_LOG(LogExtraction, Log,
				TEXT("PLAYER-FIRE start=%s end=%s bHit=%d hitActor=%s hitComp=%s dist=%.0f"),
				*TraceStart.ToCompactString(), *TraceEnd.ToCompactString(),
				(int32)bHit, *GetNameSafe(DbgHitActor),
				bHit ? *GetNameSafe(HitResult.GetComponent()) : TEXT("none"),
				bHit ? HitResult.Distance : 0.f);
			if (bHit && IsValid(DbgHitActor) && !DbgHitActor->FindComponentByClass<UHealthComponent>())
				UE_LOG(LogExtraction, Warning, TEXT("PLAYER-FIRE hit %s but it has NO UHealthComponent — damage will be ignored"), *GetNameSafe(DbgHitActor));
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

// ---- Magazine swap ----

void AWeaponBase::DetachMagazineToHand(USkeletalMeshComponent* HandMesh, FName HandSocket)
{
	if (!CachedMagazineComp.IsValid()) return;
	if (!IsValid(HandMesh)) return;
	if (HandSocket.IsNone())
	{
		UE_LOG(LogExtraction, Warning, TEXT("DetachMagazineToHand: %s — HandSocket is None, notify is misconfigured (mag not detached)"), *GetNameSafe(this));
		return;
	}

	CachedMagazineComp->AttachToComponent(HandMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, HandSocket);
	bMagazineDetached = true;
}

void AWeaponBase::ReattachMagazine()
{
	if (!bMagazineDetached) return;

	if (CachedMagazineComp.IsValid())
	{
		if (MagazineHomeParent.IsValid())
		{
			CachedMagazineComp->AttachToComponent(MagazineHomeParent.Get(),
				FAttachmentTransformRules::KeepRelativeTransform, MagazineHomeSocket);
			CachedMagazineComp->SetRelativeTransform(MagazineHomeRelativeTransform);
		}
		else
		{
			// Home parent went stale — detach from the hand so the mag doesn't stay welded to it.
			UE_LOG(LogExtraction, Warning, TEXT("ReattachMagazine: %s — home parent invalid, detaching mag in world space"), *GetNameSafe(this));
			CachedMagazineComp->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		}
	}

	bMagazineDetached = false;
}

// ---- Weapon fire alignment ----

void AWeaponBase::SetupFireAlign(USkeletalMeshComponent* EnemyMesh, FName FireSocket)
{
	bFireAlignReady = false;

	if (!IsValid(EnemyMesh) || !IsValid(WeaponMesh)) return;
	if (FireSocket.IsNone())
	{
		UE_LOG(LogExtraction, Warning, TEXT("SetupFireAlign: %s — FireSocket is None"), *GetNameSafe(this));
		return;
	}

	const FName RestSocket = WeaponMesh->GetAttachSocketName();
	if (!EnemyMesh->DoesSocketExist(RestSocket))
	{
		UE_LOG(LogExtraction, Warning, TEXT("SetupFireAlign: %s — rest socket '%s' not found on enemy mesh (fire-align disabled)"),
			*GetNameSafe(this), *RestSocket.ToString());
		return;
	}
	if (!EnemyMesh->DoesSocketExist(FireSocket))
	{
		UE_LOG(LogExtraction, Warning, TEXT("SetupFireAlign: %s — fire socket '%s' not found on enemy mesh (fire-align disabled)"),
			*GetNameSafe(this), *FireSocket.ToString());
		return;
	}

	// Capture the rest-pose relative transform as the Alpha=0 target.
	FireAlignRestRelative = WeaponMesh->GetRelativeTransform();

	// Compute the Alpha=1 target in one step — pre-compose so SetFireAlignAlpha is a pure blend.
	//
	// Derivation (UE: A*B means apply A then B):
	//   We want: R1 * P_rest = R0 * P_fire  (weapon world transform is the same as sitting at WeaponSocket_Fire)
	//   ⇒  R1 = R0 * P_fire * P_rest.Inverse() = FireAlignRestRelative * (T_fire * T_rest.Inverse())
	//
	// T_fire * T_rest.Inverse() is bone-pose-invariant: both sockets share the same bone B,
	// so T_x = S_x * B and T_fire * T_rest.Inverse() = S_fire * S_rest.Inverse() (B cancels).
	// Caching this at setup-time is therefore valid regardless of hand pose at runtime.
	const FTransform TRest = EnemyMesh->GetSocketTransform(RestSocket, RTS_Component);
	const FTransform TFire = EnemyMesh->GetSocketTransform(FireSocket, RTS_Component);
	FireAlignFireRelative = FireAlignRestRelative * (TFire * TRest.Inverse());

	bFireAlignReady = true;

	if (CVarFireAlignDebug.GetValueOnGameThread() != 0)
	{
		const FTransform FireOffset = FireAlignFireRelative.GetRelativeTransform(FireAlignRestRelative);
		const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f;
		UE_LOG(LogExtraction, Warning,
			TEXT("[FIREALIGN] %s SetupFireAlign t=%.2f restSock='%s' fireSock='%s' | restLoc=%s restRot=%s | fireLoc=%s fireRot=%s | fireOffsetLoc=%s fireOffsetRotDeg=%s"),
			*GetNameSafe(this), Now,
			*RestSocket.ToString(), *FireSocket.ToString(),
			*FireAlignRestRelative.GetLocation().ToString(), *FireAlignRestRelative.Rotator().ToString(),
			*FireAlignFireRelative.GetLocation().ToString(), *FireAlignFireRelative.Rotator().ToString(),
			*FireOffset.GetLocation().ToString(), *FireOffset.Rotator().ToString());
	}
}

void AWeaponBase::SetFireAlignAlpha(float Alpha)
{
	if (!bFireAlignReady) return;
	if (!IsValid(WeaponMesh)) return;

	Alpha = FMath::Clamp(Alpha, 0.f, 1.f);

	// Blend directly between the two pre-computed targets — no per-frame recompose.
	FTransform Blended;
	Blended.SetLocation(FMath::Lerp(FireAlignRestRelative.GetLocation(), FireAlignFireRelative.GetLocation(), Alpha));
	Blended.SetRotation(FQuat::Slerp(FireAlignRestRelative.GetRotation(), FireAlignFireRelative.GetRotation(), Alpha));
	Blended.SetScale3D(FMath::Lerp(FireAlignRestRelative.GetScale3D(), FireAlignFireRelative.GetScale3D(), Alpha));

	WeaponMesh->SetRelativeTransform(Blended);

	if (CVarFireAlignDebug.GetValueOnGameThread() != 0 &&
		(GFrameCounter % 10 == 0 || Alpha <= KINDA_SMALL_NUMBER || Alpha >= 1.f - KINDA_SMALL_NUMBER))
	{
		UE_LOG(LogExtraction, Warning,
			TEXT("[FIREALIGN] %s Alpha=%.3f relLoc=%s relRotDeg=%s worldLoc=%s"),
			*GetNameSafe(this), Alpha,
			*Blended.GetLocation().ToString(), *Blended.Rotator().ToString(),
			*WeaponMesh->GetComponentLocation().ToString());
	}
}

// ---- Weapon melee alignment ----

void AWeaponBase::SetupMeleeAlign(USkeletalMeshComponent* EnemyMesh, FName MeleeSocket)
{
	bMeleeAlignReady = false;

	if (!IsValid(EnemyMesh) || !IsValid(WeaponMesh)) return;
	if (MeleeSocket.IsNone())
	{
		UE_LOG(LogExtraction, Warning, TEXT("SetupMeleeAlign: %s — MeleeSocket is None"), *GetNameSafe(this));
		return;
	}

	const FName RestSocket = WeaponMesh->GetAttachSocketName();
	if (!EnemyMesh->DoesSocketExist(RestSocket))
	{
		UE_LOG(LogExtraction, Warning, TEXT("SetupMeleeAlign: %s — rest socket '%s' not found on enemy mesh (melee-align disabled)"),
			*GetNameSafe(this), *RestSocket.ToString());
		return;
	}
	if (!EnemyMesh->DoesSocketExist(MeleeSocket))
	{
		// Verbose, not Warning: the socket is authored manually per-weapon and is legitimately absent
		// until then — melee-align stays disabled and writes nothing, so this must not spam every equip.
		UE_LOG(LogExtraction, Verbose, TEXT("SetupMeleeAlign: %s — melee socket '%s' not found on enemy mesh (melee-align disabled)"),
			*GetNameSafe(this), *MeleeSocket.ToString());
		return;
	}

	MeleeAlignRestRelative = WeaponMesh->GetRelativeTransform();

	const FTransform TRest = EnemyMesh->GetSocketTransform(RestSocket, RTS_Component);
	const FTransform TMelee = EnemyMesh->GetSocketTransform(MeleeSocket, RTS_Component);
	MeleeAlignMeleeRelative = MeleeAlignRestRelative * (TMelee * TRest.Inverse());

	bMeleeAlignReady = true;
}

void AWeaponBase::SetMeleeAlignAlpha(float Alpha)
{
	if (!bMeleeAlignReady) return;
	if (!IsValid(WeaponMesh)) return;

	Alpha = FMath::Clamp(Alpha, 0.f, 1.f);

	FTransform Blended;
	Blended.SetLocation(FMath::Lerp(MeleeAlignRestRelative.GetLocation(), MeleeAlignMeleeRelative.GetLocation(), Alpha));
	Blended.SetRotation(FQuat::Slerp(MeleeAlignRestRelative.GetRotation(), MeleeAlignMeleeRelative.GetRotation(), Alpha));
	Blended.SetScale3D(FMath::Lerp(MeleeAlignRestRelative.GetScale3D(), MeleeAlignMeleeRelative.GetScale3D(), Alpha));

	WeaponMesh->SetRelativeTransform(Blended);
}

void AWeaponBase::PlayVisualWeaponReload(float PlayRate)
{
	if (!CachedWeaponVisualMesh.IsValid()) return;
	if (!IsValid(WeaponData) || !IsValid(WeaponData->EnemyAnimSet.WeaponReload)) return;

	UAnimInstance* AnimInst = CachedWeaponVisualMesh->GetAnimInstance();
	if (!IsValid(AnimInst)) return;
	if (AnimInst->Montage_IsPlaying(WeaponData->EnemyAnimSet.WeaponReload)) return;

	AnimInst->Montage_Play(WeaponData->EnemyAnimSet.WeaponReload, PlayRate);
}

void AWeaponBase::StopVisualWeaponReload(float BlendOutTime)
{
	if (!CachedWeaponVisualMesh.IsValid()) return;
	if (!IsValid(WeaponData) || !IsValid(WeaponData->EnemyAnimSet.WeaponReload)) return;

	UAnimInstance* AnimInst = CachedWeaponVisualMesh->GetAnimInstance();
	if (!IsValid(AnimInst)) return;

	AnimInst->Montage_Stop(BlendOutTime, WeaponData->EnemyAnimSet.WeaponReload);
}

void AWeaponBase::PlayVisualWeaponFire(float PlayRate)
{
	if (!CachedWeaponVisualMesh.IsValid()) return;
	if (!IsValid(WeaponData) || !IsValid(WeaponData->EnemyAnimSet.WeaponFire)) return;

	UAnimInstance* AnimInst = CachedWeaponVisualMesh->GetAnimInstance();
	if (!IsValid(AnimInst)) return;

	// Intentionally NO Montage_IsPlaying guard — each shot must restart the bolt cycle from the top.
	// Fit the bolt cycle to the fire cadence: cycle-time x rounds/sec. This makes each per-shot
	// restart land on the clip's end pose (which equals idle == frame 0), eliminating the mid-travel
	// snap from restarting a 0.333s clip every ~0.1s. Clamp [1,6]: floor keeps natural speed for
	// slow/semi fire, ceiling guards extreme fire rates. (PlayLen x FireRate is also the physically
	// correct cyclic rate for full-auto.)
	float Rate = PlayRate;
	const float PlayLen = WeaponData->EnemyAnimSet.WeaponFire->GetPlayLength();
	if (PlayLen > 0.f && WeaponData->FireRate > 0.f)
		Rate = FMath::Clamp(PlayLen * WeaponData->FireRate, 1.f, 6.f);

	AnimInst->Montage_Play(WeaponData->EnemyAnimSet.WeaponFire, Rate);
}

void AWeaponBase::StopVisualWeaponFire(float BlendOutTime)
{
	if (!CachedWeaponVisualMesh.IsValid()) return;
	if (!IsValid(WeaponData) || !IsValid(WeaponData->EnemyAnimSet.WeaponFire)) return;

	UAnimInstance* AnimInst = CachedWeaponVisualMesh->GetAnimInstance();
	if (!IsValid(AnimInst)) return;

	AnimInst->Montage_Stop(BlendOutTime, WeaponData->EnemyAnimSet.WeaponFire);
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

		// Safety net: snap the magazine home if the notify-end was missed (montage interrupted, etc.).
		ReattachMagazine();
		StopVisualWeaponReload();
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
	// Safety net for simulated proxies: if the state transitioned away from Reloading and the
	// notify-end was missed (e.g. the montage was interrupted on the server before it reached
	// NotifyEnd), snap the magazine home so it doesn't stay floating on clients.
	if (CurrentState != EWeaponState::Reloading)
	{
		ReattachMagazine();
		StopVisualWeaponReload();
	}
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

	// Build the team friendly-ignore list at burst start (mirrors StartFiring for AI) so the player's
	// shots pass through allies — PerformHitscan applies it for the player branch too.
	if (IsValid(Cast<ACharacter>(GetOwner())))
	{
		RebuildFFIgnoreList();
		RebuildSuppressionTargets();
	}
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
	if (!CanFire())
	{
		// Bug 6b: throttled diagnostic — log once per second why CanFire failed.
		if (CVarPlayerTraceDebug.GetValueOnGameThread() != 0)
		{
			static float LastKitFireFailLogTime = -1e9f;
			const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
			if ((Now - LastKitFireFailLogTime) >= 1.f)
			{
				LastKitFireFailLogTime = Now;
				UE_LOG(LogExtraction, Warning,
					TEXT("KitFire_HitScan BLOCKED: owner=%s state=%d ammo=%d WeaponData=%s"),
					*GetNameSafe(GetOwner()), (int32)CurrentState, CurrentAmmo, *GetNameSafe(WeaponData));
			}
		}
		return;
	}

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
	StopVisualWeaponReload();
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
		ReattachMagazine();
		StopVisualWeaponReload();
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
