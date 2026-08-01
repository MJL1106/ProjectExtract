// AI companion character — follows player, engages enemies, revives downed teammates.

#include "CompanionCharacter.h"

#include "AIController.h"  // EAIFocusPriority / GetFocalPointForPriority — strafe clamp focus test
#include "AITypes.h"       // FAISystem::IsValidLocation — same
#include "AI/AITargetingStatics.h"
#include "Animation/AnimMontage.h"
#include "AI/CompanionDiag.h"
#include "AI/CompanionTuningDataAsset.h"
#include "CompanionAIController.h"
#include "CompanionBarkTypes.h"
#include "BarkSubsystem.h"
#include "EnemyGrenadeProjectile.h"
#include "EnemyGrenadierComponent.h"
#include "HealthComponent.h"
#include "FootstepNoiseComponent.h"
#include "WeaponBase.h"
#include "WeaponDataAsset.h"
#include "TraversalComponent.h"
#include "CompanionAnimInstance.h"
#include "SuppressionComponent.h"
#include "CoverPoseComponent.h"
#include "ExtractionTypes.h"
#include "Character/ExtractionPlayer.h"
#include "Character/ExtractionPlayerInterface.h"
#include "EnemyCharacter.h"
#include "TakedownVolume.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "UI/OverheadWidgetComponent.h"
#include "UI/CompanionModeIndicatorWidget.h"
#include "Game/ExtractionGameMode.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h" // companion.AimLog diagnostics
#include "DrawDebugHelpers.h"
#include "EnemyDebug.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"
#endif

namespace
{
	float CalculateReviveMontagePlayRate(float MontageLength, float BlendOutTime, float ExpectedDuration)
	{
		const float PlayableLength = MontageLength - BlendOutTime;
		return ExpectedDuration > 0.f && PlayableLength > 0.f
			? PlayableLength / ExpectedDuration : 1.f;
	}

	// A companion can appear after the last scan (the VIP is placed, but a respawn is a spawn), so
	// the list can't be built once. TActorIterator walks the whole level, so it can't run per frame
	// either — this is the compromise cadence.
	constexpr float AllyRescanInterval = 2.f;

	// Primary + armed VIP, minus ourselves. Sized once on the first build; Reset() then keeps the
	// slack, so the Reserve is a deliberate no-op on every rebuild after that and none reallocate.
	constexpr int32 ExpectedAllyCount = 1;
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCompanionReviveMontageTimingTest,
	"Extraction.Companion.Revive.MontageTiming",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCompanionReviveMontageTimingTest::RunTest(const FString& Parameters)
{
	constexpr float MontageLength = 1.9333167f;
	constexpr float BlendOutTime = 0.25f;
	constexpr float ReviveDuration = 5.f;
	const float PlayRate = CalculateReviveMontagePlayRate(MontageLength, BlendOutTime, ReviveDuration);

	TestEqual(TEXT("Montage reaches blend-out boundary when revive completes"),
		PlayRate * ReviveDuration, MontageLength - BlendOutTime, KINDA_SMALL_NUMBER);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRevivePatientYawTrimRangeTest,
	"Extraction.Companion.Revive.PatientYawTrimRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRevivePatientYawTrimRangeTest::RunTest(const FString& Parameters)
{
	const FProperty* Property = FindFProperty<FProperty>(ACompanionCharacter::StaticClass(),
		GET_MEMBER_NAME_CHECKED(ACompanionCharacter, PlayerRevivePatientYawTrimDeg));
	if (!TestNotNull(TEXT("Patient yaw trim property exists"), Property)) return false;

	TestEqual(TEXT("Patient yaw trim permits negative values"), Property->GetMetaData(TEXT("ClampMin")), TEXT("-180.0"));
	TestEqual(TEXT("Patient yaw trim has a symmetric maximum"), Property->GetMetaData(TEXT("ClampMax")), TEXT("180.0"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReviveMeshPoseRefreshContractTest,
	"Extraction.Companion.Revive.MeshPoseRefreshContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FReviveMeshPoseRefreshContractTest::RunTest(const FString& Parameters)
{
	const FString SourcePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(),
		TEXT("Source/Extraction/Private/Character/ExtractionPlayer.cpp")));
	FString Source;
	if (!TestTrue(TEXT("Player source is readable"), FFileHelper::LoadFileToString(Source, *SourcePath))) return false;

	const int32 RestoreIndex = Source.Find(TEXT("MeshComp->SetRelativeRotation(ReviverSavedMeshRelativeRotation);"));
	const int32 TickPoseIndex = Source.Find(TEXT("MeshComp->TickPose(0.f, false);"), ESearchCase::CaseSensitive,
		ESearchDir::FromStart, RestoreIndex);
	const int32 RefreshIndex = Source.Find(TEXT("MeshComp->RefreshBoneTransforms();"), ESearchCase::CaseSensitive,
		ESearchDir::FromStart, RestoreIndex);
	const int32 SeatClearIndex = Source.Find(TEXT("bReviverSeated = false;"), ESearchCase::CaseSensitive,
		ESearchDir::FromStart, RestoreIndex);

	TestTrue(TEXT("Mesh rotation restore exists"), RestoreIndex != INDEX_NONE);
	TestTrue(TEXT("Pose ticks after the mesh rotation restore"),
		RestoreIndex != INDEX_NONE && TickPoseIndex > RestoreIndex);
	TestTrue(TEXT("Bones refresh after the pose tick"), TickPoseIndex != INDEX_NONE && RefreshIndex > TickPoseIndex);
	TestTrue(TEXT("Pose refresh completes before clearing the seated state"),
		RefreshIndex != INDEX_NONE && SeatClearIndex > RefreshIndex);
	return true;
}
#endif

DEFINE_LOG_CATEGORY(LogCompanion);

static const FName NAME_IsDowned(TEXT("IsDowned"));

ACompanionCharacter::ACompanionCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetNetUpdateFrequency(10.0f);
	SetMinNetUpdateFrequency(5.0f);

	GetCapsuleComponent()->SetCapsuleSize(34.0f, 88.0f);

	HealthComponent = CreateDefaultSubobject<UHealthComponent>(TEXT("HealthComponent"));
	FootstepAudioComponent = CreateDefaultSubobject<UFootstepNoiseComponent>(TEXT("FootstepAudioComponent"));
	FootstepAudioComponent->SetEmitAINoise(false); // companion steps are flavour, not stimuli
	FootstepAudioComponent->SetAudioVolume(0.08f); // trails the player constantly — even 0.55 read as loud doubled player steps; near-silent is right
	SuppressionComponent = CreateDefaultSubobject<USuppressionComponent>(TEXT("SuppressionComponent"));
	CoverPoseComponent = CreateDefaultSubobject<UCoverPoseComponent>(TEXT("CoverPoseComponent"));
	TraversalComponent = CreateDefaultSubobject<UTraversalComponent>(TEXT("TraversalComponent"));

	HealthWidgetComponent = CreateDefaultSubobject<UOverheadWidgetComponent>(TEXT("HealthWidget"));
	HealthWidgetComponent->SetupAttachment(GetMesh(), TEXT("head"));
	HealthWidgetComponent->SetRelativeLocation(FVector(0.f, 0.f, 30.f));
	HealthWidgetComponent->SetWidgetSpace(EWidgetSpace::Screen);
	HealthWidgetComponent->SetDrawSize(FVector2D(150.f, 40.f));
	HealthWidgetComponent->SetPivot(FVector2D(0.5f, 0.5f));
	HealthWidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HealthWidgetComponent->SetTwoSided(false);
	HealthWidgetComponent->SetVisibility(false);

	// Sits above the health bar so the two never overlap.
	ModeWidgetComponent = CreateDefaultSubobject<UOverheadWidgetComponent>(TEXT("ModeWidget"));
	ModeWidgetComponent->SetupAttachment(GetMesh(), TEXT("head"));
	ModeWidgetComponent->SetRelativeLocation(FVector(0.f, 0.f, 60.f));
	ModeWidgetComponent->SetWidgetSpace(EWidgetSpace::Screen);
	ModeWidgetComponent->SetDrawSize(FVector2D(64.f, 64.f));
	ModeWidgetComponent->SetPivot(FVector2D(0.5f, 0.5f));
	ModeWidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ModeWidgetComponent->SetTwoSided(false);
	ModeWidgetComponent->SetVisibility(false);

	// Configure inherited skeletal mesh — designer assigns mesh + anim class on BP_Companion
	GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, -88.f));
	GetMesh()->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));

	// Bug 6: weapon hitscan traces ECC_Visibility, which the inherited CharacterMesh profile ignores —
	// block it on the mesh so enemy fire actually registers on the companion.
	GetMesh()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	TakedownKnifeMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("TakedownKnifeMesh"));
	TakedownKnifeMesh->SetupAttachment(GetMesh(), KnifeAttachSocket);
	TakedownKnifeMesh->SetHiddenInGame(true);
	TakedownKnifeMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TakedownKnifeMesh->SetCastShadow(false);
	TakedownKnifeMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	TakedownKnifeMesh->SetComponentTickEnabled(false);

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

void ACompanionCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// Tuning-driven speeds become reachable only once the companion AI controller is attached.
	ApplyMovementSpeeds();
}

void ACompanionCharacter::UnPossessed()
{
	// No BT task can survive losing the controller -- release any speed override so the pawn
	// is not pinned at a stale task speed if it is ever re-possessed.
	ClearTaskSpeedOverride();
	ClearCommandedCoverHold();
	ClearCoveringFire();
	CommandedCoverTarget = FCover();
	CommandedCoverTargetStamp = -1e9f;

	Super::UnPossessed();
}

ACompanionCharacter* ACompanionCharacter::GetPrimaryCompanion(UWorld* World)
{
	if (!World) return nullptr;
	for (TActorIterator<ACompanionCharacter> It(World); It; ++It)
		if (IsValid(*It) && It->bIsPrimaryCompanion) return *It;
	return nullptr;
}

bool ACompanionCharacter::IsReviveClaimantCapable(const AActor* Claimant)
{
	const ACompanionCharacter* Companion = Cast<ACompanionCharacter>(Claimant);
	if (!IsValid(Companion)) return false;
	if (Companion->GetIsCompanionDBNO()) return false;
	if (!Companion->GetController()) return false; // captive extractee — can't help yet
	const UHealthComponent* HC = Companion->GetHealthComponent();
	if (IsValid(HC) && HC->IsDead()) return false;
	return true;
}

bool ACompanionCharacter::IsAnyCompanionReviveCapable(UWorld* World, const ACompanionCharacter* Exclude)
{
	if (!World) return false;
	for (TActorIterator<ACompanionCharacter> It(World); It; ++It)
	{
		const ACompanionCharacter* Companion = *It;
		if (!IsValid(Companion) || Companion == Exclude) continue;
		if (!IsReviveClaimantCapable(Companion)) continue;
		return true;
	}
	return false;
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
	// PossessedBy re-applies again once the AI controller (and its tuning asset) is attached.
	ApplyMovementSpeeds();

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

		// The widget binds OnHealthChanged itself, but delegate-bind nodes for OnShieldChanged
		// can't be authored in its graph externally — C++ pushes shield changes into the widget's
		// OnShieldUpdated custom event instead.
		if (HealthComponent && !HealthComponent->OnShieldChanged.IsAlreadyBound(this, &ACompanionCharacter::HandleShieldChangedForWidget))
			HealthComponent->OnShieldChanged.AddDynamic(this, &ACompanionCharacter::HandleShieldChangedForWidget);
	}

	if (ModeWidgetClass && ModeWidgetComponent)
	{
		ModeWidgetComponent->SetWidgetClass(ModeWidgetClass);
		ModeWidgetComponent->SetVisibility(true);
		TryLinkModeWidget();
	}

	UE_LOG(LogCompanion, Log, TEXT("%s spawned with tag Character.Companion"), *GetName());
}

void ACompanionCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// No BT task survives EndPlay -- unconditionally release any speed override so it cannot
	// persist into a respawn or a stale actor state.
	ClearTaskSpeedOverride();

	if (IsValid(TraversalComponent))
	{
		TraversalComponent->OnTraversalStarted.RemoveAll(this);
		TraversalComponent->OnTraversalEnded.RemoveAll(this);
	}

	DisarmCommandedTakedown();
	EndSearchRoomExposure();
	ClearCommandedCoverHold();
	ClearCoveringFire();
	CommandedCoverTarget = FCover();
	CommandedCoverTargetStamp = -1e9f;

	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BleedoutTimerHandle);
		World->GetTimerManager().ClearTimer(ShootDelayTimerHandle);
		World->GetTimerManager().ClearTimer(ModeWidgetLinkTimerHandle);
		World->GetTimerManager().ClearTimer(KnifeKillTimerHandle);
		World->GetTimerManager().ClearTimer(CallForHelpTimerHandle);
		World->GetTimerManager().ClearTimer(CatchupBarkTimerHandle);
	}

	if (HealthComponent)
	{
		HealthComponent->OnDeath.RemoveDynamic(this, &ACompanionCharacter::HandleDeath);
		HealthComponent->OnRevive.RemoveDynamic(this, &ACompanionCharacter::HandleRevive);
		HealthComponent->OnShieldChanged.RemoveDynamic(this, &ACompanionCharacter::HandleShieldChangedForWidget);
	}

	Super::EndPlay(EndPlayReason);
}

void ACompanionCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (CurrentAimTarget.IsValid())
		TimeAimingAtCurrentTarget += DeltaTime;

	// Strafe clamp re-resolve, EDGE ONLY. ApplyMovementSpeeds is otherwise invoked solely on
	// sprint / stance / stealth / catch-up-pace edges, so a Gameplay focus appearing or vanishing
	// under it would leave MaxWalkSpeed resolved against the old answer -- a companion that acquires
	// a focus mid-jog keeps running the forward-only sprint clip at 550, and one that drops its focus
	// keeps crawling at the strafe cap. Deliberately NOT a per-frame re-resolve. Any task that owns
	// MaxWalkSpeed does so via SetTaskSpeedOverride (ApplyMovementSpeeds skips overridden channels),
	// so the edge test here cannot fight active task speeds.
	const bool bStrafingNow = IsStrafingForFocus();
	if (bStrafingNow != bLastStrafingForFocus)
	{
		bLastStrafingForFocus = bStrafingNow;
		ApplyMovementSpeeds();
	}

	// Covering-fire clock runs unconditionally in Tick so it is not gated by any combat-task
	// branch. The reload-held mirror is written once per combat-task tick.
	if (bCoveringFireActive || bCoveringFirePending)
		TickCoveringFire(DeltaTime, bCoveringFireReloadHeld || IsReloading());

	// Takedown hush heartbeat. Deliberately driven from Tick and nowhere else: dying, being downed,
	// having the BT abort the task or the level tearing down all stop Tick, so the pocket wakes on its
	// own without a single teardown call site to forget.
	if (bTakedownArmed || bTakedownExecuting || bTakedownMontagePlaying)
		RefreshTakedownWindow();

	TickPlayerSoftSeparation();
	TickAllySoftSeparation();

#if ENABLE_DRAW_DEBUG
	if (GetDrawDistancesLevel() > 0 && bIsPrimaryCompanion)
	{
		constexpr float PressureLabelOffset = 60.f;
		constexpr float FallbackHalfHeight = 90.f;
		constexpr float StaleThreshold = 0.5f;
		const float HH = GetCapsuleComponent()
			? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : FallbackHalfHeight;
		const FVector LabelPos = GetActorLocation() + FVector(0.f, 0.f, HH + PressureLabelOffset);
		const UWorld* DrawWorld = GetWorld();
		const float Age = IsValid(DrawWorld) ? (DrawWorld->GetTimeSeconds() - CachedPressure01Time) : 0.f;
		const bool bStale = Age > StaleThreshold;
		const FString Label = bStale
			? FString::Printf(TEXT("p01 %.2f (stale)"), CachedPressure01)
			: FString::Printf(TEXT("p01 %.2f"), CachedPressure01);
		const FColor LabelColor = bStale ? FColor(128, 128, 128) : FColor::Green;
		DrawDebugString(GetWorld(), LabelPos, Label, nullptr, LabelColor, DeltaTime * 2.f, true);
	}
#endif
}

void ACompanionCharacter::Bark(ECompanionBarkType Type, FName Context) const
{
	if (!IsValid(BarkSet)) return;

	const UWorld* World = GetWorld();
	UBarkSubsystem* Barks = IsValid(World) ? World->GetSubsystem<UBarkSubsystem>() : nullptr;
	if (Barks)
		Barks->RequestCompanionBark(this, BarkSet, Type, Context);
}

float ACompanionCharacter::SpeakScriptedLine(USoundBase* Sound) const
{
	const UWorld* World = GetWorld();
	UBarkSubsystem* Barks = IsValid(World) ? World->GetSubsystem<UBarkSubsystem>() : nullptr;
	if (!Barks) return 0.f;

	// BarkSet only supplies attenuation/volume here — a missing set must not silently drop an
	// explicitly assigned scripted line.
	if (!IsValid(BarkSet))
		UE_LOG(LogCompanion, Warning, TEXT("%s: SpeakScriptedLine with no BarkSet — playing unattenuated"), *GetName());

	USoundAttenuation* Attenuation = IsValid(BarkSet) ? BarkSet->Attenuation.Get() : nullptr;
	const float Volume = IsValid(BarkSet) ? BarkSet->VolumeMultiplier : 1.f;
	return Barks->RequestScriptedLine(this, Sound, Attenuation, Volume);
}

float ACompanionCharacter::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (bIsDBNO) return 0.f;
	if (bIsRevivingPlayer) DamageAmount *= ReviveDamageMultiplier;
	else if (bRescueCommitted) DamageAmount *= RescueApproachDamageMultiplier;
	else if (bCoveringFireActive) DamageAmount *= CoveringFireDamageMultiplier;
	const float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (IsValid(HealthComponent))
	{
		HealthComponent->TakeDamage(ActualDamage);
		UE_LOG(LogCompanion, Verbose, TEXT("Companion took %.1f damage — HP: %.0f / Shield: %.0f"),
			ActualDamage, HealthComponent->GetCurrentHealth(), HealthComponent->GetCurrentShield());
	}
	if (ActualDamage > 0.f && GetWorld())
	{
		const float Now = GetWorld()->GetTimeSeconds();
		LastDamageWorldTime = Now;
		// Prune anything older than the widest release window consumers use (2x commit window,
		// bounded generously) so the ring never grows past a handful of entries.
		RecentDamageTimes.RemoveAll([Now](float T) { return Now - T > 20.f; });
		RecentDamageTimes.Add(Now);

		// Attacker stamp for the flanker response (cover break + target steal). Prefer the
		// instigating pawn — DamageCauser is usually the weapon/projectile, whose location is
		// useless for the exposed-side geometry test.
		AActor* Attacker = EventInstigator ? EventInstigator->GetPawn() : nullptr;
		if (!Attacker && IsValid(DamageCauser)) Attacker = DamageCauser->GetInstigator();
		if (!Attacker) Attacker = DamageCauser;
		if (IsValid(Attacker) && Attacker != this)
		{
			LastDamageAttacker = Attacker;
			LastAttackerStampTime = Now;
		}
	}

	// Hit react — skip if dying (death path takes over).
	if (ActualDamage > 0.f && IsValid(HealthComponent) && HealthComponent->IsAlive())
	{
		if (USkeletalMeshComponent* MeshComp = GetMesh())
		{
			if (UCompanionAnimInstance* AnimInst = Cast<UCompanionAnimInstance>(MeshComp->GetAnimInstance()))
				AnimInst->PlayHitReactMontage(1.0f);
		}

		Bark(ECompanionBarkType::CompanionHurt);
	}

	return ActualDamage;
}

bool ACompanionCharacter::IsSuppressed(float Window) const
{
	if (IsValid(SuppressionComponent) && SuppressionComponent->IsSuppressed()) return true;
	if (Window <= 0.f || !GetWorld()) return false;
	return (GetWorld()->GetTimeSeconds() - LastDamageWorldTime) < Window;
}

int32 ACompanionCharacter::GetRecentDamageCount(float Window) const
{
	if (Window <= 0.f || !GetWorld()) return 0;
	const float Now = GetWorld()->GetTimeSeconds();
	int32 Count = 0;
	for (float T : RecentDamageTimes)
		if (Now - T < Window) ++Count;
	return Count;
}

float ACompanionCharacter::GetSuppression01() const
{
	return IsValid(SuppressionComponent) ? SuppressionComponent->GetSuppression01() : 0.f;
}

AActor* ACompanionCharacter::GetRecentAttacker(float Window) const
{
	if (Window <= 0.f || !GetWorld()) return nullptr;
	if (GetWorld()->GetTimeSeconds() - LastAttackerStampTime >= Window) return nullptr;
	return LastDamageAttacker.Get();
}

void ACompanionCharacter::StampCompromiseBreak()
{
	if (GetWorld())
		LastCompromiseBreakTime = GetWorld()->GetTimeSeconds();
}

void ACompanionCharacter::StampNaturalCoverRelease()
{
	if (GetWorld())
		LastNaturalReleaseTime = GetWorld()->GetTimeSeconds();

	Bark(ECompanionBarkType::Repositioning);
}

void ACompanionCharacter::StampCoverCommit()
{
	if (GetWorld())
		LastCoverCommitTime = GetWorld()->GetTimeSeconds();

	Bark(ECompanionBarkType::TakingCover);
}

void ACompanionCharacter::StampConfirmedKill()
{
	if (GetWorld())
		LastConfirmedKillTime = GetWorld()->GetTimeSeconds();
}

// Params are NOT named WalkSpeed/CrouchedWalkSpeed: those are member fallbacks on this class and
// shadowing them trips C4458, which this module escalates to an error.
void ACompanionCharacter::SetTaskSpeedOverride(float InWalkSpeed, float InCrouchedSpeed)
{
	TaskSpeedOverrideWalk = InWalkSpeed;
	TaskSpeedOverrideCrouched = InCrouchedSpeed;

	// Apply the overridden values to the CMC immediately so the task's authored pace takes effect
	// on the same frame it was set, not the next Tick edge.
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!MoveComp) return;
	if (InWalkSpeed > 0.f)
		MoveComp->MaxWalkSpeed = InWalkSpeed;
	if (InCrouchedSpeed > 0.f)
		MoveComp->MaxWalkSpeedCrouched = InCrouchedSpeed;
}

void ACompanionCharacter::ClearTaskSpeedOverride()
{
	TaskSpeedOverrideWalk = 0.f;
	TaskSpeedOverrideCrouched = 0.f;
}

void ACompanionCharacter::SetFollowCatchupPace(bool bPace)
{
	if (bFollowCatchupPace == bPace) return;
	bFollowCatchupPace = bPace;
	ApplyMovementSpeeds();

	// Bark only when the gap is real: catch-up must hold for FallingBehindBarkDelay unbroken.
	UWorld* World = GetWorld();
	if (!World) return;
	if (bPace)
		World->GetTimerManager().SetTimer(CatchupBarkTimerHandle, FTimerDelegate::CreateWeakLambda(this,
			[this] { if (bFollowCatchupPace) Bark(ECompanionBarkType::FallingBehind); }),
			FallingBehindBarkDelay, false);
	else
		World->GetTimerManager().ClearTimer(CatchupBarkTimerHandle);
}

void ACompanionCharacter::SetCoverCommitGrant(bool bPending)
{
	CoverCommitGrantStamp = (bPending && GetWorld()) ? GetWorld()->GetTimeSeconds() : -1e9f;
}

bool ACompanionCharacter::ConsumeCoverCommitGrant()
{
	// Generous lifetime: a Cover Me grant must survive a slow cover commit, since the window is
	// already counting down while the companion repositions. Other callers (cover switch,
	// take-cover command, combat cover commit) consume within one BT tick, well under this.
	const float Stamp = CoverCommitGrantStamp;
	CoverCommitGrantStamp = -1e9f;
	return GetWorld() && (GetWorld()->GetTimeSeconds() - Stamp) <= CoveringFireArmTimeout;
}

void ACompanionCharacter::SetCommandedCoverTarget(const FCover& Cover)
{
	CommandedCoverTarget = Cover;
	CommandedCoverTargetStamp = GetWorld() ? GetWorld()->GetTimeSeconds() : -1e9f;
}

bool ACompanionCharacter::ConsumeCommandedCoverTarget(FCover& OutCover)
{
	// Same one-shot anti-stale contract as ConsumeCoverCommitGrant.
	constexpr float TargetLifetime = 5.f;
	const FCover Stored = CommandedCoverTarget;
	const float Stamp = CommandedCoverTargetStamp;
	CommandedCoverTarget = FCover();
	CommandedCoverTargetStamp = -1e9f;
	if (!Stored.IsValid()) return false;
	if (!GetWorld() || (GetWorld()->GetTimeSeconds() - Stamp) > TargetLifetime) return false;
	OutCover = Stored;
	return true;
}

void ACompanionCharacter::SetCommandedCoverHold(const FVector& AnchorLocation, float LeashRadius, const FCover& HoldCover)
{
	bCommandedCoverHoldActive = true;
	CommandedCoverHoldAnchor = AnchorLocation;
	CommandedCoverHoldLeashRadius = LeashRadius;
	CommandedCoverHoldCover = HoldCover;
	CommandedCoverCombatStartTime = -1e9f; // re-arm: a fresh hold has no combat clock
	bCommandedCoverCombatGraceArmed = false;
}

void ACompanionCharacter::StampCommandedCoverCombatStart()
{
	// Stamp once. Target flicker must not re-stamp.
	if (CommandedCoverCombatStartTime < 0.f && GetWorld())
		CommandedCoverCombatStartTime = GetWorld()->GetTimeSeconds();
	bCommandedCoverCombatGraceArmed = true;
}

void ACompanionCharacter::ClearCommandedCoverHold()
{
	if (bCommandedCoverHoldActive)
	{
		bCommandedCoverHoldActive = false;
		CommandedCoverHoldAnchor = FVector::ZeroVector;
		CommandedCoverHoldLeashRadius = 0.f;
		CommandedCoverHoldCover = FCover();
		CommandedCoverCombatStartTime = -1e9f;
		bCommandedCoverCombatGraceArmed = false;
	}
	// Unconditional, and outside the active guard: these one-shots are armed by the same task that
	// latches the hold, so every teardown that drops the hold must also disarm them. Leaving them
	// set would suppress the multi-threat re-rank or the distance/Z gate on the next autonomous commit.
	bCommandedCoverSkipRerank = false;
	bCommandedCoverBypass = false;
}

void ACompanionCharacter::ArmCoveringFire(float Duration)
{
	ClearCoveringFire();
	bCoveringFirePending = true;
	CoveringFireDuration = Duration;
	CoveringFireRemaining = Duration;
}

void ACompanionCharacter::StartCoveringFire()
{
	// Already running — do not re-stamp CoveringFireStartTime or the hard ceiling pushes out,
	// leaking the 0.4x damage-resistance window.
	if (bCoveringFireActive) return;
	if (!bCoveringFirePending) return;
	bCoveringFirePending = false;
	bCoveringFireActive = true;
	CoveringFireStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
}

void ACompanionCharacter::TickCoveringFire(float DeltaSeconds, bool bReloadHeld)
{
	if (!bCoveringFireActive) return;

	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.f;

	// Hard wall-clock ceiling — force-clears regardless of pause state.
	if ((Now - CoveringFireStartTime) > (CoveringFireDuration + CoveringFireCeilingSlack))
	{
		ClearCoveringFire();
		return;
	}

	// Clock only ticks while the companion is actually working the window. It stops for a reload,
	// and for idle time sat in cover not peeking: during covering fire the companion is only meant
	// to break into cover to reload, so hunker time is not window time. Without this the window
	// could burn its last seconds behind a wall doing nothing.
	if (!bReloadHeld && !bCoveringFireCoverIdle)
	{
		CoveringFireRemaining -= DeltaSeconds;
	}

	if (CoveringFireRemaining <= 0.f)
	{
		CoveringFireRemaining = 0.f;
		ClearCoveringFire();
		return;
	}

	// Throttle the multicast to ~10 Hz while counting, but fire immediately on pause edges
	// so the HUD can show a distinct paused state.
	const float Quantized = FMath::FloorToFloat(CoveringFireRemaining * 10.f) * 0.1f;
	const bool bPausedStateChanged = (bReloadHeld != bCoveringFireLastBroadcastPaused);
	if (Quantized != CoveringFireLastBroadcast || bPausedStateChanged)
	{
		CoveringFireLastBroadcast = Quantized;
		bCoveringFireLastBroadcastPaused = bReloadHeld;
		OnCoveringFireTick.Broadcast(CoveringFireRemaining, bReloadHeld);
	}
}

void ACompanionCharacter::ClearCoveringFire()
{
	const bool bWasActive = bCoveringFireActive || bCoveringFirePending;
	// Stamp cooldown only when a window actually ran (not on arm, not on teardown clears).
	if (bCoveringFireActive && GetWorld())
		CoveringFireEndTime = GetWorld()->GetTimeSeconds();
	bCoveringFirePending = false;
	bCoveringFireActive = false;
	CoveringFireRemaining = 0.f;
	CoveringFireDuration = 0.f;
	CoveringFireStartTime = -1e9f;
	bCoveringFireReloadHeld = false;
	bCoveringFireCoverIdle = false;
	CoveringFireLastBroadcast = -1.f;
	bCoveringFireLastBroadcastPaused = false;
	ClearAimLocationOverride();

	if (bWasActive)
		OnCoveringFireTick.Broadcast(0.f, false);
}

float ACompanionCharacter::GetCoveringFireCooldownRemaining() const
{
	if (CoveringFireCooldown <= 0.f || CoveringFireEndTime < 0.f) return 0.f;
	const UWorld* World = GetWorld();
	if (!World) return 0.f;
	return FMath::Max(0.f, CoveringFireCooldown - (World->GetTimeSeconds() - CoveringFireEndTime));
}

bool ACompanionCharacter::IsCoveringFireOnCooldown() const
{
	return GetCoveringFireCooldownRemaining() > 0.f;
}

void ACompanionCharacter::StampCombatTargetSeen()
{
	const UWorld* World = GetWorld();
	if (World)
		LastCombatTargetSeenTime = World->GetTimeSeconds();
}

float ACompanionCharacter::GetTimeSinceCombatTarget() const
{
	const UWorld* World = GetWorld();
	if (!World || LastCombatTargetSeenTime <= -1e8f)
		return TNumericLimits<float>::Max();
	return World->GetTimeSeconds() - LastCombatTargetSeenTime;
}

void ACompanionCharacter::StampCombatContact()
{
	const UWorld* World = GetWorld();
	if (World)
		LastCombatContactTime = World->GetTimeSeconds();
}

float ACompanionCharacter::GetTimeSinceCombatContact() const
{
	const UWorld* World = GetWorld();
	if (!World || LastCombatContactTime <= -1e8f)
		return TNumericLimits<float>::Max();
	return World->GetTimeSeconds() - LastCombatContactTime;
}

float ACompanionCharacter::GetHealthFraction() const
{
	if (!IsValid(HealthComponent)) return 1.f;
	return HealthComponent->GetHealthPercent();
}

float ACompanionCharacter::GetAmmoFraction() const
{
	if (!IsValid(CurrentWeapon)) return 1.f;
	const UWeaponDataAsset* Data = CurrentWeapon->GetWeaponData();
	if (!Data || Data->MagazineSize <= 0) return 1.f;
	return static_cast<float>(CurrentWeapon->GetCurrentAmmo()) / static_cast<float>(Data->MagazineSize);
}

void ACompanionCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(ACompanionCharacter, bIsSprinting, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(ACompanionCharacter, Posture, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(ACompanionCharacter, bLowReadyAim, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(ACompanionCharacter, Mode, COND_SkipOwner);
	DOREPLIFETIME(ACompanionCharacter, bIsDBNO);
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

// --- Mode API ---

void ACompanionCharacter::SetMode(ECompanionMode NewMode)
{
	if (!HasAuthority()) return;
	if (NewMode == Mode) return;

	// Same ordering invariant as SetPosture: Mode assigned before broadcast.
	const ECompanionMode OldMode = Mode;
	Mode = NewMode;

	// A fresh Stealth order always starts unbroken; leaving Stealth clears the flag so a later
	// return to Stealth doesn't inherit a stale broken state.
	bStealthBroken = false;
	if (NewMode == ECompanionMode::Stealth && GetWorld())
		StealthPinTime = GetWorld()->GetTimeSeconds();

	// Clamps on the Stealth MODE boundary, not an IsStealthActive edge: broken stealth reads as
	// inactive on both sides of a Stealth->Combat switch, which left the crouch-mirror's crouch
	// and the stealth speed clamps latched. Non-stealth cycles still skip — an unconditional call
	// would UnCrouch out of crouch cover mid-fight. A stale catch-up stage from the last stealth
	// stint would sprint-pop the clamps; follow re-evaluates next tick.
	if ((OldMode == ECompanionMode::Stealth) != (NewMode == ECompanionMode::Stealth))
	{
		StealthCatchupStage = EStealthCatchup::None;
		ApplyStealthMovementClamps();
	}

	UE_LOG(LogCompanion, Log, TEXT("Companion mode -> %s"), *UEnum::GetValueAsString(Mode));
	OnRep_Mode();

	// Mode switches are player orders — acknowledge them (hushed for stealth).
	Bark(NewMode == ECompanionMode::Stealth ? ECompanionBarkType::GoingStealth : ECompanionBarkType::AckGeneric);
}

void ACompanionCharacter::OnRep_Mode()
{
	// No state mutation — broadcast only. See SetMode for ordering invariant.
	OnModeChanged.Broadcast(Mode);
}

void ACompanionCharacter::SetStealthBroken(bool bBroken)
{
	if (!HasAuthority()) return;
	if (bStealthBroken == bBroken) return;

	bStealthBroken = bBroken;
	if (!bBroken && GetWorld())
		StealthPinTime = GetWorld()->GetTimeSeconds();
	// Stale catch-up stage from before the break would make the re-pin clamps sprint-pop.
	StealthCatchupStage = EStealthCatchup::None;
	UE_LOG(LogCompanion, Log, TEXT("Companion stealth %s"), bBroken ? TEXT("BROKEN") : TEXT("re-pinned"));
	ApplyStealthMovementClamps();

	if (bBroken)
		Bark(ECompanionBarkType::StealthBroken);
}

// --- Post-breach engagement grant ---

void ACompanionCharacter::SetPostBreachEngagement(const FVector& Anchor, float Radius, float Duration)
{
	if (!HasAuthority() || Radius <= 0.f || Duration <= 0.f) return;

	PostBreachAnchor = Anchor;
	PostBreachRadiusSq = Radius * Radius;
	PostBreachExpiryTime = GetWorld() ? GetWorld()->GetTimeSeconds() + Duration : -1.f;
	UE_LOG(LogCompanion, Log, TEXT("%s: post-breach engagement grant at %s r=%.0f for %.1fs"),
		*GetName(), *Anchor.ToCompactString(), Radius, Duration);
}

void ACompanionCharacter::ClearPostBreachEngagement()
{
	PostBreachRadiusSq = 0.f;
	PostBreachExpiryTime = -1.f;
}

bool ACompanionCharacter::IsWithinPostBreachEngagement(const FVector& Location) const
{
	if (PostBreachRadiusSq <= 0.f) return false;
	const UWorld* World = GetWorld();
	if (!World || World->GetTimeSeconds() > PostBreachExpiryTime) return false;
	return FVector::DistSquared(Location, PostBreachAnchor) <= PostBreachRadiusSq;
}

void ACompanionCharacter::BeginSearchRoomExposure(const FVector& RoomAnchor, float Radius, bool bSilentStartle)
{
	if (!HasAuthority() || Radius <= 0.f) return;

	SearchRoomExposure.Begin(RoomAnchor, Radius, bSilentStartle);
	UE_LOG(LogCompanion, Log, TEXT("%s: search-room exposure begin gen=%u anchor=%s r=%.0f silentStartle=%d"),
		*GetName(), SearchRoomExposure.GetActiveGeneration(), *RoomAnchor.ToCompactString(), Radius,
		bSilentStartle ? 1 : 0);
}

void ACompanionCharacter::EndSearchRoomExposure()
{
	if (!SearchRoomExposure.IsActive()) return;

	const uint32 EndingGeneration = SearchRoomExposure.GetActiveGeneration();
	SearchRoomExposure.End();
	UE_LOG(LogCompanion, Log, TEXT("%s: search-room exposure end gen=%u"), *GetName(), EndingGeneration);
}

void ACompanionCharacter::TryLinkModeWidget()
{
	if (!IsValid(ModeWidgetComponent)) return;

	if (UCompanionModeIndicatorWidget* ModeWidget = Cast<UCompanionModeIndicatorWidget>(ModeWidgetComponent->GetUserWidgetObject()))
	{
		ModeWidget->SetCompanion(this);
		return;
	}

	// Widget not constructed yet (screen-space widgets build on first render) — retry shortly.
	if (UWorld* World = GetWorld())
		World->GetTimerManager().SetTimer(ModeWidgetLinkTimerHandle, this, &ACompanionCharacter::TryLinkModeWidget, 0.25f, false);
}

bool ACompanionCharacter::IsStanceOwnedElsewhere() const
{
	// Single definition of the stance-ownership set, shared by ApplyStealthMovementClamps and the BT
	// service's stance backstop. Two hand-written copies of this list is exactly how one of them ends
	// up popping a crouch the other was protecting: takedowns own the crouch-approach and the authored
	// knife pose, traversal resizes the capsule mid-vault, route legs set their own stances, and a
	// downed companion's stance belongs to the DBNO pose.
	if (bIsDBNO || bTakedownArmed || bTakedownExecuting || bTakedownMontagePlaying) return true;
	if (IsValid(TraversalComponent) && TraversalComponent->IsBusy()) return true;

	const AAIController* AIC = Cast<AAIController>(GetController());
	const UBlackboardComponent* BB = AIC ? AIC->GetBlackboardComponent() : nullptr;
	return BB && BB->GetValueAsBool(ACompanionAIController::BB_RouteActive);
}

void ACompanionCharacter::ApplyStealthMovementClamps()
{
	// Stance enforcement yields to systems that own stance/aim right now (mirrors the BT service's
	// enforcement-yield set). A stealth break / mode keypress landing mid-vault must not Crouch/
	// UnCrouch under them. Speeds always apply.
	const bool bStanceOwnedElsewhere = IsStanceOwnedElsewhere();

	if (!bStanceOwnedElsewhere)
	{
		if (IsStealthActive())
		{
			if (StealthCatchupStage == EStealthCatchup::Sprint)
			{
				// Clearly losing the player — break the low profile and sprint to close the gap.
				// SetSprinting's stealth veto passes only in this stage.
				UnCrouch();
				bCrouchOwnedByStealth = false;
				SetSprinting(true);
			}
			else
			{
				// Stance (crouch/stand) is no longer force-applied here (F4a): Stealth defaults to
				// a standing slow-walk, and only the service's crouch-mirror (mirroring the
				// player's own crouch state) pulls the companion down. Speed still applies below.
				SetSprinting(false);
			}
		}
		else
		{
			// Only release a crouch the stealth mirror applied — a cover task's crouch is not
			// ours to pop (a stealth break / mode switch can land while crouched at cover).
			if (bCrouchOwnedByStealth) UnCrouch();
		}
	}

	// The ownership claim drops whenever stealth rules stop applying — even when a DBNO/route/
	// takedown/traversal owns stance and the UnCrouch above was skipped (that owner restores
	// stance in its own teardown). A claim latched through the yield would pop a later cover crouch.
	if (!IsStealthActive())
		bCrouchOwnedByStealth = false;

	ApplyMovementSpeeds();
}

void ACompanionCharacter::SetStealthCatchup(EStealthCatchup NewStage)
{
	if (!HasAuthority()) return;
	if (StealthCatchupStage == NewStage) return;

	StealthCatchupStage = NewStage;
	if (IsStealthActive()) ApplyStealthMovementClamps();
	else ApplyMovementSpeeds();
}

void ACompanionCharacter::MirrorCrouch(bool bCrouch)
{
	if (!HasAuthority()) return;

	if (bCrouch)
	{
		Crouch();
		bCrouchOwnedByStealth = true;
	}
	else
	{
		UnCrouch();
		bCrouchOwnedByStealth = false;
	}
}

// --- Low Ready Aim ---

void ACompanionCharacter::SetLowReadyAim(bool bNewLowReady)
{
	if (!HasAuthority()) return;
	if (bLowReadyAim == bNewLowReady) return;
	bLowReadyAim = bNewLowReady;

	// AimLog diagnostics (companion.AimLog 1): change-only, so cost is the edge, not per call.
	if (const IConsoleVariable* AimLogCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("companion.AimLog"));
		AimLogCVar && AimLogCVar->GetInt() != 0)
		UE_LOG(LogCompanion, Display, TEXT("[AimLog] SetLowReadyAim -> %d"), (int32)bNewLowReady);

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
	if (bSprint && !CanSprint()) return;
	// Stealth rules: no sprinting — except the Sprint catch-up stage (clearly losing the player).
	if (bSprint && IsStealthActive() && StealthCatchupStage != EStealthCatchup::Sprint) return;
	if (bIsSprinting == bSprint) return;

	bIsSprinting = bSprint;
	OnRep_IsSprinting();
}

void ACompanionCharacter::OnRep_IsSprinting()
{
	ApplyMovementSpeeds();
}

const UCompanionTuningDataAsset* ACompanionCharacter::GetTuning() const
{
	const ACompanionAIController* AIC = Cast<ACompanionAIController>(GetController());
	return AIC ? AIC->GetTuning() : nullptr;
}

// --- Grenade (Grenadier-pattern reuse) ---

UEnemyGrenadierComponent* ACompanionCharacter::GetOrCreateGrenadierComponent()
{
	if (IsValid(GrenadierComponent)) return GrenadierComponent;

	const UCompanionTuningDataAsset* T = GetTuning();
	if (!T || !T->bGrenadeEnabled || !T->GrenadeProjectileClass) return nullptr;

	FGrenadierInitParams Params;
	Params.GrenadeSupply = T->GrenadeSupply;
	Params.GrenadeCooldown = T->GrenadeCooldown;
	Params.GrenadeFuseTime = T->GrenadeFuseTime;
	Params.GrenadeTelegraphTime = T->GrenadeTelegraphTime;
	Params.GrenadeMinRange = T->GrenadeMinRange;
	Params.GrenadeMaxRange = T->GrenadeMaxRange;
	Params.GrenadeDamage = T->GrenadeDamage;
	Params.GrenadeDamageRadius = T->GrenadeDamageRadius;
	Params.GrenadeLandingDistanceScale = T->GrenadeLandingDistanceScale;
	Params.GrenadeThrowSocket = T->GrenadeThrowSocket;
	Params.GrenadeProjectileClass = T->GrenadeProjectileClass;

	GrenadierComponent = NewObject<UEnemyGrenadierComponent>(this, UEnemyGrenadierComponent::StaticClass());
	GrenadierComponent->RegisterComponent();
	GrenadierComponent->InitFromParams(Params);
	GrenadierComponent->OnGrenadeTelegraph.AddDynamic(this, &ACompanionCharacter::HandleGrenadeTelegraph);
	GrenadierComponent->OnGrenadeCancelled.AddDynamic(this, &ACompanionCharacter::HandleGrenadeCancelled);
	return GrenadierComponent;
}

void ACompanionCharacter::HandleGrenadeTelegraph(FVector PredictedLanding, float TimeToImpact)
{
	// Telegraph the throw out loud — the player is often inside the danger arc.
	Bark(ECompanionBarkType::ThrowingGrenade);

	const UCompanionTuningDataAsset* T = GetTuning();
	if (!T) return;

	UAnimMontage* Montage = nullptr;
	if (bIsCrouched && IsValid(T->GrenadeThrowCrouchMontage))
		Montage = T->GrenadeThrowCrouchMontage;
	else if (T->GrenadeThrowStandMontages.Num() > 0)
		Montage = T->GrenadeThrowStandMontages[FMath::RandRange(0, T->GrenadeThrowStandMontages.Num() - 1)];

	if (!IsValid(Montage)) return;

	USkeletalMeshComponent* MeshComp = GetMesh();
	UAnimInstance* AnimInst = MeshComp ? MeshComp->GetAnimInstance() : nullptr;
	if (!IsValid(AnimInst)) return;

	if (AnimInst->Montage_Play(Montage) > 0.f)
		ActiveGrenadeThrowMontage = Montage;
}

void ACompanionCharacter::HandleGrenadeCancelled()
{
	if (!IsValid(ActiveGrenadeThrowMontage)) return;

	USkeletalMeshComponent* MeshComp = GetMesh();
	UAnimInstance* AnimInst = MeshComp ? MeshComp->GetAnimInstance() : nullptr;
	if (IsValid(AnimInst) && AnimInst->Montage_IsPlaying(ActiveGrenadeThrowMontage))
		AnimInst->Montage_Stop(0.2f, ActiveGrenadeThrowMontage);

	ActiveGrenadeThrowMontage = nullptr;
}

float ACompanionCharacter::TunedWalkSpeed() const
{
	const UCompanionTuningDataAsset* T = GetTuning();

	// Stealth no longer force-crouches (F4a) — the standing channel must carry the stealth-tuned
	// speeds itself. FastCrouch catch-up hustles even while standing; the crouched channel
	// (TunedCrouchedWalkSpeed) still owns the tier for whenever the crouch-mirror has it hunkered.
	if (IsStealthActive())
	{
		const bool bFastCrouch = StealthCatchupStage == EStealthCatchup::FastCrouch;
		if (bFastCrouch) return T ? T->StealthCatchupSpeed : StealthCatchupSpeed;
		return T ? T->StealthWalkSpeed : StealthWalkSpeed;
	}

	return T ? T->WalkSpeed : WalkSpeed;
}

float ACompanionCharacter::TunedSprintSpeed() const
{
	const UCompanionTuningDataAsset* T = GetTuning();
	return T ? T->SprintSpeed : SprintSpeed;
}

float ACompanionCharacter::TunedFollowCatchupSprintSpeed() const
{
	// Stealth's catch-up ladder owns its own pace — the reduced follow tier never applies there.
	if (IsStealthActive()) return TunedSprintSpeed();

	const UCompanionTuningDataAsset* T = GetTuning();
	const float Full = TunedSprintSpeed();
	const float Reduced = T ? T->FollowCatchupSprintSpeed : 0.f;
	return Reduced > 0.f ? FMath::Min(Reduced, Full) : Full;
}

float ACompanionCharacter::TunedCrouchedWalkSpeed() const
{
	const UCompanionTuningDataAsset* T = GetTuning();

	// Stealth crouch-walk mirrors the PLAYER's crouched cap: riding the standing WalkSpeed (the
	// old fix for the mirror-crouch being unusably slow) made the companion skate — crouch anim at
	// walk velocity. Matching the player keeps the mirror in step: slower skates behind the anim,
	// faster outruns the player it's mirroring. FastCrouch keeps its catch-up tier, floored at the
	// base so the "hustle" tier can never be slower than ordinary stealth crouch-walk.
	if (IsStealthActive())
	{
		float Base = T ? T->WalkSpeed : WalkSpeed;
		const ACompanionAIController* AIC = Cast<ACompanionAIController>(GetController());
		const ACharacter* Player = AIC ? Cast<ACharacter>(AIC->GetPlayerCharacter()) : nullptr;
		const UCharacterMovementComponent* PlayerMove = Player ? Player->GetCharacterMovement() : nullptr;
		if (PlayerMove) Base = PlayerMove->MaxWalkSpeedCrouched;
		if (StealthCatchupStage == EStealthCatchup::FastCrouch && T)
			return FMath::Max(Base, T->StealthCrouchCatchupSpeed);
		return Base;
	}

	return T ? T->CrouchedWalkSpeed : CrouchedWalkSpeed;
}

float ACompanionCharacter::GetStrafeMaxSpeed() const
{
	const UCompanionTuningDataAsset* T = GetTuning();
	return T ? T->StrafeMaxSpeed : StrafeMaxSpeed;
}

bool ACompanionCharacter::IsStrafingForFocus() const
{
	// Travelling and strafing are mutually exclusive by policy. Travelling = sprinting OR closing a
	// follow gap (bFollowCatchupPace). A travelling companion faces where it is going at full speed;
	// a strafing companion holds its gameplay focal at the capped speed. Never both.
	//
	// bFollowCatchupPace is the explicit follow-gap signal set by BTTask_FollowPlayer. Without it
	// the strafe clamp (275) would apply while the companion closes a gap at 550-650, starving it
	// below the player's walk speed (410) and creating an oscillation at the sprint threshold.
	// Every non-combat facing tier in BTService_UpdateCompanionState releases its focal under the
	// same widened condition (IsTravellingToClose) so the body actually faces travel, not sideways
	// at the capped speed in a forward-sprint clip.
	if (bIsSprinting) return false;
	if (bFollowCatchupPace) return false;

	const AAIController* AIC = Cast<AAIController>(GetController());
	if (!AIC) return false;

	// Gameplay priority only. The Move-priority focal is path-following's own "look where you are
	// going" point, which is travel-facing by definition and must never trip the clamp. Mirrors how
	// UCompanionAnimInstance derives bFocusLive.
	return FAISystem::IsValidLocation(AIC->GetFocalPointForPriority(EAIFocusPriority::Gameplay));
}

void ACompanionCharacter::ApplyMovementSpeeds()
{
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!MoveComp) return;

	// Task speed override contract: a BT task that has called SetTaskSpeedOverride owns the
	// overridden channels. Positive = overridden (skip), zero/negative = normal control. Each
	// channel is independent so e.g. the combat task can override walk while leaving crouched
	// under normal control, and the route task can override whichever channel its stance needs.
	const bool bWalkOverridden = TaskSpeedOverrideWalk > 0.f;
	const bool bCrouchOverridden = TaskSpeedOverrideCrouched > 0.f;

	if (!bWalkOverridden)
	{
		float Resolved = bIsSprinting
			? (bFollowCatchupPace ? TunedFollowCatchupSprintSpeed() : TunedSprintSpeed())
			: TunedWalkSpeed();

		// Strafe cap. Clamp, never raise: the tiers above are still the ceiling, this only lowers
		// them. A focus-held body faces the focus while the feet travel sideways, and the locomotion
		// blendspace only carries a full directional sample set up to its 275 row -- above that the
		// legs blend into a forward-only sprint clip and the companion visibly runs forwards while
		// side-stepping.
		if (IsStrafingForFocus())
			Resolved = FMath::Min(Resolved, GetStrafeMaxSpeed());

		MoveComp->MaxWalkSpeed = Resolved;
	}

	if (!bCrouchOverridden)
	{
		// MaxWalkSpeedCrouched is deliberately NOT strafe-clamped: the crouch blendspace's top row
		// is fully directional, so an over-speed crouch foot-slides rather than facing the wrong way.
		MoveComp->MaxWalkSpeedCrouched = TunedCrouchedWalkSpeed();
	}
}

// --- Soft Collision (F2 asymmetric blocking — companion self-push) ---

bool ACompanionCharacter::CanApplySoftSeparation() const
{
	if (bIsDBNO || bIsRevivingPlayer || bBeingRevived) return false;
	if (bTakedownMontagePlaying) return false;
	if (IsValid(TraversalComponent) && TraversalComponent->IsBusy()) return false;

	// Unpossessed = the captive VIP before rescue. CMC skips ControlledCharacterMove without a
	// controller (bRunPhysicsWithNoController is off), so the input would only accrue and then fire
	// the moment he is possessed — walking him off the placed kneeling spot on his first frame.
	return GetController() != nullptr;
}

void ACompanionCharacter::TickPlayerSoftSeparation()
{
	if (!HasAuthority()) return;
	if (!CanApplySoftSeparation()) return;

	const ACompanionAIController* AIC = Cast<ACompanionAIController>(GetController());
	const APawn* PlayerPawn = AIC ? AIC->GetPlayerCharacter() : nullptr;
	if (!IsValid(PlayerPawn)) return;

	const IExtractionPlayerInterface* PlayerIface = Cast<IExtractionPlayerInterface>(PlayerPawn);
	if (PlayerIface && PlayerIface->GetIsDBNO()) return;

	// ACharacter::GetCapsuleComponent() is a direct accessor (no per-frame component-class scan the
	// way FindComponentByClass is) — the player pawn is always an ACharacter, so this is free.
	const ACharacter* PlayerCharacter = Cast<ACharacter>(PlayerPawn);
	if (!IsValid(PlayerCharacter)) return;

	SoftPushAwayFrom(*PlayerCharacter);
}

void ACompanionCharacter::TickAllySoftSeparation()
{
	// Wiring runs on clients too — IgnoreActorWhenMoving is a local movement-filter flag with no
	// authority semantics, and simulated proxies still sweep through MoveSmooth, so a server-only
	// assert leaves the two capsules hard-blocking each other on clients until the next correction.
	RefreshAllyCompanions();

	// Wiring first, push second, and deliberately not under the same gate: the ignore assert has to
	// keep running for a pair we won't push this frame, or a DBNO ally's crawl retreat hard-blocks
	// on whoever is standing over it waiting to revive.
	const bool bCanPush = HasAuthority() && CanApplySoftSeparation();

	for (const TWeakObjectPtr<ACompanionCharacter>& Entry : AllyCompanions)
	{
		ACompanionCharacter* Ally = Entry.Get();
		if (!IsValid(Ally)) continue;

		// Unpossessed = the captive VIP at his placed kneeling spot. He never steers, so his half of
		// the mutual ignore is a dead no-op and only ours stays live — we would clip straight through
		// a scripted body the player stares at for the whole rescue hold. One static obstacle is what
		// CMC wall-slide already handles; the jam this pass fixes needs two steering bodies.
		if (!Ally->GetController()) continue;

		EnsureAllySoftCollisionIgnores(*Ally);
		if (!bCanPush) continue;

		// A downed ally is a revive destination, not an obstacle — pushing off it would fight the
		// approach exactly the way the DBNO-player guard above stops it fighting a player rescue.
		if (Ally->GetIsCompanionDBNO()) continue;
		SoftPushAwayFrom(*Ally);
	}
}

void ACompanionCharacter::RefreshAllyCompanions()
{
	const UWorld* World = GetWorld();
	if (!IsValid(World)) return;

	// Rebuild on the interval, plus immediately when an entry goes stale. Interval-gated rather
	// than empty-gated: with no other companion in the level an empty-list trigger would re-walk
	// every actor every frame.
	const float Now = World->GetTimeSeconds();
	bool bRebuild = (Now - LastAllyScanTime) >= AllyRescanInterval;
	if (!bRebuild)
	{
		for (const TWeakObjectPtr<ACompanionCharacter>& Entry : AllyCompanions)
		{
			if (Entry.IsValid()) continue;
			bRebuild = true;
			break;
		}
	}
	if (!bRebuild) return;

	LastAllyScanTime = Now;
	AllyCompanions.Reset();
	AllyCompanions.Reserve(ExpectedAllyCount);
	for (TActorIterator<ACompanionCharacter> It(World); It; ++It)
	{
		ACompanionCharacter* Other = *It;
		if (IsValid(Other) && Other != this) AllyCompanions.Add(Other);
	}
}

void ACompanionCharacter::EnsureAllySoftCollisionIgnores(ACompanionCharacter& Ally)
{
	UCapsuleComponent* MyCapsule = GetCapsuleComponent();
	UCapsuleComponent* AllyCapsule = Ally.GetCapsuleComponent();
	if (!IsValid(MyCapsule) || !IsValid(AllyCapsule)) return;

	// Idempotent per-tick assert, matching the extractee's: any external MoveIgnoreActorRemove (a
	// revive/takedown teardown clearing an actor off either capsule, a respawned ally re-registering)
	// self-heals next tick, where a latch-gated one-time wire would leave the pair hard-blocking for
	// the rest of the level.
	MyCapsule->IgnoreActorWhenMoving(&Ally, true);
	AllyCapsule->IgnoreActorWhenMoving(this, true);
}

void ACompanionCharacter::SoftPushAwayFrom(const ACharacter& Other)
{
	const UCapsuleComponent* OtherCapsule = Other.GetCapsuleComponent();
	if (!IsValid(OtherCapsule)) return;

	// A ragdolled/dead body disables its capsule — without this it keeps emitting a push nothing can
	// collide with. Matches the player's ally pass (AExtractionPlayer::UpdateAllyCompanionSoftCollision).
	if (OtherCapsule->GetCollisionEnabled() == ECollisionEnabled::NoCollision) return;

	FVector Delta = GetActorLocation() - Other.GetActorLocation();
	Delta.Z = 0.f;

	const float CombinedRadius = GetCapsuleComponent()->GetScaledCapsuleRadius()
		+ OtherCapsule->GetScaledCapsuleRadius()
		+ CompanionSelfPushPadding;

	const float Dist = Delta.Size();
	if (Dist >= CombinedRadius) return;

	FVector PushDir = (Dist > KINDA_SMALL_NUMBER) ? (Delta / Dist) : GetActorRightVector();
	PushDir.Z = 0.f;
	PushDir = PushDir.GetSafeNormal();

	const float DepthFraction = 1.f - (Dist / CombinedRadius);
	AddMovementInput(PushDir * (CompanionSelfPushStrength * DepthFraction), 1.f);
}

void ACompanionCharacter::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
{
	TagContainer.AppendTags(OwnedTags);
}

// --- Weapon Interface ---

void ACompanionCharacter::StartWeaponFire()
{
	// Never spray one-handed over a grenade wind-up (enemy blind-burst parity).
	if (IsValid(GrenadierComponent) && GrenadierComponent->IsTelegraphing()) return;

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
	if (USkeletalMeshComponent* M = GetMesh())
		if (UCompanionAnimInstance* A = Cast<UCompanionAnimInstance>(M->GetAnimInstance()))
			A->AddRecoilImpulse();
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
	if (IsValid(CurrentWeapon) && CurrentWeapon->CanReload() && !CurrentWeapon->IsReloading())
		// Always the plain reload line. An empty magazine is a routine reload, not a supply problem:
		// the companion's reserve never runs out, so LowAmmo lines ("last mag") are always a lie.
		Bark(ECompanionBarkType::Reloading);

	if (IsValid(CurrentWeapon))
		CurrentWeapon->Reload();
}

bool ACompanionCharacter::CanFire() const
{
	return IsValid(CurrentWeapon) && CurrentWeapon->CanFire();
}

bool ACompanionCharacter::IsCombatReady() const
{
	return IsValid(CurrentWeapon);
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

bool ACompanionCharacter::IsCurrentWeaponSuppressed() const
{
	return IsValid(CurrentWeapon) && CurrentWeapon->IsSuppressedEffective();
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
		// AimLog diagnostics (companion.AimLog 1): change-only.
		if (const IConsoleVariable* AimLogCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("companion.AimLog"));
			AimLogCVar && AimLogCVar->GetInt() != 0)
			UE_LOG(LogCompanion, Display, TEXT("[AimLog] SetAimTarget %s -> %s"),
				*GetNameSafe(CurrentAimTarget.Get()), *GetNameSafe(NewTarget));

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
	// Only honour the override while covering fire is running. Other callers (commanded
	// takedown LoS trace, etc.) must get the real sight location.
	if (bHasAimLocationOverride && bCoveringFireActive) return AimLocationOverride;
	return AITargeting::GetSightLocation(Target);
}

// --- Death ---

void ACompanionCharacter::HandleDeath()
{
	UE_LOG(LogCompanion, Log, TEXT("%s died — entering DBNO"), *GetName());
	CachedPressure01 = 0.f;
	EnterDBNO();
}

void ACompanionCharacter::EnterDBNO()
{
	if (bIsDBNO) return;
	bIsDBNO = true;

	// Tear down active commands
	DisarmCommandedTakedown();
	ClearPostBreachEngagement();
	EndSearchRoomExposure();
	ClearCommandedCoverHold();
	ClearCoveringFire();
	CommandedCoverTarget = FCover();
	CommandedCoverTargetStamp = -1e9f;
	if (ACompanionAIController* CompAIC = Cast<ACompanionAIController>(GetController()))
		CompAIC->ClearActiveCommand();

	if (IsValid(TraversalComponent))
		TraversalComponent->CancelTraversal();

	if (IsValid(CurrentWeapon))
	{
		CurrentWeapon->StopFiring();
		CurrentWeapon->CancelReload();
		CurrentWeapon->SetWeaponHidden(true);
	}

	// Abort an in-flight grenade wind-up (no-op unless telegraphing; the cancel broadcast stops the montage).
	if (IsValid(GrenadierComponent))
		GrenadierComponent->CancelThrow();

	// If mid-reviving the player, clear that state
	if (bIsRevivingPlayer)
	{
		StopReviveMontage();
		SetIsRevivingPlayer(false);
	}

	// No live task can survive DBNO entry -- clear any active override so it can never latch past
	// the revive (the revived companion would be pinned at a stale task speed forever).
	ClearTaskSpeedOverride();

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->bUseControllerDesiredRotation = false;
		// Crawl channel: the downed-retreat BT task moves this body to cover at crawl pace, body
		// facing the crawl direction (BS_Downed_Crawl is a Speed x Direction space). HandleRevive
		// restores both via ApplyMovementSpeeds + the flag flip.
		Movement->MaxWalkSpeed = DownedCrawlSpeed;
		Movement->MaxWalkSpeedCrouched = DownedCrawlSpeed;
		Movement->bOrientRotationToMovement = true;
	}

	if (AAIController* AIC = Cast<AAIController>(GetController()))
		AIC->ClearFocus(EAIFocusPriority::Gameplay);

	// QueryOnly: the player's revive sweep is SweepSingleByChannel(ECC_Pawn)
	// and must still find the downed companion.
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);

	SetActorTickEnabled(false);

	// Write BB IsDowned for BT gating; drop the combat target so the post-revive tree can't
	// re-enter Combat against a stale/dead enemy (the state service early-outs while DBNO and
	// would not clear it for the whole downed stint).
	if (AAIController* AIC = Cast<AAIController>(GetController()))
		if (UBlackboardComponent* BB = AIC->GetBlackboardComponent())
		{
			BB->SetValueAsBool(NAME_IsDowned, true);
			BB->ClearValue(ACompanionAIController::BB_CombatTarget);
		}

	// Going down is a priority telegraph; the repeating call-for-help keeps the revive on the
	// player's mind while bleeding out (the bark set's cooldown paces any overlap).
	Bark(ECompanionBarkType::CompanionDown);
	constexpr float CallForHelpFirstDelay = 10.f;
	constexpr float CallForHelpRepeatInterval = 18.f;
	GetWorldTimerManager().SetTimer(CallForHelpTimerHandle,
		FTimerDelegate::CreateUObject(this, &ACompanionCharacter::Bark, ECompanionBarkType::CompanionCallForHelp, FName(NAME_None)),
		CallForHelpRepeatInterval, true, CallForHelpFirstDelay);

	// Start bleedout timer (authority only)
	if (HasAuthority())
	{
		GetWorldTimerManager().SetTimer(
			BleedoutTimerHandle, this,
			&ACompanionCharacter::OnBleedoutExpired,
			BleedoutDuration, false);

		// Squad-wipe check: fail only when the player is down AND no other companion can still
		// pick anyone up (with a second companion in the level, one downed ally isn't a wipe).
		APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
		if (IExtractionPlayerInterface* PlayerIface = Cast<IExtractionPlayerInterface>(PlayerPawn))
		{
			if (PlayerIface->GetIsDBNO() && !IsAnyCompanionReviveCapable(GetWorld(), this))
			{
				if (AExtractionGameMode* GM = GetWorld()->GetAuthGameMode<AExtractionGameMode>())
					GM->FailLevel(NSLOCTEXT("Extraction", "BothDownReason", "Your squad was wiped out."));
			}
		}
	}

	OnRep_IsDBNO();
	UE_LOG(LogCompanion, Log, TEXT("%s entered DBNO (%.0fs bleedout)"), *GetName(), BleedoutDuration);
}

void ACompanionCharacter::ExitDBNO()
{
	if (!bIsDBNO) return;
	bIsDBNO = false;

	GetWorldTimerManager().ClearTimer(BleedoutTimerHandle);

	if (IsValid(CurrentWeapon))
		CurrentWeapon->SetWeaponHidden(false);

	// Restore capsule to full collision — HandleRevive (bound to OnRevive) restores
	// QueryAndPhysics, tick, and movement mode, so avoid double-restoring those here.

	// Write BB IsDowned = false
	if (AAIController* AIC = Cast<AAIController>(GetController()))
		if (UBlackboardComponent* BB = AIC->GetBlackboardComponent())
			BB->SetValueAsBool(NAME_IsDowned, false);

	SetBeingRevived(false);

	// HealthComponent->Revive triggers OnRevive which fires HandleRevive.
	// HandleRevive restores tick, collision, and movement mode.
	if (IsValid(HealthComponent))
		HealthComponent->Revive(ReviveHealthPercent);

	OnRep_IsDBNO();
	UE_LOG(LogCompanion, Log, TEXT("%s revived at %.0f%% health"), *GetName(), ReviveHealthPercent * 100.f);
}

void ACompanionCharacter::OnBleedoutExpired()
{
	if (!bIsDBNO) return;

	UE_LOG(LogCompanion, Log, TEXT("%s bleedout expired — mission failed"), *GetName());

	GetWorldTimerManager().ClearTimer(CallForHelpTimerHandle);

	if (HasAuthority())
	{
		if (AExtractionGameMode* GM = GetWorld()->GetAuthGameMode<AExtractionGameMode>())
			GM->FailLevel(GetBleedoutFailReason());
	}
}

FText ACompanionCharacter::GetBleedoutFailReason() const
{
	return NSLOCTEXT("Extraction", "CompanionBledOut", "Your companion bled out.");
}

void ACompanionCharacter::OnRep_IsDBNO()
{
	if (!bIsDBNO) SetBeingRevived(false);
	OnCompanionDownedStateChanged.Broadcast(bIsDBNO, bIsDBNO ? BleedoutDuration : 0.f);
}

float ACompanionCharacter::GetBleedoutTimeRemaining() const
{
	if (!bIsDBNO) return 0.f;
	const UWorld* World = GetWorld();
	if (!World) return 0.f;
	return FMath::Max(0.f, World->GetTimerManager().GetTimerRemaining(BleedoutTimerHandle));
}

ETraversalType ACompanionCharacter::GetActiveTraversalType() const
{
	return IsValid(TraversalComponent) ? TraversalComponent->GetActiveType() : ETraversalType::None;
}

bool ACompanionCharacter::IsInTraversal() const
{
	return IsValid(TraversalComponent) && TraversalComponent->IsBusy();
}

bool ACompanionCharacter::GetIsVaulting() const
{
	return IsValid(TraversalComponent) && TraversalComponent->GetActiveType() == ETraversalType::Vault;
}

void ACompanionCharacter::SetBeingRevived(bool bRevived, float ExpectedDuration)
{
	if (bBeingRevived == bRevived) return;
	bBeingRevived = bRevived;

	// Stop the downed retreat, but keep a grounded movement mode so CMC consumes the
	// being-revived montage's authored root motion. The retreat task remains suppressed by
	// bBeingRevived and re-asserts its crawl speed after an interrupted hold.
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		if (bRevived)
		{
			if (AAIController* AIC = Cast<AAIController>(GetController()))
				AIC->StopMovement();
			Movement->StopMovementImmediately();
			Movement->SetMovementMode(MOVE_Walking);
		}
		else
			Movement->SetMovementMode(MOVE_Walking);
	}

	USkeletalMeshComponent* MeshComp = GetMesh();
	UAnimInstance* AnimInst = IsValid(MeshComp) ? MeshComp->GetAnimInstance() : nullptr;
	if (!IsValid(AnimInst) || !BeingRevivedMontage)
	{
		return;
	}

	if (bRevived && bIsDBNO && bPlayPlayerReviveMontages)
	{
		AnimInst->SetRootMotionMode(ERootMotionMode::RootMotionFromMontagesOnly);
		const float MontageLength = BeingRevivedMontage->GetPlayLength();
		const float PlayRate = CalculateReviveMontagePlayRate(
			MontageLength, BeingRevivedMontage->BlendOut.GetBlendTime(), ExpectedDuration);
		if (AnimInst->Montage_Play(BeingRevivedMontage, PlayRate) <= 0.f)
			UE_LOG(LogCompanion, Warning, TEXT("%s: BeingRevivedMontage '%s' failed to play"),
				*GetName(), *GetNameSafe(BeingRevivedMontage));
	}
	else if (AnimInst->Montage_IsPlaying(BeingRevivedMontage))
		AnimInst->Montage_Stop(0.25f, BeingRevivedMontage);
}

bool ACompanionCharacter::IsBeingRevivedMontagePlaying() const
{
	if (!BeingRevivedMontage) return false;
	const USkeletalMeshComponent* MeshComp = GetMesh();
	const UAnimInstance* AnimInst = IsValid(MeshComp) ? MeshComp->GetAnimInstance() : nullptr;
	return IsValid(AnimInst) && AnimInst->Montage_IsPlaying(BeingRevivedMontage);
}

void ACompanionCharacter::AlignForRevive(const FVector& ReviverLocation)
{
	if (!bIsDBNO) return;

	const FVector ToReviver = (ReviverLocation - GetActorLocation()).GetSafeNormal2D();
	if (ToReviver.IsNearlyZero()) return;

	// Montage-less hold: the authored-angle math below exists purely to line up the montage
	// pair — without it, just face the reviver.
	if (!bPlayPlayerReviveMontages)
	{
		SetActorRotation(FRotator(0.f, ToReviver.Rotation().Yaw, 0.f));
		return;
	}

	// Rotation-only mirror of BTTask_RevivePlayer's position snap: instead of moving the reviver
	// to the authored offset, face so the reviver's actual position sits at the authored local
	// angle (atan2 of the PLAYER-direction pair offset, ≈ -47°). Works from any approach
	// direction; the radial distance is whatever the reviver chose within revive range. Must use
	// the same offset as SeatReviverForHold or the seat step stops being purely radial.
	const float AuthoredLocalAngle = FMath::RadiansToDegrees(FMath::Atan2(PlayerRevivePairOffset.Y, PlayerRevivePairOffset.X));
	// The trim rotates only this body; SeatReviverForHold subtracts it back out of its anchor,
	// so the reviver's seat and facing stay put in world.
	SetActorRotation(FRotator(0.f, ToReviver.Rotation().Yaw - AuthoredLocalAngle + PlayerRevivePatientYawTrimDeg, 0.f));
}

void ACompanionCharacter::HandleShieldChangedForWidget(float CurrentShield, float MaxShield)
{
	UUserWidget* Widget = IsValid(HealthWidgetComponent) ? HealthWidgetComponent->GetWidget() : nullptr;
	if (!IsValid(Widget)) return;

	UFunction* Fn = Widget->FindFunction(TEXT("OnShieldUpdated"));
	if (!Fn) return;

	// BP-defined event params are doubles (BP "float" = real) — layout must match exactly.
	struct { double CurrentShield; double MaxShield; } Params{ CurrentShield, MaxShield };
	Widget->ProcessEvent(Fn, &Params);
}

void ACompanionCharacter::HandleRevive()
{
	UE_LOG(LogCompanion, Log, TEXT("%s HandleRevive fired"), *GetName());

	GetWorldTimerManager().ClearTimer(CallForHelpTimerHandle);
	Bark(ECompanionBarkType::CompanionRevived);

	SetActorTickEnabled(true);

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->SetMovementMode(MOVE_Walking);
		Movement->bUseControllerDesiredRotation = true;
		Movement->bOrientRotationToMovement = false;
	}

	// Undo the DBNO crawl clamp — clear any stale override (EnterDBNO already clears it, but a
	// task that started between DBNO and revive could theoretically set one) then re-derive
	// walk/crouch speeds from the tuning asset.
	ClearTaskSpeedOverride();
	ApplyMovementSpeeds();
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

// --- Commanded Loot ---

void ACompanionCharacter::PlayLootMontage()
{
	if (!LootMontage) return;

	USkeletalMeshComponent* MeshComp = GetMesh();
	UAnimInstance* AnimInst = MeshComp ? MeshComp->GetAnimInstance() : nullptr;
	if (!IsValid(AnimInst)) return;

	AnimInst->Montage_Play(LootMontage);
}

// --- Revive ---

void ACompanionCharacter::PlayReviveMontage()
{
	if (!ReviveMontage) return;
	USkeletalMeshComponent* MeshComp = GetMesh();
	UAnimInstance* AnimInst = MeshComp ? MeshComp->GetAnimInstance() : nullptr;
	if (!IsValid(AnimInst)) return;
	if (AnimInst->Montage_IsPlaying(ReviveMontage)) return;
	AnimInst->SetRootMotionMode(ERootMotionMode::RootMotionFromMontagesOnly);

	const float MontageLength = ReviveMontage->GetPlayLength();
	const float PlayRate = ReviveDuration > 0.f && MontageLength > 0.f
		? MontageLength / ReviveDuration : 1.f;
	if (AnimInst->Montage_Play(ReviveMontage, PlayRate) <= 0.f)
		UE_LOG(LogCompanion, Warning, TEXT("%s: ReviveMontage '%s' failed to play (slot/skeleton mismatch?)"),
			*GetName(), *GetNameSafe(ReviveMontage));
}

void ACompanionCharacter::StopReviveMontage()
{
	if (!ReviveMontage) return;
	USkeletalMeshComponent* MeshComp = GetMesh();
	UAnimInstance* AnimInst = MeshComp ? MeshComp->GetAnimInstance() : nullptr;
	if (IsValid(AnimInst) && AnimInst->Montage_IsPlaying(ReviveMontage))
		AnimInst->Montage_Stop(0.25f, ReviveMontage);
}

// --- Commanded Breach ---

float ACompanionCharacter::PlayBreachMontage(EBreachType Type)
{
	// Context-tagged variants let the loud kick shout and the quiet open whisper.
	static const FName BreachContexts[] = { TEXT("Tactical"), TEXT("Loud"), TEXT("Quiet") };
	const int32 TypeIndex = static_cast<int32>(Type);
	Bark(ECompanionBarkType::Breaching,
		BreachContexts[FMath::Clamp(TypeIndex, 0, static_cast<int32>(UE_ARRAY_COUNT(BreachContexts)) - 1)]);

	const TObjectPtr<UAnimMontage>* Found = BreachMontages.Find(Type);
	UAnimMontage* Montage = Found ? Found->Get() : nullptr;
	if (!Montage) return 0.f;

	USkeletalMeshComponent* MeshComp = GetMesh();
	UAnimInstance* AnimInst = MeshComp ? MeshComp->GetAnimInstance() : nullptr;
	if (!IsValid(AnimInst)) return 0.f;

	return AnimInst->Montage_Play(Montage);
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
	bTakedownCommandedInstant = false;
	bTakedownKillRegistered = false;
	bTakedownInPosition = false;
	bTakedownExecuting = false;
	bTakedownMontagePlaying = false;

	TakedownWindowEnemies.Reset();
	if (AEnemyCharacter* ReservedVictim = Cast<AEnemyCharacter>(Victim))
	{
		// Grandfather the victim against the global alert ratchet for as long as we stay armed on it —
		// one missed player shot escalates the level to Loud, which wakes every Unaware enemy and would
		// otherwise invalidate a takedown we are already lined up on.
		ReservedVictim->ReserveForTakedown(this);

		// The hush covers the whole pocket, not just the victim: the partner is the one that turns
		// round when the player takes their half of the synced kill. Same union the BT task syncs on
		// (GatherEligiblePeers), so the pair we hush can never disagree with the pair we co-ordinate.
		TSet<AEnemyCharacter*> Pocket;
		ATakedownVolume::GatherEligiblePeers(ReservedVictim, Pocket);

		TakedownWindowEnemies.Reserve(Pocket.Num() + 1);
		for (AEnemyCharacter* PocketEnemy : Pocket)
			TakedownWindowEnemies.Add(PocketEnemy);

		// Backstop: the victim itself may already have dropped out of IsTakedownEligible (the alert
		// ratchet drifting it to Searching) and so be absent from the union — it still gets hushed.
		TakedownWindowEnemies.AddUnique(ReservedVictim);

		// Stamp now rather than waiting for the next Tick: a knife arm can execute on the very next
		// frame, and an unstamped frame is a frame the partner can hear the player's shot in.
		RefreshTakedownWindow();
	}

	// Aim at the victim — and mark LOS clear so the anim-side fade does not relax the spine
	// while the companion holds the armed aim (the service pins HasTargetLOS false in stealth).
	SetAimTarget(Victim);
	SetHasTargetLOS(true);

	// Shoot: raise weapon + face immediately so the companion lines up the instant it's commanded
	// (and stays aimed through the autonomous 2-4s wait / until the player's synced shot).
	if (Method == ETakedownMethod::Shoot)
	{
		// A commanded execution owns the weapon immediately. Do not let an empty-mag auto-reload
		// delay the takedown; normal combat can resume reloading after the execution finishes.
		if (IsValid(CurrentWeapon))
			CurrentWeapon->AbortFireAndReload();

		SetLowReadyAim(false);
		if (AAIController* AIC = Cast<AAIController>(GetController()))
			AIC->SetFocus(Victim, EAIFocusPriority::Gameplay);
	}

	// Bind BOTH commit signals regardless of method. Binding was method-exclusive, so a KNIFE-armed
	// companion listened only for the player's melee input — the player taking their own SHOT was
	// invisible to it and it sat Armed until the alert ratchet woke its victim and the takedown
	// whiffed. Either signal is the player committing. OnPlayerFiredWeapon broadcasts BEFORE the
	// hitscan, so the commit lands on the trigger-pull frame whether or not the shot connects, and
	// both handlers guard on bTakedownExecuting so a double signal cannot double-commit.
	ACompanionAIController* CompAIC = Cast<ACompanionAIController>(GetController());
	APawn* PlayerPawn = IsValid(CompAIC) ? CompAIC->GetPlayerCharacter() : nullptr;
	AExtractionPlayer* Player = Cast<AExtractionPlayer>(PlayerPawn);
	if (IsValid(Player))
	{
		TakedownPlayerRef = Player;
		Player->OnPlayerFiredWeapon.AddDynamic(this, &ACompanionCharacter::OnPlayerFiredWeaponHandler);
		Player->OnPlayerTakedownCommitted.AddDynamic(this, &ACompanionCharacter::OnPlayerTakedownCommittedHandler);
	}

	UE_LOG(LogCompanion, Log, TEXT("Takedown armed: victim=%s method=%s"),
		*GetNameSafe(Victim), Method == ETakedownMethod::Knife ? TEXT("Knife") : TEXT("Shoot"));
}

void ACompanionCharacter::RefreshTakedownWindow()
{
	// Prune as we stamp: a dead enemy stays IsValid (and keeps getting stamped, harmlessly — its
	// awareness is already stopped) until corpse cleanup destroys the actor, at which point the
	// weak ptr goes stale and drops out here instead of accumulating across a session.
	for (int32 i = TakedownWindowEnemies.Num() - 1; i >= 0; --i)
	{
		AEnemyCharacter* Enemy = TakedownWindowEnemies[i].Get();
		if (!IsValid(Enemy))
		{
			TakedownWindowEnemies.RemoveAtSwap(i);
			continue;
		}
		Enemy->BeginTakedownWindow(TakedownWindowRefreshSeconds);
	}
}

void ACompanionCharacter::DisarmCommandedTakedown()
{
	if (!bTakedownArmed && !bTakedownMontagePlaying) return;

	StopWeaponFire();

	// Unbind both delegates — safe even if only one was bound
	AExtractionPlayer* Player = TakedownPlayerRef.Get();
	if (IsValid(Player))
	{
		Player->OnPlayerTakedownCommitted.RemoveDynamic(this, &ACompanionCharacter::OnPlayerTakedownCommittedHandler);
		Player->OnPlayerFiredWeapon.RemoveDynamic(this, &ACompanionCharacter::OnPlayerFiredWeaponHandler);
	}

	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ShootDelayTimerHandle);
		// A pending mid-montage knife kill must not fire into the next takedown's state
		// (already-fired timers make this a no-op on the normal completion path).
		World->GetTimerManager().ClearTimer(KnifeKillTimerHandle);
	}

	if (AActor* IgnoredVictim = TakedownVictim.Get())
	{
		if (UCapsuleComponent* Capsule = GetCapsuleComponent())
			Capsule->IgnoreActorWhenMoving(IgnoredVictim, false);
		if (ACharacter* VictimChar = Cast<ACharacter>(IgnoredVictim))
			if (UCapsuleComponent* VictimCapsule = VictimChar->GetCapsuleComponent())
				VictimCapsule->IgnoreActorWhenMoving(this, false);
	}

	// If we tear down mid-stab (companion death / BT abort), kill the frozen victim now — attributed to
	// us — instead of leaving it frozen-alive until the enemy's own watchdog fires with no instigator.
	if (bTakedownMontagePlaying)
		if (AEnemyCharacter* DyingVictim = Cast<AEnemyCharacter>(TakedownVictim.Get()))
			DyingVictim->FinishTakedownKill(this);

	// Release the alert-ratchet grandfather — we no longer have a claim on this victim.
	if (AEnemyCharacter* ReservedVictim = Cast<AEnemyCharacter>(TakedownVictim.Get()))
		ReservedVictim->ClearTakedownReservation(this);

	// Stop the heartbeat; the already-stamped expiry runs out on its own (TakedownWindowRefreshSeconds).
	TakedownWindowEnemies.Reset();

	TakedownVictim.Reset();
	TakedownPlayerRef.Reset();
	bTakedownArmed = false;
	bTakedownPlayerCommitted = false;
	bTakedownCommandedInstant = false;
	bTakedownKillRegistered = false;
	bTakedownInPosition = false;
	bTakedownExecuting = false;
	bTakedownCrouchApproach = false;
	bTakedownMontagePlaying = false;
	SetAimTarget(nullptr);
	SetLowReadyAim(true);
	if (IsValid(CurrentWeapon)) CurrentWeapon->SetWeaponHidden(false);
	if (IsValid(TakedownKnifeMesh)) TakedownKnifeMesh->SetHiddenInGame(true);
	if (AAIController* AIC = Cast<AAIController>(GetController()))
		AIC->ClearFocus(EAIFocusPriority::Gameplay);

	UE_LOG(LogCompanion, Log, TEXT("Takedown disarmed — broadcasting finished"));
	OnCommandedTakedownFinished.Broadcast();
}

void ACompanionCharacter::LatchForcedCombatTarget(AActor* Target, float HoldSeconds)
{
	const UWorld* World = GetWorld();
	if (!IsValid(Target) || !World || HoldSeconds <= 0.f) return;

	ForcedCombatTarget = Target;
	ForcedCombatTargetExpiry = World->GetTimeSeconds() + HoldSeconds;

	UE_LOG(LogCompanion, Log, TEXT("Takedown commitment latched on %s for %.1fs"),
		*GetNameSafe(Target), HoldSeconds);
}

AActor* ACompanionCharacter::GetForcedCombatTarget() const
{
	AActor* Target = ForcedCombatTarget.Get();
	if (!IsValid(Target)) return nullptr;

	const UWorld* World = GetWorld();
	if (!World || World->GetTimeSeconds() > ForcedCombatTargetExpiry) return nullptr;

	// Dead victim = commitment discharged. Evaluated here rather than latched so the override
	// releases on the kill frame with no teardown plumbing.
	const UHealthComponent* TargetHealth = Target->FindComponentByClass<UHealthComponent>();
	if (IsValid(TargetHealth) && TargetHealth->IsDead()) return nullptr;

	return Target;
}

void ACompanionCharacter::ClearForcedCombatTarget()
{
	ForcedCombatTarget.Reset();
	ForcedCombatTargetExpiry = 0.f;
}

void ACompanionCharacter::OnPlayerTakedownCommittedHandler()
{
	UE_LOG(LogCompanion, Warning, TEXT("[Takedown-Companion] player-commit signal received (armed=%d inPosition=%d)"),
		(int32)bTakedownArmed, (int32)bTakedownInPosition);
	if (!bTakedownArmed) return;

	bTakedownPlayerCommitted = true;
	if (bTakedownInPosition) ExecuteCommandedTakedown();
	else UE_LOG(LogCompanion, Warning, TEXT("[Takedown-Companion] committed but NOT in position yet — will execute on arrival"));
}

void ACompanionCharacter::OnPlayerFiredWeaponHandler()
{
	UE_LOG(LogCompanion, Log, TEXT("[Takedown-Companion] player-fired signal received (armed=%d inPosition=%d)"),
		(int32)bTakedownArmed, (int32)bTakedownInPosition);
	// bTakedownExecuting guard: a follow-up player shot mid-chain (autonomous commit already in
	// flight) must not convert the remaining timers to the instant cadence.
	if (!bTakedownArmed || bTakedownExecuting) return;

	// Player pulled the trigger — the synced kill must land this frame, not on the phased cadence.
	bTakedownCommandedInstant = true;
	bTakedownPlayerCommitted = true;
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

void ACompanionCharacter::CommitTakedownNow()
{
	if (!bTakedownArmed || bTakedownExecuting) return;
	bTakedownCommandedInstant = false; // autonomous solo — keep the phased cadence
	bTakedownPlayerCommitted = true;
	if (bTakedownInPosition) ExecuteCommandedTakedown();
	// If not yet in position, SetTakedownInPosition(true) will fire it (committed already set).
}

void ACompanionCharacter::ExecuteCommandedTakedown()
{
	if (bTakedownExecuting) return;   // re-entry guard

	UE_LOG(LogCompanion, Warning, TEXT("[Takedown-Companion] ExecuteCommandedTakedown: committed=%d inPos=%d armed=%d method=%d"),
		(int32)bTakedownPlayerCommitted, (int32)bTakedownInPosition, (int32)bTakedownArmed, (int32)TakedownActiveMethod);
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
		// No montage assigned → instant kill BEFORE we freeze/teleport anyone (a paired hold with no
		// attacker animation would leave the victim reacting to a T-posing companion).
		if (!IsValid(KnifeTakedownMontage))
		{
			UE_LOG(LogCompanion, Warning, TEXT("Takedown: no KnifeTakedownMontage assigned — instant-kill fallback"));
			Enemy->FinishTakedownKill(this);
			FinishCommandedTakedown();
			return;
		}

		OnCommandedTakedownStarted.Broadcast();
		if (IsValid(CurrentWeapon)) CurrentWeapon->SetWeaponHidden(true);
		if (IsValid(TakedownKnifeMesh)) TakedownKnifeMesh->SetHiddenInGame(false);

		// Authored placement, measured from the StealthFinishers pack's own demo map: for
		// Paired_Knife_Stealth_ClavicleStabDown the victim sits 115cm along the attacker MESH's +Y,
		// same facing (dYaw 0). Both characters carry a -90 mesh yaw, which maps that mesh-+Y onto
		// actor FORWARD — so the layout is: the companion stands CommandedTakedownOffset BEHIND the
		// (stationary) victim, both sharing the victim's facing, and the stab lands as the Att pose
		// lunges in. Offset is victim-relative-to-attacker in that shared frame (X fwd, Y right, Z up).
		UnCrouch();
		SetTakedownCrouchApproach(false);

		const float SnapYaw = Enemy->GetActorRotation().Yaw;
		const FVector SnapLoc = Enemy->GetActorLocation();   // victim stays where it stands
		const float WatchdogTimeout = KnifeTakedownMontage->GetPlayLength() + 1.f;

		// Hold the victim FIRST (freeze in place + play its Vic reaction, broadcasts OnTakedownExecuted);
		// relocate ourselves only once that succeeds, so a failed hold never strands the companion.
		if (!Enemy->BeginTakedownHold(this, SnapLoc, SnapYaw, WatchdogTimeout))
		{
			UE_LOG(LogCompanion, Warning, TEXT("Takedown: BeginTakedownHold failed — aborting"));
			FinishCommandedTakedown();
			return;
		}

		// Companion stands behind the victim (victim − offset). Keep our OWN grounded Z (capsule
		// half-heights can differ), and sweep from the victim toward the spot so an enemy backed
		// against a wall can't embed us in geometry — mirrors AExtractionPlayer::StartMontageDeferred.
		// Backpad extends the authored offset along its own direction — radial only, so the
		// approach angle the pair anims were tuned around never changes.
		FVector OffsetLocal = CommandedTakedownOffset;
		if (CommandedTakedownBackpad > 0.f)
		{
			FVector Dir(OffsetLocal.X, OffsetLocal.Y, 0.f);
			Dir = Dir.IsNearlyZero() ? FVector::ForwardVector : Dir.GetSafeNormal();
			OffsetLocal += Dir * CommandedTakedownBackpad;
		}
		const FVector OffsetWorld = FRotator(0.f, SnapYaw, 0.f).RotateVector(OffsetLocal);
		FVector CompLoc(SnapLoc.X - OffsetWorld.X, SnapLoc.Y - OffsetWorld.Y, GetActorLocation().Z);
		if (const UWorld* World = GetWorld())
		{
			static constexpr float SweepRadius = 30.f;
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(CompanionTakedownPlace), false, this);
			Params.AddIgnoredActor(Victim);
			if (World->SweepSingleByChannel(Hit, FVector(SnapLoc.X, SnapLoc.Y, CompLoc.Z), CompLoc,
				FQuat::Identity, ECC_WorldStatic, FCollisionShape::MakeSphere(SweepRadius), Params))
				CompLoc = Hit.Location;
		}
		SetActorLocation(CompLoc, false, nullptr, ETeleportType::TeleportPhysics);
		SetActorRotation(FRotator(0.f, SnapYaw, 0.f));

		// Mutually ignore capsule collision so the two never shove apart — lets the bodies sit as close
		// as CommandedTakedownOffset asks, even when the gap is below the summed capsule radii. Both
		// directions are cleared in FinishCommandedTakedown / DisarmCommandedTakedown.
		if (UCapsuleComponent* Capsule = GetCapsuleComponent())
			Capsule->IgnoreActorWhenMoving(Victim, true);
		if (UCapsuleComponent* VictimCapsule = Enemy->GetCapsuleComponent())
			VictimCapsule->IgnoreActorWhenMoving(this, true);

		// Kill at the stab/montage end, NOT instantly (instant ragdolls the victim mid-grapple).
		UAnimInstance* AnimInst = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
		const float MontageLen = IsValid(AnimInst) ? AnimInst->Montage_Play(KnifeTakedownMontage) : 0.f;
		if (MontageLen <= 0.f)
		{
			UE_LOG(LogCompanion, Warning, TEXT("Takedown: knife montage did not play — instant-kill fallback"));
			Enemy->FinishTakedownKill(this);   // never leave the victim frozen-alive
			FinishCommandedTakedown();
			return;
		}

		Enemy->SetTakedownWasMontageDriven(true);
		bTakedownMontagePlaying = true;
		FOnMontageEnded EndDelegate;
		EndDelegate.BindUObject(this, &ACompanionCharacter::OnTakedownMontageEnded);
		AnimInst->Montage_SetEndDelegate(EndDelegate, KnifeTakedownMontage);

		// Kill mid-collapse (the player takedown's stab-notify timing) instead of at montage end:
		// the ragdoll inherits the fall and settles naturally, where engaging it on the already-
		// flat end pose pops on floor contact. The montage-end kill stays as the safety net.
		const float KillAt = FMath::Clamp(KnifeTakedownKillAtSeconds, 0.1f, MontageLen - 0.05f);
		GetWorldTimerManager().SetTimer(KnifeKillTimerHandle,
			FTimerDelegate::CreateWeakLambda(this, [this, WeakVictim = TWeakObjectPtr<AEnemyCharacter>(Enemy)]()
			{
				if (AEnemyCharacter* KillVictim = WeakVictim.Get())
					KillVictim->FinishTakedownKill(this);
			}),
			KillAt, false);
		UE_LOG(LogCompanion, Warning, TEXT("[Takedown-Companion] knife executed (behind victim) on victim=%s"), *GetNameSafe(Victim));
	}
	else // Shoot — phased aim-in → cosmetic fire → kill → lower
	{
		if (!IsValid(CurrentWeapon))
		{
			UE_LOG(LogCompanion, Warning, TEXT("Takedown: shoot aborted - no weapon"));
			FinishCommandedTakedown();
			return;
		}

		// The execution uses the cosmetic shot path and owns ammo handling independently. Cancel any
		// reload that restarted during the armed delay so zero ammo cannot postpone the kill.
		CurrentWeapon->AbortFireAndReload();

		// LoS trace from companion eyes to victim
		const UWorld* World = GetWorld();
		if (!World)
		{
			FinishCommandedTakedown();
			return;
		}

		FVector EyesLoc;
		FRotator EyesRot;
		GetActorEyesViewPoint(EyesLoc, EyesRot);

		FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(CompanionShootTakedown), false, this);
		TraceParams.AddIgnoredActor(Victim);
		if (IsValid(CurrentWeapon)) TraceParams.AddIgnoredActor(CurrentWeapon);

		// Routed through AITargeting::HasClearLineIgnoringPawns — reproduced byte-for-byte by the BT
		// task's own approval trace (HasShootTakedownLosFromEye) so a line the task approves can never
		// be rejected here. A body in the path (the double takedown's second enemy, or the player
		// lining up their own shot) is stepped over rather than counted as cover: the kill is
		// registered directly and this shot is cosmetic, so a pawn standing in the way was never a
		// real obstruction to it.
		const FVector TraceEnd = GetAimPointForTarget(Victim);
		AActor* Blocker = nullptr;
		if (!AITargeting::HasClearLineIgnoringPawns(World, EyesLoc, TraceEnd, TraceParams, &Blocker))
		{
			UE_LOG(LogCompanion, Warning, TEXT("Takedown: shoot aborted — LoS blocked by %s"), *GetNameSafe(Blocker));
			FinishCommandedTakedown();
			return;
		}

		// Phase 0: aim in — face + raise weapon. Re-confirm LOS clear so the anim-side fade
		// recovers even if the service overwrote it between arm and commit.
		SetAimTarget(Victim);
		SetHasTargetLOS(true);
		SetLowReadyAim(false);
		if (AAIController* AIC = Cast<AAIController>(GetController()))
			AIC->SetFocus(Victim, EAIFocusPriority::Gameplay);

		TakedownShotsRemaining = FMath::Max(ShootShotCount, 0);

		// Player-synced commit: the enemy must die on the player's shot frame. Register the kill
		// FIRST — the player's own (unsuppressed) shot can alert the victim out of Unaware inside
		// even the double-tap window, and ExecuteTakedown's reject would turn the synced takedown
		// into a visible whiff. The double-tap then plays over the first ragdoll frames, which
		// reads as the killing burst. The autonomous path keeps the phased aim-in cadence below.
		if (bTakedownCommandedInstant)
		{
			if (!Enemy->ExecuteTakedown(this, /*bIgnoreRangeAndArc=*/true))
			{
				UE_LOG(LogCompanion, Warning, TEXT("Takedown: instant shoot kill rejected (enemy no longer Unaware)"));
				FinishCommandedTakedown();
				return;
			}
			bTakedownKillRegistered = true;

			if (TakedownShotsRemaining <= 0) HandleTakedownKill();
			else HandleTakedownAimedIn();
		}
		else if (TakedownShotsRemaining <= 0)
		{
			// Rate <= 0 silently clears a timer instead of firing — call zero-delay steps directly.
			if (ShootAimInDuration > 0.f)
				World->GetTimerManager().SetTimer(ShootDelayTimerHandle,
					FTimerDelegate::CreateUObject(this, &ACompanionCharacter::HandleTakedownKill),
					ShootAimInDuration, false);
			else
				HandleTakedownKill();
		}
		else
		{
			if (ShootAimInDuration > 0.f)
				World->GetTimerManager().SetTimer(ShootDelayTimerHandle,
					FTimerDelegate::CreateUObject(this, &ACompanionCharacter::HandleTakedownAimedIn),
					ShootAimInDuration, false);
			else
				HandleTakedownAimedIn();
		}
	}
}

void ACompanionCharacter::OnTakedownMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	bTakedownMontagePlaying = false;
	// Montage-driven death: the kill lands at the END of the stab montage, not mid-grapple.
	if (AEnemyCharacter* DeadVictim = Cast<AEnemyCharacter>(TakedownVictim.Get()))
		DeadVictim->FinishTakedownKill(this);
	FinishCommandedTakedown();
}

void ACompanionCharacter::FireCosmeticShotAt(const FVector& AimEndPoint)
{
	if (!IsValid(CurrentWeapon)) return;

	// Ensure the fire arms montage is running (idempotent — won't restart if already playing)
	if (USkeletalMeshComponent* MeshComp = GetMesh())
		if (UCompanionAnimInstance* AnimInst = Cast<UCompanionAnimInstance>(MeshComp->GetAnimInstance()))
			AnimInst->PlayFireMontage(1.0f);

	CurrentWeapon->FireCosmetic(AimEndPoint);
}

void ACompanionCharacter::HandleTakedownAimedIn()
{
	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(TakedownVictim.Get());
	if (!IsValid(Enemy))
	{
		FinishCommandedTakedown();
		return;
	}

	// Fire one cosmetic shot
	FireCosmeticShotAt(GetAimPointForTarget(Enemy));
	--TakedownShotsRemaining;

	const UWorld* World = GetWorld();
	if (!World) { FinishCommandedTakedown(); return; }

	// Commanded (player-synced) chain runs on near-zero intervals; a rate <= 0 would silently
	// clear the timer instead of firing, so zero-delay steps are called directly.
	if (TakedownShotsRemaining > 0)
	{
		const float ShotGap = bTakedownCommandedInstant ? ShootCommandedShotInterval : ShootShotInterval;
		if (ShotGap > 0.f)
			World->GetTimerManager().SetTimer(ShootDelayTimerHandle,
				FTimerDelegate::CreateUObject(this, &ACompanionCharacter::HandleTakedownAimedIn),
				ShotGap, false);
		else
			HandleTakedownAimedIn();
	}
	else
	{
		const float KillDelay = bTakedownCommandedInstant ? ShootCommandedKillDelay : ShootShotInterval;
		if (KillDelay > 0.f)
			World->GetTimerManager().SetTimer(ShootDelayTimerHandle,
				FTimerDelegate::CreateUObject(this, &ACompanionCharacter::HandleTakedownKill),
				KillDelay, false);
		else
			HandleTakedownKill();
	}
}

void ACompanionCharacter::HandleTakedownKill()
{
	StopWeaponFire();

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(TakedownVictim.Get());
	if (!IsValid(Enemy))
	{
		FinishCommandedTakedown();
		return;
	}

	// Instant chain already registered the kill before the cosmetic shots — don't re-execute on
	// the corpse (it would reject and mislabel a successful takedown as whiffed).
	if (!bTakedownKillRegistered && !Enemy->ExecuteTakedown(this, /*bIgnoreRangeAndArc=*/true))
	{
		UE_LOG(LogCompanion, Warning, TEXT("Takedown: shoot kill rejected (enemy no longer Unaware)"));
		FinishCommandedTakedown();
		return;
	}

	UE_LOG(LogCompanion, Log, TEXT("Takedown: shoot executed"));

	const UWorld* World = GetWorld();
	if (!World) { FinishCommandedTakedown(); return; }

	// Rate <= 0 silently clears a timer instead of firing — ShootLowerDelay=0 must still finish
	// the chain (a hang here leaves bTakedownExecuting stuck and the BT takedown branch waiting).
	if (ShootLowerDelay > 0.f)
		World->GetTimerManager().SetTimer(ShootDelayTimerHandle,
			FTimerDelegate::CreateUObject(this, &ACompanionCharacter::HandleTakedownLower),
			ShootLowerDelay, false);
	else
		HandleTakedownLower();
}

void ACompanionCharacter::HandleTakedownLower()
{
	SetLowReadyAim(true);
	SetAimTarget(nullptr);
	if (AAIController* AIC = Cast<AAIController>(GetController()))
		AIC->ClearFocus(EAIFocusPriority::Gameplay);

	FinishCommandedTakedown();
}

void ACompanionCharacter::FinishCommandedTakedown()
{
	const bool bWasArmed = bTakedownArmed;

	// Finish only runs after an execution (kill applied or shot fired) — a disarm never routes here.
	if (bWasArmed)
		Bark(ECompanionBarkType::TakedownConfirm);

	StopWeaponFire();

	// Unbind both delegates before broadcast to avoid re-entry
	AExtractionPlayer* Player = TakedownPlayerRef.Get();
	if (IsValid(Player))
	{
		Player->OnPlayerTakedownCommitted.RemoveDynamic(this, &ACompanionCharacter::OnPlayerTakedownCommittedHandler);
		Player->OnPlayerFiredWeapon.RemoveDynamic(this, &ACompanionCharacter::OnPlayerFiredWeaponHandler);
	}

	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ShootDelayTimerHandle);
		// A pending mid-montage knife kill must not fire into the next takedown's state
		// (already-fired timers make this a no-op on the normal completion path).
		World->GetTimerManager().ClearTimer(KnifeKillTimerHandle);
	}

	if (AActor* IgnoredVictim = TakedownVictim.Get())
	{
		if (UCapsuleComponent* Capsule = GetCapsuleComponent())
			Capsule->IgnoreActorWhenMoving(IgnoredVictim, false);
		if (ACharacter* VictimChar = Cast<ACharacter>(IgnoredVictim))
			if (UCapsuleComponent* VictimCapsule = VictimChar->GetCapsuleComponent())
				VictimCapsule->IgnoreActorWhenMoving(this, false);
	}

	// Release the grandfather here too. The success path never routed through Disarm's release (the
	// task's CleanupTask calls Disarm afterwards, but Disarm early-outs once Finish has cleared the
	// armed flags), so the claim outlived the takedown. Benign while the victim is a corpse, but a
	// stale reservation is exactly what keeps a survivor asleep if the kill ever fails to register.
	if (AEnemyCharacter* ReservedVictim = Cast<AEnemyCharacter>(TakedownVictim.Get()))
		ReservedVictim->ClearTakedownReservation(this);

	// Stop the heartbeat. Must be done HERE and not left to Disarm for the same reason as above —
	// Disarm's early-out makes its own Reset unreachable on this path. The last stamp's
	// TakedownWindowRefreshSeconds is the deliberate grace tail: the player's synced shot broadcasts
	// its commit before the hitscan, so its noise can arrive after we have already finished.
	TakedownWindowEnemies.Reset();

	TakedownVictim.Reset();
	TakedownPlayerRef.Reset();
	bTakedownArmed = false;
	bTakedownPlayerCommitted = false;
	bTakedownCommandedInstant = false;
	bTakedownKillRegistered = false;
	bTakedownInPosition = false;
	bTakedownExecuting = false;
	bTakedownCrouchApproach = false;
	bTakedownMontagePlaying = false;

	SetAimTarget(nullptr);
	if (IsValid(CurrentWeapon)) CurrentWeapon->SetWeaponHidden(false);
	if (IsValid(TakedownKnifeMesh)) TakedownKnifeMesh->SetHiddenInGame(true);
	if (AAIController* AIC = Cast<AAIController>(GetController()))
		AIC->ClearFocus(EAIFocusPriority::Gameplay);

	if (bWasArmed)
	{
		UE_LOG(LogCompanion, Log, TEXT("Takedown: finished, broadcasting completion"));
		OnCommandedTakedownFinished.Broadcast();
	}
}
