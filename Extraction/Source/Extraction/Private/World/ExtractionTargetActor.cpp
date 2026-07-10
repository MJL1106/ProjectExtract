// AExtractionTargetActor -- placeable extraction point that triggers a Director wave on interact.

#include "ExtractionTargetActor.h"

#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Net/UnrealNetwork.h"

#include "EnemyDirectorSubsystem.h"
#include "ObjectiveSubsystem.h"
#include "MissionInventorySubsystem.h"
#include "LevelCompletionLiftGate.h"
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

	// Placeholder-mesh fallback: no skeletal mesh assigned yet, so the box must catch
	// the interact trace instead. Trade-off (box eats bullets/sight) only exists until
	// a mesh asset is assigned in the placed BP.
	if (IsValid(SkeletalMesh) && !SkeletalMesh->GetSkeletalMeshAsset())
		InteractionBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	// Late joiners receive bActivated/bCompleted (and their OnReps) before BeginPlay;
	// re-registering the reach objective here would leave it dangling.
	if (!bActivated && !bCompleted)
		RegisterReachObjective();

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
}

void AExtractionTargetActor::OnRep_bActivated()
{
	if (bActivated)
	{
		RemoveReachObjective();
		RegisterDefenceObjective();
	}
}

void AExtractionTargetActor::OnRep_bCompleted()
{
	if (bCompleted)
	{
		RemoveDefenceObjective();
	}
}

// ------------------------------------------------------------------
// IWorldInteractable
// ------------------------------------------------------------------

bool AExtractionTargetActor::CanWorldInteract_Implementation(AActor* /*Interactor*/) const
{
	return !bActivated && !bCompleted;
}

void AExtractionTargetActor::WorldInteract_Implementation(AActor* Interactor)
{
	if (!HasAuthority()) return;
	if (bActivated || bCompleted) return;

	// Swap objectives: remove reach, add defence.
	RemoveReachObjective();
	RegisterDefenceObjective();

	// Bind Director delegates before starting the wave so we never miss the completion.
	BindDirectorDelegates();

	UEnemyDirectorSubsystem* Director = GetDirectorSubsystem();
	if (!IsValid(Director) || !Director->StartWave(WaveRequest))
	{
		// Wave failed to start: unbind, restore original state, notify the player.
		UnbindDirectorDelegates();
		RemoveDefenceObjective();
		RegisterReachObjective();

		FText FailureReason = FText::Format(
			NSLOCTEXT("ExtractionTarget", "WaveStartFailed", "Cannot begin extraction: wave \"{0}\" failed to start"),
			FText::FromName(WaveRequest.WaveId));

		UE_LOG(LogExtractionTarget, Warning, TEXT("%s: StartWave failed for '%s'"), *GetName(), *WaveRequest.WaveId.ToString());

		if (UMissionInventorySubsystem* Inventory = GetWorld() ? GetWorld()->GetSubsystem<UMissionInventorySubsystem>() : nullptr)
			Inventory->OnLootNotify.Broadcast(FailureReason);

		return;
	}

	bActivated = true;
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
		// TODO(Task 6): AExtractionGameMode::CompleteLevel() -- wire here once the method exists.
		UE_LOG(LogExtractionTarget, Log, TEXT("%s: CompletionAction is CompleteLevel (stub -- Task 6 wires GameMode::CompleteLevel)"), *GetName());
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

	if (IsValid(InteractionBox) && !InteractionBox->GetCollisionEnabled())
		UE_LOG(LogExtractionTarget, Warning, TEXT("%s: InteractionBox has no collision enabled"), *GetName());

	if (IsValid(SkeletalMesh) && !SkeletalMesh->GetSkeletalMeshAsset())
		UE_LOG(LogExtractionTarget, Warning, TEXT("%s: SkeletalMesh has no mesh asset assigned (placeholder mode -- interaction box blocks Visibility)"), *GetName());
}
#endif
