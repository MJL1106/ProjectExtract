// USquadAuraComponent

#include "SquadAuraComponent.h"
#include "EnemyArchetypeData.h"
#include "EnemyCharacter.h"
#include "HealthComponent.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"

static constexpr float AuraScanInterval = 1.f;
static constexpr float AuraScanRandomOffset = 0.2f;

USquadAuraComponent::USquadAuraComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USquadAuraComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RemoveBuffFromAll();

	if (UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(ScanTimerHandle);

	Super::EndPlay(EndPlayReason);
}

void USquadAuraComponent::InitFromArchetype(const UEnemyArchetypeData* Data)
{
	if (!IsValid(Data)) return;

	AuraRadius = Data->AuraRadius;
	AuraSpreadMultiplier = Data->AuraSpreadMultiplier;

	UWorld* World = GetWorld();
	if (!IsValid(World)) return;

	const float InitialDelay = AuraScanInterval + FMath::RandRange(0.f, AuraScanRandomOffset);
	World->GetTimerManager().SetTimer(
		ScanTimerHandle,
		this,
		&USquadAuraComponent::Scan,
		AuraScanInterval,
		true,
		InitialDelay);
}

void USquadAuraComponent::Scan()
{
	// Guard: officer is dead — deactivate so the corpse doesn't keep buffing the squad.
	if (const AEnemyCharacter* OwnerEnemy = Cast<AEnemyCharacter>(GetOwner()))
	{
		const UHealthComponent* HC = OwnerEnemy->GetHealthComponent();
		if (IsValid(HC) && HC->IsDead())
		{
			DeactivateAura();
			return;
		}
	}

	UWorld* World = GetWorld();
	if (!IsValid(World)) return;

	AActor* OwnerActor = GetOwner();
	if (!IsValid(OwnerActor)) return;

	const FVector OwnerLoc = OwnerActor->GetActorLocation();
	const float RadiusSq = FMath::Square(AuraRadius);

	TSet<TWeakObjectPtr<AEnemyCharacter>> NewBuffed;

	for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
	{
		AEnemyCharacter* Member = *It;
		if (!IsValid(Member)) continue;
		if (Member == OwnerActor) continue;

		// Skip dead members
		UHealthComponent* HC = Member->GetHealthComponent();
		if (IsValid(HC) && HC->IsDead()) continue;

		if (FVector::DistSquared(OwnerLoc, Member->GetActorLocation()) > RadiusSq) continue;

		NewBuffed.Add(Member);

		// Re-apply every scan unconditionally — handles two-officer overlap correctly
		// (last scan before leaving radius always writes the current value).
		Member->SetCommandSpreadMultiplier(AuraSpreadMultiplier);
	}

	// Restore members who left the radius
	for (const TWeakObjectPtr<AEnemyCharacter>& Weak : BuffedMembers)
	{
		if (!Weak.IsValid()) continue;
		if (!NewBuffed.Contains(Weak))
			Weak->SetCommandSpreadMultiplier(1.f);
	}

	BuffedMembers = MoveTemp(NewBuffed);
}

void USquadAuraComponent::DeactivateAura()
{
	RemoveBuffFromAll();

	if (UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(ScanTimerHandle);
}

void USquadAuraComponent::RemoveBuffFromAll()
{
	for (const TWeakObjectPtr<AEnemyCharacter>& Weak : BuffedMembers)
	{
		if (Weak.IsValid())
			Weak->SetCommandSpreadMultiplier(1.f);
	}
	BuffedMembers.Empty();
}
