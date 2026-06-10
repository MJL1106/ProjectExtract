// UEnemyDirectorSubsystem — level-wide alert ladder and corpse registry.

#include "EnemyDirectorSubsystem.h"
#include "EnemyAIController.h"
#include "EnemyCharacter.h"
#include "EnemyArchetypeData.h"

void UEnemyDirectorSubsystem::ReportEnemySearching()
{
	Escalate(EGlobalAlertLevel::Searching);
}

void UEnemyDirectorSubsystem::ReportEnemyCombat()
{
	Escalate(EGlobalAlertLevel::Loud);
}

void UEnemyDirectorSubsystem::ReportBodyDiscovered()
{
	++BodiesDiscovered;

	const bool bEscalateToLoud = BodiesDiscovered >= 2 || AlertLevel >= EGlobalAlertLevel::Searching;
	Escalate(bEscalateToLoud ? EGlobalAlertLevel::Loud : EGlobalAlertLevel::Searching);
}

void UEnemyDirectorSubsystem::TripAlarm()
{
	Escalate(EGlobalAlertLevel::Loud);
}

void UEnemyDirectorSubsystem::RegisterCorpse(AEnemyCharacter* Corpse)
{
	if (!IsValid(Corpse)) return;

	const bool bOfficer = IsValid(Corpse->GetArchetypeData()) && Corpse->GetArchetypeData()->bHasCommandAura;
	OnEnemyDied.Broadcast(Corpse, Corpse->GetActorLocation(), bOfficer);

	Corpses.RemoveAll([](const TWeakObjectPtr<AEnemyCharacter>& Entry) { return !Entry.IsValid(); });
	Corpses.Add(Corpse);

	while (Corpses.Num() > MaxCorpses)
	{
		if (AEnemyCharacter* Oldest = Corpses[0].Get())
			Oldest->Destroy();
		Corpses.RemoveAt(0);
	}
}

void UEnemyDirectorSubsystem::Escalate(EGlobalAlertLevel NewLevel)
{
	if (NewLevel <= AlertLevel) return;

	const EGlobalAlertLevel OldLevel = AlertLevel;
	AlertLevel = NewLevel;

	UE_LOG(LogEnemyAI, Log, TEXT("Global alert: %d -> %d"), static_cast<int32>(OldLevel), static_cast<int32>(NewLevel));
	OnGlobalAlertChanged.Broadcast(OldLevel, NewLevel);
}
