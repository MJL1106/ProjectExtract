// AEnemyCharacter — single character class for all enemy archetypes.

#include "EnemyCharacter.h"
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
#include "WeaponBase.h"
#include "ExtractionDamageType.h"
#include "ExtractionTypes.h"
#include "EnemyArmourComponent.h"
#include "EnemyShieldComponent.h"
#include "EnemyGrenadierComponent.h"
#include "SquadAuraComponent.h"
#include "EnemySniperTelegraphComponent.h"
#include "SuppressionComponent.h"
#include "EnemyMoraleComponent.h"
#include "EnemySquadSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/WidgetComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/DamageEvents.h"
#include "TimerManager.h"
#include "EnemyAwarenessWidget.h"

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
	SuppressionComponent = CreateDefaultSubobject<USuppressionComponent>(TEXT("SuppressionComponent"));
	MoraleComponent = CreateDefaultSubobject<UEnemyMoraleComponent>(TEXT("MoraleComponent"));

	AIControllerClass = AEnemyAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	// Bug 8: enable crouching for NavAgent so Crouch()/UnCrouch() works.
	GetCharacterMovement()->GetNavAgentPropertiesRef().bCanCrouch = true;

	// Bug 6: weapon hitscan traces ECC_Visibility, but the inherited CharacterMesh profile ignores it,
	// so player/companion shots passed straight through. Block Visibility on the mesh so hits register
	// (the trace returns the struck bone, so hit-region multipliers resolve correctly).
	if (USkeletalMeshComponent* MeshComp = GetMesh()) MeshComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	OwnedTags.AddTag(TAG_Character_Enemy);

	static constexpr float AwarenessWidgetOffsetZ = 30.f;
	static constexpr float AwarenessWidgetSize     = 64.f;

	AwarenessWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("AwarenessWidget"));
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
	BoneToHitRegionMap.Add(FName("neck_01"),    EHitRegion::Torso);
	BoneToHitRegionMap.Add(FName("neck_02"),    EHitRegion::Torso);
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

void AEnemyCharacter::BeginPlay()
{
	Super::BeginPlay();

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
	if (const USkeletalMeshComponent* MeshComp = GetMesh())
		bCachedHasChestBone = MeshComp->DoesSocketExist(ChestBoneName);

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
	if (HasAuthority())
	{
		if (AWeaponBase* Weapon = CurrentWeapon.Get()) Weapon->Destroy();
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

	// Phase 3: squad aura narrows spread; extra spread from BT tasks (e.g. shield sidearm) widens it.
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

	// Apply generic damage (no hit region — melee bypasses hitbox multiplier)
	FDamageEvent MeleeDmgEvent;
	Target->TakeDamage(ArchetypeData->MeleeDamage, MeleeDmgEvent, GetController(), this);

	OnMeleePerformed.Broadcast();
	return true;
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

	if (ArchetypeData->bHasShield && !ShieldComponent)
	{
		ShieldComponent = NewObject<UEnemyShieldComponent>(this, UEnemyShieldComponent::StaticClass());
		ShieldComponent->RegisterComponent();
		if (USkeletalMeshComponent* MeshComp = GetMesh())
		{
			if (ArchetypeData->ShieldAttachSocket != NAME_None && MeshComp->DoesSocketExist(ArchetypeData->ShieldAttachSocket))
				ShieldComponent->AttachToComponent(MeshComp, FAttachmentTransformRules::SnapToTargetIncludingScale, ArchetypeData->ShieldAttachSocket);
			else
				ShieldComponent->AttachToComponent(MeshComp, FAttachmentTransformRules::KeepRelativeTransform);
			ShieldComponent->SetRelativeTransform(ArchetypeData->ShieldRelativeTransform);
		}
		ShieldComponent->InitFromArchetype(ArchetypeData);
	}

	if (ArchetypeData->bIsGrenadier && !GrenadierComponent)
	{
		GrenadierComponent = NewObject<UEnemyGrenadierComponent>(this, UEnemyGrenadierComponent::StaticClass());
		GrenadierComponent->RegisterComponent();
		GrenadierComponent->InitFromArchetype(ArchetypeData);
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
	if (!DamageEvent.IsOfType(FPointDamageEvent::ClassID)) return 1.f;

	const FPointDamageEvent& PointDamage = static_cast<const FPointDamageEvent&>(DamageEvent);
	if (!PointDamage.DamageTypeClass) return 1.f;

	const UExtractionDamageType* DmgType = Cast<UExtractionDamageType>(
		PointDamage.DamageTypeClass->GetDefaultObject());
	if (!IsValid(DmgType)) return 1.f;

	return DmgType->GetMultiplierForRegion(ResolveHitRegion(DamageEvent));
}

float AEnemyCharacter::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (IsValid(HealthComponent) && HealthComponent->IsDead()) return 0.f;

	float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);

	// Phase 3: route through shield first — shield may absorb the hit entirely.
	UEnemyShieldComponent* Shield = ShieldComponent.Get();
	if (IsValid(Shield) && !Shield->IsShieldBroken())
	{
		ActualDamage = Shield->ProcessIncomingDamage(ActualDamage, DamageEvent, DamageCauser);
		// If the shield absorbed everything, still notify awareness but deal no health damage.
		if (ActualDamage <= 0.f)
		{
			if (IsValid(EventInstigator))
			{
				LastDamageInstigator = EventInstigator;
				LastDamageWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

				if (AEnemyAIController* AIC = Cast<AEnemyAIController>(GetController()))
				{
					if (UEnemyAwarenessComponent* Awareness = AIC->GetAwarenessComponent())
						Awareness->NotifyDamaged(EventInstigator);
				}
			}
			return 0.f;
		}
	}

	// Apply hitbox multiplier, then armour directional reduction.
	float FinalDamage = ActualDamage * GetHitboxDamageMultiplier(DamageEvent);

	UEnemyArmourComponent* Armour = ArmourComponent.Get();
	if (IsValid(Armour))
		FinalDamage = Armour->ModifyIncomingDamage(FinalDamage, DamageEvent, DamageCauser);

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
		OnHitReact.Broadcast(ResolveHitRegion(DamageEvent));

	return FinalDamage;
}

// --- Death ---

void AEnemyCharacter::HandleDeath()
{
	if (IsValid(AwarenessWidgetComponent))
		AwarenessWidgetComponent->SetVisibility(false);

	if (AWeaponBase* Weapon = CurrentWeapon.Get())
		Weapon->StopFiring();

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Shut down bolt-on components before registering as a corpse.
	if (UEnemyGrenadierComponent* Grenadier = GrenadierComponent.Get())
		Grenadier->CancelThrow();

	if (USquadAuraComponent* Aura = SquadAuraComp.Get())
		Aura->DeactivateAura();

	if (UEnemySniperTelegraphComponent* Telegraph = SniperTelegraphComp.Get())
		Telegraph->CancelTelegraph();

	if (UEnemyShieldComponent* Shield = ShieldComponent.Get())
		Shield->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	if (IsValid(SuppressionComponent))
		SuppressionComponent->DeactivateForDeath();

	if (IsValid(MoraleComponent))
		MoraleComponent->DeactivateForDeath();

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (IsValid(MoveComp))
	{
		MoveComp->StopMovementImmediately();
		MoveComp->DisableMovement();
	}

	if (bPendingTakedownDeath)
	{
		if (UWorld* W = GetWorld())
		{
			W->GetTimerManager().SetTimer(
				TakedownRagdollTimerHandle, this,
				&AEnemyCharacter::ApplyRagdoll,
				TakedownRagdollDelay, false);
		}
	}
	else
	{
		ApplyRagdoll();
	}

	UWorld* World = GetWorld();
	if (!IsValid(World)) return;

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

void AEnemyCharacter::ApplyRagdoll()
{
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!IsValid(MeshComp)) return;

	if (UAnimInstance* AnimInst = MeshComp->GetAnimInstance())
		AnimInst->Montage_StopGroupByName(0.f, NAME_None);

	MeshComp->SetCollisionProfileName(TEXT("Ragdoll"));
	MeshComp->SetSimulatePhysics(true);

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

// --- Silent takedown ---

bool AEnemyCharacter::CanBeTakenDown(const AActor* TakedownInstigator) const
{
#if !UE_BUILD_SHIPPING
	const bool bLogTakedown = IsValid(TakedownInstigator) &&
		(TakedownInstigator->GetActorLocation() - GetActorLocation()).SizeSquared() < FMath::Square(600.f);
#endif

	if (!IsValid(TakedownInstigator) || !IsValid(ArchetypeData))
	{
#if !UE_BUILD_SHIPPING
		// Can't gate on bLogTakedown here — instigator may be invalid.
		UE_LOG(LogEnemyAI, Warning, TEXT("[Takedown] %s reject: invalid instigator/ArchetypeData"), *GetNameSafe(this));
#endif
		return false;
	}

	if (IsValid(HealthComponent) && HealthComponent->IsDead())
	{
#if !UE_BUILD_SHIPPING
		if (bLogTakedown) UE_LOG(LogEnemyAI, Warning, TEXT("[Takedown] %s reject: already dead"), *GetNameSafe(this));
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

	if (Awareness->GetAwarenessState() != EEnemyAwarenessState::Unaware)
	{
#if !UE_BUILD_SHIPPING
		if (bLogTakedown)
		{
			const FString AwarenessStr = StaticEnum<EEnemyAwarenessState>()
				? StaticEnum<EEnemyAwarenessState>()->GetNameStringByValue((int64)Awareness->GetAwarenessState())
				: TEXT("Unknown");
			UE_LOG(LogEnemyAI, Warning, TEXT("[Takedown] %s reject: awareness=%s (need Unaware)"),
				*GetNameSafe(this), *AwarenessStr);
		}
#endif
		return false;
	}

	const FVector ToInstigator = TakedownInstigator->GetActorLocation() - GetActorLocation();
	const float DistSq = ToInstigator.SizeSquared();
	if (DistSq > FMath::Square(ArchetypeData->TakedownRange))
	{
#if !UE_BUILD_SHIPPING
		if (bLogTakedown) UE_LOG(LogEnemyAI, Warning,
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
			UE_LOG(LogEnemyAI, Warning, TEXT("[Takedown] %s TAKEABLE (dist ok, dot=%.2f)"), *GetNameSafe(this), Dot);
		}
		else
		{
			UE_LOG(LogEnemyAI, Warning, TEXT("[Takedown] %s reject: outside rear arc (dot=%.2f, need <= %.2f)"),
				*GetNameSafe(this), Dot, ArcThreshold);
		}
	}
#endif

	return Dot <= ArcThreshold;
}

bool AEnemyCharacter::BeginTakedownHold(AActor* TakedownInstigator, FVector SnapLocation, float SnapYaw, float WatchdogTimeout)
{
	if (!CanBeTakenDown(TakedownInstigator)) return false;

	UE_LOG(LogEnemyAI, Warning, TEXT("[Takedown] %s BeginTakedownHold OK (watchdog=%.1f)"), *GetNameSafe(this), WatchdogTimeout);

	bPendingTakedownDeath = true;
	bTakedownFrozen = true;

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
		UE_LOG(LogEnemyAI, Warning, TEXT("[Takedown] %s FinishTakedownKill early-out: not frozen"), *GetNameSafe(this));
		return;
	}
	if (IsValid(HealthComponent) && HealthComponent->IsDead())
	{
		UE_LOG(LogEnemyAI, Warning, TEXT("[Takedown] %s FinishTakedownKill early-out: already dead"), *GetNameSafe(this));
		return;
	}

	bTakedownFrozen = false;

	if (UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(TakedownWatchdogTimerHandle);

	const APawn* InstigatorPawn = Cast<APawn>(TakedownInstigator);
	AController* InstigatorController = InstigatorPawn ? InstigatorPawn->GetController() : nullptr;

	UE_LOG(LogEnemyAI, Warning, TEXT("[Takedown] %s FinishTakedownKill applying %.0f damage"), *GetNameSafe(this), TakedownDamage);
	TakeDamage(TakedownDamage, FDamageEvent(), InstigatorController, TakedownInstigator);
}

bool AEnemyCharacter::ExecuteTakedown(AActor* TakedownInstigator)
{
	UE_LOG(LogEnemyAI, Warning, TEXT("[Takedown] %s ExecuteTakedown (instant path)"), *GetNameSafe(this));

	// Instant path: no snap (enemy stays in place), no watchdog (kill follows immediately).
	const FVector SnapLoc = GetActorLocation();
	const FVector ToInstigator = IsValid(TakedownInstigator)
		? (TakedownInstigator->GetActorLocation() - SnapLoc).GetSafeNormal2D()
		: -GetActorForwardVector();
	const float SnapYaw = FRotationMatrix::MakeFromX(-ToInstigator).Rotator().Yaw;

	if (!BeginTakedownHold(TakedownInstigator, SnapLoc, SnapYaw, 0.f))
	{
		UE_LOG(LogEnemyAI, Warning, TEXT("[Takedown] %s ExecuteTakedown aborted (BeginTakedownHold failed)"), *GetNameSafe(this));
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
