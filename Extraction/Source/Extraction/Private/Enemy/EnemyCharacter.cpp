// AEnemyCharacter — single character class for all enemy archetypes.

#include "EnemyCharacter.h"
#include "EnemyAnimInstance.h"
#include "AI/AITargetingStatics.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AIPerceptionSystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EnemyAIController.h"
#include "BrainComponent.h"
#include "EnemyAwarenessComponent.h"
#include "EnemyArchetypeData.h"
#include "EnemyDirectorSubsystem.h"
#include "PatrolRoute.h"
#include "HealthComponent.h"
#include "FootstepNoiseComponent.h"
#include "WeaponBase.h"
#include "WeaponDataAsset.h"
#include "ExtractionDamageType.h"
#include "ExtractionTypes.h"
#include "EnemyArmourComponent.h"
#include "EnemyGrenadierComponent.h"
#include "SquadAuraComponent.h"
#include "EnemySniperTelegraphComponent.h"
#include "SuppressionComponent.h"
#include "EnemyMoraleComponent.h"
#include "CoverPoseComponent.h"
#include "EnemyPostureComponent.h"
#include "EnemySquadSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "UI/OverheadWidgetComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/DamageEvents.h"
#include "TimerManager.h"
#include "EnemyAwarenessWidget.h"
#include "Data/AmmoDropTableDataAsset.h"
#include "World/AmmoPickup.h"
#include "World/LootPickup.h"
#include "Companion/CompanionCharacter.h"
#include "Companion/CompanionBarkTypes.h"
#include "BarkSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

static TAutoConsoleVariable<int32> CVarEnemyPersistCorpses(
	TEXT("enemy.PersistCorpses"), 1,
	TEXT("If non-zero (default), dead enemies persist as discoverable corpses (director-managed). If 0, they disappear after a short delay."),
	ECVF_Default);

static constexpr float CrouchSpeedFraction = 0.5f; // MaxWalkSpeedCrouched = mode speed * this

AEnemyCharacter::AEnemyCharacter()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetNetUpdateFrequency(10.f);
	SetMinNetUpdateFrequency(5.f);

	HealthComponent = CreateDefaultSubobject<UHealthComponent>(TEXT("HealthComponent"));
	FootstepAudioComponent = CreateDefaultSubobject<UFootstepNoiseComponent>(TEXT("FootstepAudioComponent"));
	FootstepAudioComponent->SetEmitAINoise(false); // enemies must not feed the hearing sense with their own steps
	SuppressionComponent = CreateDefaultSubobject<USuppressionComponent>(TEXT("SuppressionComponent"));
	MoraleComponent = CreateDefaultSubobject<UEnemyMoraleComponent>(TEXT("MoraleComponent"));
	CoverPoseComponent = CreateDefaultSubobject<UCoverPoseComponent>(TEXT("CoverPoseComponent"));
	PostureComponent = CreateDefaultSubobject<UEnemyPostureComponent>(TEXT("PostureComponent"));

	AIControllerClass = AEnemyAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	// Bug 8: enable crouching for NavAgent so Crouch()/UnCrouch() works.
	GetCharacterMovement()->GetNavAgentPropertiesRef().bCanCrouch = true;

	// Bug 6: weapon hitscan traces ECC_Visibility, but the inherited CharacterMesh profile ignores it,
	// so player/companion shots passed straight through. Block Visibility on the mesh so hits register
	// (the trace returns the struck bone, so hit-region multipliers resolve correctly).
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		MeshComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		// Throttle anim eval for offscreen/distant enemies. Head-cone sight and fire sockets read
		// component-relative transforms, so the reduced-rate pose is accurate enough while unseen.
		MeshComp->bEnableUpdateRateOptimizations = true;
	}

	OwnedTags.AddTag(TAG_Character_Enemy);

	static constexpr float AwarenessWidgetOffsetZ = 30.f;
	static constexpr float AwarenessWidgetSize     = 76.f;

	AwarenessWidgetComponent = CreateDefaultSubobject<UOverheadWidgetComponent>(TEXT("AwarenessWidget"));
	AwarenessWidgetComponent->SetupAttachment(GetMesh(), TEXT("head"));
	AwarenessWidgetComponent->SetRelativeLocation(FVector(0.f, 0.f, AwarenessWidgetOffsetZ));
	AwarenessWidgetComponent->SetWidgetSpace(EWidgetSpace::Screen);
	AwarenessWidgetComponent->SetDrawSize(FVector2D(AwarenessWidgetSize, AwarenessWidgetSize));
	AwarenessWidgetComponent->SetPivot(FVector2D(0.5f, 0.5f));
	AwarenessWidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AwarenessWidgetComponent->SetTwoSided(false);
	AwarenessWidgetComponent->SetVisibility(false);

	// Default mannequin bone-to-region map
	BoneToHitRegionMap.Reserve(25);
	BoneToHitRegionMap.Add(FName("head"),       EHitRegion::Head);
	// Neck counts as head: grows the headshot region downward so near-miss-low shots still
	// register. Incoming-damage regions only — the enemy PERCEPTION body ladder (AITargeting)
	// keeps its own neck_01 entry and is untouched.
	BoneToHitRegionMap.Add(FName("neck_01"),    EHitRegion::Head);
	BoneToHitRegionMap.Add(FName("neck_02"),    EHitRegion::Head);
	BoneToHitRegionMap.Add(FName("spine_01"),   EHitRegion::Torso);
	BoneToHitRegionMap.Add(FName("spine_02"),   EHitRegion::Torso);
	BoneToHitRegionMap.Add(FName("spine_03"),   EHitRegion::Torso);
	BoneToHitRegionMap.Add(FName("spine_04"),   EHitRegion::Torso);
	BoneToHitRegionMap.Add(FName("spine_05"),   EHitRegion::Torso);
	BoneToHitRegionMap.Add(FName("pelvis"),     EHitRegion::Torso);
	BoneToHitRegionMap.Add(FName("clavicle_l"), EHitRegion::Arms);
	BoneToHitRegionMap.Add(FName("upperarm_l"), EHitRegion::Arms);
	BoneToHitRegionMap.Add(FName("lowerarm_l"), EHitRegion::Arms);
	BoneToHitRegionMap.Add(FName("hand_l"),     EHitRegion::Arms);
	BoneToHitRegionMap.Add(FName("clavicle_r"), EHitRegion::Arms);
	BoneToHitRegionMap.Add(FName("upperarm_r"), EHitRegion::Arms);
	BoneToHitRegionMap.Add(FName("lowerarm_r"), EHitRegion::Arms);
	BoneToHitRegionMap.Add(FName("hand_r"),     EHitRegion::Arms);
	BoneToHitRegionMap.Add(FName("thigh_l"),    EHitRegion::Legs);
	BoneToHitRegionMap.Add(FName("calf_l"),     EHitRegion::Legs);
	BoneToHitRegionMap.Add(FName("foot_l"),     EHitRegion::Legs);
	BoneToHitRegionMap.Add(FName("thigh_r"),    EHitRegion::Legs);
	BoneToHitRegionMap.Add(FName("calf_r"),     EHitRegion::Legs);
	BoneToHitRegionMap.Add(FName("foot_r"),     EHitRegion::Legs);
	BoneToHitRegionMap.Add(FName("ball_l"),     EHitRegion::Legs);
	BoneToHitRegionMap.Add(FName("ball_r"),     EHitRegion::Legs);
}

void AEnemyCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// Capture guard-post here — runs before BeginPlay and before OnPossess, so BB_PostLocation
	// written in OnPossess will always see the correct values. Valid for placed actors.
	if (HasAuthority())
	{
		InitialPostLocation = GetActorLocation();
		InitialPostYaw = GetActorRotation().Yaw;
	}
}

bool AEnemyCharacter::CanCrouch() const
{
	// Debug stand-and-shoot: never crouch — the enemy holds its standing spot and fires from there.
	if (bDebugStandAndShoot) return false;
	return Super::CanCrouch();
}

void AEnemyCharacter::GetActorEyesViewPoint(FVector& OutLocation, FRotator& OutRotation) const
{
	// Lazy re-resolve: deferred anim init may have returned null at BeginPlay.
	if (bUseHeadDrivenSightCone && !CachedAnimInstance.IsValid())
	{
		if (USkeletalMeshComponent* M = GetMesh())
			CachedAnimInstance = Cast<UEnemyAnimInstance>(M->GetAnimInstance());

		if (!CachedAnimInstance.IsValid() && !bLoggedMissingSightAnimInstance)
		{
			bLoggedMissingSightAnimInstance = true;
			UE_LOG(LogTemp, Warning,
				TEXT("AEnemyCharacter [%s]: bUseHeadDrivenSightCone=true but UEnemyAnimInstance is null — falling back to capsule origin."),
				*GetName());
		}
	}

	const bool bInCombat = CachedAnimInstance.IsValid() && CachedAnimInstance->IsInCombat();
	const bool bAlive = IsValid(HealthComponent) && HealthComponent->IsAlive();

	if (bUseHeadDrivenSightCone && bAlive && !bInCombat)
	{
		const USkeletalMeshComponent* MeshComp = GetMesh();
		if (IsValid(MeshComp) && MeshComp->DoesSocketExist(SightHeadBoneName))
		{
			const FTransform T = MeshComp->GetSocketTransform(SightHeadBoneName, RTS_World);
			OutLocation = T.GetLocation();
			const FVector LocalAxis = HeadSightForwardAxis.GetSafeNormal();
			if (!LocalAxis.IsNearlyZero())
			{
				OutRotation = T.GetRotation().RotateVector(LocalAxis).Rotation();
				return;
			}
		}
	}

	Super::GetActorEyesViewPoint(OutLocation, OutRotation);
}

void AEnemyCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Belt-and-suspenders: clear any starting crouch when the debug flag is set pre-play.
	if (bDebugStandAndShoot) UnCrouch();

	if (IsValid(HealthComponent))
		HealthComponent->OnDeath.AddUniqueDynamic(this, &AEnemyCharacter::HandleDeath);

	if (AwarenessWidgetClass && IsValid(AwarenessWidgetComponent))
	{
		AwarenessWidgetComponent->SetWidgetClass(AwarenessWidgetClass);
		AwarenessWidgetComponent->SetVisibility(true);
		TryLinkAwarenessWidget();
	}

	// Cache chest bone availability once — used per CanBeSeenFrom call.
	static const FName ChestBoneName(TEXT("spine_03"));
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		bCachedHasChestBone = MeshComp->DoesSocketExist(ChestBoneName);
		CachedAnimInstance = Cast<UEnemyAnimInstance>(MeshComp->GetAnimInstance());
	}

	if (!IsValid(ArchetypeData))
	{
		UE_LOG(LogEnemyAI, Error, TEXT("%s: ArchetypeData not assigned — character will idle harmlessly."), *GetName());
		return;
	}

	// Phase 5: register with the squad subsystem (tolerates non-game worlds where subsystem is null).
	if (UWorld* W = GetWorld())
	{
		if (UEnemySquadSubsystem* SquadSys = W->GetSubsystem<UEnemySquadSubsystem>())
			SquadSys->RegisterMember(this, SquadId);
	}

	// Spawn weapon on authority only
	if (!HasAuthority()) return;
	if (!ArchetypeData->WeaponClass) return;

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.Instigator = this;

	AWeaponBase* Weapon = GetWorld()->SpawnActor<AWeaponBase>(ArchetypeData->WeaponClass, Params);
	if (!IsValid(Weapon)) return;

	CurrentWeapon = Weapon;

	const FName AttachSocket = WeaponSocket;
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (IsValid(MeshComp) && MeshComp->DoesSocketExist(AttachSocket))
	{
		Weapon->AttachToComponent(MeshComp,
			FAttachmentTransformRules::SnapToTargetNotIncludingScale, AttachSocket);
	}
	else
	{
		Weapon->AttachToComponent(GetCapsuleComponent(),
			FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	}

	Weapon->InitializeAmmo();
	// Enemies have no BT reload task — auto-reload regardless of the weapon asset config.
	Weapon->SetAutoReloadOnEmpty(true);
}

void AEnemyCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// CurrentWeapon is a separate owned/attached actor — UE does not auto-destroy it
	// when this character is destroyed. Destroy server-side so removal replicates.
	// The corpse weapon pickup (SetupCorpseWeaponPickup) rides the weapon actor and is not
	// auto-destroyed with it either — take it down first so no orphaned interactable lingers.
	if (HasAuthority())
	{
		if (AWeaponBase* Weapon = CurrentWeapon.Get())
		{
			TArray<AActor*> Attached;
			Weapon->GetAttachedActors(Attached);
			for (AActor* AttachedActor : Attached)
			{
				if (IsValid(AttachedActor))
					AttachedActor->Destroy();
			}
			Weapon->Destroy();
		}
		CurrentWeapon = nullptr;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AwarenessWidgetLinkTimerHandle);
		World->GetTimerManager().ClearAllTimersForObject(this);

		if (UEnemySquadSubsystem* SquadSys = World->GetSubsystem<UEnemySquadSubsystem>())
			SquadSys->UnregisterMember(this);
	}

	Super::EndPlay(EndPlayReason);
}

void AEnemyCharacter::TryLinkAwarenessWidget()
{
	if (!IsValid(AwarenessWidgetComponent)) return;

	UEnemyAwarenessWidget* W = Cast<UEnemyAwarenessWidget>(AwarenessWidgetComponent->GetUserWidgetObject());
	if (IsValid(W))
	{
		W->SetEnemy(this);
		GetWorldTimerManager().ClearTimer(AwarenessWidgetLinkTimerHandle);
		UE_LOG(LogTemp, Log, TEXT("[AwarenessMeter] linked widget on %s"), *GetName());
		return;
	}

	++AwarenessWidgetLinkAttempts;
	if (AwarenessWidgetLinkAttempts > MaxAwarenessLinkAttempts)
	{
		GetWorldTimerManager().ClearTimer(AwarenessWidgetLinkTimerHandle);
		UE_LOG(LogTemp, Warning, TEXT("[AwarenessMeter] gave up linking widget on %s (GetUserWidgetObject null)"), *GetName());
		return;
	}

	if (!AwarenessWidgetLinkTimerHandle.IsValid())
	{
		static constexpr float LinkRetryInterval = 0.1f;
		GetWorldTimerManager().SetTimer(AwarenessWidgetLinkTimerHandle, this,
			&AEnemyCharacter::TryLinkAwarenessWidget, LinkRetryInterval, true);
	}
}

UEnemySquad* AEnemyCharacter::GetSquad() const
{
	const UWorld* World = GetWorld();
	if (!World) return nullptr;

	UEnemySquadSubsystem* SquadSys = World->GetSubsystem<UEnemySquadSubsystem>();
	if (!IsValid(SquadSys)) return nullptr;

	return SquadSys->GetSquadFor(this);
}

// --- IGameplayTagAssetInterface ---

void AEnemyCharacter::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
{
	TagContainer.AppendTags(OwnedTags);
}

// --- IAISightTargetInterface ---

UAISense_Sight::EVisibilityResult AEnemyCharacter::CanBeSeenFrom(
	const FCanBeSeenFromContext& Context,
	FVector& OutSeenLocation,
	int32& OutNumberOfLoSChecksPerformed,
	int32& OutNumberOfAsyncLosCheckRequested,
	float& OutSightStrength,
	int32* UserData,
	const FOnPendingVisibilityQueryProcessedDelegate* Delegate)
{
	OutNumberOfAsyncLosCheckRequested = 0;
	OutNumberOfLoSChecksPerformed = 0;
	OutSightStrength = 0.f;

	UWorld* World = GetWorld();
	if (!World) return UAISense_Sight::EVisibilityResult::NotVisible;

	// Dead enemies flip to NoTeam (neutral) so allied enemy perception can discover corpses
	// for the body-sighting path (HandleBodySighted). Combat targeting is gated separately:
	// HandleSightStimulus bails on dead actors, and the companion's BTService_UpdateCompanionState
	// explicitly filters IsDead() out of target selection. Allow the LOS traces to run for
	// corpses so body discovery actually fires.

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(EnemySightTarget), true);
	QueryParams.AddIgnoredActor(this);
	if (IsValid(Context.IgnoreActor)) QueryParams.AddIgnoredActor(Context.IgnoreActor);

	// Test candidate points — head clears cover first when enemy stands up from crouch.
	// Chest provides a backup visible point when the head is still occluded.
	static const FName ChestBoneName(TEXT("spine_03"));
	static constexpr float ChestFallbackOffsetZ = 40.f;

	const USkeletalMeshComponent* EnemyMesh = GetMesh();

	const FVector HeadLoc  = AITargeting::GetSightLocation(this); // head bone → eye → centre
	const FVector ChestLoc = (IsValid(EnemyMesh) && bCachedHasChestBone)
		? EnemyMesh->GetSocketLocation(ChestBoneName)
		: GetActorLocation() + FVector(0.f, 0.f, ChestFallbackOffsetZ);
	const FVector CentreLoc = GetActorLocation();

	const FVector CandidatePoints[] = { HeadLoc, ChestLoc, CentreLoc };

	for (const FVector& Candidate : CandidatePoints)
	{
		FHitResult Hit;
		++OutNumberOfLoSChecksPerformed;
		const bool bBlocked = World->LineTraceSingleByChannel(
			Hit, Context.ObserverLocation, Candidate, ECC_Visibility, QueryParams);
		if (!bBlocked || Hit.GetActor() == this)
		{
			OutSeenLocation = Candidate;
			OutSightStrength = 1.f;
			return UAISense_Sight::EVisibilityResult::Visible;
		}
	}

	return UAISense_Sight::EVisibilityResult::NotVisible;
}

// --- IGenericTeamAgentInterface ---

FGenericTeamId AEnemyCharacter::GetGenericTeamId() const
{
	// Corpses go team-neutral so enemy sight (hostiles + neutrals) can perceive dead allies for
	// body discovery — live allies stay friendly and are filtered out of perception entirely.
	if (IsValid(HealthComponent) && HealthComponent->IsDead())
		return FGenericTeamId::NoTeam;

	// Prefer the controller's team (set by AEnemyAIController) so it's the single source of truth.
	if (const AController* C = GetController())
	{
		if (const IGenericTeamAgentInterface* TeamAgent = Cast<IGenericTeamAgentInterface>(C))
			return TeamAgent->GetGenericTeamId();
	}
	return FGenericTeamId(1);
}

// --- IAIShooterInterface ---

AActor* AEnemyCharacter::GetAIAimTarget() const
{
	AActor* Target = CurrentAimTarget.Get();
	if (!IsValid(Target)) return nullptr;

	// MaxAimYawDeg: when > 0, suppress aim until the body has rotated within the arc.
	// This prevents a heavy from shooting sideways while its slow turn catches up.
	if (IsValid(ArchetypeData) && ArchetypeData->MaxAimYawDeg > 0.f)
	{
		const FVector ToTarget = (Target->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
		const float YawDelta = FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(GetActorForwardVector().GetSafeNormal2D(), ToTarget)));
		if (YawDelta > ArchetypeData->MaxAimYawDeg) return nullptr;
	}

	return Target;
}

float AEnemyCharacter::GetAIAimSpreadDegrees() const
{
	if (!IsValid(ArchetypeData)) return 0.f;

	const float TimeAiming = (GetWorld() && AimStartWorldTime > 0.f)
		? GetWorld()->GetTimeSeconds() - AimStartWorldTime
		: 0.f;

	const float SettleAlpha = (ArchetypeData->SpreadSettleTime > 0.f)
		? FMath::Clamp(TimeAiming / ArchetypeData->SpreadSettleTime, 0.f, 1.f)
		: 1.f;

	float Spread = FMath::Lerp(ArchetypeData->SpreadStartDeg, ArchetypeData->SpreadSettledDeg, SettleAlpha);

	// Widen spread when the target is moving fast
	if (CurrentAimTarget.IsValid())
	{
		if (const APawn* TargetPawn = Cast<APawn>(CurrentAimTarget.Get()))
		{
			if (TargetPawn->GetVelocity().Size() > ArchetypeData->MovingTargetSpeedThreshold)
				Spread += ArchetypeData->SpreadWidenMovingTarget;
		}
	}

	// Phase 4: suppression widens spread before the command multiplier
	if (IsValid(SuppressionComponent))
		Spread += ArchetypeData->SuppressionSpreadPenaltyDeg * SuppressionComponent->GetSuppression01();

	// Mercy spread: widen when targeting a companion that is mid-revive or committed to a rescue
	// (sprinting to the body) — the approach has to be survivable, not just the hold.
	if (const ACompanionCharacter* TargetCompanion = Cast<ACompanionCharacter>(CurrentAimTarget.Get()))
	{
		if (TargetCompanion->IsRevivingPlayer() || TargetCompanion->IsRescueCommitted())
			Spread += ArchetypeData->RevivingCompanionExtraSpreadDeg;
	}

	// Phase 3: squad aura narrows spread; extra spread from BT tasks widens it.
	Spread *= CommandSpreadMultiplier;
	Spread += ExtraSpreadDegrees;

	return FMath::Max(Spread, 0.f);
}

bool AEnemyCharacter::GetAIAimLocation(FVector& OutLocation) const
{
	if (!bHasAimLocationOverride) return false;

	// MaxAimYawDeg: suppress the override aim too until the body has turned far enough.
	if (IsValid(ArchetypeData) && ArchetypeData->MaxAimYawDeg > 0.f)
	{
		const FVector ToOverride = (AimLocationOverride - GetActorLocation()).GetSafeNormal2D();
		const float YawDelta = FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(GetActorForwardVector().GetSafeNormal2D(), ToOverride)));
		if (YawDelta > ArchetypeData->MaxAimYawDeg) return false;
	}

	OutLocation = AimLocationOverride;
	return true;
}

FVector AEnemyCharacter::GetAimPointForTarget(const AActor* Target) const
{
	if (!IsValid(Target)) return FVector::ZeroVector;

	FVector BodyPoint;
	const bool bAllowHead = AITargeting::ShouldIncludeHeadForObserver(this, Target);
	if (AITargeting::GetVisibleBodyPoint(Target, GetPawnViewLocation(), this, BodyPoint, bAllowHead))
		return BodyPoint;

	// Nothing visible (fire-gate won't be open anyway) — aim toward centre mass.
	// Snipers may aim at a standing target's head when body is fully occluded (sniper+standing exception).
	return Target->GetActorLocation();
}

// --- Aim API ---

void AEnemyCharacter::SetAimTarget(AActor* NewTarget)
{
	const AActor* OldTarget = CurrentAimTarget.Get();
	if (OldTarget == NewTarget) return;

	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.f;

	if (!IsValid(NewTarget))
	{
		// Clearing aim — record the old target and the current settle time for grace restore.
		LastSettleTarget = const_cast<AActor*>(OldTarget);
		LastAimClearWorldTime = Now;
		SavedAimStartWorldTime = AimStartWorldTime;
		CurrentAimTarget = nullptr;
		AimStartWorldTime = -1e9f;
		return;
	}

	// Acquiring a new target. Check if it matches the grace window.
	if (NewTarget == LastSettleTarget.Get() && (Now - LastAimClearWorldTime) < ReAimSettleGrace)
	{
		// Same target re-acquired within grace — restore prior settle progress.
		CurrentAimTarget = NewTarget;
		AimStartWorldTime = SavedAimStartWorldTime;
	}
	else
	{
		// Genuine new target — reset settle.
		CurrentAimTarget = NewTarget;
		AimStartWorldTime = Now;
	}

	LastSettleTarget = nullptr;
}

void AEnemyCharacter::SetAimLocationOverride(FVector Location)
{
	bHasAimLocationOverride = true;
	AimLocationOverride = Location;
}

void AEnemyCharacter::ClearAimLocationOverride()
{
	bHasAimLocationOverride = false;
}

void AEnemyCharacter::SetCommandSpreadMultiplier(float Multiplier)
{
	CommandSpreadMultiplier = FMath::Max(Multiplier, 0.f);
}

void AEnemyCharacter::SetExtraSpreadDegrees(float Degrees)
{
	ExtraSpreadDegrees = FMath::Max(Degrees, 0.f);
}

// --- Melee ---

bool AEnemyCharacter::PerformMelee(AActor* Target)
{
	if (!IsValid(Target) || !IsValid(ArchetypeData)) return false;
	if (!ArchetypeData->bCanMelee) return false;

	// Range check
	const float DistSq = FVector::DistSquared(GetActorLocation(), Target->GetActorLocation());
	if (DistSq > FMath::Square(ArchetypeData->MeleeRange)) return false;

	// Cooldown check
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (Now - LastMeleeWorldTime < ArchetypeData->MeleeCooldown) return false;

	LastMeleeWorldTime = Now;

	// Commit the swing: capture the target and play the montage. Damage is applied at the
	// contact frame by UAnimNotify_EnemyMeleeHit → ApplyMeleeDamage(), not here.
	PendingMeleeTarget = Target;
	OnMeleePerformed.Broadcast();
	return true;
}

void AEnemyCharacter::ApplyMeleeDamage()
{
	if (!IsValid(ArchetypeData)) return;

	AActor* Target = PendingMeleeTarget.Get();
	PendingMeleeTarget = nullptr;
	if (!IsValid(Target)) return;

	// A dead enemy lands no hit (the death montage can overlap the upper-body melee slot).
	if (IsValid(HealthComponent) && !HealthComponent->IsAlive()) return;

	FDamageEvent MeleeDmgEvent;
	Target->TakeDamage(ArchetypeData->MeleeDamage, MeleeDmgEvent, GetController(), this);
}

void AEnemyCharacter::ReleaseGrenade()
{
	if (!IsValid(GrenadierComponent)) return;
	GrenadierComponent->ReleaseGrenade();
}

// --- Guard Post ---

FVector AEnemyCharacter::GetGuardPostLocation() const
{
	// GuardPostOverride is stored in actor-local space (MakeEditWidget convention, mirrors APatrolRoute::Points).
	// TransformPosition converts to world space the same way APatrolRoute::GetWorldPoint does.
	if (bOverrideGuardPost) return GetActorTransform().TransformPosition(GuardPostOverride);
	return InitialPostLocation;
}

// --- Move Speed ---

void AEnemyCharacter::SetMoveSpeedMode(EEnemyMoveSpeedMode Mode)
{
	if (!IsValid(ArchetypeData)) return;

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!IsValid(MoveComp)) return;

	switch (Mode)
	{
	case EEnemyMoveSpeedMode::Combat:
		MoveComp->MaxWalkSpeed = ArchetypeData->CombatSpeed;
		MoveComp->MaxWalkSpeedCrouched = ArchetypeData->CombatSpeed * CrouchSpeedFraction;
		break;
	case EEnemyMoveSpeedMode::Strafe:
		MoveComp->MaxWalkSpeed = ArchetypeData->StrafeWalkSpeed;
		MoveComp->MaxWalkSpeedCrouched = ArchetypeData->StrafeWalkSpeed * CrouchSpeedFraction;
		break;
	case EEnemyMoveSpeedMode::Patrol:
		MoveComp->MaxWalkSpeed = ArchetypeData->PatrolSpeed;
		MoveComp->MaxWalkSpeedCrouched = ArchetypeData->PatrolSpeed * CrouchSpeedFraction;
		break;
	}
}

// --- Hand-swap ---

void AEnemyCharacter::SetWeaponHandSocket(bool bUsePatrolHand, bool bImmediate)
{
	if (!HasAuthority()) return;
	if (bUsePatrolHand == bWeaponOnPatrolHand) return;

	AWeaponBase* Weapon = CurrentWeapon.Get();
	if (!IsValid(Weapon)) return;

	const UWeaponDataAsset* DA = Weapon->GetWeaponData();
	if (!IsValid(DA)) return;

	const FName PatrolSock = DA->EnemyPatrolHandSocket;
	if (PatrolSock.IsNone()) return;

	const FName CombatSock = DA->EnemyCombatHandSocket.IsNone() ? WeaponSocket : DA->EnemyCombatHandSocket;
	const FName TargetSocket = bUsePatrolHand ? PatrolSock : CombatSock;

	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!IsValid(MeshComp) || !MeshComp->DoesSocketExist(TargetSocket))
	{
		UE_LOG(LogEnemyAI, Warning, TEXT("%s: SetWeaponHandSocket — socket '%s' not found on mesh"),
			*GetName(), *TargetSocket.ToString());
		return;
	}

	if (bImmediate)
	{
		// Hard seat: snap directly onto the socket with identity relative (no glide).
		// Used by the firing override and death path where visual continuity is less
		// important than correctness (gun must be on the right socket NOW).
		Weapon->AttachToComponent(MeshComp, FAttachmentTransformRules::SnapToTargetNotIncludingScale, TargetSocket);
		Weapon->ResetHandSwapSettle();
	}
	else
	{
		// Smooth path: keep world transform so there is no visual pop on the swap frame,
		// then ease to the seated rest via BeginHandSwapSettle.
		Weapon->AttachToComponent(MeshComp, FAttachmentTransformRules::KeepWorldTransform, TargetSocket);
		Weapon->BeginHandSwapSettle();
	}

	bWeaponOnPatrolHand = bUsePatrolHand;
}

// --- Archetype ---

void AEnemyCharacter::ApplyArchetypeData()
{
	if (!IsValid(ArchetypeData))
	{
		UE_LOG(LogEnemyAI, Error, TEXT("%s: ApplyArchetypeData called with no ArchetypeData."), *GetName());
		return;
	}

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (IsValid(MoveComp))
	{
		MoveComp->MaxWalkSpeed = ArchetypeData->PatrolSpeed;

		// Phase 3: heavy turn-rate clamp. Controller yaw must be disabled so the movement component's
		// bUseControllerDesiredRotation path (which honours RotationRate) drives the body turn instead
		// of the controller snapping rotation directly each frame.
		if (ArchetypeData->TurnRateDegPerSec > 0.f)
		{
			bUseControllerRotationYaw = false;
			MoveComp->bOrientRotationToMovement = false;
			MoveComp->bUseControllerDesiredRotation = true;
			MoveComp->RotationRate = FRotator(0.f, ArchetypeData->TurnRateDegPerSec, 0.f);
		}
	}

	if (IsValid(HealthComponent))
		HealthComponent->InitializeHealth(ArchetypeData->MaxHealth, ArchetypeData->MaxShield);

	if (IsValid(SuppressionComponent))
		SuppressionComponent->ConfigureSuppression(ArchetypeData->SuppressionResistance);

	if (IsValid(MoraleComponent))
		MoraleComponent->InitFromArchetype(ArchetypeData);

	// Phase 3: conditionally add bolt-on components. Guards prevent duplication on re-possess.

	if (ArchetypeData->bHasArmour && !ArmourComponent)
	{
		ArmourComponent = NewObject<UEnemyArmourComponent>(this, UEnemyArmourComponent::StaticClass());
		ArmourComponent->RegisterComponent();
		ArmourComponent->InitFromArchetype(ArchetypeData);
	}

	if (ArchetypeData->bIsGrenadier && !GrenadierComponent)
	{
		GrenadierComponent = NewObject<UEnemyGrenadierComponent>(this, UEnemyGrenadierComponent::StaticClass());
		GrenadierComponent->RegisterComponent();
		GrenadierComponent->InitFromArchetype(ArchetypeData);

		// Forward the grenadier's telegraph/cancel events through this actor so the
		// anim instance has a single binding source (mirrors OnMeleePerformed pattern).
		GrenadierComponent->OnGrenadeTelegraph.AddDynamic(this, &AEnemyCharacter::HandleGrenadeTelegraph);
		GrenadierComponent->OnGrenadeCancelled.AddDynamic(this, &AEnemyCharacter::HandleGrenadeCancelled);
	}

	if (ArchetypeData->bHasCommandAura && !SquadAuraComp)
	{
		SquadAuraComp = NewObject<USquadAuraComponent>(this, USquadAuraComponent::StaticClass());
		SquadAuraComp->RegisterComponent();
		SquadAuraComp->InitFromArchetype(ArchetypeData);
	}

	if (ArchetypeData->bIsSniper && !SniperTelegraphComp)
	{
		SniperTelegraphComp = NewObject<UEnemySniperTelegraphComponent>(this, UEnemySniperTelegraphComponent::StaticClass());
		SniperTelegraphComp->RegisterComponent();
		SniperTelegraphComp->InitFromArchetype(ArchetypeData);
	}

	OnBoltOnComponentsReady();
}

// --- TakeDamage ---

EHitRegion AEnemyCharacter::ResolveHitRegion(const FDamageEvent& DamageEvent) const
{
	if (!DamageEvent.IsOfType(FPointDamageEvent::ClassID)) return EHitRegion::Torso;

	const FPointDamageEvent& PointDamage = static_cast<const FPointDamageEvent&>(DamageEvent);
	const EHitRegion* Region = BoneToHitRegionMap.Find(PointDamage.HitInfo.BoneName);
	return Region ? *Region : EHitRegion::Torso;
}

float AEnemyCharacter::GetHitboxDamageMultiplier(const FDamageEvent& DamageEvent) const
{
	return GetHitboxDamageMultiplier(DamageEvent, ResolveHitRegion(DamageEvent));
}

float AEnemyCharacter::GetHitboxDamageMultiplier(const FDamageEvent& DamageEvent, EHitRegion Region) const
{
	if (!DamageEvent.IsOfType(FPointDamageEvent::ClassID)) return 1.f;

	const FPointDamageEvent& PointDamage = static_cast<const FPointDamageEvent&>(DamageEvent);
	if (!PointDamage.DamageTypeClass) return 1.f;

	const UExtractionDamageType* DmgType = Cast<UExtractionDamageType>(
		PointDamage.DamageTypeClass->GetDefaultObject());
	if (!IsValid(DmgType)) return 1.f;

	return DmgType->GetMultiplierForRegion(Region);
}

float AEnemyCharacter::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (IsValid(HealthComponent) && HealthComponent->IsDead()) return 0.f;

	float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);

	const EHitRegion HitRegion = ResolveHitRegion(DamageEvent);

	// Headshot: fraction-of-max-health floor (bullet point damage to Head only).
	// High-damage weapons (sniper) keep their multiplied damage when it exceeds the
	// flat fraction, so heavy calibres stay lethal on the head.
	float FinalDamage = ActualDamage * GetHitboxDamageMultiplier(DamageEvent, HitRegion);
	if (HitRegion == EHitRegion::Head &&
		DamageEvent.IsOfType(FPointDamageEvent::ClassID) &&
		IsValid(HealthComponent))
	{
		const float HeadshotFraction = IsValid(ArchetypeData) ? ArchetypeData->HeadshotMaxHealthFraction : 0.65f;
		if (HeadshotFraction > 0.f)
			FinalDamage = FMath::Max(FinalDamage, HeadshotFraction * HealthComponent->GetMaxHealth());
	}

	UEnemyArmourComponent* Armour = ArmourComponent.Get();
	if (IsValid(Armour))
		FinalDamage = Armour->ModifyIncomingDamage(FinalDamage, DamageEvent, DamageCauser);

	// One-tap headshot: PLAYER bullets to the head kill outright for every archetype that opts
	// in (Heavy DA sets bHeadshotOneTap false and keeps the fraction floor above). Applied AFTER
	// the armour step so shields and helmets can't save the target. Player-only — the companion's
	// AI aim deliberately targets the head and would otherwise one-tap everything it shoots.
	// Exactly shield+health (not a huge constant) so the damage-number HUD reports real damage.
	if (HitRegion == EHitRegion::Head &&
		DamageEvent.IsOfType(FPointDamageEvent::ClassID) &&
		IsValid(EventInstigator) && EventInstigator->IsPlayerController() &&
		IsValid(HealthComponent) &&
		(!IsValid(ArchetypeData) || ArchetypeData->bHeadshotOneTap))
	{
		FinalDamage = HealthComponent->GetCurrentShield() + HealthComponent->GetCurrentHealth();
	}

	if (IsValid(HealthComponent))
		HealthComponent->TakeDamage(FinalDamage);

	if (IsValid(EventInstigator))
	{
		LastDamageInstigator = EventInstigator;
		LastDamageWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

		// Notify the awareness component so it can react to being shot
		if (AEnemyAIController* AIC = Cast<AEnemyAIController>(GetController()))
		{
			if (UEnemyAwarenessComponent* Awareness = AIC->GetAwarenessComponent())
				Awareness->NotifyDamaged(EventInstigator);
		}
	}

	// Phase 4: broadcast hit-react for flinch montages (only while alive)
	if (FinalDamage > 0.f && IsValid(HealthComponent) && !HealthComponent->IsDead())
	{
		OnHitReact.Broadcast(HitRegion);

		// Damage-defiance taunt while still healthy — content-gated (only the Heavy's bark set
		// carries Taunt lines; everyone else's request drops on the no-lines check).
		constexpr float TauntMinHealthFraction = 0.6f;
		if (HealthComponent->GetHealthPercent() > TauntMinHealthFraction
			&& IsValid(ArchetypeData) && IsValid(ArchetypeData->BarkSet))
			if (UBarkSubsystem* Barks = GetWorld()->GetSubsystem<UBarkSubsystem>())
				Barks->RequestBark(this, ArchetypeData->BarkSet, EBarkType::Taunt);
	}

	SpawnBloodImpactFX(DamageEvent, HitRegion);

	return FinalDamage;
}

void AEnemyCharacter::SpawnBloodImpactFX(const FDamageEvent& DamageEvent, EHitRegion HitRegion) const
{
	if (!IsValid(BloodImpactFX)) return;
	if (!DamageEvent.IsOfType(FPointDamageEvent::ClassID)) return;

	const FPointDamageEvent& PointDamage = static_cast<const FPointDamageEvent&>(DamageEvent);
	const FHitResult& Hit = PointDamage.HitInfo;
	if (Hit.ImpactPoint.IsNearlyZero()) return;

	// Face the burst back along the surface normal; fall back to opposing the shot when the
	// normal is unset (e.g. hand-built damage events).
	const FVector BurstDir = Hit.ImpactNormal.IsNearlyZero() ? -PointDamage.ShotDirection : FVector(Hit.ImpactNormal);
	const float BurstScale = (HitRegion == EHitRegion::Head) ? HeadshotBloodScale : 1.f;
	UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(), BloodImpactFX, Hit.ImpactPoint, BurstDir.Rotation(),
		FVector(BurstScale), /*bAutoDestroy=*/true, /*bAutoActivate=*/true, ENCPoolMethod::AutoRelease);
}

// --- Death ---

void AEnemyCharacter::HandleDeath()
{
	// Kill attribution for the companion's voice — its own confirm, or approval of the player's
	// kill. Takedown deaths skip this (no instigator stamp); TakedownConfirm covers those.
	// The confirmed-kill stamp rides the same attribution: it is what earns Combat mode its one
	// free advance bound, so it must only ever fire on a kill the companion actually made.
	if (AController* Killer = LastDamageInstigator.Get())
	{
		if (ACompanionCharacter* CompanionKiller = Cast<ACompanionCharacter>(Killer->GetPawn()))
		{
			CompanionKiller->Bark(ECompanionBarkType::TargetDown);
			CompanionKiller->StampConfirmedKill();
		}
		else if (Killer->IsPlayerController())
			if (ACompanionCharacter* Companion = ACompanionCharacter::GetPrimaryCompanion(GetWorld()))
				Companion->Bark(ECompanionBarkType::ApprovePlayerKill);
	}

	if (IsValid(AwarenessWidgetComponent))
		AwarenessWidgetComponent->SetVisibility(false);

	if (AWeaponBase* Weapon = CurrentWeapon.Get())
	{
		Weapon->StopFiring();
		Weapon->CancelReload();
		Weapon->ReattachMagazine();
		Weapon->SetFireAlignAlpha(0.f);
		Weapon->SetMeleeAlignAlpha(0.f);
		Weapon->SetPatrolAlignAlpha(0.f);
		Weapon->StopVisualWeaponFire();

		// Ensure the corpse shows the combat-hand grip with no frozen settle offset.
		// Snap immediately so the death montage (authored for the combat hand) lines up.
		Weapon->ResetHandSwapSettle();
		if (bWeaponOnPatrolHand)
			SetWeaponHandSocket(false, /*bImmediate=*/true);
	}

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Shut down bolt-on components before registering as a corpse.
	if (UEnemyGrenadierComponent* Grenadier = GrenadierComponent.Get())
		Grenadier->CancelThrow();

	if (USquadAuraComponent* Aura = SquadAuraComp.Get())
		Aura->DeactivateAura();

	if (UEnemySniperTelegraphComponent* Telegraph = SniperTelegraphComp.Get())
		Telegraph->CancelTelegraph();

	if (IsValid(SuppressionComponent))
		SuppressionComponent->DeactivateForDeath();

	if (IsValid(MoraleComponent))
		MoraleComponent->DeactivateForDeath();

	if (IsValid(PostureComponent))
		PostureComponent->DeactivateForDeath();

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (IsValid(MoveComp))
	{
		MoveComp->StopMovementImmediately();
		MoveComp->DisableMovement();
	}

	// Play the death montage before ragdoll so there is a pose transition on death.
	// Only on the non-takedown path — takedowns use TakedownReactionMontage / DeathMontage via
	// the OnTakedownExecuted delegate path in UEnemyAnimInstance::HandleTakedown.
	// ApplyRagdoll calls Montage_StopGroupByName(0, None) which terminates the montage cleanly
	// when ragdoll physics takes over — no risk of the montage fighting the ragdoll.
	if (!bPendingTakedownDeath)
	{
		if (USkeletalMeshComponent* MeshComp = GetMesh())
		{
			if (UEnemyAnimInstance* AnimInst = Cast<UEnemyAnimInstance>(MeshComp->GetAnimInstance()))
				AnimInst->PlayDeathMontage();
		}
	}

	if (bPendingTakedownDeath && !bTakedownWasMontageDriven)
	{
		// Instant takedown: ranged (shoot) uses the short delay so the enemy drops promptly;
		// melee grapple keeps the longer beat for the reaction montage.
		const float Delay = bTakedownRagdollImmediate ? RangedTakedownRagdollDelay : TakedownRagdollDelay;
		if (UWorld* W = GetWorld())
		{
			W->GetTimerManager().SetTimer(
				TakedownRagdollTimerHandle, this,
				&AEnemyCharacter::ApplyRagdoll,
				Delay, false);
		}
	}
	else
	{
		// Normal death OR a montage-driven takedown whose finisher already played -> ragdoll now,
		// so there's no pose-snap gap between the finisher ending and physics taking over.
		ApplyRagdoll();
	}

	UWorld* World = GetWorld();
	if (!IsValid(World)) return;

	SetupCorpseWeaponPickup();
	TrySpawnAmmoDrop();
	TrySpawnDeathLoot();

	// Sight perception bakes the listener-target affiliation into its query at registration and never
	// re-evaluates a runtime team change. This pawn just flipped to NoTeam (dead) — living enemies
	// ignored it as a friendly while alive, so no sight query pair exists. Evict + re-register it as a
	// sight source so perceivers re-pair with it as a neutral and can discover the body.
	if (UAIPerceptionSystem* PerceptionSys = UAIPerceptionSystem::GetCurrent(World))
	{
		PerceptionSys->UnregisterSource(*this, UAISense_Sight::StaticClass());
		PerceptionSys->RegisterSourceForSenseClass(UAISense_Sight::StaticClass(), *this);
	}

	if (CVarEnemyPersistCorpses.GetValueOnGameThread() != 0)
	{
		if (UEnemyDirectorSubsystem* Director = World->GetSubsystem<UEnemyDirectorSubsystem>())
		{
			Director->RegisterCorpse(this);

			World->GetTimerManager().SetTimer(
				DestroyTimerHandle,
				this,
				&AEnemyCharacter::DestroyAfterDeath,
				CorpseMaxLifespanSeconds,
				false);
			return;
		}
	}

	World->GetTimerManager().SetTimer(
		DestroyTimerHandle,
		this,
		&AEnemyCharacter::DestroyAfterDeath,
		CorpseDisappearSeconds,
		false);
}

void AEnemyCharacter::HandleGrenadeTelegraph(FVector PredictedLanding, float TimeToImpact)
{
	OnGrenadeThrow.Broadcast(PredictedLanding, TimeToImpact);
}

void AEnemyCharacter::HandleGrenadeCancelled()
{
	if (USkeletalMeshComponent* MeshComp = GetMesh())
		if (UEnemyAnimInstance* AnimInst = Cast<UEnemyAnimInstance>(MeshComp->GetAnimInstance()))
			AnimInst->StopGrenadeMontage();
}

void AEnemyCharacter::ApplyRagdoll()
{
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!IsValid(MeshComp)) return;

	if (UAnimInstance* AnimInst = MeshComp->GetAnimInstance())
		AnimInst->Montage_StopGroupByName(0.f, NAME_None);

	MeshComp->SetCollisionProfileName(TEXT("Ragdoll"));
	MeshComp->SetSimulatePhysics(true);

	// A takedown corpse starts from a held floor pose — bodies already touching (or slightly
	// inside) the ground get a depenetration kick at physics start that reads as the corpse
	// bouncing. Cap the resolve speed so overlaps ease out instead of popping.
	static constexpr float CorpseMaxDepenetrationVelocity = 120.f;
	for (FBodyInstance* Body : MeshComp->Bodies)
		if (Body) Body->SetMaxDepenetrationVelocity(CorpseMaxDepenetrationVelocity);

	// Snapshot the corpse location after the ragdoll settles to avoid per-tick bone lookups.
	if (UWorld* World = GetWorld())
	{
		RagdollStartTime = World->GetTimeSeconds();
		World->GetTimerManager().SetTimer(
			CorpseSettleTimerHandle, this,
			&AEnemyCharacter::CacheCorpseLocation,
			CorpseSettleTime, false);
	}
}

void AEnemyCharacter::DestroyAfterDeath()
{
	Destroy();
}

void AEnemyCharacter::TrySpawnAmmoDrop()
{
	if (!HasAuthority() || !AmmoDropTable) return;

	const AWeaponBase* Weapon = CurrentWeapon.Get();
	const UWeaponDataAsset* Data = Weapon ? Weapon->GetWeaponData() : nullptr;
	if (!Data) return; // died weaponless — no drop

	const EEnemyWeaponAnimType Category =
		bOverrideAmmoDropCategory ? AmmoDropCategoryOverride : Data->EnemyWeaponAnimType;
	const FAmmoDropEntry* Entry = AmmoDropTable->Find(Category);
	if (!Entry || !Entry->PickupClass) return;
	if (FMath::FRand() > Entry->DropChance) return;

	const int32 Amount = FMath::RandRange(Entry->MinAmount, Entry->MaxAmount);
	if (Amount <= 0) return;

	UWorld* World = GetWorld();
	if (!World) return;

	const FTransform SpawnTransform(FRotator::ZeroRotator, FindDeathLootSpawnLocation());
	AAmmoPickup* Pickup = World->SpawnActorDeferred<AAmmoPickup>(
		Entry->PickupClass, SpawnTransform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Pickup) return;

	Pickup->InitPickup(Category, Amount);
	Pickup->FinishSpawning(SpawnTransform);
}

void AEnemyCharacter::SetupCorpseWeaponPickup()
{
	if (!HasAuthority() || !AmmoDropTable) return;

	AWeaponBase* Weapon = CurrentWeapon.Get();
	if (!IsValid(Weapon)) return; // died weaponless — nothing to collect

	const FWeaponDropEntry* Entry = AmmoDropTable->FindWeaponDrop(Weapon->GetClass());
	if (!Entry || !Entry->PickupClass) return;

	UWorld* World = GetWorld();
	if (!World) return;

	// The corpse keeps its gun in hand; the pickup actor spawns invisible and rides the weapon
	// so the interact prompt sits on the gun wherever the ragdoll lands it. Collecting runs the
	// normal swap flow; the pickup BP destroys the weapon it is attached to on success.
	const FTransform SpawnTransform = Weapon->GetActorTransform();
	AActor* Pickup = World->SpawnActorDeferred<AActor>(
		Entry->PickupClass, SpawnTransform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Pickup) return;

	// Stamp the rolled ammo before FinishSpawning so the pickup's construction script builds
	// its item payload from these values. Contract: BP custom event InitDropAmmo(Mag, Reserve).
	if (UFunction* InitFn = Pickup->FindFunction(TEXT("InitDropAmmo")))
	{
		if (InitFn->ParmsSize == sizeof(int32) * 2)
		{
			int32 Parms[2] = {
				FMath::RandRange(Entry->MinMag, Entry->MaxMag),
				FMath::RandRange(Entry->MinReserve, Entry->MaxReserve) };
			Pickup->ProcessEvent(InitFn, Parms);
		}
		else
		{
			UE_LOG(LogEnemyAI, Warning,
				TEXT("%s: weapon-drop pickup '%s' InitDropAmmo signature mismatch — expected (int32, int32)."),
				*GetName(), *GetNameSafe(Entry->PickupClass));
		}
	}
	else
	{
		UE_LOG(LogEnemyAI, Warning,
			TEXT("%s: weapon-drop pickup '%s' has no InitDropAmmo event — dropped gun ships BP-default ammo."),
			*GetName(), *GetNameSafe(Entry->PickupClass));
	}

	Pickup->FinishSpawning(SpawnTransform);
	Pickup->AttachToActor(Weapon, FAttachmentTransformRules::SnapToTargetNotIncludingScale);

	// Hide the pickup's display meshes — the corpse's own gun is the visual. Collision and any
	// interaction prompt widgets stay live.
	TInlineComponentArray<UMeshComponent*> MeshComps;
	Pickup->GetComponents(MeshComps);
	for (UMeshComponent* MeshComp : MeshComps)
	{
		if (IsValid(MeshComp))
			MeshComp->SetVisibility(false, false);
	}
}

FVector AEnemyCharacter::FindDeathLootSpawnLocation() const
{
	const FVector Origin = GetActorLocation();
	const FVector Fallback = Origin + FVector(0.f, 0.f, 30.f);

	UWorld* World = GetWorld();
	if (!IsValid(World)) return Fallback;

	// Candidate lateral directions: behind facing, left, right, forward.
	const FVector Forward = GetActorForwardVector();
	const FVector Candidates[4] = {
		-Forward, FVector::CrossProduct(FVector::UpVector, Forward),
		FVector::CrossProduct(Forward, FVector::UpVector), Forward
	};

	constexpr float LateralDist = 80.f;
	constexpr float SweepRadius = 15.f;
	constexpr float TraceDown = 500.f;
	constexpr float GroundOffset = 5.f;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(DeathLoot), false, this);

	for (const FVector& Dir : Candidates)
	{
		const FVector TestPoint = Origin + Dir * LateralDist + FVector(0.f, 0.f, 30.f);

		// Candidate must be reachable from the corpse — reject spots on the far side of a wall.
		FHitResult PathHit;
		if (World->LineTraceSingleByChannel(PathHit, Origin + FVector(0.f, 0.f, 30.f),
				TestPoint, ECC_WorldStatic, Params))
			continue;

		// Check the candidate spot isn't inside blocking geometry.
		if (World->OverlapBlockingTestByChannel(TestPoint, FQuat::Identity,
				ECC_WorldStatic, FCollisionShape::MakeSphere(SweepRadius), Params))
			continue;

		// Ground-snap: line trace down to find the floor.
		FHitResult GroundHit;
		if (World->LineTraceSingleByChannel(GroundHit, TestPoint,
				TestPoint - FVector(0.f, 0.f, TraceDown), ECC_WorldStatic, Params))
			return GroundHit.ImpactPoint + FVector(0.f, 0.f, GroundOffset);
	}

	return Fallback;
}

void AEnemyCharacter::TrySpawnDeathLoot()
{
	if (!HasAuthority() || DeathLoot.Num() == 0) return;

	if (!LootPickupClass)
	{
		UE_LOG(LogEnemyAI, Warning, TEXT("%s: DeathLoot authored but LootPickupClass is unset — loot will not drop"),
			*GetName());
		return;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World)) return;

	const FVector SpawnLocation = FindDeathLootSpawnLocation();
	const FTransform SpawnTransform(FRotator::ZeroRotator, SpawnLocation);

	ALootPickup* Pickup = World->SpawnActorDeferred<ALootPickup>(
		LootPickupClass, SpawnTransform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!IsValid(Pickup)) return;

	Pickup->SetContents(DeathLoot);
	Pickup->FinishSpawning(SpawnTransform);
}

// --- Silent takedown ---

bool AEnemyCharacter::CanBeTakenDown(const AActor* TakedownInstigator, bool bIgnoreRangeAndArc) const
{
#if !UE_BUILD_SHIPPING
	const bool bLogTakedown = IsValid(TakedownInstigator) &&
		(TakedownInstigator->GetActorLocation() - GetActorLocation()).SizeSquared() < FMath::Square(600.f);
#endif

	if (!IsValid(TakedownInstigator) || !IsValid(ArchetypeData))
	{
#if !UE_BUILD_SHIPPING
		// Can't gate on bLogTakedown here — instigator may be invalid.
		UE_LOG(LogEnemyAI, Verbose, TEXT("[Takedown] %s reject: invalid instigator/ArchetypeData"), *GetNameSafe(this));
#endif
		return false;
	}

	if (IsValid(HealthComponent) && HealthComponent->IsDead())
	{
#if !UE_BUILD_SHIPPING
		if (bLogTakedown) UE_LOG(LogEnemyAI, Verbose, TEXT("[Takedown] %s reject: already dead"), *GetNameSafe(this));
#endif
		return false;
	}

	// Only an Unaware enemy can be taken down (design §4: Unaware/Dormant).
	const AEnemyAIController* AIC = Cast<AEnemyAIController>(GetController());
	const UEnemyAwarenessComponent* Awareness = AIC ? AIC->GetAwarenessComponent() : nullptr;
	if (!Awareness)
	{
#if !UE_BUILD_SHIPPING
		if (bLogTakedown) UE_LOG(LogEnemyAI, Warning,
			TEXT("[Takedown] %s reject: no EnemyAIController/awareness comp (controller=%s)"),
			*GetNameSafe(this), *GetNameSafe(GetController()));
#endif
		return false;
	}

	// Unaware is the rule — but a victim this instigator already reserved is grandfathered through
	// anything short of Combat. The global alert ladder is a ratchet: one missed player shot
	// escalates it to Loud, which wakes every Unaware enemy in the level, and the strict rule then
	// whiffs an in-flight takedown that was already lined up. Combat stays a hard reject.
	const EEnemyAwarenessState AwarenessState = Awareness->GetAwarenessState();
	const bool bGrandfathered = AwarenessState < EEnemyAwarenessState::Combat
		&& IsTakedownReservedBy(TakedownInstigator);
	if (AwarenessState != EEnemyAwarenessState::Unaware && !bGrandfathered)
	{
#if !UE_BUILD_SHIPPING
		if (bLogTakedown)
		{
			const FString AwarenessStr = StaticEnum<EEnemyAwarenessState>()
				? StaticEnum<EEnemyAwarenessState>()->GetNameStringByValue((int64)AwarenessState)
				: TEXT("Unknown");
			UE_LOG(LogEnemyAI, Verbose, TEXT("[Takedown] %s reject: awareness=%s (need Unaware, or reserved and below Combat)"),
				*GetNameSafe(this), *AwarenessStr);
		}
#endif
		return false;
	}

	// Range and rear-arc checks apply only to melee takedowns. Ranged takedowns
	// (e.g. companion shoot) pass bIgnoreRangeAndArc=true and skip straight to success.
	if (!bIgnoreRangeAndArc)
	{
		const FVector ToInstigator = TakedownInstigator->GetActorLocation() - GetActorLocation();
		const float DistSq = ToInstigator.SizeSquared();
		if (DistSq > FMath::Square(ArchetypeData->TakedownRange))
		{
#if !UE_BUILD_SHIPPING
			if (bLogTakedown) UE_LOG(LogEnemyAI, Verbose,
				TEXT("[Takedown] %s reject: out of range (dist=%.0f > TakedownRange=%.0f)"),
				*GetNameSafe(this), FMath::Sqrt(DistSq), ArchetypeData->TakedownRange);
#endif
			return false;
		}

		// Instigator must sit inside the rear arc (centred on backward).
		const float Dot = FVector::DotProduct(GetActorForwardVector(), ToInstigator.GetSafeNormal2D());
		const float ArcThreshold = -FMath::Cos(FMath::DegreesToRadians(ArchetypeData->TakedownRearArcDeg * 0.5f));

#if !UE_BUILD_SHIPPING
		if (bLogTakedown)
		{
			if (Dot <= ArcThreshold)
			{
				UE_LOG(LogEnemyAI, Verbose, TEXT("[Takedown] %s TAKEABLE (dist ok, dot=%.2f)"), *GetNameSafe(this), Dot);
			}
			else
			{
				UE_LOG(LogEnemyAI, Verbose, TEXT("[Takedown] %s reject: outside rear arc (dot=%.2f, need <= %.2f)"),
					*GetNameSafe(this), Dot, ArcThreshold);
			}
		}
#endif

		return Dot <= ArcThreshold;
	}

	return true;
}

bool AEnemyCharacter::IsTakedownEligible() const
{
	const AEnemyAIController* AIC = Cast<AEnemyAIController>(GetController());
	const UEnemyAwarenessComponent* Awareness = AIC ? AIC->GetAwarenessComponent() : nullptr;
	return (TakedownVolumeRefCount > 0) && IsValid(Awareness)
		&& (Awareness->GetAwarenessState() == EEnemyAwarenessState::Unaware);
}

bool AEnemyCharacter::IsTakedownEligibleFor(const AActor* TakedownInstigator) const
{
	if (IsTakedownEligible()) return true;

	// Grandfather path: only for the holder of the reservation, only inside a volume, only while
	// alive, and only below Combat. Everything else falls back to the strict rule above so a
	// Searching enemy never becomes newly pingable (the offer path uses IsTakedownEligible()).
	if (!IsTakedownReservedBy(TakedownInstigator)) return false;
	if (TakedownVolumeRefCount <= 0) return false;
	if (IsValid(HealthComponent) && HealthComponent->IsDead()) return false;

	const AEnemyAIController* AIC = Cast<AEnemyAIController>(GetController());
	const UEnemyAwarenessComponent* Awareness = AIC ? AIC->GetAwarenessComponent() : nullptr;
	return IsValid(Awareness) && Awareness->GetAwarenessState() < EEnemyAwarenessState::Combat;
}

void AEnemyCharacter::ReserveForTakedown(AActor* TakedownInstigator)
{
	if (!IsValid(TakedownInstigator)) return;
	TakedownReservedBy = TakedownInstigator;
}

void AEnemyCharacter::ClearTakedownReservation(const AActor* TakedownInstigator)
{
	if (TakedownReservedBy.Get() != TakedownInstigator) return;
	TakedownReservedBy.Reset();
}

bool AEnemyCharacter::IsTakedownReservedBy(const AActor* TakedownInstigator) const
{
	return IsValid(TakedownInstigator) && TakedownReservedBy.Get() == TakedownInstigator;
}

bool AEnemyCharacter::HasDetectedPlayer() const
{
	const AEnemyAIController* AIC = Cast<AEnemyAIController>(GetController());
	if (!AIC) return false;

	const UEnemyAwarenessComponent* Awareness = AIC->GetAwarenessComponent();
	if (!IsValid(Awareness)) return false;

	return Awareness->GetAwarenessState() == EEnemyAwarenessState::Combat;
}

bool AEnemyCharacter::HasEngagedCompanion(const AActor* Companion, float MemorySeconds) const
{
	if (!IsValid(Companion)) return false;

	const AEnemyAIController* AIC = Cast<AEnemyAIController>(GetController());
	if (!AIC) return false;

	const UEnemyAwarenessComponent* Awareness = AIC->GetAwarenessComponent();
	if (!IsValid(Awareness)) return false;

	if (Awareness->GetCombatTarget() == Companion) return true;

	// Cloak-bypassing on purpose, and the only clause that is: landing hits on the companion proves
	// this enemy can see it regardless of what the mode cloak says it is allowed to perceive.
	if (MemorySeconds > 0.f && Awareness->GetTimeSinceDamagedBy(Companion) <= MemorySeconds)
		return true;

	// Nothing below can hold for an enemy that has perceived nothing at all — skip the map lookup.
	if (Awareness->GetAwarenessState() == EEnemyAwarenessState::Unaware) return false;

	return Awareness->HasLiveKnowledgeOf(Companion, MemorySeconds);
}

float AEnemyCharacter::GetTimeEnteredCombat() const
{
	const AEnemyAIController* AIC = Cast<AEnemyAIController>(GetController());
	if (!AIC) return -1e9f;

	const UEnemyAwarenessComponent* Awareness = AIC->GetAwarenessComponent();
	if (!IsValid(Awareness)) return -1e9f;

	return Awareness->GetLastCombatEnterTime();
}

bool AEnemyCharacter::IsAlertedForCompanionReadiness() const
{
	const AEnemyAIController* AIC = Cast<AEnemyAIController>(GetController());
	if (!AIC) return false;

	const UEnemyAwarenessComponent* Awareness = AIC->GetAwarenessComponent();
	if (!IsValid(Awareness)) return false;

	const EEnemyAwarenessState State = Awareness->GetAwarenessState();
	return State == EEnemyAwarenessState::Searching || State == EEnemyAwarenessState::Combat;
}

void AEnemyCharacter::SetInTakedownVolume(bool bInVolume)
{
	TakedownVolumeRefCount = FMath::Max(0, TakedownVolumeRefCount + (bInVolume ? 1 : -1));
}

bool AEnemyCharacter::BeginTakedownHold(AActor* TakedownInstigator, FVector SnapLocation, float SnapYaw, float WatchdogTimeout, bool bIgnoreRangeAndArc)
{
	if (!CanBeTakenDown(TakedownInstigator, bIgnoreRangeAndArc)) return false;

	UE_LOG(LogEnemyAI, Verbose, TEXT("[Takedown] %s BeginTakedownHold OK (watchdog=%.1f)"), *GetNameSafe(this), WatchdogTimeout);

	bPendingTakedownDeath = true;
	bTakedownFrozen = true;
	bTakedownWasMontageDriven = false;

	// Stop any active body anim (walk/idle/fidget) so it doesn't play through the finisher.
	if (USkeletalMeshComponent* MeshComp = GetMesh())
		if (UAnimInstance* AnimInst = MeshComp->GetAnimInstance())
			AnimInst->Montage_Stop(0.f);

	OnTakedownExecuted.Broadcast(TakedownInstigator);

	// Snap position and facing so any finisher montage lines up consistently.
	SetActorLocation(SnapLocation, false, nullptr, ETeleportType::TeleportPhysics);
	SetActorRotation(FRotator(0.f, SnapYaw, 0.f), ETeleportType::TeleportPhysics);

	// Freeze AI brain and movement so the enemy can't walk away during the montage.
	if (AAIController* AIC = Cast<AAIController>(GetController()))
		if (AIC->BrainComponent) AIC->BrainComponent->StopLogic(TEXT("Takedown"));

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->StopMovementImmediately();
		MoveComp->DisableMovement();
	}

	// Watchdog: if the player montage never fires the kill notify, kill the enemy after timeout
	// so it can never be left frozen-alive. Skipped on the instant path (WatchdogTimeout == 0).
	// EndPlay clears via ClearAllTimersForObject.
	if (WatchdogTimeout > 0.f)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				TakedownWatchdogTimerHandle,
				[this]() { FinishTakedownKill(nullptr); },
				WatchdogTimeout, false);
		}
	}

	return true;
}

void AEnemyCharacter::FinishTakedownKill(AActor* TakedownInstigator)
{
	if (!bTakedownFrozen)
	{
		UE_LOG(LogEnemyAI, Verbose, TEXT("[Takedown] %s FinishTakedownKill early-out: not frozen"), *GetNameSafe(this));
		return;
	}
	if (IsValid(HealthComponent) && HealthComponent->IsDead())
	{
		UE_LOG(LogEnemyAI, Verbose, TEXT("[Takedown] %s FinishTakedownKill early-out: already dead"), *GetNameSafe(this));
		return;
	}

	bTakedownFrozen = false;

	if (UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(TakedownWatchdogTimerHandle);

	const APawn* InstigatorPawn = Cast<APawn>(TakedownInstigator);
	AController* InstigatorController = InstigatorPawn ? InstigatorPawn->GetController() : nullptr;

	UE_LOG(LogEnemyAI, Warning, TEXT("[Takedown] %s KILLED by instigator=%s (%.0f dmg)"), *GetNameSafe(this), *GetNameSafe(TakedownInstigator), TakedownDamage);
	TakeDamage(TakedownDamage, FDamageEvent(), InstigatorController, TakedownInstigator);
}

bool AEnemyCharacter::ExecuteTakedown(AActor* TakedownInstigator, bool bIgnoreRangeAndArc)
{
	UE_LOG(LogEnemyAI, Verbose, TEXT("[Takedown] %s ExecuteTakedown (instant path, ranged=%s)"),
		*GetNameSafe(this), bIgnoreRangeAndArc ? TEXT("true") : TEXT("false"));

	// Ranged shoot takedown: flag for near-instant ragdoll instead of the grapple-reaction beat.
	if (bIgnoreRangeAndArc) bTakedownRagdollImmediate = true;

	// Instant path: no snap (enemy stays in place), no watchdog (kill follows immediately).
	const FVector SnapLoc = GetActorLocation();

	// Melee: face away from instigator so finisher lines up. Ranged: keep current facing (no spin).
	float SnapYaw;
	if (bIgnoreRangeAndArc)
	{
		SnapYaw = GetActorRotation().Yaw;
	}
	else
	{
		const FVector ToInstigator = IsValid(TakedownInstigator)
			? (TakedownInstigator->GetActorLocation() - SnapLoc).GetSafeNormal2D()
			: -GetActorForwardVector();
		SnapYaw = FRotationMatrix::MakeFromX(-ToInstigator).Rotator().Yaw;
	}

	if (!BeginTakedownHold(TakedownInstigator, SnapLoc, SnapYaw, 0.f, bIgnoreRangeAndArc))
	{
		UE_LOG(LogEnemyAI, Verbose, TEXT("[Takedown] %s ExecuteTakedown aborted (BeginTakedownHold failed)"), *GetNameSafe(this));
		return false;
	}
	FinishTakedownKill(TakedownInstigator);
	return true;
}

void AEnemyCharacter::AbortTakedownHold()
{
	if (!bTakedownFrozen) return;

	bTakedownFrozen = false;
	bPendingTakedownDeath = false;
	bTakedownWasMontageDriven = false;

	if (UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(TakedownWatchdogTimerHandle);

	// Re-enable movement so the enemy can react normally.
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
		MoveComp->SetMovementMode(MOVE_Walking);

	// Restart the AI brain so the enemy can resume its behaviour tree.
	if (AAIController* AIC = Cast<AAIController>(GetController()))
		if (AIC->BrainComponent) AIC->BrainComponent->RestartLogic();
}

// --- Body discovery ---

bool AEnemyCharacter::TryMarkBodyReported()
{
	if (bBodyReported) return false;
	bBodyReported = true;
	return true;
}

// --- Corpse Location ---

FVector AEnemyCharacter::GetCorpseLocation() const
{
	if (bCorpseLocationCached) return CachedCorpseLocation;

	static const FName PelvisBone(TEXT("pelvis"));
	static const FName SpineBone(TEXT("spine_03"));

	const USkeletalMeshComponent* MeshComp = GetMesh();
	if (IsValid(MeshComp) && MeshComp->IsSimulatingPhysics())
	{
		if (MeshComp->DoesSocketExist(PelvisBone))
			return MeshComp->GetSocketLocation(PelvisBone);
		if (bCachedHasChestBone)
			return MeshComp->GetSocketLocation(SpineBone);
		return MeshComp->Bounds.Origin;
	}

	return GetActorLocation();
}

void AEnemyCharacter::CacheCorpseLocation()
{
	static const FName PelvisBone(TEXT("pelvis"));
	static const FName SpineBone(TEXT("spine_03"));

	UWorld* World = GetWorld();
	USkeletalMeshComponent* MeshComp = GetMesh();
	const bool bHardCeiling = !IsValid(World) || (World->GetTimeSeconds() - RagdollStartTime) >= CorpseSettleMaxWait;

	if (!bHardCeiling && IsValid(MeshComp) && MeshComp->IsSimulatingPhysics())
	{
		const FName CheckBone = MeshComp->DoesSocketExist(PelvisBone) ? PelvisBone : (bCachedHasChestBone ? SpineBone : NAME_None);
		const float SpeedSq = (CheckBone != NAME_None)
			? MeshComp->GetPhysicsLinearVelocity(CheckBone).SizeSquared()
			: MeshComp->GetPhysicsLinearVelocity().SizeSquared();

		if (SpeedSq > FMath::Square(CorpseSettleSpeedThreshold))
		{
			World->GetTimerManager().SetTimer(
				CorpseSettleTimerHandle, this,
				&AEnemyCharacter::CacheCorpseLocation,
				CorpseSettleRetryInterval, false);
			return;
		}
	}

	CachedCorpseLocation = GetCorpseLocation();
	bCorpseLocationCached = true;
}

// --- Corpse Removal ---

void AEnemyCharacter::BeginCorpseRemoval()
{
	if (bCorpseRemovalStarted) return;

	// Takedown victims persist to the full corpse lifespan — don't fast-remove them when a
	// living enemy reaches the body (keeps player/companion takedown kills consistent).
	if (bPendingTakedownDeath) return;

	bCorpseRemovalStarted = true;

	UWorld* World = GetWorld();
	if (!IsValid(World)) return;

	World->GetTimerManager().ClearTimer(DestroyTimerHandle);
	World->GetTimerManager().SetTimer(
		DestroyTimerHandle,
		this,
		&AEnemyCharacter::DestroyAfterDeath,
		CorpseRemovalAfterReachSeconds,
		false);
}

// --- Damage query ---

bool AEnemyCharacter::WasDamagedRecently(float Window) const
{
	const UWorld* World = GetWorld();
	if (!World) return false;
	return (World->GetTimeSeconds() - LastDamageWorldTime) <= Window;
}
