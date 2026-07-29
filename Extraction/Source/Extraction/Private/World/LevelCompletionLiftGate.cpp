// ALevelCompletionLiftGate -- replicated interaction gate for the marketplace lift.

#include "World/LevelCompletionLiftGate.h"

#include "Components/BoxComponent.h"
#include "Net/UnrealNetwork.h"

#include "Game/ExtractionGameMode.h"
#include "Game/ObjectiveSubsystem.h"
#include "Game/MissionInventorySubsystem.h"
#include "World/InteractionEventSubsystem.h"
#include "Core/Extraction.h"

DEFINE_LOG_CATEGORY(LogLiftGate);

// ------------------------------------------------------------------
// Construction
// ------------------------------------------------------------------

ALevelCompletionLiftGate::ALevelCompletionLiftGate()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	// Tight button-sized box so it catches the player's ECC_Visibility interact trace
	// without broadly blocking bullets or AI sight lines. Keep it small and against the
	// lift wall/button surface.
	InteractionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionBox"));
	InteractionBox->SetupAttachment(SceneRoot);
	InteractionBox->SetBoxExtent(FVector(20.f, 30.f, 30.f));
	InteractionBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	InteractionBox->SetCanEverAffectNavigation(false);
}

// ------------------------------------------------------------------
// Lifecycle
// ------------------------------------------------------------------

void ALevelCompletionLiftGate::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority() && !IsValid(LiftActor))
		UE_LOG(LogLiftGate, Warning, TEXT("%s: LiftActor is null -- objective marker will use own location"), *GetName());

	// Late joiners: if bUnlocked was already replicated before BeginPlay, register the objective.
	if (bUnlocked)
		RegisterUseLiftObjective();
}

void ALevelCompletionLiftGate::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RemoveUseLiftObjective();
	Super::EndPlay(EndPlayReason);
}

// ------------------------------------------------------------------
// Replication
// ------------------------------------------------------------------

void ALevelCompletionLiftGate::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ALevelCompletionLiftGate, bUnlocked);
}

void ALevelCompletionLiftGate::OnRep_bUnlocked()
{
	if (bUnlocked)
		RegisterUseLiftObjective();
}

// ------------------------------------------------------------------
// IWorldInteractable
// ------------------------------------------------------------------

bool ALevelCompletionLiftGate::CanWorldInteract_Implementation(AActor* /*Interactor*/) const
{
	// Always interactable: locked state gives feedback, unlocked state completes.
	return true;
}

FText ALevelCompletionLiftGate::GetWorldInteractionPrompt_Implementation(AActor* /*Interactor*/) const
{
	return bUnlocked ? UnlockedPrompt : LockedPrompt;
}

void ALevelCompletionLiftGate::WorldInteract_Implementation(AActor* Interactor)
{
	if (!HasAuthority()) return;

	if (!bUnlocked)
	{
		// Locked: toast the "eliminate enemies" message.
		UMissionInventorySubsystem* Inventory = GetWorld() ? GetWorld()->GetSubsystem<UMissionInventorySubsystem>() : nullptr;
		if (IsValid(Inventory))
			Inventory->OnLootNotify.Broadcast(LockedInteractionMessage);

		UE_LOG(LogLiftGate, Verbose, TEXT("%s: locked interaction by %s"), *GetName(), *GetNameSafe(Interactor));
		return;
	}

	if (bCompletesLevel)
	{
		AExtractionGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AExtractionGameMode>() : nullptr;
		if (!IsValid(GM))
		{
			UE_LOG(LogLiftGate, Warning, TEXT("%s: could not resolve AExtractionGameMode for CompleteLevel"), *GetName());
			return;
		}

		// Raised AFTER the game mode resolves: it signals "the lift was used", so a press that could
		// not complete the level must not raise it.
		RaiseUsedNotify(Interactor);

		UE_LOG(LogLiftGate, Log, TEXT("%s: completing level via %s"), *GetName(), *GetNameSafe(Interactor));
		GM->CompleteLevel();
		return;
	}

	// bCompletesLevel off: the ride is a beat, not the end of the mission. No game mode is resolved —
	// there is nothing here that needs one — and the raise is the entire payload, because the
	// objective step watching this gate is what decides where the squad ends up.
	RaiseUsedNotify(Interactor);
	UE_LOG(LogLiftGate, Log, TEXT("%s: used by %s — level completion is owned by the objective chain"),
		*GetName(), *GetNameSafe(Interactor));
}

void ALevelCompletionLiftGate::RaiseUsedNotify(AActor* Interactor)
{
	if (bCompletionRaised) return;
	bCompletionRaised = true;

	if (UInteractionEventSubsystem* Events = GetWorld() ? GetWorld()->GetSubsystem<UInteractionEventSubsystem>() : nullptr)
		Events->NotifyWorldInteract(this, Interactor);
}

// ------------------------------------------------------------------
// Unlock
// ------------------------------------------------------------------

void ALevelCompletionLiftGate::UnlockExit()
{
	if (!HasAuthority()) return;
	if (bUnlocked) return;

	bUnlocked = true;
	UE_LOG(LogLiftGate, Log, TEXT("%s: exit unlocked"), *GetName());

	// Server-side objective registration (OnRep handles clients).
	RegisterUseLiftObjective();
}

// ------------------------------------------------------------------
// Objective helpers
// ------------------------------------------------------------------

void ALevelCompletionLiftGate::RegisterUseLiftObjective()
{
	// When an AObjectiveStep owns this beat, bRegisterOwnObjective is false and the gate
	// stays out of the objective subsystem entirely. bObjectiveRegistered stays false,
	// so RemoveUseLiftObjective is a no-op on EndPlay.
	if (!bRegisterOwnObjective) return;

	if (UObjectiveSubsystem* Objectives = GetObjectiveSubsystem())
	{
		Objectives->AddObjective(UseLiftObjectiveId, UseLiftObjectiveLabel, GetObjectiveLocation());
		bObjectiveRegistered = true;
	}
}

void ALevelCompletionLiftGate::RemoveUseLiftObjective()
{
	if (!bObjectiveRegistered) return;

	if (UObjectiveSubsystem* Objectives = GetObjectiveSubsystem())
		Objectives->RemoveObjective(UseLiftObjectiveId);

	bObjectiveRegistered = false;
}

FVector ALevelCompletionLiftGate::GetObjectiveLocation() const
{
	if (IsValid(LiftActor))
		return LiftActor->GetActorLocation();

	return GetActorLocation();
}

UObjectiveSubsystem* ALevelCompletionLiftGate::GetObjectiveSubsystem() const
{
	UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UObjectiveSubsystem>() : nullptr;
}
