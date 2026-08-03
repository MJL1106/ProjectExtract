// ACompanionModeDoorGate -- locks a door until the player sets the companion to the required mode.

#include "World/CompanionModeDoorGate.h"
#include "World/DoorBase.h"
#include "Game/ObjectiveSubsystem.h"
#include "Components/CompanionCommandComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogModeDoorGate, Log, All);

ACompanionModeDoorGate::ACompanionModeDoorGate()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
	TriggerBox->SetupAttachment(Root);
	TriggerBox->SetBoxExtent(FVector(200.f, 200.f, 120.f));
	TriggerBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TriggerBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	TriggerBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	TriggerBox->SetGenerateOverlapEvents(true);
	TriggerBox->SetCanEverAffectNavigation(false);
}

void ACompanionModeDoorGate::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACompanionModeDoorGate, bUnlocked);
}

void ACompanionModeDoorGate::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		if (bUnlocked)
		{
			// Checkpoint fast-forward retired this gate before BeginPlay — do not re-lock the door.
		}
		else if (IsValid(TargetDoor))
		{
			TargetDoor->SetExternalGateLocked(true);
			UE_LOG(LogModeDoorGate, Log, TEXT("%s: locked target door %s (requires %s)"),
				*GetName(), *TargetDoor->GetName(),
				*UEnum::GetValueAsString(RequiredMode));
		}
		else
		{
			UE_LOG(LogModeDoorGate, Warning, TEXT("%s: TargetDoor is null — this gate will never lock anything. Wire it in the level."), *GetName());
		}
	}

	if (TriggerBox)
		TriggerBox->OnComponentBeginOverlap.AddDynamic(this, &ACompanionModeDoorGate::OnTriggerOverlap);
}

void ACompanionModeDoorGate::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindModeDelegate();
	RemoveObjectiveLocal();
	Super::EndPlay(EndPlayReason);
}

// --- Overlap ---

void ACompanionModeDoorGate::OnTriggerOverlap(UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	if (bUnlocked) return;

	APawn* Pawn = Cast<APawn>(OtherActor);
	if (!IsValid(Pawn) || !Pawn->IsPlayerControlled()) return;

	// Objective is local UI: only the controlling client adds it.
	// When an AObjectiveStep owns this beat, bRegisterOwnObjective is false and the gate
	// stays out of the objective subsystem. bObjectiveAdded stays false, so
	// RemoveObjectiveLocal is a no-op on EndPlay / PerformUnlock / OnRep.
	if (bRegisterOwnObjective && Pawn->IsLocallyControlled() && !bObjectiveAdded)
	{
		if (UObjectiveSubsystem* Objectives = GetWorld()->GetSubsystem<UObjectiveSubsystem>())
		{
			Objectives->AddObjective(ObjectiveId, ObjectiveLabel, GetActorLocation(), this);
			bObjectiveAdded = true;
		}
	}

	// Bind + unlock logic is authority-only (server owns the gate state).
	if (!HasAuthority()) return;

	if (!bDelegateBound)
	{
		UCompanionCommandComponent* CmdComp = Pawn->FindComponentByClass<UCompanionCommandComponent>();
		if (IsValid(CmdComp))
		{
			CmdComp->OnCompanionModeChanged.AddDynamic(this, &ACompanionModeDoorGate::OnCompanionModeChanged);
			BoundCommandComp = CmdComp;
			bDelegateBound = true;

			// If the player already has the required mode, unlock immediately.
			if (CmdComp->GetCompanionMode() == RequiredMode)
			{
				PerformUnlock();
				return;
			}
		}
	}
}

// --- Mode callback ---

void ACompanionModeDoorGate::OnCompanionModeChanged(ECompanionMode NewMode)
{
	if (bUnlocked) return;
	if (NewMode != RequiredMode) return;

	PerformUnlock();
}

// --- Unlock ---

void ACompanionModeDoorGate::ForceUnlockForCheckpoint()
{
	if (bUnlocked) return;
	PerformUnlock();
	ForceNetUpdate();
}

void ACompanionModeDoorGate::PerformUnlock()
{
	bUnlocked = true;

	if (IsValid(TargetDoor))
		TargetDoor->SetExternalGateLocked(false);

	RemoveObjectiveLocal();
	UnbindModeDelegate();

	if (TriggerBox)
		TriggerBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	UE_LOG(LogModeDoorGate, Log, TEXT("%s: unlocked (mode %s reached)"),
		*GetName(), *UEnum::GetValueAsString(RequiredMode));
}

void ACompanionModeDoorGate::OnRep_Unlocked()
{
	if (!bUnlocked) return;

	RemoveObjectiveLocal();

	if (TriggerBox)
		TriggerBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	UnbindModeDelegate();
}

// --- Helpers ---

void ACompanionModeDoorGate::RemoveObjectiveLocal()
{
	if (!bObjectiveAdded) return;

	if (UWorld* World = GetWorld())
		if (UObjectiveSubsystem* Objectives = World->GetSubsystem<UObjectiveSubsystem>())
			Objectives->RemoveObjective(ObjectiveId);

	bObjectiveAdded = false;
}

void ACompanionModeDoorGate::UnbindModeDelegate()
{
	if (!bDelegateBound) return;

	UCompanionCommandComponent* CmdComp = BoundCommandComp.Get();
	if (IsValid(CmdComp))
		CmdComp->OnCompanionModeChanged.RemoveDynamic(this, &ACompanionModeDoorGate::OnCompanionModeChanged);

	BoundCommandComp.Reset();
	bDelegateBound = false;
}
