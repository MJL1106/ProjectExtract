// AExtractionTargetActor -- placeable extraction point that triggers a Director wave on interact.

#include "ExtractionTargetActor.h"

#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Net/UnrealNetwork.h"

#include "EnemyDirectorSubsystem.h"
#include "ExtractionGameMode.h"
#include "ObjectiveSubsystem.h"
#include "MissionInventorySubsystem.h"
#include "LevelCompletionLiftGate.h"
#include "InteractionEventSubsystem.h"
#include "Extraction.h"

DEFINE_LOG_CATEGORY(LogExtractionTarget);

// ------------------------------------------------------------------
// Construction
// ------------------------------------------------------------------

AExtractionTargetActor::AExtractionTargetActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	SkeletalMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("SkeletalMesh"));
	SkeletalMesh->SetupAttachment(SceneRoot);
	SkeletalMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	InteractionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionBox"));
	InteractionBox->SetupAttachment(SceneRoot);
	InteractionBox->SetBoxExtent(FVector(50.f, 50.f, 90.f));
	InteractionBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionBox->SetCanEverAffectNavigation(false);
}

// ------------------------------------------------------------------
// Lifecycle
// ------------------------------------------------------------------

void AExtractionTargetActor::BeginPlay()
{
	Super::BeginPlay();

	// Placeholder-mesh fallback: no skeletal mesh assigned yet — or one without a physics
	// asset, which has zero collision bodies and lets the interact trace pass through — so
	// the box must catch the trace instead. Trade-off (box eats bullets/sight) only exists
	// until a properly set-up mesh asset is assigned in the placed BP.
	const USkeletalMesh* MeshAsset = IsValid(SkeletalMesh) ? SkeletalMesh->GetSkeletalMeshAsset() : nullptr;
	if (!MeshAsset || !MeshAsset->GetPhysicsAsset())
		InteractionBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	// Pure wave controller behind an extractee rescue: invisible, untouchable, no prompt.
	if (bExternalTriggerOnly)
	{
		if (IsValid(SkeletalMesh))
		{
			SkeletalMesh->SetHiddenInGame(true);
			SkeletalMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		InteractionBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}


#if WITH_EDITOR
	ValidateConfig();
#endif
}

void AExtractionTargetActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindDirectorDelegates();
	Super::EndPlay(EndPlayReason);
}

float AExtractionTargetActor::TakeDamage(float /*DamageAmount*/, const FDamageEvent& /*DamageEvent*/,
	AController* /*EventInstigator*/, AActor* /*DamageCauser*/)
{
	// Non-damageable: silently absorb all damage.
	return 0.f;
}

// ------------------------------------------------------------------
// Replication
// ------------------------------------------------------------------

void AExtractionTargetActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AExtractionTargetActor, bActivated);
	DOREPLIFETIME(AExtractionTargetActor, bCompleted);
	DOREPLIFETIME(AExtractionTargetActor, bAvailable);
}

void AExtractionTargetActor::ActivateTarget()
{
	if (!HasAuthority() || bAvailable || bActivated || bCompleted) return;

	bAvailable = true;
	if (!bObjectiveManagedExternally)
		RegisterReachObjective();
}

void AExtractionTargetActor::SetObjectiveManagedExternally(bool bManagedExternally)
{
	bObjectiveManagedExternally = bManagedExternally;
	if (bObjectiveManagedExternally)
	{
		RemoveReachObjective();
		RemoveDefenceObjective();
	}
}
void AExtractionTargetActor::OnRep_bActivated()
{
	if (bActivated && !bObjectiveManagedExternally)
	{
		RemoveReachObjective();
		RegisterDefenceObjective();
	}
}

void AExtractionTargetActor::OnRep_bCompleted()
{
	if (bCompleted && !bObjectiveManagedExternally)
		RemoveDefenceObjective();
}

void AExtractionTargetActor::OnRep_bAvailable()
{
	if (bAvailable && !bObjectiveManagedExternally && !bActivated && !bCompleted)
		RegisterReachObjective();
}

// ------------------------------------------------------------------
// IWorldInteractable
// ------------------------------------------------------------------

bool AExtractionTargetActor::CanWorldInteract_Implementation(AActor* /*Interactor*/) const
{
	return !bExternalTriggerOnly && bAvailable && !bActivated && !bCompleted;
}

bool AExtractionTargetActor::BeginExtractionExternal()
{
	WorldInteract_Implementation(nullptr);
	return bActivated || bCompleted;
}

void AExtractionTargetActor::WorldInteract_Implementation(AActor* Interactor)
{
	if (!HasAuthority()) return;
	if (!bAvailable || bActivated || bCompleted) return;

	// Swap objectives when this actor owns presentation.
	if (!bObjectiveManagedExternally)
	{
		RemoveReachObjective();
		RegisterDefenceObjective();
	}

	// Bind Director delegates before starting the wave so we never miss the completion.
	BindDirectorDelegates();

	UEnemyDirectorSubsystem* Director = GetDirectorSubsystem();
	if (!IsValid(Director) || !Director->StartWave(WaveRequest))
	{
		// Wave failed to start: unbind, restore original state, notify the player.
		UnbindDirectorDelegates();
		if (!bObjectiveManagedExternally)
		{
			RemoveDefenceObjective();
			RegisterReachObjective();
		}

		FText FailureReason = FText::Format(
			NSLOCTEXT("ExtractionTarget", "WaveStartFailed", "Cannot begin extraction: wave \"{0}\" failed to start"),
			FText::FromName(WaveRequest.WaveId));

		UE_LOG(LogExtractionTarget, Warning, TEXT("%s: StartWave failed for '%s'"), *GetName(), *WaveRequest.WaveId.ToString());

		if (UMissionInventorySubsystem* Inventory = GetWorld() ? GetWorld()->GetSubsystem<UMissionInventorySubsystem>() : nullptr)
			Inventory->OnLootNotify.Broadcast(FailureReason);

		return;
	}

	bActivated = true;

	// Success branch only — every earlier return above is a refusal, and a refusal must not tick
	// off an objective beat watching this actor.
	if (UInteractionEventSubsystem* Events = GetWorld() ? GetWorld()->GetSubsystem<UInteractionEventSubsystem>() : nullptr)
		Events->NotifyWorldInteract(this, Interactor);

	OnExtractionTargetWaveStarted.Broadcast();
	UE_LOG(LogExtractionTarget, Log, TEXT("%s: activated by %s, wave '%s' started"),
		*GetName(), *GetNameSafe(Interactor), *WaveRequest.WaveId.ToString());
}

FText AExtractionTargetActor::GetWorldInteractionPrompt_Implementation(AActor* /*Interactor*/) const
{
	return InteractionPrompt;
}

// ------------------------------------------------------------------
// Director event handlers
// ------------------------------------------------------------------

void AExtractionTargetActor::HandleWaveCompleted(FName WaveId)
{
	if (WaveId != WaveRequest.WaveId) return;

	UE_LOG(LogExtractionTarget, Log, TEXT("%s: wave '%s' completed"), *GetName(), *WaveId.ToString());

	if (!bObjectiveManagedExternally)
		RemoveDefenceObjective();
	bCompleted = true;

	PerformCompletionAction();
	UnbindDirectorDelegates();
}

void AExtractionTargetActor::HandleWaveBlocked(FName WaveId, FText Reason)
{
	if (WaveId != WaveRequest.WaveId) return;

	UE_LOG(LogExtractionTarget, Warning, TEXT("%s: wave '%s' blocked: %s"),
		*GetName(), *WaveId.ToString(), *Reason.ToString());

	if (UMissionInventorySubsystem* Inventory = GetWorld() ? GetWorld()->GetSubsystem<UMissionInventorySubsystem>() : nullptr)
		Inventory->OnLootNotify.Broadcast(Reason);
}

// ------------------------------------------------------------------
// Completion actions
// ------------------------------------------------------------------

void AExtractionTargetActor::PerformCompletionAction()
{
	switch (CompletionAction)
	{
	case EWaveCompletionAction::UnlockExit:
		if (IsValid(LiftGateTarget))
		{
			LiftGateTarget->UnlockExit();
		}
		else
		{
			UE_LOG(LogExtractionTarget, Warning, TEXT("%s: CompletionAction is UnlockExit but LiftGateTarget is null"), *GetName());
		}
		break;

	case EWaveCompletionAction::CompleteLevel:
		if (AExtractionGameMode* GameMode = GetWorld()->GetAuthGameMode<AExtractionGameMode>())
			GameMode->CompleteLevel();
		else
			UE_LOG(LogExtractionTarget, Warning, TEXT("%s: CompletionAction is CompleteLevel but no AExtractionGameMode found"), *GetName());
		break;

	case EWaveCompletionAction::BroadcastOnly:
		break;
	}

	// All paths broadcast the completion delegate so Blueprint listeners always fire.
	OnExtractionTargetCompleted.Broadcast();
}

// ------------------------------------------------------------------
// Director delegate binding
// ------------------------------------------------------------------

void AExtractionTargetActor::BindDirectorDelegates()
{
	UEnemyDirectorSubsystem* Director = GetDirectorSubsystem();
	if (!IsValid(Director)) return;

	if (!Director->OnDirectorWaveCompleted.IsAlreadyBound(this, &AExtractionTargetActor::HandleWaveCompleted))
		Director->OnDirectorWaveCompleted.AddDynamic(this, &AExtractionTargetActor::HandleWaveCompleted);

	if (!Director->OnDirectorWaveBlocked.IsAlreadyBound(this, &AExtractionTargetActor::HandleWaveBlocked))
		Director->OnDirectorWaveBlocked.AddDynamic(this, &AExtractionTargetActor::HandleWaveBlocked);
}

void AExtractionTargetActor::UnbindDirectorDelegates()
{
	UEnemyDirectorSubsystem* Director = GetDirectorSubsystem();
	if (!IsValid(Director)) return;

	Director->OnDirectorWaveCompleted.RemoveDynamic(this, &AExtractionTargetActor::HandleWaveCompleted);
	Director->OnDirectorWaveBlocked.RemoveDynamic(this, &AExtractionTargetActor::HandleWaveBlocked);
}

// ------------------------------------------------------------------
// Objective helpers
// ------------------------------------------------------------------

void AExtractionTargetActor::RegisterReachObjective()
{
	if (UObjectiveSubsystem* Objectives = GetObjectiveSubsystem())
		Objectives->AddObjective(ReachObjectiveId, ReachObjectiveLabel, GetActorLocation(), this);
}

void AExtractionTargetActor::RemoveReachObjective()
{
	if (UObjectiveSubsystem* Objectives = GetObjectiveSubsystem())
		Objectives->RemoveObjective(ReachObjectiveId);
}

void AExtractionTargetActor::RegisterDefenceObjective()
{
	if (UObjectiveSubsystem* Objectives = GetObjectiveSubsystem())
		Objectives->AddObjective(DefenceObjectiveId, DefenceObjectiveLabel, GetActorLocation(), this);
}

void AExtractionTargetActor::RemoveDefenceObjective()
{
	if (UObjectiveSubsystem* Objectives = GetObjectiveSubsystem())
		Objectives->RemoveObjective(DefenceObjectiveId);
}

// ------------------------------------------------------------------
// Subsystem accessors
// ------------------------------------------------------------------

UObjectiveSubsystem* AExtractionTargetActor::GetObjectiveSubsystem() const
{
	UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UObjectiveSubsystem>() : nullptr;
}

UEnemyDirectorSubsystem* AExtractionTargetActor::GetDirectorSubsystem() const
{
	UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UEnemyDirectorSubsystem>() : nullptr;
}

// ------------------------------------------------------------------
// Editor validation
// ------------------------------------------------------------------

#if WITH_EDITOR
void AExtractionTargetActor::ValidateConfig() const
{
	if (WaveRequest.TargetSquads < 1)
		UE_LOG(LogExtractionTarget, Warning, TEXT("%s: WaveRequest.TargetSquads is %d (< 1)"), *GetName(), WaveRequest.TargetSquads);

	if (!IsValid(WaveRequest.ConfigOverride))
		UE_LOG(LogExtractionTarget, Warning, TEXT("%s: WaveRequest.ConfigOverride is null"), *GetName());

	if (CompletionAction == EWaveCompletionAction::UnlockExit && !IsValid(LiftGateTarget))
		UE_LOG(LogExtractionTarget, Warning, TEXT("%s: CompletionAction is UnlockExit but LiftGateTarget is null"), *GetName());

	if (!bExternalTriggerOnly && IsValid(InteractionBox) && !InteractionBox->GetCollisionEnabled())
		UE_LOG(LogExtractionTarget, Warning, TEXT("%s: InteractionBox has no collision enabled"), *GetName());

	if (IsValid(SkeletalMesh) && !SkeletalMesh->GetSkeletalMeshAsset())
		UE_LOG(LogExtractionTarget, Warning, TEXT("%s: SkeletalMesh has no mesh asset assigned (placeholder mode -- interaction box blocks Visibility)"), *GetName());
}
#endif
