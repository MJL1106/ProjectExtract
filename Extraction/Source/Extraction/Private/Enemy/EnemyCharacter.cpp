// AEnemyCharacter — single character class for all enemy archetypes.

#include "EnemyCharacter.h"
#include "EnemyAIController.h"
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
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/DamageEvents.h"
#include "TimerManager.h"

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

	OwnedTags.AddTag(TAG_Character_Enemy);

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

void AEnemyCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (IsValid(HealthComponent))
		HealthComponent->OnDeath.AddUniqueDynamic(this, &AEnemyCharacter::HandleDeath);

	if (!IsValid(ArchetypeData))
	{
		UE_LOG(LogEnemyAI, Error, TEXT("%s: ArchetypeData not assigned — character will idle harmlessly."), *GetName());
		return;
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
	if (const UWorld* World = GetWorld())
		World->GetTimerManager().ClearAllTimersForObject(this);

	Super::EndPlay(EndPlayReason);
}

// --- IGameplayTagAssetInterface ---

void AEnemyCharacter::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
{
	TagContainer.AppendTags(OwnedTags);
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

// --- Aim API ---

void AEnemyCharacter::SetAimTarget(AActor* NewTarget)
{
	const AActor* OldTarget = CurrentAimTarget.Get();
	if (OldTarget == NewTarget) return;

	CurrentAimTarget = NewTarget;
	AimStartWorldTime = (IsValid(NewTarget) && GetWorld()) ? GetWorld()->GetTimeSeconds() : -1e9f;
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

// --- Move Speed ---

void AEnemyCharacter::SetMoveSpeedMode(EEnemyMoveSpeedMode Mode)
{
	if (!IsValid(ArchetypeData)) return;

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!IsValid(MoveComp)) return;

	MoveComp->MaxWalkSpeed = (Mode == EEnemyMoveSpeedMode::Combat)
		? ArchetypeData->CombatSpeed
		: ArchetypeData->PatrolSpeed;
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
			ShieldComponent->AttachToComponent(MeshComp, FAttachmentTransformRules::KeepRelativeTransform);
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

	// Persist as a discoverable body — the director's corpse registry owns cleanup (capped recycle).
	if (UEnemyDirectorSubsystem* Director = World->GetSubsystem<UEnemyDirectorSubsystem>())
	{
		Director->RegisterCorpse(this);
		return;
	}

	// No director (non-game world) — fall back to the old destroy-after-delay.
	if (!IsValid(ArchetypeData)) return;
	World->GetTimerManager().SetTimer(
		DestroyTimerHandle,
		this,
		&AEnemyCharacter::DestroyAfterDeath,
		ArchetypeData->DestroyDelay,
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
}

void AEnemyCharacter::DestroyAfterDeath()
{
	Destroy();
}

// --- Silent takedown ---

bool AEnemyCharacter::CanBeTakenDown(const AActor* TakedownInstigator) const
{
	if (!IsValid(TakedownInstigator) || !IsValid(ArchetypeData)) return false;
	if (IsValid(HealthComponent) && HealthComponent->IsDead()) return false;

	// Only an Unaware enemy can be taken down (design §4: Unaware/Dormant).
	const AEnemyAIController* AIC = Cast<AEnemyAIController>(GetController());
	const UEnemyAwarenessComponent* Awareness = AIC ? AIC->GetAwarenessComponent() : nullptr;
	if (!Awareness || Awareness->GetAwarenessState() != EEnemyAwarenessState::Unaware) return false;

	const FVector ToInstigator = TakedownInstigator->GetActorLocation() - GetActorLocation();
	if (ToInstigator.SizeSquared() > FMath::Square(ArchetypeData->TakedownRange)) return false;

	// Instigator must sit inside the rear arc (centred on backward).
	const float Dot = FVector::DotProduct(GetActorForwardVector(), ToInstigator.GetSafeNormal2D());
	return Dot <= -FMath::Cos(FMath::DegreesToRadians(ArchetypeData->TakedownRearArcDeg * 0.5f));
}

bool AEnemyCharacter::ExecuteTakedown(AActor* TakedownInstigator)
{
	if (!CanBeTakenDown(TakedownInstigator)) return false;

	bPendingTakedownDeath = true;
	OnTakedownExecuted.Broadcast(TakedownInstigator);

	// Silent kill: generic damage event (no hit-region path), no noise emission anywhere on this path.
	const APawn* InstigatorPawn = Cast<APawn>(TakedownInstigator);
	AController* InstigatorController = InstigatorPawn ? InstigatorPawn->GetController() : nullptr;
	TakeDamage(TakedownDamage, FDamageEvent(), InstigatorController, TakedownInstigator);
	return true;
}

// --- Body discovery ---

bool AEnemyCharacter::TryMarkBodyReported()
{
	if (bBodyReported) return false;
	bBodyReported = true;
	return true;
}

// --- Damage query ---

bool AEnemyCharacter::WasDamagedRecently(float Window) const
{
	const UWorld* World = GetWorld();
	if (!World) return false;
	return (World->GetTimeSeconds() - LastDamageWorldTime) <= Window;
}
