// Copyright Epic Games, Inc. All Rights Reserved.

#include "WeaponBase.h"
#include "AI/CompanionDiag.h"
#include "WeaponDataAsset.h"
#include "Character/ExtractionPlayerInterface.h"
#include "AIShooterInterface.h"
#include "ExtractionDamageType.h"
#include "HealthComponent.h"
#include "SuppressionComponent.h"
#include "CompanionCharacter.h"
#include "EnemyCharacter.h"
#include "EnemyAIController.h"
#include "EnemyAwarenessComponent.h"
#include "EnemyMoraleComponent.h"
#include "Animation/AnimInstance.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Sound/SoundBase.h"
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
#include "EnemyDebug.h"
#include "DamageMitigationSettings.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

namespace WeaponConstants
{
	static const FName MuzzleSocketName(TEXT("Muzzle"));

	/** Extra seconds added to the computed shell reload duration to cover montage blend-in/out. */
	static constexpr float ShellReloadSafetyMargin = 1.5f;
}

namespace
{
	/** The kit visual item (BP_Item_Base child stored in BP_ExtractionCharacter.SpawnedItem) keeps its own
	 *  AmmoCount/MaxAmmo vars, and its Event Reload chain gates the reload ANIMATION on them. Nothing depletes
	 *  them while our C++ owns firing, so after the first reload tops them up every later reload skips the
	 *  animation. Mirror the real counts into it via its SetAmmo BP function whenever our ammo changes. */
	void SyncKitVisualItemAmmo(AActor* OwnerActor, int32 InCurrentAmmo, int32 InReserveAmmo)
	{
		if (!IsValid(OwnerActor)) return;
		const FObjectProperty* ItemProp = CastField<FObjectProperty>(OwnerActor->GetClass()->FindPropertyByName(TEXT("SpawnedItem")));
		if (!ItemProp) return;
		UObject* Item = ItemProp->GetObjectPropertyValue_InContainer(OwnerActor);
		if (!IsValid(Item)) return;
		UFunction* SetAmmoFn = Item->FindFunction(TEXT("SetAmmo"));
		if (!SetAmmoFn || SetAmmoFn->ParmsSize != sizeof(int32) * 2) return;
		struct { int32 AmmoCount; int32 MaxAmmo; } Params{ InCurrentAmmo, InReserveAmmo };
		Item->ProcessEvent(SetAmmoFn, &Params);
	}

	/** C++-initiated reloads (auto-reload on empty, held-fire dry reload) never pass through the kit
	 *  character's IA_Reload chain, so the kit item's Event Reload — which owns the arms + weapon-mesh
	 *  reload animation — doesn't fire for them. Trigger it directly; the kit chain's own Do Once and
	 *  ammo gates make a duplicate call from the manual R-key path a no-op. */
	void TriggerKitVisualItemReload(AActor* OwnerActor)
	{
		if (!IsValid(OwnerActor)) return;
		const FObjectProperty* ItemProp = CastField<FObjectProperty>(OwnerActor->GetClass()->FindPropertyByName(TEXT("SpawnedItem")));
		if (!ItemProp) return;
		UObject* Item = ItemProp->GetObjectPropertyValue_InContainer(OwnerActor);
		if (!IsValid(Item)) return;
		UFunction* ReloadFn = Item->FindFunction(TEXT("Reload"));
		if (!ReloadFn || ReloadFn->ParmsSize != 0) return;
		Item->ProcessEvent(ReloadFn, nullptr);
	}

	/** The kit's procedural fire feel (arms + gun kick via AC_ProceduralAnimation::RecoilAnimation and
	 *  the arms fire anim) is normally driven by the kit item's own Trigger flow, which our C++ fire
	 *  path bypasses — so the camera moved but the hands didn't. Our item BPs implement a cosmetic-only
	 *  FireKick(bADS) event mirroring that chain; invoke it per local shot. No-ops when the item BP
	 *  has no FireKick (kit originals, enemies, companion). */
	void TriggerKitVisualItemFireKick(AActor* OwnerActor, bool bADS)
	{
		if (!IsValid(OwnerActor)) return;
		const FObjectProperty* ItemProp = CastField<FObjectProperty>(OwnerActor->GetClass()->FindPropertyByName(TEXT("SpawnedItem")));
		if (!ItemProp) return;
		UObject* Item = ItemProp->GetObjectPropertyValue_InContainer(OwnerActor);
		if (!IsValid(Item)) return;
		UFunction* KickFn = Item->FindFunction(TEXT("FireKick"));
		if (!KickFn || KickFn->ParmsSize != sizeof(bool)) return;
		bool bParam = bADS;
		Item->ProcessEvent(KickFn, &bParam);
	}
}

static TAutoConsoleVariable<int32> CVarShowBulletTracers(
	TEXT("weapon.ShowTracers"),
	0,
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

// Single definition — other translation units (companion BT service/task) re-query this by name
// via IConsoleManager::Get().FindConsoleVariable to avoid duplicate CVar registration.
static TAutoConsoleVariable<int32> CVarCompanionFireDebug(
	TEXT("companion.FireDebug"),
	0,
	TEXT("If non-zero, log companion fire-decision denials (state/ammo), stealth-break signal state, and burst fire-withhold transitions."),
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

	// Capture the rest relative transform now that the visual actor is attached (or confirmed absent).
	// Doing this here guarantees a true rest capture regardless of when SetRecoilOffset is first called.
	if (IsValid(WeaponMesh))
	{
		RecoilRestRelative = WeaponMesh->GetRelativeTransform();
		bRecoilRestCaptured = true;
	}
}

void AWeaponBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(AWeaponBase, CurrentState, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AWeaponBase, CurrentAmmo, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AWeaponBase, ReserveAmmo, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AWeaponBase, AttachmentSelection, COND_SkipOwner);
}

// ---- Attachments (gameplay effects) ----

void AWeaponBase::SetAttachmentSelection(uint8 Sight, uint8 Muzzle, uint8 Laser, uint8 Grip, uint8 Handguard)
{
	FWeaponAttachmentSelection NewSelection;
	NewSelection.Sight = Sight;
	NewSelection.Muzzle = Muzzle;
	NewSelection.Laser = Laser;
	NewSelection.Grip = Grip;
	NewSelection.Handguard = Handguard;

	// Apply locally either way (owning-client feel); non-authority forwards to the server.
	ApplySelectionInternal(NewSelection);
	if (!HasAuthority())
		Server_SetAttachmentSelection(NewSelection);
}

void AWeaponBase::Server_SetAttachmentSelection_Implementation(FWeaponAttachmentSelection NewSelection)
{
	ApplySelectionInternal(NewSelection);
}

void AWeaponBase::ApplySelectionInternal(const FWeaponAttachmentSelection& NewSelection)
{
	AttachmentSelection = NewSelection;
	RecalculateAttachmentEffects();
}

void AWeaponBase::OnRep_AttachmentSelection()
{
	RecalculateAttachmentEffects();
}

void AWeaponBase::RecalculateAttachmentEffects()
{
	const UNiagaraSystem* OldFlash = GetEffectiveMuzzleFlashFX();

	CombinedModifiers = FWeaponStatModifiers();
	bAttachmentSuppressed = false;
	AttachmentMuzzleFlashOverride = nullptr;

	if (IsValid(WeaponData))
	{
		const auto Accumulate = [this](const TArray<TObjectPtr<UWeaponAttachmentDataAsset>>& Options, uint8 Index)
		{
			if (!Options.IsValidIndex(Index)) return;
			const UWeaponAttachmentDataAsset* Attachment = Options[Index];
			if (!IsValid(Attachment)) return;

			const FWeaponStatModifiers& M = Attachment->Modifiers;
			CombinedModifiers.DamageMult *= M.DamageMult;
			CombinedModifiers.RecoilPitchMult *= M.RecoilPitchMult;
			CombinedModifiers.RecoilYawMult *= M.RecoilYawMult;
			CombinedModifiers.ADSTransitionMult *= M.ADSTransitionMult;
			CombinedModifiers.ADSMoveSpeedMult *= M.ADSMoveSpeedMult;
			CombinedModifiers.HipSpreadMult *= M.HipSpreadMult;
			CombinedModifiers.NoiseLoudnessMult *= M.NoiseLoudnessMult;
			CombinedModifiers.NoiseRangeMult *= M.NoiseRangeMult;
			CombinedModifiers.FalloffStartMult *= M.FalloffStartMult;
			CombinedModifiers.ADSFOVDelta += M.ADSFOVDelta;
			if (M.ADSFOVOverride > 0.f)
				CombinedModifiers.ADSFOVOverride = M.ADSFOVOverride;

			bAttachmentSuppressed |= Attachment->bSetsSuppressed;
			if (IsValid(Attachment->MuzzleFlashFXOverride))
				AttachmentMuzzleFlashOverride = Attachment->MuzzleFlashFXOverride;
		};

		Accumulate(WeaponData->SightAttachments, AttachmentSelection.Sight);
		Accumulate(WeaponData->MuzzleAttachments, AttachmentSelection.Muzzle);
		Accumulate(WeaponData->LaserAttachments, AttachmentSelection.Laser);
		Accumulate(WeaponData->GripAttachments, AttachmentSelection.Grip);
		Accumulate(WeaponData->HandguardAttachments, AttachmentSelection.Handguard);
	}

	// Flash FX changed — drop the pooled components so the next shot rebuilds with the new system.
	if (GetEffectiveMuzzleFlashFX() != OldFlash)
	{
		if (IsValid(MuzzleFlashComponent))
		{
			MuzzleFlashComponent->DestroyComponent();
			MuzzleFlashComponent = nullptr;
		}
		if (IsValid(FirstPersonMuzzleFlashComponent))
		{
			FirstPersonMuzzleFlashComponent->DestroyComponent();
			FirstPersonMuzzleFlashComponent = nullptr;
		}
	}
}

float AWeaponBase::GetEffectiveDamage() const
{
	return IsValid(WeaponData) ? WeaponData->BaseDamage * CombinedModifiers.DamageMult : 0.f;
}

bool AWeaponBase::IsSuppressedEffective() const
{
	return (IsValid(WeaponData) && WeaponData->bSuppressed) || bAttachmentSuppressed;
}

float AWeaponBase::GetEffectiveADSFOV() const
{
	if (!IsValid(WeaponData)) return 65.f;
	if (CombinedModifiers.ADSFOVOverride > 0.f)
		return FMath::Clamp(CombinedModifiers.ADSFOVOverride, 20.f, 120.f);
	return FMath::Clamp(WeaponData->ADSFOV + CombinedModifiers.ADSFOVDelta, 20.f, 120.f);
}

float AWeaponBase::GetEffectiveADSTransitionTime() const
{
	return IsValid(WeaponData) ? WeaponData->ADSTransitionTime * CombinedModifiers.ADSTransitionMult : 0.15f;
}

float AWeaponBase::GetEffectiveADSMovementSpeed() const
{
	return IsValid(WeaponData) ? WeaponData->ADSMovementSpeed * CombinedModifiers.ADSMoveSpeedMult : 400.f;
}

float AWeaponBase::GetEffectiveHipFireSpreadDeg() const
{
	return IsValid(WeaponData) ? WeaponData->HipFireSpreadDeg * CombinedModifiers.HipSpreadMult : 0.f;
}

float AWeaponBase::GetEffectiveNoiseLoudness() const
{
	return IsValid(WeaponData) ? WeaponData->NoiseLoudness * CombinedModifiers.NoiseLoudnessMult : 0.f;
}

float AWeaponBase::GetEffectiveNoiseRange() const
{
	return IsValid(WeaponData) ? WeaponData->NoiseRange * CombinedModifiers.NoiseRangeMult : 0.f;
}

UNiagaraSystem* AWeaponBase::GetEffectiveMuzzleFlashFX() const
{
	if (IsValid(AttachmentMuzzleFlashOverride)) return AttachmentMuzzleFlashOverride;
	return IsValid(WeaponData) ? WeaponData->MuzzleFlashFX : nullptr;
}

void AWeaponBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetFireAlignAlpha(0.f);
	SetMeleeAlignAlpha(0.f);
	SetPatrolAlignAlpha(0.f);

	// Restore the weapon mesh to its captured rest pose so no recoil offset freezes in place.
	if (bRecoilRestCaptured && IsValid(WeaponMesh))
	{
		WeaponMesh->SetRelativeTransform(RecoilRestRelative);
		bRecoilRestCaptured = false;
	}

	if (IsValid(MuzzleFlashComponent))
	{
		MuzzleFlashComponent->DestroyComponent();
		MuzzleFlashComponent = nullptr;
	}

	if (IsValid(FirstPersonMuzzleFlashComponent))
	{
		FirstPersonMuzzleFlashComponent->DestroyComponent();
		FirstPersonMuzzleFlashComponent = nullptr;
	}

	if (IsValid(SpawnedVisualActor))
	{
		SpawnedVisualActor->Destroy();
		SpawnedVisualActor = nullptr;
	}

	StopReloadAudio();

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
	// The kit FP item's Muzzle component is the designer-placed barrel tip; the hidden TP frame
	// has no Muzzle socket, so without this the fallback is the actor (hand) location.
	if (IsValid(FirstPersonMuzzle))
		return FirstPersonMuzzle->GetComponentLocation();
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

void AWeaponBase::SetWeaponHidden(bool bNewHidden)
{
	SetActorHiddenInGame(bNewHidden);
	if (IsValid(SpawnedVisualActor))
		SpawnedVisualActor->SetActorHiddenInGame(bNewHidden);
}

// ---- Fire Control ----

bool AWeaponBase::CanFire() const
{
	if (CurrentState != EWeaponState::Idle && CurrentState != EWeaponState::Firing) return false;
	if (CurrentAmmo <= 0 || !IsValid(WeaponData)) return false;

	// Post-reload settle: block the first shot until the reload's end section has played out
	// (gun back in hand). Only armed when WeaponData->PostReloadFireDelay > 0; player weapons leave it 0.
	if (FireReadyTimeSeconds > 0.f)
	{
		const UWorld* World = GetWorld();
		if (World && World->GetTimeSeconds() < FireReadyTimeSeconds) return false;
	}

	return true;
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

	if (!CanFire())
	{
		if (CVarCompanionFireDebug.GetValueOnGameThread() != 0
			&& IsValid(GetOwner()) && GetOwner()->IsA<ACompanionCharacter>())
		{
			const float FireReadyIn = (FireReadyTimeSeconds > 0.f && GetWorld())
				? FireReadyTimeSeconds - GetWorld()->GetTimeSeconds() : 0.f;
			UE_LOG(LogCompanionDiag, Warning,
				TEXT("%s: [FireDebug] StartFiring DENIED state=%d ammo=%d reserve=%d fireReadyIn=%.2f dataValid=%d"),
				*GetNameSafe(GetOwner()), (int32)CurrentState, CurrentAmmo, ReserveAmmo,
				FireReadyIn, IsValid(WeaponData) ? 1 : 0);
		}
		// Dry trigger press on an empty mag — kick the reload instead of silently no-oping.
		if (bAutoReloadOnEmpty && CurrentAmmo <= 0 && CanReload())
			Reload();
		return;
	}

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
		if (bWantsToFire && CVarCompanionFireDebug.GetValueOnGameThread() != 0
			&& IsValid(GetOwner()) && GetOwner()->IsA<ACompanionCharacter>())
		{
			UE_LOG(LogCompanionDiag, Warning,
				TEXT("%s: [FireDebug] AutoFire DENIED state=%d ammo=%d reserve=%d dataValid=%d"),
				*GetNameSafe(GetOwner()), (int32)CurrentState, CurrentAmmo, ReserveAmmo,
				IsValid(WeaponData) ? 1 : 0);
		}

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

	// Per-shooter weight: enemies acknowledge companion fire below player/enemy fire.
	const bool bCompanionShooter = GetOwner() && GetOwner()->IsA<ACompanionCharacter>();
	const float NearMissWeight = bCompanionShooter ? CompanionNearMissWeight : 1.f;

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
			Comp->RegisterNearMiss(NearMissWeight);
			if (bCompanionShooter)
				UE_LOG(LogCompanionDiag, Verbose, TEXT("COMPANION-NEARMISS -> %s w=%.2f supp=%.2f"),
					*GetNameSafe(Pawn), NearMissWeight, Comp->GetSuppression01());

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
		SyncKitVisualItemAmmo(GetOwner(), CurrentAmmo, ReserveAmmo);
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
		{
			ApplyRecoil();
			TriggerKitVisualItemFireKick(GetOwner(), bOwnerIsAiming);
		}
	}

	OnWeaponFired.Broadcast();
	PlayVisualWeaponFire();
}

void AWeaponBase::FireCosmetic(const FVector& AimEndPoint)
{
	if (!IsValid(WeaponData)) return;

	OnWeaponFired.Broadcast();
	PlayVisualWeaponFire();
	Multicast_PlayFireFX(GetMuzzleLocation(), AimEndPoint, true);
}

void AWeaponBase::PerformHitscan()
{
	if (!IsValid(WeaponData)) return;

	ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());
	if (!IsValid(OwnerChar)) return;

	FVector TraceStart;
	FVector AimDirection;

	// Player: trace from camera. AI/enemy: trace from eye height.
	APlayerController* PC = Cast<APlayerController>(OwnerChar->GetController());
	if (IsValid(PC))
	{
		FVector CameraLoc;
		FRotator CameraRot;
		PC->GetPlayerViewPoint(CameraLoc, CameraRot);
		TraceStart = CameraLoc;
		AimDirection = CameraRot.Vector();

		// Player hip-fire cone — zero while ADS or when the DA leaves HipFireSpreadDeg at 0.
		if (!bOwnerIsAiming)
			AimDirection = ApplyConeSpread(AimDirection, GetEffectiveHipFireSpreadDeg());
	}
	else
	{
		// AI path: trace from eye height (GetPawnViewLocation) so damage agrees with LOS checks
		// in BTService_EnemyCombat and EnemyAwarenessComponent. Muzzle socket is kept for FX/noise only.
		TraceStart = OwnerChar->GetPawnViewLocation();
		AimDirection = OwnerChar->GetActorForwardVector(); // fallback

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

		// Bug 5b: uniform cone spread for AI inaccuracy — reuses ApplyConeSpread.
		if (InaccuracyDeg > 0.0f)
			AimDirection = ApplyConeSpread(AimDirection, InaccuracyDeg);
	}

	// Build QueryParams once — reused for every pellet.
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(OwnerChar);
	QueryParams.bReturnPhysicalMaterial = false;

	// Friendly-fire prevention for ALL shooters: built once per burst, refreshed at most every 1s.
	const bool bAIOwned = !IsValid(PC);
	{
		const UWorld* QueryWorld = GetWorld();
		if (QueryWorld && (QueryWorld->GetTimeSeconds() - FFIgnoreListBuiltTime) > 1.f)
			RebuildFFIgnoreList();
		QueryParams.AddIgnoredActors(CachedFFIgnoreList);
	}

	UWorld* World = GetWorld();
	if (!World) return;

	const int32 NumPellets = FMath::Max(1, WeaponData->PelletCount);

	// Collected pellet hits — trace pass only, no TakeDamage during traces.
	struct FPelletRecord
	{
		TWeakObjectPtr<AActor> Victim;
		FHitResult Hit;
		FVector Dir;
		float Damage;
	};
	TArray<FPelletRecord, TInlineAllocator<8>> PelletRecords;

	// Per-victim dedup for morale + mitigation — populated on first hit per victim, caches the HealthComponent.
	struct FVictimRecord
	{
		TWeakObjectPtr<AActor> Victim;
		UHealthComponent* Health; // cached once, reused in morale pass
		bool bWasAlive;
		bool bGateAllowsDamage = true; // set false by the mitigation gate when the shot is suppressed
	};
	TArray<FVictimRecord, TInlineAllocator<4>> VictimRecords;

	// Center-pellet result — used for FX, near-miss, and debug logging.
	bool bCenterHit = false;
	FVector CenterImpactOrEnd = TraceStart + AimDirection * WeaponData->MaxRange;
	AActor* CenterHitActor = nullptr;

	// === TRACE PASS (no TakeDamage — avoids re-entrancy during traces) ===
	for (int32 P = 0; P < NumPellets; ++P)
	{
		const FVector PelletDir = (P == 0) ? AimDirection : ApplyConeSpread(AimDirection, WeaponData->PelletSpreadDeg);
		const FVector PelletEnd = TraceStart + PelletDir * WeaponData->MaxRange;

		FHitResult PelletHit;
		const bool bHit = World->LineTraceSingleByChannel(PelletHit, TraceStart, PelletEnd, ECC_Visibility, QueryParams);

		if (P == 0)
		{
			bCenterHit = bHit;
			CenterImpactOrEnd = bHit ? PelletHit.ImpactPoint : PelletEnd;
			CenterHitActor = bHit ? PelletHit.GetActor() : nullptr;
		}

		if (!bHit) continue;

		AActor* HitActor = PelletHit.GetActor();
		if (!IsValid(HitActor)) continue;

		// Record this victim the first time it's hit (cache HealthComponent + alive state).
		bool bAlreadySeen = false;
		for (const FVictimRecord& VR : VictimRecords)
		{
			if (VR.Victim.Get() == HitActor) { bAlreadySeen = true; break; }
		}
		if (!bAlreadySeen)
		{
			UHealthComponent* VH = HitActor->FindComponentByClass<UHealthComponent>();
			VictimRecords.Add({ HitActor, VH, VH && VH->IsAlive(), true });
		}

		const float PelletDamage = GetEffectiveDamage() * ComputeFalloffScale(PelletHit.Distance);
		PelletRecords.Add({ HitActor, PelletHit, PelletDir, PelletDamage });
	}

	// === AI DAMAGE MITIGATION GATE (between trace and damage passes) ===
	const UDamageMitigationSettings* MitS = WeaponData->DamageMitigation;
	const bool bGateActive = bAIOwned && MitS && MitS->bEnabled;
	if (bGateActive)
	{
		const float Now = World->GetTimeSeconds();
		const IAIShooterInterface* GateShooter = Cast<IAIShooterInterface>(OwnerChar);
		AActor* GateTarget = GateShooter ? GateShooter->GetAIAimTarget() : CenterHitActor;
		const bool bShotDamages = RollShotDamage(*MitS, GateTarget, Now);
		for (FVictimRecord& VR : VictimRecords)
		{
			if (!bShotDamages)
			{
				VR.bGateAllowsDamage = false;
				continue;
			}
			// Shot passed the roll; check per-victim cadence cap.
			if (VR.Health)
				VR.bGateAllowsDamage = VR.Health->TryConsumeGatedDamage(Now, MitS->PerVictimDamageInterval);
			else
				VR.bGateAllowsDamage = bShotDamages; // no HealthComponent (world geometry) = pass through
		}
	}

	// === DAMAGE PASS (per-pellet TakeDamage preserves per-bone hitbox multipliers) ===
	if (!WeaponData->DamageTypeClass)
		UE_LOG(LogExtraction, Warning, TEXT("'%s': WeaponData has no DamageTypeClass — hitbox multipliers won't apply."), *GetNameSafe(this));

	for (const FPelletRecord& PR : PelletRecords)
	{
		AActor* HitActor = PR.Victim.Get();
		if (!IsValid(HitActor)) continue;

		// Check mitigation gate: find this pellet's victim record and skip TakeDamage if gated.
		if (bGateActive)
		{
			const FVictimRecord* VR = nullptr;
			for (const FVictimRecord& V : VictimRecords)
			{
				if (V.Victim.Get() == HitActor) { VR = &V; break; }
			}
			if (VR && !VR->bGateAllowsDamage) continue;
		}

		FPointDamageEvent DamageEvent;
		DamageEvent.Damage = PR.Damage;
		DamageEvent.HitInfo = PR.Hit;
		DamageEvent.ShotDirection = PR.Dir;
		if (WeaponData->DamageTypeClass) DamageEvent.DamageTypeClass = WeaponData->DamageTypeClass;

		UE_LOG(LogExtraction, Verbose, TEXT("%s hit %s for %.1f damage"),
			*GetNameSafe(OwnerChar), *GetNameSafe(HitActor), PR.Damage);

		HitActor->TakeDamage(PR.Damage, DamageEvent, OwnerChar->GetController(), this);
	}

	// === MORALE PASS (once per victim per shot, reusing cached HealthComponent) ===
	AEnemyCharacter* OwnerEnemy = Cast<AEnemyCharacter>(OwnerChar);
	if (IsValid(OwnerEnemy))
	{
		if (UEnemyMoraleComponent* Morale = OwnerEnemy->GetMoraleComponent())
		{
			for (const FVictimRecord& VR : VictimRecords)
			{
				if (!VR.bWasAlive) continue;
				Morale->NotifyDamagedTarget();
				if (VR.Health && VR.Health->IsDead()) Morale->NotifyTargetDowned();
			}
		}
	}

	// Debug logging — tied to the center pellet.
	if (bAIOwned && CVarAIWeaponTraceDebug.GetValueOnGameThread() != 0)
	{
		const IAIShooterInterface* DebugShooter = Cast<IAIShooterInterface>(OwnerChar);
		const AActor* AILogAimTarget = DebugShooter ? DebugShooter->GetAIAimTarget() : nullptr;
		UE_LOG(LogExtraction, Verbose,
			TEXT("AI-FIRE owner=%s eye=%s end=%s aimTarget=%s bHit=%d hitActor=%s hitDist=%.0f"),
			*GetNameSafe(OwnerChar), *TraceStart.ToCompactString(),
			*(TraceStart + AimDirection * WeaponData->MaxRange).ToCompactString(),
			*GetNameSafe(AILogAimTarget), (int32)bCenterHit,
			*GetNameSafe(CenterHitActor), bCenterHit ? FVector::Dist(TraceStart, CenterImpactOrEnd) : 0.f);
	}
	if (!bAIOwned && CVarPlayerTraceDebug.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogExtraction, Log,
			TEXT("PLAYER-FIRE start=%s end=%s bHit=%d hitActor=%s dist=%.0f"),
			*TraceStart.ToCompactString(),
			*(TraceStart + AimDirection * WeaponData->MaxRange).ToCompactString(),
			(int32)bCenterHit, *GetNameSafe(CenterHitActor),
			bCenterHit ? FVector::Dist(TraceStart, CenterImpactOrEnd) : 0.f);
		if (bCenterHit && IsValid(CenterHitActor) && !CenterHitActor->FindComponentByClass<UHealthComponent>())
			UE_LOG(LogExtraction, Verbose, TEXT("PLAYER-FIRE hit %s but it has NO UHealthComponent — damage will be ignored"), *GetNameSafe(CenterHitActor));
	}

	// FX and noise — one per shot, using the center pellet.
	ReportNearMisses(TraceStart, CenterImpactOrEnd, CenterHitActor);
	Multicast_PlayFireFX(GetMuzzleLocation(), CenterImpactOrEnd, bCenterHit);

	if (GetEffectiveNoiseRange() > 0.f)
		UAISense_Hearing::ReportNoiseEvent(World, GetMuzzleLocation(), GetEffectiveNoiseLoudness(), OwnerChar, GetEffectiveNoiseRange(), TEXT("WeaponFire"));
}

float AWeaponBase::ComputeFalloffScale(float Distance) const
{
	if (!IsValid(WeaponData) || !WeaponData->bUseDamageFalloff) return 1.f;
	const float FalloffStart = WeaponData->DamageFalloffStartRange * CombinedModifiers.FalloffStartMult;
	if (WeaponData->DamageFalloffEndRange <= FalloffStart) return 1.f;
	if (Distance <= FalloffStart) return 1.f;
	if (Distance >= WeaponData->DamageFalloffEndRange) return WeaponData->MinDamageFraction;
	return FMath::GetMappedRangeValueClamped(
		FVector2D(FalloffStart, WeaponData->DamageFalloffEndRange),
		FVector2D(1.f, WeaponData->MinDamageFraction),
		Distance);
}

FVector AWeaponBase::ApplyConeSpread(const FVector& Dir, float HalfAngleDeg)
{
	if (HalfAngleDeg <= 0.f) return Dir;
	const float RandAngle = FMath::RandRange(0.f, 360.f);
	const float RandMagnitude = FMath::RandRange(0.f, HalfAngleDeg);
	FRotator SpreadRot = Dir.Rotation();
	SpreadRot.Yaw += FMath::Cos(FMath::DegreesToRadians(RandAngle)) * RandMagnitude;
	SpreadRot.Pitch += FMath::Sin(FMath::DegreesToRadians(RandAngle)) * RandMagnitude;
	return SpreadRot.Vector();
}

// ---- FX RPCs ----

void AWeaponBase::Multicast_PlayFireFX_Implementation(const FVector& MuzzleLocation, const FVector& EndPoint, bool bHit)
{
	// Muzzle flash: persistent component, re-activated per shot.
	EnsureMuzzleFlashComponent();
	if (IsValid(MuzzleFlashComponent))
		MuzzleFlashComponent->Activate(true);

	// First-person flash on the kit FP gun — the TP flash above is OwnerNoSee.
	// The kit's Muzzle slot component only carries a mesh when a muzzle attachment
	// (suppressor) is equipped via the modding screen — no mesh = bare muzzle = flash.
	const UStaticMeshComponent* MuzzleSlot = Cast<UStaticMeshComponent>(FirstPersonMuzzle);
	const bool bMuzzleAttachmentEquipped = IsValid(MuzzleSlot) && IsValid(MuzzleSlot->GetStaticMesh());
	if (!bMuzzleAttachmentEquipped)
	{
		EnsureFirstPersonMuzzleFlashComponent();
		if (IsValid(FirstPersonMuzzleFlashComponent))
			FirstPersonMuzzleFlashComponent->Activate(true);
	}

	// Fire report. A mounted muzzle attachment (modding-screen suppressor) or effective
	// suppression swaps to the suppressed report when one is assigned.
	if (IsValid(WeaponData))
	{
		const bool bWantSuppressedReport = bMuzzleAttachmentEquipped || IsSuppressedEffective();
		USoundBase* Report = (bWantSuppressedReport && IsValid(WeaponData->SuppressedFireSound))
			? WeaponData->SuppressedFireSound.Get()
			: WeaponData->FireSound.Get();
		if (IsValid(Report))
			UGameplayStatics::PlaySoundAtLocation(GetWorld(), Report, MuzzleLocation);
	}

	// Bullet tracer: one-shot pooled Niagara streak along the fire line.
	SpawnTracer(MuzzleLocation, EndPoint);

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

// ---- Weapon cover alignment ----

void AWeaponBase::SetupCoverAlign(USkeletalMeshComponent* EnemyMesh, FName SocketSpaceBone,
	const FCoverAlignPoses& Poses)
{
	bCoverAlignReady = false;
	bCoverAlignWriting = false;
	for (bool& bReady : bCoverAlignTargetReady) bReady = false;

	if (!IsValid(EnemyMesh) || !IsValid(WeaponMesh)) return;

	const FName RestSocket = WeaponMesh->GetAttachSocketName();
	if (!EnemyMesh->DoesSocketExist(RestSocket) || !EnemyMesh->DoesSocketExist(SocketSpaceBone))
	{
		UE_LOG(LogExtraction, Warning, TEXT("SetupCoverAlign: %s — rest socket '%s' or bone '%s' not found on enemy mesh (cover-align disabled)"),
			*GetNameSafe(this), *RestSocket.ToString(), *SocketSpaceBone.ToString());
		return;
	}

	CoverAlignRestRelative = WeaponMesh->GetRelativeTransform();
	const FTransform TRest = EnemyMesh->GetSocketTransform(RestSocket, RTS_Component);
	const FTransform TBone = EnemyMesh->GetSocketTransform(SocketSpaceBone, RTS_Component);

	const FTransform* ScenarioPoses[CoverAlignScenarioCount] = {
		&Poses.Idle, &Poses.OverTop, &Poses.PeekLeft, &Poses.PeekRight,
		&Poses.StandIdleLeft, &Poses.StandIdleRight, &Poses.StandPeekLeft, &Poses.StandPeekRight
	};
	for (int32 i = 0; i < CoverAlignScenarioCount; ++i)
	{
		if (ScenarioPoses[i]->Equals(FTransform::Identity)) continue;
		const FTransform TScenario = *ScenarioPoses[i] * TBone;
		CoverAlignTargets[i] = CoverAlignRestRelative * (TScenario * TRest.Inverse());
		bCoverAlignTargetReady[i] = true;
		bCoverAlignReady = true;
	}

	CoverAlignCurrent = CoverAlignRestRelative;
}

void AWeaponBase::UpdateCoverAlign(ECoverWeaponAlign Scenario, float DeltaSeconds, float InterpSpeed)
{
	if (!bCoverAlignReady) return;
	if (!IsValid(WeaponMesh)) return;

	const FTransform* Target = &CoverAlignRestRelative;
	if (Scenario != ECoverWeaponAlign::None)
	{
		const int32 Index = static_cast<int32>(Scenario) - 1;
		if (Index < 0 || Index >= CoverAlignScenarioCount) return;
		if (bCoverAlignTargetReady[Index]) Target = &CoverAlignTargets[Index];
	}

	// Dormant: settled at rest with no off-rest blend in flight — write nothing so fire/melee/
	// patrol align and the hand-swap settle own the weapon out of cover.
	const bool bWantsRest = (Target == &CoverAlignRestRelative);
	if (bWantsRest && !bCoverAlignWriting) return;

	CoverAlignCurrent.SetLocation(FMath::VInterpTo(CoverAlignCurrent.GetLocation(), Target->GetLocation(), DeltaSeconds, InterpSpeed));
	CoverAlignCurrent.SetRotation(FMath::QInterpTo(CoverAlignCurrent.GetRotation(), Target->GetRotation(), DeltaSeconds, InterpSpeed));
	CoverAlignCurrent.SetScale3D(Target->GetScale3D());

	WeaponMesh->SetRelativeTransform(CoverAlignCurrent);
	bCoverAlignWriting = true;

	// Settled home — release the write so out-of-cover writers resume.
	if (bWantsRest && CoverAlignCurrent.Equals(CoverAlignRestRelative, 0.05f))
	{
		WeaponMesh->SetRelativeTransform(CoverAlignRestRelative);
		bCoverAlignWriting = false;
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

// ---- Weapon patrol alignment ----

void AWeaponBase::SetupPatrolAlign()
{
	bPatrolAlignReady = false;

	if (!IsValid(WeaponMesh)) return;
	if (!IsValid(WeaponData)) return;

	// Zero offsets = no patrol-carry pose. Weapon stays at ADS — skip entirely.
	if (WeaponData->PatrolAlignLocationOffset.IsNearlyZero() &&
		WeaponData->PatrolAlignRotationOffset.IsNearlyZero())
		return;

	// Capture the rest-pose relative transform as the Alpha=0 target.
	PatrolAlignRestRelative = WeaponMesh->GetRelativeTransform();

	// Pre-compose the Alpha=1 target: rest pose + the DA-driven offset.
	const FTransform OffsetTransform(
		FQuat(WeaponData->PatrolAlignRotationOffset),
		WeaponData->PatrolAlignLocationOffset);
	PatrolAlignPatrolRelative = OffsetTransform * PatrolAlignRestRelative;

	bPatrolAlignReady = true;
}

void AWeaponBase::SetPatrolAlignAlpha(float Alpha)
{
	if (!bPatrolAlignReady) return;
	if (!IsValid(WeaponMesh)) return;

	Alpha = FMath::Clamp(Alpha, 0.f, 1.f);

	FTransform Blended;
	Blended.SetLocation(FMath::Lerp(PatrolAlignRestRelative.GetLocation(), PatrolAlignPatrolRelative.GetLocation(), Alpha));
	Blended.SetRotation(FQuat::Slerp(PatrolAlignRestRelative.GetRotation(), PatrolAlignPatrolRelative.GetRotation(), Alpha));
	Blended.SetScale3D(FMath::Lerp(PatrolAlignRestRelative.GetScale3D(), PatrolAlignPatrolRelative.GetScale3D(), Alpha));

	WeaponMesh->SetRelativeTransform(Blended);
}

// ---- Hand-swap settle ----

void AWeaponBase::BeginHandSwapSettle()
{
	if (!IsValid(WeaponMesh)) return;

	HandSwapSettleStart = WeaponMesh->GetRelativeTransform();
	HandSwapSettleAlpha = 0.f;
	bHandSwapSettling = true;
}

bool AWeaponBase::UpdateHandSwapSettle(float DeltaSeconds)
{
	if (!bHandSwapSettling) return false;
	if (!IsValid(WeaponMesh))
	{
		bHandSwapSettling = false;
		return false;
	}

	HandSwapSettleAlpha = FMath::FInterpTo(HandSwapSettleAlpha, 1.f, DeltaSeconds, HandSwapSettleSpeed);

	FTransform Blended;
	Blended.SetLocation(FMath::Lerp(HandSwapSettleStart.GetLocation(), FVector::ZeroVector, HandSwapSettleAlpha));
	Blended.SetRotation(FQuat::Slerp(HandSwapSettleStart.GetRotation(), FQuat::Identity, HandSwapSettleAlpha));
	Blended.SetScale3D(FMath::Lerp(HandSwapSettleStart.GetScale3D(), FVector::OneVector, HandSwapSettleAlpha));

	WeaponMesh->SetRelativeTransform(Blended);

	if (HandSwapSettleAlpha >= 1.f - KINDA_SMALL_NUMBER)
	{
		WeaponMesh->SetRelativeTransform(FTransform::Identity);
		bHandSwapSettling = false;
		return false;
	}

	return true;
}

void AWeaponBase::ResetHandSwapSettle()
{
	bHandSwapSettling = false;
	HandSwapSettleAlpha = 0.f;
	if (IsValid(WeaponMesh))
		WeaponMesh->SetRelativeTransform(FTransform::Identity);
}

// ---- Weapon recoil offset ----

void AWeaponBase::SetRecoilOffset(const FTransform& Offset)
{
	if (!IsValid(WeaponMesh)) return;

	// Rest was captured deterministically in BeginPlay. This guard is a backstop only —
	// should never trigger in practice, but prevents a write before the mesh is valid.
	if (!bRecoilRestCaptured) return;

	if (Offset.Equals(FTransform::Identity, KINDA_SMALL_NUMBER))
	{
		WeaponMesh->SetRelativeTransform(RecoilRestRelative);
		return;
	}

	// Compose: apply the offset onto the rest pose.
	// FTransform operator* = A * B (apply A first, then B in component space).
	// A local rotation/translation offset composed through Rest lands in the correct attach space.
	const FTransform Result = Offset * RecoilRestRelative;
	WeaponMesh->SetRelativeTransform(Result);
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
		&& (WeaponData->bInfiniteReserve || ReserveAmmo > 0);
}

void AWeaponBase::Reload()
{
	if (!CanReload()) return;

	if (IsReloadDebugEnabled())
	{
		const FString OwnerName = GetNameSafe(GetOwner());
		const int32 MagSize = IsValid(WeaponData) ? WeaponData->MagazineSize : -1;
		UE_LOG(LogTemp, Warning,
			TEXT("[RELOADDBG] %s Reload() entry: owner=%s ammo=%d/%d -> state=Reloading"),
			*GetName(), *OwnerName, CurrentAmmo, MagSize);
		if (GEngine)
			GEngine->AddOnScreenDebugMessage(
				static_cast<uint64>(GetTypeHash(FString::Printf(TEXT("WepReload_%s"), *GetName()))), 4.f, FColor::Orange,
				FString::Printf(TEXT("[RELOADDBG] %s ammo=%d/%d -> Reloading"), *OwnerName, CurrentAmmo, MagSize));
	}

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

	TriggerKitVisualItemReload(GetOwner());
	StartReloadAudio();

	// Stop firing
	bWantsToFire = false;
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoFireTimerHandle);

		const float TimerDuration = (IsValid(WeaponData) && WeaponData->bShellByShellReload)
			? GetShellReloadSafetyTimeout()
			: WeaponData->ReloadTime;

		World->GetTimerManager().SetTimer(
			ReloadTimerHandle,
			this,
			&AWeaponBase::OnReloadFinished,
			TimerDuration,
			false
		);
	}
}

void AWeaponBase::OnReloadFinished()
{
	if (!IsValid(WeaponData)) return;
	if (CurrentState != EWeaponState::Reloading) return;
	bDryFireLogged = false;

	if (HasAuthority())
	{
		const int32 AmmoNeeded = WeaponData->MagazineSize - CurrentAmmo;
		const int32 AmmoToLoad = WeaponData->bInfiniteReserve
			? AmmoNeeded : FMath::Min(AmmoNeeded, ReserveAmmo);

		CurrentAmmo += AmmoToLoad;
		if (!WeaponData->bInfiniteReserve)
			ReserveAmmo -= AmmoToLoad;
		CurrentState = EWeaponState::Idle;

		// Post-reload settle: hold fire briefly so the reload anim finishes seating the gun.
		if (WeaponData->PostReloadFireDelay > 0.f && GetWorld())
			FireReadyTimeSeconds = GetWorld()->GetTimeSeconds() + WeaponData->PostReloadFireDelay;

		OnAmmoChanged.Broadcast(CurrentAmmo, ReserveAmmo);
		SyncKitVisualItemAmmo(GetOwner(), CurrentAmmo, ReserveAmmo);

		// Safety net: snap the magazine home if the notify-end was missed (montage interrupted, etc.).
		ReattachMagazine();
		StopVisualWeaponReload();

		// Shell-by-shell weapons prime their Loop to self-loop — force-stop the body montage
		// so it doesn't keep looping after the safety timer filled the mag.
		if (IsValid(WeaponData) && WeaponData->bShellByShellReload)
			StopBodyReloadMontage();
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

	// Resume firing if input is still held. Player-controlled weapons skip this: the kit BP owns
	// fire cadence there (KitBeginFire never arms AutoFireTimer), and a post-reload StartFiring
	// would run the C++ auto-fire loop against the kit's own dispatch.
	const ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());
	const bool bPlayerOwned = IsValid(OwnerChar) && IsValid(Cast<APlayerController>(OwnerChar->GetController()));
	if (bWantsToFire && !bPlayerOwned)
		StartFiring();
}

void AWeaponBase::HandleShellInserted()
{
	if (!HasAuthority()) return;
	if (CurrentState != EWeaponState::Reloading) return;
	if (!IsValid(WeaponData) || !WeaponData->bShellByShellReload) return;

	// If the mag is already full or reserve is exhausted, end the reload now.
	if (CurrentAmmo >= WeaponData->MagazineSize
		|| (!WeaponData->bInfiniteReserve && ReserveAmmo <= 0))
	{
		AdvanceShellReloadSection(false);
		FinishShellReload();
		return;
	}

	// Seat one shell.
	CurrentAmmo = FMath::Min(CurrentAmmo + 1, WeaponData->MagazineSize);
	if (!WeaponData->bInfiniteReserve)
		ReserveAmmo = FMath::Max(ReserveAmmo - 1, 0);
	OnAmmoChanged.Broadcast(CurrentAmmo, ReserveAmmo);
	SyncKitVisualItemAmmo(GetOwner(), CurrentAmmo, ReserveAmmo);

	const bool bMore = CurrentAmmo < WeaponData->MagazineSize
		&& (WeaponData->bInfiniteReserve || ReserveAmmo > 0);

	if (IsReloadDebugEnabled())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[RELOADDBG] %s HandleShellInserted: owner=%s ammo=%d/%d reserve=%d more=%d"),
			*GetName(), *GetNameSafe(GetOwner()),
			CurrentAmmo, WeaponData->MagazineSize, ReserveAmmo, (int32)bMore);
	}

	AdvanceShellReloadSection(bMore);
	if (!bMore) FinishShellReload();
}

void AWeaponBase::CancelReload()
{
	if (CurrentState != EWeaponState::Reloading) return;

	if (const UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(ReloadTimerHandle);

	if (HasAuthority()) CurrentState = EWeaponState::Idle;

	ReattachMagazine();
	StopVisualWeaponReload();
	StopReloadAudio();

	// Shell-by-shell weapons prime their Loop to self-loop — force-stop the body montage
	// so it doesn't keep looping after an interrupt.
	if (IsValid(WeaponData) && WeaponData->bShellByShellReload)
		StopBodyReloadMontage();
}

void AWeaponBase::AdvanceShellReloadSection(bool bContinue)
{
	if (!IsValid(WeaponData)) return;

	const FName LoopName = WeaponData->ShellReloadLoopSection;
	const FName NextSection = bContinue ? WeaponData->ShellReloadLoopSection : WeaponData->ShellReloadEndSection;

	// Body montage — drive the character mesh anim instance.
	{
		UAnimMontage* BodyReload = WeaponData->EnemyAnimSet.Reload;
		if (IsValid(BodyReload))
		{
			if (ACharacter* OwnerChar = Cast<ACharacter>(GetOwner()))
			{
				if (USkeletalMeshComponent* CharMesh = OwnerChar->GetMesh())
				{
					if (UAnimInstance* AnimInst = CharMesh->GetAnimInstance())
					{
						if (AnimInst->Montage_IsPlaying(BodyReload))
						{
							AnimInst->Montage_SetNextSection(LoopName, NextSection, BodyReload);
							// Make End terminal so section flow is fully code-driven, not authoring-dependent.
							if (!bContinue)
								AnimInst->Montage_SetNextSection(WeaponData->ShellReloadEndSection, NAME_None, BodyReload);
						}
					}
				}
			}
		}
	}

	// Gun montage — drive the visual weapon mesh anim instance.
	{
		UAnimMontage* GunReload = WeaponData->EnemyAnimSet.WeaponReload;
		if (IsValid(GunReload) && CachedWeaponVisualMesh.IsValid())
		{
			if (UAnimInstance* AnimInst = CachedWeaponVisualMesh->GetAnimInstance())
			{
				if (AnimInst->Montage_IsPlaying(GunReload))
				{
					AnimInst->Montage_SetNextSection(LoopName, NextSection, GunReload);
					// Make End terminal so section flow is fully code-driven, not authoring-dependent.
					if (!bContinue)
						AnimInst->Montage_SetNextSection(WeaponData->ShellReloadEndSection, NAME_None, GunReload);
				}
			}
		}
	}
}

void AWeaponBase::FinishShellReload()
{
	if (CurrentState != EWeaponState::Reloading) return;

	if (const UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(ReloadTimerHandle);

	if (HasAuthority()) CurrentState = EWeaponState::Idle;

	// Post-reload settle: hold fire briefly so the shell-by-shell end section seats the gun.
	if (HasAuthority() && IsValid(WeaponData) && WeaponData->PostReloadFireDelay > 0.f && GetWorld())
		FireReadyTimeSeconds = GetWorld()->GetTimeSeconds() + WeaponData->PostReloadFireDelay;

	bDryFireLogged = false;
	OnReloadComplete.Broadcast();

	if (bWantsToFire) StartFiring();
}

void AWeaponBase::PrimeShellReloadLoop()
{
	if (!IsValid(WeaponData) || !WeaponData->bShellByShellReload) return;

	// Loud-fail: warn if the body montage is missing the expected Loop section.
	const UAnimMontage* BodyReload = WeaponData->EnemyAnimSet.Reload;
	if (!IsValid(BodyReload) || BodyReload->GetSectionIndex(WeaponData->ShellReloadLoopSection) == INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ShellReload] %s: bShellByShellReload set but reload montage missing the '%s' loop section — reload will fall back to the safety timer"),
			*GetNameSafe(this), *WeaponData->ShellReloadLoopSection.ToString());
	}

	// Set Loop → Loop on both body and gun montages so the first section boundary self-loops.
	// HandleShellInserted's per-shell AdvanceShellReloadSection(bMore) re-asserts this on continue
	// and sets Loop → End on break-out.
	AdvanceShellReloadSection(true);
}

void AWeaponBase::StopBodyReloadMontage(float BlendOut)
{
	if (!IsValid(WeaponData) || !IsValid(WeaponData->EnemyAnimSet.Reload)) return;

	ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());
	if (!IsValid(OwnerChar)) return;

	USkeletalMeshComponent* CharMesh = OwnerChar->GetMesh();
	if (!IsValid(CharMesh)) return;

	UAnimInstance* AnimInst = CharMesh->GetAnimInstance();
	if (!IsValid(AnimInst)) return;

	AnimInst->Montage_Stop(BlendOut, WeaponData->EnemyAnimSet.Reload);
}

float AWeaponBase::GetShellReloadSafetyTimeout() const
{
	if (!IsValid(WeaponData)) return WeaponConstants::ShellReloadSafetyMargin;

	const UAnimMontage* Body = WeaponData->EnemyAnimSet.Reload;
	if (!IsValid(Body))
	{
		// No montage — fall back to shell-count × ReloadTime.
		return WeaponData->MagazineSize * WeaponData->ReloadTime + WeaponConstants::ShellReloadSafetyMargin;
	}

	const float Total = Body->GetPlayLength();
	const int32 LoopIdx = Body->GetSectionIndex(WeaponData->ShellReloadLoopSection);
	const float LoopLen = (LoopIdx != INDEX_NONE) ? Body->GetSectionLength(LoopIdx) : Total;

	// Mirror the rate clamp used in EnemyAnimInstance::PlayReloadMontage.
	const float Rate = (WeaponData->ReloadTime > 0.f)
		? FMath::Clamp(Total / WeaponData->ReloadTime, 0.5f, 2.0f)
		: 1.f;

	// Total playback: one full montage pass + (MagazineSize-1) extra Loop sections.
	// Assumes Loop is followed by End (Loop is not the terminal section).
	const int32 ExtraLoops = FMath::Max(0, WeaponData->MagazineSize - 1);
	const float Duration = (Total + ExtraLoops * LoopLen) / Rate;

	// Scale slack with magazine size so larger mags keep proportional headroom.
	static constexpr float PerShellSlack = 0.15f;
	return Duration + WeaponConstants::ShellReloadSafetyMargin + PerShellSlack * WeaponData->MagazineSize;
}

// ---- Recoil ----

void AWeaponBase::ApplyRecoil()
{
	if (!IsValid(WeaponData)) return;
	const FRecoilPattern& Pattern = WeaponData->RecoilPattern;
	if (Pattern.Points.Num() == 0) return;

	IExtractionPlayerInterface* OwnerIface = IsValid(GetOwner()) ? Cast<IExtractionPlayerInterface>(GetOwner()) : nullptr;
	if (!OwnerIface) return;
	if (OwnerIface->IsInTakedown()) return;

	// Get current recoil point
	const int32 PatternIndex = FMath::Min(RecoilIndex, Pattern.Points.Num() - 1);
	FVector2D RecoilOffset = Pattern.Points[PatternIndex];

	RecoilOffset *= (bOwnerIsAiming ? Pattern.ADSMultiplier : 1.0f);

	// Attachment recoil scaling (grips/handguards).
	RecoilOffset.X *= CombinedModifiers.RecoilYawMult;
	RecoilOffset.Y *= CombinedModifiers.RecoilPitchMult;

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

	IExtractionPlayerInterface* OwnerIface = IsValid(GetOwner()) ? Cast<IExtractionPlayerInterface>(GetOwner()) : nullptr;
	if (OwnerIface && OwnerIface->IsInTakedown()) return;

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
	SyncKitVisualItemAmmo(GetOwner(), CurrentAmmo, ReserveAmmo);
}

int32 AWeaponBase::AddReserveAmmo(int32 Amount)
{
	if (!HasAuthority() || Amount <= 0) return 0;

	ReserveAmmo += Amount;
	OnAmmoChanged.Broadcast(CurrentAmmo, ReserveAmmo);
	// A stowed weapon must not stomp the held weapon's kit item counts — the owner has ONE
	// SpawnedItem (the held gun's); equip re-syncs via ResyncVisualAmmo when this slot activates.
	if (!IsHidden())
		SyncKitVisualItemAmmo(GetOwner(), CurrentAmmo, ReserveAmmo);
	return Amount;
}

void AWeaponBase::SetAmmoState(int32 Mag, int32 Reserve)
{
	if (!HasAuthority()) return;

	if (Mag >= 0)
		CurrentAmmo = IsValid(WeaponData) ? FMath::Min(Mag, WeaponData->MagazineSize) : Mag;
	if (Reserve >= 0)
		ReserveAmmo = Reserve;

	OnAmmoChanged.Broadcast(CurrentAmmo, ReserveAmmo);
	if (!IsHidden())
		SyncKitVisualItemAmmo(GetOwner(), CurrentAmmo, ReserveAmmo);
}

void AWeaponBase::ResyncVisualAmmo()
{
	OnAmmoChanged.Broadcast(CurrentAmmo, ReserveAmmo);
	SyncKitVisualItemAmmo(GetOwner(), CurrentAmmo, ReserveAmmo);
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
	SyncKitVisualItemAmmo(GetOwner(), CurrentAmmo, ReserveAmmo);
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

	// Dry trigger press on an empty mag — kick the reload instead of silently no-oping.
	// Lives here as well as the per-shot dispatch: the kit BP may gate its own HitScan
	// dispatches on its mirrored ammo count, but it always signals the trigger press.
	if (bAutoReloadOnEmpty && CurrentAmmo <= 0 && CanReload())
		Reload();
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
		// Dry click once per trigger press — only when truly dry and idle (not mid-reload).
		// Reuses the bDryFireLogged once-per-cycle latch (reset by KitBeginFire / reload finish).
		if (!bDryFireLogged && CurrentAmmo <= 0 && CurrentState == EWeaponState::Idle
			&& IsValid(WeaponData) && IsValid(WeaponData->DryFireSound))
		{
			bDryFireLogged = true;
			UGameplayStatics::PlaySoundAtLocation(GetWorld(), WeaponData->DryFireSound.Get(), GetActorLocation());
		}

		// Dry dispatch on an empty mag — kick the reload instead of silently no-oping.
		if (bAutoReloadOnEmpty && CurrentAmmo <= 0 && CanReload())
			Reload();
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
	UE_LOG(LogExtraction, Verbose, TEXT("[KitWeapon] %s KitInspect — no-op stub"), *GetNameSafe(this));
}

void AWeaponBase::KitMelee_Implementation()
{
	UE_LOG(LogExtraction, Verbose, TEXT("[KitWeapon] %s KitMelee — no-op stub"), *GetNameSafe(this));
}

void AWeaponBase::KitChangeFireMode_Implementation()
{
	UE_LOG(LogExtraction, Verbose, TEXT("[KitWeapon] %s KitChangeFireMode — no-op stub"), *GetNameSafe(this));
}

void AWeaponBase::KitBurstFire_Implementation()
{
	UE_LOG(LogExtraction, Verbose, TEXT("[KitWeapon] %s KitBurstFire — no-op stub"), *GetNameSafe(this));
}

void AWeaponBase::KitFinishFire_Implementation()
{
	bWantsToFire = false;
	if (HasAuthority() && CurrentState == EWeaponState::Firing)
		CurrentState = EWeaponState::Idle;
}

void AWeaponBase::KitTrigger_Implementation()
{
	UE_LOG(LogExtraction, Verbose, TEXT("[KitWeapon] %s KitTrigger — no-op stub"), *GetNameSafe(this));
}

void AWeaponBase::KitSpawnAttachments_Implementation()
{
	UE_LOG(LogExtraction, Verbose, TEXT("[KitWeapon] %s KitSpawnAttachments — no-op stub"), *GetNameSafe(this));
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
	SyncKitVisualItemAmmo(GetOwner(), CurrentAmmo, ReserveAmmo);
}

// ---- AI Damage Mitigation ----

bool AWeaponBase::RollShotDamage(const UDamageMitigationSettings& S, AActor* Target, float Now)
{
	if (!IsValid(WeaponData)) return true;

	const float EffGap = UDamageMitigationSettings::EffectiveResetGap(S, WeaponData->FireRate);

	// Reset ramp when target changes or the gap between shots exceeds the effective threshold.
	if (Target != GateRampTarget.Get() || (Now - GateLastShotTime) > EffGap)
		GateRampStartTime = Now;

	GateRampTarget = Target;
	GateLastShotTime = Now;

	const float TimeOnTarget = Now - GateRampStartTime;
	const float Chance = UDamageMitigationSettings::RampChance01(S, TimeOnTarget);
	return FMath::FRand() < Chance;
}

// ---- Muzzle Flash ----

void AWeaponBase::EnsureMuzzleFlashComponent()
{
	if (IsValid(MuzzleFlashComponent)) return;
	if (!IsValid(GetEffectiveMuzzleFlashFX())) return;

	USkeletalMeshComponent* GripMesh = GetThirdPersonGripMesh();
	if (!IsValid(GripMesh)) return;

	if (!GripMesh->DoesSocketExist(WeaponConstants::MuzzleSocketName))
		UE_LOG(LogExtraction, Warning, TEXT("'%s': grip mesh '%s' lacks socket '%s' — muzzle flash will attach at origin"),
			*GetNameSafe(this), *GetNameSafe(GripMesh->GetSkeletalMeshAsset()), *WeaponConstants::MuzzleSocketName.ToString());

	MuzzleFlashComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(
		GetEffectiveMuzzleFlashFX(),
		GripMesh,
		WeaponConstants::MuzzleSocketName,
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		EAttachLocation::SnapToTarget,
		false,  // bAutoDestroy
		false); // bAutoActivate

	if (IsValid(MuzzleFlashComponent))
		MuzzleFlashComponent->SetOwnerNoSee(GripMesh->bOwnerNoSee);
}

void AWeaponBase::SetFirstPersonMuzzle(USceneComponent* InMuzzle)
{
	if (FirstPersonMuzzle == InMuzzle) return;

	FirstPersonMuzzle = InMuzzle;

	// Anchor changed (weapon swap) or cleared (unequip) — drop the old component; it will be
	// lazily rebuilt on the new anchor by the next shot.
	if (IsValid(FirstPersonMuzzleFlashComponent))
	{
		FirstPersonMuzzleFlashComponent->DestroyComponent();
		FirstPersonMuzzleFlashComponent = nullptr;
	}
}

void AWeaponBase::EnsureFirstPersonMuzzleFlashComponent()
{
	if (IsValid(FirstPersonMuzzleFlashComponent)) return;
	if (!IsValid(FirstPersonMuzzle)) return;
	if (!IsValid(WeaponData) || !IsValid(GetEffectiveMuzzleFlashFX())) return;
	if (GetNetMode() == NM_DedicatedServer) return;

	// Only the owning player's screen shows the FP gun — gate on local control rather than
	// SetOnlyOwnerSee, which silently hides the flash if the kit item's Owner chain doesn't
	// reach the pawn. Remote clients never receive the anchor, so this only filters the server
	// copy on a listen host.
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!IsValid(OwnerPawn) || !OwnerPawn->IsLocallyControlled()) return;

	FirstPersonMuzzleFlashComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(
		GetEffectiveMuzzleFlashFX(),
		FirstPersonMuzzle,
		NAME_None,
		FVector::ZeroVector,
		WeaponData->FirstPersonMuzzleFlashRotation,
		EAttachLocation::KeepRelativeOffset,
		false,  // bAutoDestroy
		false); // bAutoActivate

}

void AWeaponBase::SpawnTracer(const FVector& MuzzleLocation, const FVector& EndPoint)
{
	if (!IsValid(WeaponData)) return;
	if (!IsValid(WeaponData->TracerFX)) return;

	// Skip degenerate segments (e.g. point-blank melee range).
	static constexpr float MinTracerLengthSq = 100.f; // 10 cm squared
	if (FVector::DistSquared(MuzzleLocation, EndPoint) < MinTracerLengthSq) return;

	UWorld* World = GetWorld();
	if (!IsValid(World)) return;

	const FRotator TracerRotation = (EndPoint - MuzzleLocation).Rotation();

	UNiagaraComponent* TracerComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		World,
		WeaponData->TracerFX,
		MuzzleLocation,
		TracerRotation,
		FVector(1.f),
		true,  // bAutoDestroy
		true,  // bAutoActivate
		ENCPoolMethod::AutoRelease);

	if (!IsValid(TracerComp)) return;
	if (!WeaponData->TracerEndParamName.IsNone())
		TracerComp->SetVectorParameter(WeaponData->TracerEndParamName, EndPoint);

	// Lyra NS_WeaponFire_Tracer contract: MuzzlePosition (Position) + ImpactPositions
	// (Vector-array data interface) + Trigger gate. Trigger's authored type isn't
	// introspectable here, so both bool and int are set — a type-mismatched user-param
	// set is a silent no-op, which also makes this whole block harmless on systems
	// that don't expose these names.
	static const FName TracerMuzzleParam(TEXT("MuzzlePosition"));
	static const FName TracerImpactsParam(TEXT("ImpactPositions"));
	static const FName TracerTriggerParam(TEXT("Trigger"));
	TracerComp->SetVariablePosition(TracerMuzzleParam, MuzzleLocation);
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(TracerComp, TracerImpactsParam, { EndPoint });
	TracerComp->SetVariableBool(TracerTriggerParam, true);
	TracerComp->SetVariableInt(TracerTriggerParam, 1);
}

// ---- Reload audio ----

void AWeaponBase::StartReloadAudio()
{
	if (!IsValid(WeaponData)) return;

	// Empty-mag reload gets the bolt/slide-release variant; tactical (or no variant) uses the base sound.
	USoundBase* Sound = (CurrentAmmo == 0 && IsValid(WeaponData->ReloadEmptySound))
		? WeaponData->ReloadEmptySound.Get()
		: WeaponData->ReloadSound.Get();
	if (!IsValid(Sound)) return;

	StopReloadAudio();
	ReloadAudioComponent = UGameplayStatics::SpawnSoundAttached(Sound, GetRootComponent());
}

void AWeaponBase::StopReloadAudio()
{
	if (!IsValid(ReloadAudioComponent)) { ReloadAudioComponent = nullptr; return; }

	// Short fade instead of a hard cut so an interrupted reload doesn't click.
	ReloadAudioComponent->FadeOut(0.1f, 0.f);
	ReloadAudioComponent = nullptr;
}
