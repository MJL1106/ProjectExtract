#include "EnemySquadSubsystem.h"
#include "EnemySquad.h"
#include "EnemyCharacter.h"
#include "EnemyDirectorSubsystem.h"

DEFINE_LOG_CATEGORY(LogEnemySquad);

void UEnemySquadSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Collection.InitializeDependency<UEnemyDirectorSubsystem>();

	UWorld* World = GetWorld();
	if (!IsValid(World)) return;

	UEnemyDirectorSubsystem* Director = World->GetSubsystem<UEnemyDirectorSubsystem>();
	if (IsValid(Director))
	{
		Director->OnEnemyDied.AddDynamic(this, &UEnemySquadSubsystem::HandleEnemyDied);
	}
}

void UEnemySquadSubsystem::Deinitialize()
{
	UWorld* World = GetWorld();
	if (IsValid(World))
	{
		UEnemyDirectorSubsystem* Director = World->GetSubsystem<UEnemyDirectorSubsystem>();
		if (IsValid(Director))
		{
			Director->OnEnemyDied.RemoveDynamic(this, &UEnemySquadSubsystem::HandleEnemyDied);
		}
	}

	Squads.Empty();
	EnemyToSquadId.Empty();

	Super::Deinitialize();
}

UEnemySquad* UEnemySquadSubsystem::GetOrCreateSquad(FName SquadId)
{
	if (SquadId == NAME_None) return nullptr;

	TObjectPtr<UEnemySquad>* Found = Squads.Find(SquadId);
	if (Found && IsValid(*Found)) return *Found;

	UEnemySquad* NewSquad = NewObject<UEnemySquad>(this);
	NewSquad->SetSquadId(SquadId);
	Squads.Add(SquadId, NewSquad);
	UE_LOG(LogEnemySquad, Log, TEXT("Created squad: %s"), *SquadId.ToString());
	return NewSquad;
}

UEnemySquad* UEnemySquadSubsystem::GetSquadFor(const AEnemyCharacter* Enemy) const
{
	if (!IsValid(Enemy)) return nullptr;

	const FName* FoundId = EnemyToSquadId.Find(const_cast<AEnemyCharacter*>(Enemy));
	if (!FoundId || *FoundId == NAME_None) return nullptr;

	const TObjectPtr<UEnemySquad>* FoundSquad = Squads.Find(*FoundId);
	if (FoundSquad && IsValid(*FoundSquad)) return *FoundSquad;

	return nullptr;
}

void UEnemySquadSubsystem::RegisterMember(AEnemyCharacter* Enemy, FName SquadId)
{
	if (!IsValid(Enemy) || SquadId == NAME_None) return;

	UEnemySquad* Squad = GetOrCreateSquad(SquadId);
	if (!IsValid(Squad)) return;

	Squad->AddMember(Enemy);
	EnemyToSquadId.Add(Enemy, SquadId);
	UE_LOG(LogEnemySquad, Verbose, TEXT("Registered %s in squad %s"), *Enemy->GetName(), *SquadId.ToString());
}

void UEnemySquadSubsystem::UnregisterMember(AEnemyCharacter* Enemy)
{
	if (!IsValid(Enemy)) return;

	FName SquadId;
	if (!EnemyToSquadId.RemoveAndCopyValue(Enemy, SquadId)) return;

	TObjectPtr<UEnemySquad>* FoundSquad = Squads.Find(SquadId);
	if (FoundSquad && IsValid(*FoundSquad))
	{
		(*FoundSquad)->RemoveMember(Enemy);
	}

	UE_LOG(LogEnemySquad, Verbose, TEXT("Unregistered %s from squad %s"), *Enemy->GetName(), *SquadId.ToString());
}

UEnemySquad* UEnemySquadSubsystem::CreateSquadForGroup(const TArray<AEnemyCharacter*>& Group)
{
	FName GeneratedId = FName(*FString::Printf(TEXT("DirSquad_%d"), GeneratedSquadCounter++));
	UEnemySquad* Squad = GetOrCreateSquad(GeneratedId);
	if (!IsValid(Squad)) return nullptr;

	for (AEnemyCharacter* Enemy : Group)
	{
		if (!IsValid(Enemy)) continue;
		Squad->AddMember(Enemy);
		EnemyToSquadId.Add(Enemy, GeneratedId);
		Enemy->SquadId = GeneratedId;
	}

	UE_LOG(LogEnemySquad, Log, TEXT("Created director squad %s with %d members"), *GeneratedId.ToString(), Group.Num());
	return Squad;
}

void UEnemySquadSubsystem::HandleEnemyDied(AEnemyCharacter* DeadEnemy, FVector Location, bool bWasOfficer)
{
	if (!IsValid(DeadEnemy)) return;

	UEnemySquad* Squad = GetSquadFor(DeadEnemy);
	if (!IsValid(Squad)) return;

	Squad->NotifyMemberDied(DeadEnemy, bWasOfficer);
	Squad->RemoveMember(DeadEnemy);
	EnemyToSquadId.Remove(DeadEnemy);
}
