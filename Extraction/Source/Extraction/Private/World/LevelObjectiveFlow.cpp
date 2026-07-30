#include "World/LevelObjectiveFlow.h"

#include "AI/CompanionAIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Companion/CompanionCharacter.h"
#include "Companion/CompanionRoute.h"
#include "Components/HealthComponent.h"
#include "Enemy/EnemyCharacter.h"
#include "Enemy/EnemyDirectorSubsystem.h"
#include "EngineUtils.h"
#include "Extractee/ExtracteeCompanion.h"
#include "Character/ExtractionPlayer.h"
#include "Game/ExtractionGameInstance.h"
#include "Game/ExtractionGameMode.h"
#include "Game/MissionInventorySubsystem.h"
#include "Game/ObjectiveSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "World/BreachableDoor.h"
#include "World/CompanionModeDoorGate.h"
#include "World/DoorBase.h"
#include "World/ExtractionTargetActor.h"
#include "World/LevelCompletionLiftGate.h"
#include "World/LootContainer.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogLevelObjectiveFlow, Log, All);

const FName ALevelObjectiveFlow::PrimaryObjectiveId(TEXT("PrimaryObjective"));
const FName ALevelObjectiveFlow::OptionalSuppliesObjectiveId(TEXT("OptionalSupplies"));

bool FLevelObjectiveStateMachine::Activate()
{
	if (CurrentStep != ELevelObjectiveStep::Inactive) return false;
	CurrentStep = ELevelObjectiveStep::BreachRoofDoor;
	return true;
}

bool FLevelObjectiveStateMachine::Advance(ELevelObjectiveEvent Event)
{
	const ELevelObjectiveStep NextStep = GetNextStep(CurrentStep, Event);
	if (NextStep == CurrentStep) return false;
	CurrentStep = NextStep;
	return true;
}

ELevelObjectiveStep FLevelObjectiveStateMachine::GetNextStep(const ELevelObjectiveStep Step,
	const ELevelObjectiveEvent Event)
{
	switch (Step)
	{
	case ELevelObjectiveStep::BreachRoofDoor:
		if (Event == ELevelObjectiveEvent::RoofDoorOpened) return ELevelObjectiveStep::FollowCompanionDownstairs;
		break;
	case ELevelObjectiveStep::FollowCompanionDownstairs:
		if (Event == ELevelObjectiveEvent::RouteCompleted) return ELevelObjectiveStep::BreachStairwellDoor;
		break;
	case ELevelObjectiveStep::BreachStairwellDoor:
		if (Event == ELevelObjectiveEvent::StairwellDoorOpened) return ELevelObjectiveStep::ClearRoom1;
		break;
	case ELevelObjectiveStep::ClearRoom1:
		if (Event == ELevelObjectiveEvent::Room1Cleared) return ELevelObjectiveStep::FindOfficeKeycard;
		break;
	case ELevelObjectiveStep::FindOfficeKeycard:
		if (Event == ELevelObjectiveEvent::KeycardLooted) return ELevelObjectiveStep::UnlockStairwellDoor;
		break;
	case ELevelObjectiveStep::UnlockStairwellDoor:
		if (Event == ELevelObjectiveEvent::ExitDoorOpened) return ELevelObjectiveStep::SwitchCompanionToStealth;
		break;
	case ELevelObjectiveStep::SwitchCompanionToStealth:
		if (Event == ELevelObjectiveEvent::Room2DoorOpened) return ELevelObjectiveStep::FirstDoubleTakedown;
		break;
	case ELevelObjectiveStep::FirstDoubleTakedown:
		if (Event == ELevelObjectiveEvent::FirstPairCleared) return ELevelObjectiveStep::SecondDoubleTakedown;
		break;
	case ELevelObjectiveStep::SecondDoubleTakedown:
		if (Event == ELevelObjectiveEvent::SecondPairCleared) return ELevelObjectiveStep::ReachExtractionTarget;
		break;
	case ELevelObjectiveStep::ReachExtractionTarget:
		if (Event == ELevelObjectiveEvent::ExtractionStarted) return ELevelObjectiveStep::DefendPosition;
		break;
	case ELevelObjectiveStep::DefendPosition:
		if (Event == ELevelObjectiveEvent::ExtractionCompleted) return ELevelObjectiveStep::UseLift;
		break;
	case ELevelObjectiveStep::UseLift:
		if (Event == ELevelObjectiveEvent::LiftCompleted) return ELevelObjectiveStep::Completed;
		break;
	default:
		break;
	}
	return Step;
}
ALevelObjectiveFlow::ALevelObjectiveFlow()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
}

void ALevelObjectiveFlow::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ALevelObjectiveFlow, CurrentStep);
	DOREPLIFETIME(ALevelObjectiveFlow, bOptionalSuppliesActive);
	DOREPLIFETIME(ALevelObjectiveFlow, CurrentPrimaryTarget);
	DOREPLIFETIME(ALevelObjectiveFlow, CurrentOptionalTarget);
	DOREPLIFETIME(ALevelObjectiveFlow, PresentationRevision);
	DOREPLIFETIME(ALevelObjectiveFlow, Room1AreaLocation);
	DOREPLIFETIME(ALevelObjectiveFlow, DefendAreaLocation);
}

void ALevelObjectiveFlow::BeginPlay()
{
	Super::BeginPlay();

	if (IsValid(ExtractionTarget))
		ExtractionTarget->SetObjectiveManagedExternally(true);

	if (HasAuthority())
	{
		BindDelegates();
		RefreshRecordedEnemyDeaths();
		if (bAutoActivate)
			ActivateFlow();
	}
	else
	{
		OnRep_CurrentStep();
		OnRep_OptionalSuppliesActive();
	}
}

void ALevelObjectiveFlow::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CheckpointHealPollHandle);
		World->GetTimerManager().ClearTimer(CheckpointSpawnRetryHandle);
		World->GetTimerManager().ClearTimer(ExtractionWaveRetryHandle);
	}
	if (HasAuthority()) UnbindDelegates();
	if (UObjectiveSubsystem* Objectives = GetWorld() ? GetWorld()->GetSubsystem<UObjectiveSubsystem>() : nullptr)
	{
		Objectives->RemoveObjective(PrimaryObjectiveId);
		Objectives->RemoveObjective(OptionalSuppliesObjectiveId);
	}
	Super::EndPlay(EndPlayReason);
}

bool ALevelObjectiveFlow::ActivateFlow()
{
	if (!HasAuthority() || CurrentStep != ELevelObjectiveStep::Inactive || !ValidateReferences()) return false;

	// Pin the ClearRoom1 marker to the room area: centroid of the placed Room 1 enemies,
	// unless a hand-placed anchor was set in the level.
	if (Room1AreaLocation.IsZero() && Room1Enemies.Num() > 0)
	{
		FVector Sum = FVector::ZeroVector;
		int32 ValidCount = 0;
		for (const AEnemyCharacter* Enemy : Room1Enemies)
		{
			if (!IsValid(Enemy)) continue;
			Sum += Enemy->GetActorLocation();
			++ValidCount;
		}
		if (ValidCount > 0)
			Room1AreaLocation = Sum / ValidCount;
	}

	WarnOnSuspectWiring();

	// The VIP is not rescuable until the Reach step is current (no out-of-order path to catch up).
	if (IsValid(Extractee))
		Extractee->SetRescueEnabled(false);

	CurrentStep = ELevelObjectiveStep::BreachRoofDoor;
	ForceNetUpdate();
	UpdatePrimaryObjective();
	TryResumeFromCheckpoint();
	return true;
}

void ALevelObjectiveFlow::NotifyLiftCompleted()
{
	if (HasAuthority()) Advance(ELevelObjectiveEvent::LiftCompleted);
}

bool ALevelObjectiveFlow::ValidateReferences() const
{
	bool bValid = true;
	auto Require = [this, &bValid](const UObject* Object, const TCHAR* Label)
	{
		if (IsValid(Object)) return;
		UE_LOG(LogLevelObjectiveFlow, Error, TEXT("%s: missing required reference: %s"), *GetName(), Label);
		bValid = false;
	};

	Require(RoofDoor, TEXT("RoofDoor"));
	Require(DownstairsRoute, TEXT("DownstairsRoute"));
	Require(StairwellBreachDoor, TEXT("StairwellBreachDoor"));
	Require(KeycardContainer, TEXT("KeycardContainer"));
	Require(Room1ExitDoor, TEXT("Room1ExitDoor"));
	Require(Room2EntryDoor, TEXT("Room2EntryDoor"));
	Require(LiftGate, TEXT("LiftGate"));

	// Extraction beat owner: the armed VIP (current design) or the legacy placeholder actor. One
	// is required; neither means the level has nothing to extract and the flow would dead-end at
	// the Reach step, so refuse to activate rather than run half a mission.
	if (!IsValid(Extractee) && !IsValid(ExtractionTarget))
	{
		UE_LOG(LogLevelObjectiveFlow, Error,
			TEXT("%s: neither Extractee nor ExtractionTarget is set — nothing owns the extraction beat"),
			*GetName());
		bValid = false;
	}

	// The flow only runs the wave itself when no target actor owns it, and StartWave rejects
	// TargetSquads < 1 — a default-constructed request would silently never start.
	if (!IsValid(ExtractionTarget) && ExtractionWave.TargetSquads < 1)
	{
		UE_LOG(LogLevelObjectiveFlow, Error,
			TEXT("%s: this flow owns the extraction wave but ExtractionWave.TargetSquads is %d (< 1)"),
			*GetName(), ExtractionWave.TargetSquads);
		bValid = false;
	}

	if (Room1Enemies.IsEmpty())
	{
		UE_LOG(LogLevelObjectiveFlow, Error, TEXT("%s: Room1Enemies is empty"), *GetName());
		bValid = false;
	}
	if (FirstTakedownPair.Num() != 2 || SecondTakedownPair.Num() != 2)
	{
		UE_LOG(LogLevelObjectiveFlow, Error, TEXT("%s: each takedown pair must contain exactly two enemies"), *GetName());
		bValid = false;
	}
	if (SupplyCrates.Num() != 7)
	{
		UE_LOG(LogLevelObjectiveFlow, Error, TEXT("%s: SupplyCrates must contain exactly seven crates"), *GetName());
		bValid = false;
	}

	for (const AEnemyCharacter* Enemy : Room1Enemies) Require(Enemy, TEXT("Room1Enemies entry"));
	for (const AEnemyCharacter* Enemy : FirstTakedownPair) Require(Enemy, TEXT("FirstTakedownPair entry"));
	for (const AEnemyCharacter* Enemy : SecondTakedownPair) Require(Enemy, TEXT("SecondTakedownPair entry"));
	for (const ALootContainer* Crate : SupplyCrates) Require(Crate, TEXT("SupplyCrates entry"));
	return bValid;
}

void ALevelObjectiveFlow::WarnOnSuspectWiring() const
{
	UWorld* World = GetWorld();
	if (!World) return;

	// Extractee is EditInstanceOnly — it lives on the placed actor, not the class defaults, so a
	// level saved before it existed silently reverts to the legacy placeholder path and the rescue
	// starts nothing. A one-tick fix, but only if someone knows to make it.
	if (!IsValid(Extractee))
	{
		for (TActorIterator<AExtracteeCompanion> It(World); It; ++It)
		{
			UE_LOG(LogLevelObjectiveFlow, Warning,
				TEXT("%s: '%s' is in the level but Extractee is unset — the rescue will not start the extraction "
					 "wave. Wire it on the placed flow actor."),
				*GetName(), *It->GetName());
			break;
		}
		return;
	}

	if (IsValid(ExtractionTarget) && !ExtractionTarget->IsExternalTriggerOnly())
	{
		UE_LOG(LogLevelObjectiveFlow, Warning,
			TEXT("%s: Extractee is wired but '%s' is not bExternalTriggerOnly — the placeholder stays visible and "
				 "separately interactable alongside the VIP rescue."),
			*GetName(), *ExtractionTarget->GetName());
	}

	// Flow-owned wave: nothing else validates the request, and a missing config DA falls back to
	// the Director's ambient profile — technically a wave, but not the scripted assault.
	if (OwnsExtractionWave())
	{
		if (ExtractionWave.WaveId.IsNone())
			UE_LOG(LogLevelObjectiveFlow, Warning,
				TEXT("%s: ExtractionWave.WaveId is None — completion matching is by id, so name it."), *GetName());
		if (!IsValid(ExtractionWave.ConfigOverride))
			UE_LOG(LogLevelObjectiveFlow, Warning,
				TEXT("%s: ExtractionWave.ConfigOverride is unset — the defence wave will run on the ambient "
					 "director profile instead of an assault profile."), *GetName());
	}
}

void ALevelObjectiveFlow::BindDelegates()
{
	auto BindDoor = [this](ADoorBase* Door)
	{
		if (IsValid(Door)) Door->OnDoorOpened.AddUniqueDynamic(this, &ALevelObjectiveFlow::HandleDoorOpened);
	};
	BindDoor(RoofDoor);
	BindDoor(StairwellBreachDoor);
	BindDoor(Room1ExitDoor);
	BindDoor(Room2EntryDoor);

	if (IsValid(DownstairsRoute))
		DownstairsRoute->OnRouteCompleted.AddUniqueDynamic(this, &ALevelObjectiveFlow::HandleRouteCompleted);
	if (IsValid(KeycardContainer))
		KeycardContainer->OnLootCompleted.AddUniqueDynamic(this, &ALevelObjectiveFlow::HandleLootCompleted);

	auto BindEnemyGroup = [this](const TArray<TObjectPtr<AEnemyCharacter>>& Enemies)
	{
		for (AEnemyCharacter* Enemy : Enemies)
		{
			if (!IsValid(Enemy)) continue;
			if (IsValid(Enemy->GetHealthComponent()))
				Enemy->GetHealthComponent()->OnDeath.AddUniqueDynamic(this, &ALevelObjectiveFlow::HandleTrackedEnemyDeath);
			Enemy->OnDestroyed.AddUniqueDynamic(this, &ALevelObjectiveFlow::HandleTrackedEnemyDestroyed);
		}
	};
	BindEnemyGroup(Room1Enemies);
	BindEnemyGroup(FirstTakedownPair);
	BindEnemyGroup(SecondTakedownPair);

	for (ALootContainer* Crate : SupplyCrates)
	{
		if (!IsValid(Crate)) continue;
		Crate->OnLootCompleted.AddUniqueDynamic(this, &ALevelObjectiveFlow::HandleLootCompleted);
		Crate->OnDestroyed.AddUniqueDynamic(this, &ALevelObjectiveFlow::HandleSupplyDestroyed);
	}

	if (IsValid(ExtractionTarget))
	{
		ExtractionTarget->OnExtractionTargetWaveStarted.AddUniqueDynamic(this, &ALevelObjectiveFlow::HandleExtractionStarted);
		ExtractionTarget->OnExtractionTargetCompleted.AddUniqueDynamic(this, &ALevelObjectiveFlow::HandleExtractionCompleted);
	}

	if (IsValid(Extractee))
		Extractee->OnRescued.AddUniqueDynamic(this, &ALevelObjectiveFlow::HandleExtracteeRescued);

	// With no target actor there is nobody else listening for the Director — bind it here, before
	// the wave can start, so the completion that unlocks the exit can never be missed.
	if (OwnsExtractionWave())
	{
		if (UEnemyDirectorSubsystem* Director = GetWorld() ? GetWorld()->GetSubsystem<UEnemyDirectorSubsystem>() : nullptr)
		{
			Director->OnDirectorWaveCompleted.AddUniqueDynamic(this, &ALevelObjectiveFlow::HandleDirectorWaveCompleted);
			Director->OnDirectorWaveBlocked.AddUniqueDynamic(this, &ALevelObjectiveFlow::HandleDirectorWaveBlocked);
		}
	}
}

void ALevelObjectiveFlow::UnbindDelegates()
{
	auto UnbindDoor = [this](ADoorBase* Door)
	{
		if (IsValid(Door)) Door->OnDoorOpened.RemoveDynamic(this, &ALevelObjectiveFlow::HandleDoorOpened);
	};
	UnbindDoor(RoofDoor);
	UnbindDoor(StairwellBreachDoor);
	UnbindDoor(Room1ExitDoor);
	UnbindDoor(Room2EntryDoor);

	if (IsValid(DownstairsRoute))
		DownstairsRoute->OnRouteCompleted.RemoveDynamic(this, &ALevelObjectiveFlow::HandleRouteCompleted);
	if (IsValid(KeycardContainer))
		KeycardContainer->OnLootCompleted.RemoveDynamic(this, &ALevelObjectiveFlow::HandleLootCompleted);

	auto UnbindEnemyGroup = [this](const TArray<TObjectPtr<AEnemyCharacter>>& Enemies)
	{
		for (AEnemyCharacter* Enemy : Enemies)
		{
			if (!IsValid(Enemy)) continue;
			if (IsValid(Enemy->GetHealthComponent()))
				Enemy->GetHealthComponent()->OnDeath.RemoveDynamic(this, &ALevelObjectiveFlow::HandleTrackedEnemyDeath);
			Enemy->OnDestroyed.RemoveDynamic(this, &ALevelObjectiveFlow::HandleTrackedEnemyDestroyed);
		}
	};
	UnbindEnemyGroup(Room1Enemies);
	UnbindEnemyGroup(FirstTakedownPair);
	UnbindEnemyGroup(SecondTakedownPair);

	for (ALootContainer* Crate : SupplyCrates)
	{
		if (!IsValid(Crate)) continue;
		Crate->OnLootCompleted.RemoveDynamic(this, &ALevelObjectiveFlow::HandleLootCompleted);
		Crate->OnDestroyed.RemoveDynamic(this, &ALevelObjectiveFlow::HandleSupplyDestroyed);
	}

	if (IsValid(ExtractionTarget))
	{
		ExtractionTarget->OnExtractionTargetWaveStarted.RemoveDynamic(this, &ALevelObjectiveFlow::HandleExtractionStarted);
		ExtractionTarget->OnExtractionTargetCompleted.RemoveDynamic(this, &ALevelObjectiveFlow::HandleExtractionCompleted);
	}

	if (IsValid(Extractee))
		Extractee->OnRescued.RemoveDynamic(this, &ALevelObjectiveFlow::HandleExtracteeRescued);

	if (UEnemyDirectorSubsystem* Director = GetWorld() ? GetWorld()->GetSubsystem<UEnemyDirectorSubsystem>() : nullptr)
	{
		Director->OnDirectorWaveCompleted.RemoveDynamic(this, &ALevelObjectiveFlow::HandleDirectorWaveCompleted);
		Director->OnDirectorWaveBlocked.RemoveDynamic(this, &ALevelObjectiveFlow::HandleDirectorWaveBlocked);
	}
}

void ALevelObjectiveFlow::Advance(ELevelObjectiveEvent Event)
{
	if (!HasAuthority()) return;
	const ELevelObjectiveStep NextStep = FLevelObjectiveStateMachine::GetNextStep(CurrentStep, Event);
	if (NextStep == CurrentStep) return;

	CurrentStep = NextStep;
	if (CurrentStep == ELevelObjectiveStep::FirstDoubleTakedown)
		ActivateOptionalSupplies();
	if (CurrentStep == ELevelObjectiveStep::ReachExtractionTarget)
	{
		if (IsValid(ExtractionTarget))
			ExtractionTarget->ActivateTarget();
		if (IsValid(Extractee))
			Extractee->SetRescueEnabled(true);
	}
	if (CheckpointSteps.Contains(CurrentStep))
	{
		TryApplyCompanionCheckpointHeal();
		// Record the checkpoint so a level restart (fail flow) resumes here.
		if (UExtractionGameInstance* GameInstance = Cast<UExtractionGameInstance>(GetGameInstance()))
			GameInstance->SetCheckpoint(FName(*UGameplayStatics::GetCurrentLevelName(this, true)), CurrentStep);

		// Capture the player's loadout so the restart restores weapons/ammo/stims.
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
		{
			if (AExtractionPlayer* Player = Cast<AExtractionPlayer>(PC->GetPawn()))
				Player->RequestLoadoutCapture();
		}
	}

	ForceNetUpdate();
	UpdatePrimaryObjective();
	RefreshRecordedEnemyDeaths();
	EvaluateCurrentEnemyStep();

	// Late-entry catch-up: an event that fires before its step is current (keycard looted
	// mid-fight, a door the companion breached on the way past) is dropped by the linear machine,
	// and the source broadcasts are one-shot — they never re-fire once the step activates. Re-check
	// world state on entry and advance again; the recursion walks through every satisfied step.
	//
	// EVERY door step gets this, not just the two that used to. A dropped door event is a hard
	// deadlock with no recovery: the step never completes, so the flow — and the extraction it
	// gates — stops for the rest of the level.
	if (CurrentStep == ELevelObjectiveStep::FindOfficeKeycard
		&& IsValid(KeycardContainer) && KeycardContainer->IsLooted())
	{
		Advance(ELevelObjectiveEvent::KeycardLooted);
		return;
	}

	struct FDoorCatchUp
	{
		ELevelObjectiveStep Step;
		const ADoorBase* Door;
		ELevelObjectiveEvent Event;
	};
	const FDoorCatchUp DoorCatchUps[] = {
		{ ELevelObjectiveStep::BreachRoofDoor,           RoofDoor,             ELevelObjectiveEvent::RoofDoorOpened },
		{ ELevelObjectiveStep::BreachStairwellDoor,      StairwellBreachDoor,  ELevelObjectiveEvent::StairwellDoorOpened },
		{ ELevelObjectiveStep::UnlockStairwellDoor,      Room1ExitDoor,        ELevelObjectiveEvent::ExitDoorOpened },
		{ ELevelObjectiveStep::SwitchCompanionToStealth, Room2EntryDoor,       ELevelObjectiveEvent::Room2DoorOpened },
	};
	for (const FDoorCatchUp& CatchUp : DoorCatchUps)
	{
		if (CurrentStep != CatchUp.Step || !IsValid(CatchUp.Door) || !CatchUp.Door->IsOpenForAcoustics()) continue;

		UE_LOG(LogLevelObjectiveFlow, Log, TEXT("%s: step %d entered with '%s' already open — catching up"),
			*GetName(), static_cast<int32>(CurrentStep), *GetNameSafe(CatchUp.Door));
		Advance(CatchUp.Event);
		return;
	}
}

void ALevelObjectiveFlow::TryApplyCompanionCheckpointHeal()
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority()) return;

	ACompanionCharacter* Companion = CachedCompanion.Get();
	if (!IsValid(Companion))
	{
		// Primary only — the extractee joins fully healed at rescue and owns its own recovery.
		Companion = ACompanionCharacter::GetPrimaryCompanion(World);
		CachedCompanion = Companion;
	}
	if (!IsValid(Companion))
	{
		UE_LOG(LogLevelObjectiveFlow, Warning, TEXT("Checkpoint heal dropped — no companion found in level (step %d)"),
			static_cast<int32>(CurrentStep));
		World->GetTimerManager().ClearTimer(CheckpointHealPollHandle);
		return;
	}

	// DBNO holds the heal — the revive flow owns recovery; the full heal lands after revive.
	bool bInCombat = Companion->GetIsCompanionDBNO();
	if (!bInCombat)
	{
		if (const AAIController* AICtl = Cast<AAIController>(Companion->GetController()))
			if (const UBlackboardComponent* BB = AICtl->GetBlackboardComponent())
				bInCombat = IsValid(BB->GetValueAsObject(ACompanionAIController::BB_CombatTarget));
	}

	if (bInCombat)
	{
		if (!World->GetTimerManager().IsTimerActive(CheckpointHealPollHandle))
			World->GetTimerManager().SetTimer(CheckpointHealPollHandle, this,
				&ALevelObjectiveFlow::TryApplyCompanionCheckpointHeal, 1.f, true);
		return;
	}

	World->GetTimerManager().ClearTimer(CheckpointHealPollHandle);
	if (UHealthComponent* Health = Companion->GetHealthComponent())
	{
		if (Health->IsAlive() && Health->GetCurrentHealth() < Health->GetMaxHealth())
		{
			Health->Heal(Health->GetMaxHealth());
			UE_LOG(LogLevelObjectiveFlow, Log, TEXT("Checkpoint step %d reached — companion healed to full"),
				static_cast<int32>(CurrentStep));
		}
	}
}

void ALevelObjectiveFlow::TryResumeFromCheckpoint()
{
	if (!HasAuthority()) return;
	const UExtractionGameInstance* GameInstance = Cast<UExtractionGameInstance>(GetGameInstance());
	if (!GameInstance) return;

	const FName LevelName(*UGameplayStatics::GetCurrentLevelName(this, true));
	const ELevelObjectiveStep Checkpoint = GameInstance->GetCheckpointForLevel(LevelName);
	if (Checkpoint == ELevelObjectiveStep::Inactive || Checkpoint <= CurrentStep) return;

	UE_LOG(LogLevelObjectiveFlow, Log, TEXT("%s: resuming at checkpoint step %d"),
		*GetName(), static_cast<int32>(Checkpoint));
	FastForwardToStep(Checkpoint);

	CheckpointSpawnRetries = 0;
	TryApplyCheckpointSpawn();
}

void ALevelObjectiveFlow::FastForwardToStep(ELevelObjectiveStep TargetStep)
{
	auto StepDone = [TargetStep](ELevelObjectiveStep Step) { return Step < TargetStep; };
	auto DestroyGroup = [](const TArray<TObjectPtr<AEnemyCharacter>>& Enemies)
	{
		for (AEnemyCharacter* Enemy : Enemies)
			if (IsValid(Enemy)) Enemy->Destroy();
	};
	TArray<const ADoorBase*, TInlineAllocator<4>> OpenedDoors;
	auto OpenDoor = [&OpenedDoors](ADoorBase* Door)
	{
		if (!IsValid(Door)) return;
		Door->ForceOpenInstant();
		OpenedDoors.Add(Door);
	};

	// Step FIRST: door force-opens fire OnDoorOpened, which re-enters Advance — with the step
	// already at the target those events no-op instead of walking the state machine again.
	CurrentStep = TargetStep;

	if (StepDone(ELevelObjectiveStep::BreachRoofDoor))
		OpenDoor(RoofDoor);
	// FollowCompanionDownstairs: nothing to restore — the route command simply never runs.
	if (StepDone(ELevelObjectiveStep::BreachStairwellDoor))
		OpenDoor(StairwellBreachDoor);
	if (StepDone(ELevelObjectiveStep::ClearRoom1))
		DestroyGroup(Room1Enemies);
	if (StepDone(ELevelObjectiveStep::FindOfficeKeycard))
	{
		// Keycards are kept, not consumed — re-granting is correct at every later step, and the
		// UnlockStairwellDoor step needs it back in the mission inventory.
		if (const ABreachableDoor* LockedDoor = Cast<ABreachableDoor>(Room1ExitDoor.Get()))
			if (UMissionInventorySubsystem* Inventory = GetWorld()->GetSubsystem<UMissionInventorySubsystem>())
				if (LockedDoor->GetRequiredKeycardId() != NAME_None)
					Inventory->RecordKeycard(LockedDoor->GetRequiredKeycardId());
	}
	if (StepDone(ELevelObjectiveStep::UnlockStairwellDoor))
		OpenDoor(Room1ExitDoor);
	if (StepDone(ELevelObjectiveStep::SwitchCompanionToStealth))
		OpenDoor(Room2EntryDoor);
	if (StepDone(ELevelObjectiveStep::FirstDoubleTakedown))
		DestroyGroup(FirstTakedownPair);
	if (StepDone(ELevelObjectiveStep::SecondDoubleTakedown))
		DestroyGroup(SecondTakedownPair);

	// Retire any mode gate whose door this fast-forward opened. BeginPlay order between gate and
	// flow is undefined: telling the gate directly covers gate-first (its lock is cleared), and
	// the gate's own BeginPlay skips locking once bUnlocked is set (flow-first).
	if (OpenedDoors.Num() > 0)
		for (TActorIterator<ACompanionModeDoorGate> It(GetWorld()); It; ++It)
			if (OpenedDoors.Contains(It->GetTargetDoor()))
				It->ForceUnlockForCheckpoint();

	// Entry side effects Advance() would have fired on the way to the target step.
	if (TargetStep >= ELevelObjectiveStep::FirstDoubleTakedown)
		ActivateOptionalSupplies();
	if (TargetStep >= ELevelObjectiveStep::ReachExtractionTarget && IsValid(ExtractionTarget))
		ExtractionTarget->ActivateTarget();
	if (TargetStep == ELevelObjectiveStep::ReachExtractionTarget && IsValid(Extractee))
		Extractee->SetRescueEnabled(true);
	if (TargetStep >= ELevelObjectiveStep::DefendPosition && IsValid(Extractee))
	{
		// Past the rescue: the VIP resumes armed at the player's side, and — since its interact
		// is gone — the defence wave must start here rather than wait for one.
		// He is still at his staged spot this frame, so it is also the last chance to read the
		// hold-ground for the Defend marker.
		if (DefendAreaLocation.IsZero())
			DefendAreaLocation = Extractee->GetActorLocation();
		Extractee->ForceRescue();
		if (TargetStep == ELevelObjectiveStep::DefendPosition)
			TryStartExtractionWave();
	}

	ForceNetUpdate();
	RefreshRecordedEnemyDeaths();
	UpdatePrimaryObjective();
	EvaluateCurrentEnemyStep();
}

void ALevelObjectiveFlow::TryApplyCheckpointSpawn()
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority()) return;

	AActor* SpawnPoint = CheckpointSpawns.FindRef(CurrentStep);
	if (!IsValid(SpawnPoint))
	{
		UE_LOG(LogLevelObjectiveFlow, Warning,
			TEXT("%s: no CheckpointSpawns entry for step %d — resuming at the level-start position"),
			*GetName(), static_cast<int32>(CurrentStep));
		return;
	}

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!IsValid(PlayerPawn))
	{
		// Player spawn order vs level-actor BeginPlay isn't guaranteed — poll briefly.
		if (++CheckpointSpawnRetries <= 40)
		{
			if (!World->GetTimerManager().IsTimerActive(CheckpointSpawnRetryHandle))
				World->GetTimerManager().SetTimer(CheckpointSpawnRetryHandle, this,
					&ALevelObjectiveFlow::TryApplyCheckpointSpawn, 0.25f, true);
			return;
		}
		World->GetTimerManager().ClearTimer(CheckpointSpawnRetryHandle);
		UE_LOG(LogLevelObjectiveFlow, Warning, TEXT("%s: no player pawn after %d spawn polls — checkpoint teleport skipped"),
			*GetName(), CheckpointSpawnRetries);
		return;
	}
	World->GetTimerManager().ClearTimer(CheckpointSpawnRetryHandle);

	const FVector SpawnLoc = SpawnPoint->GetActorLocation();
	const FRotator SpawnRot(0.f, SpawnPoint->GetActorRotation().Yaw, 0.f);
	// TeleportTo fails on encroachment (misplaced TargetPoint) — force the drop rather than
	// silently leaving the pawn at level start with the world already fast-forwarded.
	if (!PlayerPawn->TeleportTo(SpawnLoc, SpawnRot))
	{
		UE_LOG(LogLevelObjectiveFlow, Warning, TEXT("%s: checkpoint spawn encroached at %s — forcing player teleport"),
			*GetName(), *SpawnLoc.ToCompactString());
		PlayerPawn->TeleportTo(SpawnLoc, SpawnRot, /*bIsATest*/ false, /*bNoCheck*/ true);
	}
	if (AController* PlayerController = PlayerPawn->GetController())
		PlayerController->SetControlRotation(SpawnRot);

	// Every possessed companion lands beside the player (alternating sides); a captive
	// extractee has no controller and stays at its staged spot.
	int32 CompanionsMoved = 0;
	for (TActorIterator<ACompanionCharacter> It(World); It; ++It)
	{
		ACompanionCharacter* Companion = *It;
		if (!IsValid(Companion) || !Companion->GetController()) continue;

		const float Side = (CompanionsMoved % 2 == 0) ? 150.f : -150.f;
		const FVector CompanionLoc = SpawnLoc + SpawnPoint->GetActorRightVector() * Side;
		if (!Companion->TeleportTo(CompanionLoc, SpawnRot))
			Companion->TeleportTo(SpawnLoc, SpawnRot, /*bIsATest*/ false, /*bNoCheck*/ true);
		++CompanionsMoved;
	}

	UE_LOG(LogLevelObjectiveFlow, Log, TEXT("%s: checkpoint resume — player + %d companion(s) teleported to step-%d spawn"),
		*GetName(), CompanionsMoved, static_cast<int32>(CurrentStep));
}

void ALevelObjectiveFlow::UpdatePrimaryObjective()
{
	UObjectiveSubsystem* Objectives = GetWorld() ? GetWorld()->GetSubsystem<UObjectiveSubsystem>() : nullptr;
	if (!Objectives) return;

	FText Label;
	AActor* Target = nullptr;
	switch (CurrentStep)
	{
	case ELevelObjectiveStep::BreachRoofDoor:
		Label = NSLOCTEXT("LevelFlow", "BreachRoof", "Breach the door - Ping with Middle Mouse"); Target = RoofDoor; break;
	case ELevelObjectiveStep::FollowCompanionDownstairs:
		Label = NSLOCTEXT("LevelFlow", "FollowDownstairs", "Follow the companion downstairs"); Target = DownstairsRoute; break;
	case ELevelObjectiveStep::BreachStairwellDoor:
		Label = NSLOCTEXT("LevelFlow", "BreachStairwell", "Breach the stairwell door - Ping with Middle Mouse"); Target = StairwellBreachDoor; break;
	case ELevelObjectiveStep::ClearRoom1:
		Label = NSLOCTEXT("LevelFlow", "ClearRoom1", "Clear Room 1"); Target = FindFirstLivingEnemy(Room1Enemies); break;
	case ELevelObjectiveStep::FindOfficeKeycard:
		Label = NSLOCTEXT("LevelFlow", "FindKeycard", "Find the office keycard"); Target = KeycardContainer; break;
	case ELevelObjectiveStep::UnlockStairwellDoor:
		Label = NSLOCTEXT("LevelFlow", "UnlockStairwell", "Use the keycard to unlock the stairwell door"); Target = Room1ExitDoor; break;
	case ELevelObjectiveStep::SwitchCompanionToStealth:
		Label = NSLOCTEXT("LevelFlow", "SwitchStealth", "Switch the companion to Stealth mode"); Target = Room2EntryDoor; break;
	case ELevelObjectiveStep::FirstDoubleTakedown:
		Label = NSLOCTEXT("LevelFlow", "FirstTakedown", "Double takedown the enemies on the left"); Target = FindFirstLivingEnemy(FirstTakedownPair); break;
	case ELevelObjectiveStep::SecondDoubleTakedown:
		Label = NSLOCTEXT("LevelFlow", "SecondTakedown", "Perform the second double takedown"); Target = FindFirstLivingEnemy(SecondTakedownPair); break;
	case ELevelObjectiveStep::ReachExtractionTarget:
		if (IsValid(Extractee))
		{
			Label = NSLOCTEXT("LevelFlow", "FreeHostage", "Free the hostage - hold F"); Target = Extractee;
		}
		else
		{
			Label = NSLOCTEXT("LevelFlow", "ReachTarget", "Reach and interact with the extraction target"); Target = ExtractionTarget;
		}
		break;
	case ELevelObjectiveStep::DefendPosition:
		Label = NSLOCTEXT("LevelFlow", "Defend", "Defend the position");
		Target = ExtractionTarget; // Null on the VIP path — the static anchor below takes over.
		break;
	case ELevelObjectiveStep::UseLift:
		// The existing lift gate owns the only marker after it unlocks.
		Objectives->RemoveObjective(PrimaryObjectiveId); return;
	case ELevelObjectiveStep::Completed:
	case ELevelObjectiveStep::Inactive:
		Objectives->RemoveObjective(PrimaryObjectiveId); return;
	}

	// Steps that pin to a fixed patch of ground rather than following an actor. Zero anchor =
	// fall back to the Target resolved in the switch above.
	const bool bStaticAnchor =
		(CurrentStep == ELevelObjectiveStep::ClearRoom1 && !Room1AreaLocation.IsZero())
		|| (CurrentStep == ELevelObjectiveStep::DefendPosition && !DefendAreaLocation.IsZero());
	if (bStaticAnchor)
	{
		const FVector AnchorLocation = (CurrentStep == ELevelObjectiveStep::ClearRoom1)
			? Room1AreaLocation : DefendAreaLocation;
		if (HasAuthority() && CurrentPrimaryTarget != nullptr)
		{
			CurrentPrimaryTarget = nullptr;
			++PresentationRevision;
			ForceNetUpdate();
		}
		// Both anchors sit at capsule centre; lift so the marker reads at the same height
		// as target-based markers (FObjectiveMarker::HeightAboveBase above the floor).
		constexpr float AreaMarkerLift = 80.f;
		Objectives->AddObjective(PrimaryObjectiveId, Label,
			AnchorLocation + FVector(0.f, 0.f, AreaMarkerLift));
		return;
	}

	if (HasAuthority())
	{
		if (CurrentPrimaryTarget != Target)
		{
			CurrentPrimaryTarget = Target;
			++PresentationRevision;
			ForceNetUpdate();
		}
	}
	else
	{
		Target = CurrentPrimaryTarget;
	}
	if (!IsValid(Target))
	{
		// Bailing here used to leave the panel showing the PREVIOUS step's objective (or nothing
		// at all), which reads as "the game forgot about me". A step with a resolvable anchor is
		// still worth a marker — only a step with nothing to point at loses one.
		const AActor* Anchor = ResolveMarkerFallbackAnchor();
		if (!IsValid(Anchor))
		{
			UE_LOG(LogLevelObjectiveFlow, Error, TEXT("%s: step %d has no valid marker target and no fallback anchor"),
				*GetName(), static_cast<uint8>(CurrentStep));
			Objectives->RemoveObjective(PrimaryObjectiveId);
			return;
		}

		UE_LOG(LogLevelObjectiveFlow, Warning, TEXT("%s: step %d marker target unresolved — anchoring on '%s'"),
			*GetName(), static_cast<uint8>(CurrentStep), *GetNameSafe(Anchor));
		Objectives->AddObjective(PrimaryObjectiveId, Label, Anchor->GetActorLocation());
		return;
	}
	Objectives->AddObjective(PrimaryObjectiveId, Label, Target->GetActorLocation(), Target);
}

const AActor* ALevelObjectiveFlow::ResolveMarkerFallbackAnchor() const
{
	// Enemy-group steps lose their target the instant the last tracked enemy dies or is destroyed
	// (UpdatePrimaryObjective runs before the step advances). Fall back to the area the fight was
	// in; every other step falls back to the extraction end of the level.
	switch (CurrentStep)
	{
	case ELevelObjectiveStep::ClearRoom1:
		return nullptr; // Already handled above by the Room1AreaLocation static marker.
	case ELevelObjectiveStep::FirstDoubleTakedown:
	case ELevelObjectiveStep::SecondDoubleTakedown:
		return Room2EntryDoor.Get();
	default:
		break;
	}

	if (IsValid(Extractee)) return Extractee;
	if (IsValid(ExtractionTarget)) return ExtractionTarget;
	return nullptr;
}

void ALevelObjectiveFlow::EvaluateCurrentEnemyStep()
{
	if (!HasAuthority()) return;
	switch (CurrentStep)
	{
	case ELevelObjectiveStep::ClearRoom1:
		if (IsEnemyGroupDead(Room1Enemies, Room1DeadIndices)) Advance(ELevelObjectiveEvent::Room1Cleared);
		break;
	case ELevelObjectiveStep::FirstDoubleTakedown:
		if (IsEnemyGroupDead(FirstTakedownPair, FirstPairDeadIndices)) Advance(ELevelObjectiveEvent::FirstPairCleared);
		break;
	case ELevelObjectiveStep::SecondDoubleTakedown:
		if (IsEnemyGroupDead(SecondTakedownPair, SecondPairDeadIndices)) Advance(ELevelObjectiveEvent::SecondPairCleared);
		break;
	default:
		break;
	}
}

bool ALevelObjectiveFlow::IsEnemyGroupDead(const TArray<TObjectPtr<AEnemyCharacter>>& Enemies,
	const TSet<int32>& DeadIndices) const
{
	if (Enemies.IsEmpty()) return false;
	for (int32 Index = 0; Index < Enemies.Num(); ++Index)
	{
		if (DeadIndices.Contains(Index)) continue;
		const AEnemyCharacter* Enemy = Enemies[Index];
		const UHealthComponent* Health = IsValid(Enemy) ? Enemy->GetHealthComponent() : nullptr;
		if (!IsValid(Health) || !Health->IsDead()) return false;
	}
	return true;
}
AActor* ALevelObjectiveFlow::FindFirstLivingEnemy(const TArray<TObjectPtr<AEnemyCharacter>>& Enemies) const
{
	for (AEnemyCharacter* Enemy : Enemies)
	{
		const UHealthComponent* Health = IsValid(Enemy) ? Enemy->GetHealthComponent() : nullptr;
		if (IsValid(Health) && !Health->IsDead()) return Enemy;
	}
	return nullptr;
}

void ALevelObjectiveFlow::ActivateOptionalSupplies()
{
	if (!HasAuthority() || bOptionalSuppliesActive) return;
	bOptionalSuppliesActive = true;
	ForceNetUpdate();
	UpdateOptionalSupplies();
}

void ALevelObjectiveFlow::UpdateOptionalSupplies()
{
	UObjectiveSubsystem* Objectives = GetWorld() ? GetWorld()->GetSubsystem<UObjectiveSubsystem>() : nullptr;
	if (!Objectives) return;
	if (!bOptionalSuppliesActive)
	{
		Objectives->RemoveObjective(OptionalSuppliesObjectiveId);
		return;
	}

	if (!HasAuthority())
	{
		if (IsValid(CurrentOptionalTarget))
		{
			// Optional objectives are text-only on the HUD objective panel — no world marker.
			Objectives->AddObjective(OptionalSuppliesObjectiveId,
				NSLOCTEXT("LevelFlow", "OptionalSupplies", "Optional: Search side rooms for supplies - Ping with MMB, press I to loot"),
				CurrentOptionalTarget->GetActorLocation(), CurrentOptionalTarget,
				FVector::ZeroVector, /*bShowWorldMarker*/ false);
		}
		else
		{
			Objectives->RemoveObjective(OptionalSuppliesObjectiveId);
		}
		return;
	}

	int32 CompletedCount = 0;
	for (ALootContainer* Crate : SupplyCrates)
	{
		if (!IsValid(Crate) || Crate->IsLooted() || CompletedSupplyCrates.Contains(TWeakObjectPtr<ALootContainer>(Crate)))
			++CompletedCount;
	}

	if (CompletedCount >= SupplyCrates.Num())
	{
		bOptionalSuppliesActive = false;
		CurrentOptionalTarget = nullptr;
		++PresentationRevision;
		ForceNetUpdate();
		Objectives->RemoveObjective(OptionalSuppliesObjectiveId);
		return;
	}

	FVector Origin = GetActorLocation();
	if (const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
		if (const APawn* Pawn = PC->GetPawn()) Origin = Pawn->GetActorLocation();

	ALootContainer* Nearest = FindNearestUnlootedSupply(Origin);
	if (CurrentOptionalTarget != Nearest)
	{
		CurrentOptionalTarget = Nearest;
		++PresentationRevision;
		ForceNetUpdate();
	}
	if (!IsValid(CurrentOptionalTarget))
	{
		Objectives->RemoveObjective(OptionalSuppliesObjectiveId);
		return;
	}

	// Optional objectives are text-only on the HUD objective panel — no world marker.
	Objectives->AddObjective(OptionalSuppliesObjectiveId,
		NSLOCTEXT("LevelFlow", "OptionalSupplies", "Optional: Search side rooms for supplies - Ping with MMB, press I to loot"),
		CurrentOptionalTarget->GetActorLocation(), CurrentOptionalTarget,
		FVector::ZeroVector, /*bShowWorldMarker*/ false);
}
ALootContainer* ALevelObjectiveFlow::FindNearestUnlootedSupply(const FVector& Origin) const
{
	ALootContainer* Nearest = nullptr;
	float NearestDistanceSquared = TNumericLimits<float>::Max();
	for (ALootContainer* Crate : SupplyCrates)
	{
		if (!IsValid(Crate) || Crate->IsLooted() || CompletedSupplyCrates.Contains(TWeakObjectPtr<ALootContainer>(Crate))) continue;
		const float DistanceSquared = FVector::DistSquared(Origin, Crate->GetActorLocation());
		if (DistanceSquared >= NearestDistanceSquared) continue;
		NearestDistanceSquared = DistanceSquared;
		Nearest = Crate;
	}
	return Nearest;
}

void ALevelObjectiveFlow::HandleDoorOpened(AActor* Door)
{
	if (!HasAuthority()) return;
	if (Door == RoofDoor) Advance(ELevelObjectiveEvent::RoofDoorOpened);
	else if (Door == StairwellBreachDoor) Advance(ELevelObjectiveEvent::StairwellDoorOpened);
	else if (Door == Room1ExitDoor) Advance(ELevelObjectiveEvent::ExitDoorOpened);
	else if (Door == Room2EntryDoor) Advance(ELevelObjectiveEvent::Room2DoorOpened);
}

void ALevelObjectiveFlow::HandleRouteCompleted(bool bAborted)
{
	if (!HasAuthority()) return;
	if (!bAborted) Advance(ELevelObjectiveEvent::RouteCompleted);
}

void ALevelObjectiveFlow::HandleTrackedEnemyDeath()
{
	if (!HasAuthority()) return;
	RefreshRecordedEnemyDeaths();
	UpdatePrimaryObjective();
	EvaluateCurrentEnemyStep();
}

void ALevelObjectiveFlow::RefreshRecordedEnemyDeaths()
{
	auto RecordGroup = [](const TArray<TObjectPtr<AEnemyCharacter>>& Enemies, TSet<int32>& DeadIndices)
	{
		for (int32 Index = 0; Index < Enemies.Num(); ++Index)
		{
			const AEnemyCharacter* Enemy = Enemies[Index];
			const UHealthComponent* Health = IsValid(Enemy) ? Enemy->GetHealthComponent() : nullptr;
			if (IsValid(Health) && Health->IsDead())
				DeadIndices.Add(Index);
		}
	};
	RecordGroup(Room1Enemies, Room1DeadIndices);
	RecordGroup(FirstTakedownPair, FirstPairDeadIndices);
	RecordGroup(SecondTakedownPair, SecondPairDeadIndices);
}
void ALevelObjectiveFlow::HandleTrackedEnemyDestroyed(AActor* DestroyedActor)
{
	if (!HasAuthority()) return;
	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(DestroyedActor);
	const UHealthComponent* Health = Enemy ? Enemy->GetHealthComponent() : nullptr;
	if (IsValid(Health) && Health->IsDead())
	{
		const int32 Room1Index = Room1Enemies.IndexOfByKey(Enemy);
		const int32 FirstIndex = FirstTakedownPair.IndexOfByKey(Enemy);
		const int32 SecondIndex = SecondTakedownPair.IndexOfByKey(Enemy);
		if (Room1Index != INDEX_NONE) Room1DeadIndices.Add(Room1Index);
		if (FirstIndex != INDEX_NONE) FirstPairDeadIndices.Add(FirstIndex);
		if (SecondIndex != INDEX_NONE) SecondPairDeadIndices.Add(SecondIndex);
	}
	EvaluateCurrentEnemyStep();
}
void ALevelObjectiveFlow::HandleLootCompleted(ALootContainer* Container, AActor* Looter)
{
	if (!HasAuthority()) return;
	if (Container == KeycardContainer)
		Advance(ELevelObjectiveEvent::KeycardLooted);

	if (SupplyCrates.Contains(Container))
	{
		CompletedSupplyCrates.Add(TWeakObjectPtr<ALootContainer>(Container));
		if (bOptionalSuppliesActive) UpdateOptionalSupplies();
	}
}

void ALevelObjectiveFlow::HandleSupplyDestroyed(AActor* DestroyedActor)
{
	if (!HasAuthority()) return;
	if (ALootContainer* Container = Cast<ALootContainer>(DestroyedActor))
		CompletedSupplyCrates.Add(TWeakObjectPtr<ALootContainer>(Container));
	if (bOptionalSuppliesActive) UpdateOptionalSupplies();
}

void ALevelObjectiveFlow::HandleExtracteeRescued()
{
	if (!HasAuthority()) return;

	// Freeze the hold-ground here, while the VIP is still standing where the player freed him.
	if (DefendAreaLocation.IsZero())
	{
		const AActor* Anchor = IsValid(Extractee) ? static_cast<const AActor*>(Extractee)
			: static_cast<const AActor*>(ExtractionTarget);
		if (IsValid(Anchor)) DefendAreaLocation = Anchor->GetActorLocation();
	}

	// The rescue IS the extraction trigger: start the wave through the target actor's guarded
	// path. Its OnExtractionTargetWaveStarted then advances the flow to DefendPosition.
	TryStartExtractionWave();
}

void ALevelObjectiveFlow::TryStartExtractionWave()
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority()) return;

	bool bStarted = false;
	if (IsValid(ExtractionTarget))
	{
		bStarted = ExtractionTarget->BeginExtractionExternal();
	}
	else
	{
		// No target actor: the flow owns the wave. Advance ourselves — the ExtractionStarted event
		// normally arrives via the target's OnExtractionTargetWaveStarted, which nobody broadcasts here.
		UEnemyDirectorSubsystem* Director = World->GetSubsystem<UEnemyDirectorSubsystem>();
		if (IsValid(Director))
		{
			// Idempotent: a retry that lands after the wave took hold must not start a second one.
			bStarted = Director->GetActiveWaveId() == ExtractionWave.WaveId
				|| CurrentStep > ELevelObjectiveStep::ReachExtractionTarget
				|| Director->StartWave(ExtractionWave);
		}
		if (bStarted) Advance(ELevelObjectiveEvent::ExtractionStarted);
	}

	if (bStarted)
	{
		World->GetTimerManager().ClearTimer(ExtractionWaveRetryHandle);
		return;
	}

	// StartWave refused (another wave still active, Director not ready). The rescue can't be
	// re-interacted, so keep knocking until the Director accepts.
	if (!World->GetTimerManager().IsTimerActive(ExtractionWaveRetryHandle))
	{
		UE_LOG(LogLevelObjectiveFlow, Warning,
			TEXT("%s: extraction wave refused to start — retrying every 2s"), *GetName());
		World->GetTimerManager().SetTimer(ExtractionWaveRetryHandle, this,
			&ALevelObjectiveFlow::TryStartExtractionWave, 2.f, /*bLoop=*/true);
	}
}

void ALevelObjectiveFlow::HandleDirectorWaveCompleted(FName WaveId)
{
	if (!HasAuthority() || !OwnsExtractionWave()) return;
	if (WaveId != ExtractionWave.WaveId) return;

	UE_LOG(LogLevelObjectiveFlow, Log, TEXT("%s: extraction wave '%s' completed"), *GetName(), *WaveId.ToString());

	PerformExtractionCompletionAction();
	Advance(ELevelObjectiveEvent::ExtractionCompleted);
}

void ALevelObjectiveFlow::HandleDirectorWaveBlocked(FName WaveId, FText Reason)
{
	if (!HasAuthority() || !OwnsExtractionWave()) return;
	if (WaveId != ExtractionWave.WaveId) return;

	UE_LOG(LogLevelObjectiveFlow, Warning, TEXT("%s: extraction wave '%s' blocked: %s"),
		*GetName(), *WaveId.ToString(), *Reason.ToString());

	// Same player-facing surface the target actor used — a wave that can't find spawn room is
	// otherwise a silent stall on a "defend the position" objective.
	if (UMissionInventorySubsystem* Inventory = GetWorld() ? GetWorld()->GetSubsystem<UMissionInventorySubsystem>() : nullptr)
		Inventory->OnLootNotify.Broadcast(Reason);
}

void ALevelObjectiveFlow::PerformExtractionCompletionAction()
{
	switch (ExtractionCompletionAction)
	{
	case EWaveCompletionAction::UnlockExit:
		if (IsValid(LiftGate))
			LiftGate->UnlockExit();
		else
			UE_LOG(LogLevelObjectiveFlow, Warning, TEXT("%s: UnlockExit with no LiftGate"), *GetName());
		break;

	case EWaveCompletionAction::CompleteLevel:
		if (AExtractionGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AExtractionGameMode>() : nullptr)
			GameMode->CompleteLevel();
		else
			UE_LOG(LogLevelObjectiveFlow, Warning, TEXT("%s: CompleteLevel with no AExtractionGameMode"), *GetName());
		break;

	case EWaveCompletionAction::BroadcastOnly:
		break;
	}
}

void ALevelObjectiveFlow::HandleExtractionStarted()
{
	if (!HasAuthority()) return;
	Advance(ELevelObjectiveEvent::ExtractionStarted);
}

void ALevelObjectiveFlow::HandleExtractionCompleted()
{
	if (!HasAuthority()) return;
	Advance(ELevelObjectiveEvent::ExtractionCompleted);
}

void ALevelObjectiveFlow::OnRep_CurrentStep()
{
	UpdatePrimaryObjective();
	if (bOptionalSuppliesActive)
		UpdateOptionalSupplies();
}

void ALevelObjectiveFlow::OnRep_OptionalSuppliesActive()
{
	if (bOptionalSuppliesActive)
		UpdateOptionalSupplies();
	else if (UObjectiveSubsystem* Objectives = GetWorld() ? GetWorld()->GetSubsystem<UObjectiveSubsystem>() : nullptr)
		Objectives->RemoveObjective(OptionalSuppliesObjectiveId);
}
void ALevelObjectiveFlow::OnRep_PresentationRevision()
{
	UpdatePrimaryObjective();
	UpdateOptionalSupplies();
}