// Gameplay Debugger category for enemy AI — collects and draws awareness, suppression, morale, squad, and archetype state.

#if WITH_GAMEPLAY_DEBUGGER

#include "GameplayDebuggerCategory_Enemy.h"
#include "EnemyCharacter.h"
#include "EnemyAIController.h"
#include "EnemyAwarenessComponent.h"
#include "EnemyArchetypeData.h"
#include "EnemyMoraleComponent.h"
#include "SuppressionComponent.h"
#include "EnemyGrenadierComponent.h"
#include "EnemySniperTelegraphComponent.h"
#include "SquadAuraComponent.h"
#include "EnemySquad.h"
#include "EnemyTypes.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/PlayerController.h"

FGameplayDebuggerCategory_Enemy::FGameplayDebuggerCategory_Enemy()
{
	bShowOnlyWithDebugActor = true;
	SetDataPackReplication<FRepData>(&DataPack);
}

TSharedRef<FGameplayDebuggerCategory> FGameplayDebuggerCategory_Enemy::MakeInstance()
{
	return MakeShareable(new FGameplayDebuggerCategory_Enemy());
}

void FGameplayDebuggerCategory_Enemy::FRepData::Serialize(FArchive& Ar)
{
	Ar << PawnName;
	Ar << ArchetypeName;
	Ar << AwarenessState;
	Ar << Suspicion;
	Ar << bInCombat;
	Ar << bAnyHostileSighted;
	Ar << CombatTargetName;
	Ar << LastKnownLocation;
	Ar << bHasLineOfSight;
	Ar << bTargetInRange;
	Ar << bHasCover;
	Ar << CoverSlotName;
	Ar << MoraleStateBB;
	Ar << ManeuverRole;
	Ar << ActiveTaskName;
	Ar << SuppressionValue;
	Ar << bIsSuppressed;
	Ar << MoraleState;
	Ar << Morale01;
	Ar << SquadId;
	Ar << SquadAlive;
	Ar << ArchetypeExtra;
	Ar << bHasValidEnemy;
}

void FGameplayDebuggerCategory_Enemy::CollectData(APlayerController* OwnerPC, AActor* DebugActor)
{
	DataPack.bHasValidEnemy = false;

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(DebugActor);
	if (!IsValid(Enemy)) return;

	DataPack.bHasValidEnemy = true;
	DataPack.PawnName = Enemy->GetName();

	// Archetype
	const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
	DataPack.ArchetypeName = IsValid(DA) ? UEnum::GetValueAsString(DA->Archetype) : TEXT("No DA");

	// Controller + BB
	AEnemyAIController* AIC = Cast<AEnemyAIController>(Enemy->GetController());
	UBlackboardComponent* BB = IsValid(AIC) ? AIC->GetBlackboardComponent() : nullptr;

	// Awareness
	UEnemyAwarenessComponent* Awareness = IsValid(AIC) ? AIC->GetAwarenessComponent() : nullptr;
	if (IsValid(Awareness))
	{
		DataPack.AwarenessState = UEnum::GetValueAsString(Awareness->GetAwarenessState());
		DataPack.Suspicion = Awareness->GetHighestSuspicion();
		DataPack.bInCombat = (Awareness->GetAwarenessState() == EEnemyAwarenessState::Combat);
		DataPack.bAnyHostileSighted = Awareness->IsAnyHostileSighted();
	}
	else
	{
		DataPack.AwarenessState = TEXT("N/A");
		DataPack.Suspicion = 0.f;
		DataPack.bInCombat = false;
		DataPack.bAnyHostileSighted = false;
	}

	// BB keys
	if (IsValid(BB))
	{
		AActor* CombatTarget = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));
		DataPack.CombatTargetName = IsValid(CombatTarget) ? CombatTarget->GetName() : TEXT("None");
		DataPack.LastKnownLocation = BB->GetValueAsVector(AEnemyAIController::BB_LastKnownLocation);
		DataPack.bHasLineOfSight = BB->GetValueAsBool(AEnemyAIController::BB_HasLineOfSight);
		DataPack.bTargetInRange = BB->GetValueAsBool(AEnemyAIController::BB_TargetInRange);
		DataPack.bHasCover = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);

		AActor* CoverSlot = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CoverSlot));
		DataPack.CoverSlotName = IsValid(CoverSlot) ? CoverSlot->GetName() : TEXT("None");

		DataPack.MoraleStateBB = UEnum::GetValueAsString(static_cast<EMoraleState>(BB->GetValueAsEnum(AEnemyAIController::BB_MoraleState)));
		DataPack.ManeuverRole = UEnum::GetValueAsString(static_cast<EEnemyManeuverRole>(BB->GetValueAsEnum(AEnemyAIController::BB_ManeuverRole)));
	}
	else
	{
		DataPack.CombatTargetName = TEXT("No BB");
		DataPack.LastKnownLocation = FVector::ZeroVector;
		DataPack.bHasLineOfSight = false;
		DataPack.bTargetInRange = false;
		DataPack.bHasCover = false;
		DataPack.CoverSlotName = TEXT("No BB");
		DataPack.MoraleStateBB = TEXT("No BB");
		DataPack.ManeuverRole = TEXT("No BB");
	}

	// Active BT task
	if (IsValid(AIC))
	{
		UBehaviorTreeComponent* BTComp = Cast<UBehaviorTreeComponent>(AIC->GetBrainComponent());
		DataPack.ActiveTaskName = IsValid(BTComp) ? BTComp->DescribeActiveTasks() : TEXT("No BT");
	}
	else
	{
		DataPack.ActiveTaskName = TEXT("No Controller");
	}

	// Suppression
	USuppressionComponent* Suppression = Enemy->GetSuppressionComponent();
	if (IsValid(Suppression))
	{
		DataPack.SuppressionValue = Suppression->GetSuppression01();
		DataPack.bIsSuppressed = Suppression->IsSuppressed();
	}
	else
	{
		DataPack.SuppressionValue = 0.f;
		DataPack.bIsSuppressed = false;
	}

	// Morale
	UEnemyMoraleComponent* Morale = Enemy->GetMoraleComponent();
	if (IsValid(Morale))
	{
		DataPack.MoraleState = UEnum::GetValueAsString(Morale->GetMoraleState());
		DataPack.Morale01 = Morale->GetMorale01();
	}
	else
	{
		DataPack.MoraleState = TEXT("N/A");
		DataPack.Morale01 = 1.f;
	}

	// Squad
	UEnemySquad* Squad = Enemy->GetSquad();
	if (IsValid(Squad))
	{
		DataPack.SquadId = Squad->GetSquadId().ToString();
		DataPack.SquadAlive = Squad->NumAlive();
	}
	else
	{
		DataPack.SquadId = Enemy->SquadId.IsNone() ? TEXT("None") : Enemy->SquadId.ToString();
		DataPack.SquadAlive = 0;
	}

	// Archetype-specific extras
	DataPack.ArchetypeExtra.Reset();
	if (IsValid(DA))
	{
		switch (DA->Archetype)
		{
		case EEnemyArchetype::Grenadier:
		{
			UEnemyGrenadierComponent* Gren = Enemy->GetGrenadierComponent();
			if (IsValid(Gren))
			{
				DataPack.ArchetypeExtra = FString::Printf(
					TEXT("Grenade: CanThrow=%s | Telegraphing=%s"),
					Gren->CanThrow() ? TEXT("Y") : TEXT("N"),
					Gren->IsTelegraphing() ? TEXT("Y") : TEXT("N"));
			}
			break;
		}
		case EEnemyArchetype::Sniper:
		{
			UEnemySniperTelegraphComponent* Sniper = Enemy->GetSniperTelegraphComponent();
			if (IsValid(Sniper))
			{
				DataPack.ArchetypeExtra = FString::Printf(
					TEXT("Sniper: Telegraph=%s | ReadyToFire=%s | Shots=%d"),
					Sniper->IsTelegraphing() ? TEXT("Y") : TEXT("N"),
					Sniper->IsReadyToFire() ? TEXT("Y") : TEXT("N"),
					Sniper->GetShotsSinceRelocate());
			}
			break;
		}
		case EEnemyArchetype::Officer:
		{
			USquadAuraComponent* Aura = Enemy->GetSquadAuraComponent();
			const bool bAuraPresent = IsValid(Aura);
			const int32 SquadSize = IsValid(Squad) ? Squad->NumAlive() : 0;
			DataPack.ArchetypeExtra = FString::Printf(
				TEXT("Officer: Aura=%s | SquadAlive=%d"),
				bAuraPresent ? TEXT("Active") : TEXT("None"),
				SquadSize);
			break;
		}
		case EEnemyArchetype::Pistol:
			DataPack.ArchetypeExtra = TEXT("Pistol (grunt behaviour)");
			break;
		case EEnemyArchetype::Shotgun:
		{
			const UEnemyArchetypeData* ShotgunDA = Enemy->GetArchetypeData();
			DataPack.ArchetypeExtra = FString::Printf(
				TEXT("Shotgun: CommitRange=%.0f | HysteresisRange=%.0f | AbortHP=%.0f%%"),
				ShotgunDA ? ShotgunDA->ShotgunCommitRange : 0.f,
				ShotgunDA ? (ShotgunDA->ShotgunCommitRange + ShotgunDA->ShotgunCommitHysteresis) : 0.f,
				ShotgunDA ? ShotgunDA->ShotgunRushAbortHealthFraction * 100.f : 0.f);
			break;
		}
		default:
			break;
		}
	}
}

void FGameplayDebuggerCategory_Enemy::DrawData(APlayerController* OwnerPC, FGameplayDebuggerCanvasContext& CanvasContext)
{
	if (!DataPack.bHasValidEnemy)
	{
		CanvasContext.Printf(TEXT("{red}Not an enemy character"));
		return;
	}

	// Header
	CanvasContext.Printf(TEXT("{green}--- ENEMY: {yellow}%s {white}[%s] ---"), *DataPack.PawnName, *DataPack.ArchetypeName);

	// Awareness — color by state
	FString AwarenessColor = TEXT("{white}");
	if (DataPack.AwarenessState.Contains(TEXT("Suspicious"))) AwarenessColor = TEXT("{yellow}");
	else if (DataPack.AwarenessState.Contains(TEXT("Searching"))) AwarenessColor = TEXT("{orange}");
	else if (DataPack.AwarenessState.Contains(TEXT("Combat"))) AwarenessColor = TEXT("{red}");

	CanvasContext.Printf(TEXT("Awareness: %s%s {white}| Sighted: %s{white} | Suspicion: {yellow}%.0f"),
		*AwarenessColor, *DataPack.AwarenessState,
		DataPack.bAnyHostileSighted ? TEXT("{green}Y") : TEXT("{red}N"),
		DataPack.Suspicion);
	CanvasContext.Printf(TEXT("Target: {yellow}%s {white}| LastKnown: (%.0f, %.0f, %.0f)"),
		*DataPack.CombatTargetName, DataPack.LastKnownLocation.X, DataPack.LastKnownLocation.Y, DataPack.LastKnownLocation.Z);

	// BB state — LOS/InRange are only written by BTService_EnemyCombat (combat-only); show grey n/a outside combat
	if (DataPack.bInCombat)
	{
		CanvasContext.Printf(TEXT("LOS: %s{white} | InRange: %s{white} | Cover: %s{white} [%s]"),
			DataPack.bHasLineOfSight ? TEXT("{green}Y") : TEXT("{red}N"),
			DataPack.bTargetInRange ? TEXT("{green}Y") : TEXT("{red}N"),
			DataPack.bHasCover ? TEXT("{green}Y") : TEXT("{red}N"),
			*DataPack.CoverSlotName);
	}
	else
	{
		CanvasContext.Printf(TEXT("LOS: {grey}(n/a){white} | InRange: {grey}(n/a){white} | Cover: %s{white} [%s]"),
			DataPack.bHasCover ? TEXT("{green}Y") : TEXT("{red}N"),
			*DataPack.CoverSlotName);
	}

	// Active BT task
	CanvasContext.Printf(TEXT("BT Task: {yellow}%s"), *DataPack.ActiveTaskName);

	// Suppression
	const FString SuppColor = DataPack.bIsSuppressed ? TEXT("{red}") : TEXT("{green}");
	CanvasContext.Printf(TEXT("Suppression: %s%.0f%% %s{white} | Morale: {yellow}%s {white}(%.0f%%)"),
		*SuppColor, DataPack.SuppressionValue * 100.f,
		DataPack.bIsSuppressed ? TEXT("SUPPRESSED") : TEXT(""),
		*DataPack.MoraleState, DataPack.Morale01 * 100.f);

	// Squad + maneuver
	CanvasContext.Printf(TEXT("Squad: {yellow}%s {white}(%d alive) | Maneuver: {yellow}%s"),
		*DataPack.SquadId, DataPack.SquadAlive, *DataPack.ManeuverRole);

	// Archetype-specific
	if (!DataPack.ArchetypeExtra.IsEmpty())
	{
		CanvasContext.Printf(TEXT("{cyan}%s"), *DataPack.ArchetypeExtra);
	}
}

#endif // WITH_GAMEPLAY_DEBUGGER
