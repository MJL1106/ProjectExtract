// Designer-placed respawn / teleport volume implementation.

#include "World/TeleportVolume.h"

#include "Components/BoxComponent.h"
#include "Components/ArrowComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

#include "Character/ExtractionPlayerInterface.h"
#include "Components/HealthComponent.h"
#include "Companion/CompanionCharacter.h"
#include "AI/CompanionAIController.h"

ATeleportVolume::ATeleportVolume()
{
	PrimaryActorTick.bCanEverTick = false;

	TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
	TriggerBox->SetCollisionProfileName(TEXT("Trigger"));
	TriggerBox->SetGenerateOverlapEvents(true);
	SetRootComponent(TriggerBox);

	PlayerSpawnPoint = CreateDefaultSubobject<UArrowComponent>(TEXT("PlayerSpawnPoint"));
	PlayerSpawnPoint->SetupAttachment(TriggerBox);

	CompanionSpawnPoint = CreateDefaultSubobject<UArrowComponent>(TEXT("CompanionSpawnPoint"));
	CompanionSpawnPoint->SetupAttachment(TriggerBox);
}

void ATeleportVolume::BeginPlay()
{
	Super::BeginPlay();
	TriggerBox->OnComponentBeginOverlap.AddDynamic(this, &ATeleportVolume::OnTriggerBeginOverlap);
}

void ATeleportVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
	GetWorldTimerManager().ClearTimer(CooldownTimer);
}

void ATeleportVolume::OnTriggerBeginOverlap(
	UPrimitiveComponent* /*OverlappedComp*/,
	AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/,
	int32 /*OtherBodyIndex*/,
	bool /*bFromSweep*/,
	const FHitResult& /*SweepResult*/)
{
	if (!HasAuthority()) return;
	if (bCooldownActive) return;

	APawn* OverlappingPawn = Cast<APawn>(OtherActor);
	if (!IsValid(OverlappingPawn)) return;
	if (!Cast<APlayerController>(OverlappingPawn->GetController())) return;
	if (!OverlappingPawn->Implements<UExtractionPlayerInterface>()) return;

	const FTransform PlayerSpawn = GetPlayerSpawnTransform();
	const FTransform CompanionSpawn = GetCompanionSpawnTransform();

	TeleportAndHealPlayer(OverlappingPawn, PlayerSpawn);
	TeleportAndHealCompanion(CompanionSpawn);

	bCooldownActive = true;
	GetWorldTimerManager().SetTimer(
		CooldownTimer,
		[this]() { bCooldownActive = false; },
		RetriggerCooldown,
		/*bLoop*/ false);
}

void ATeleportVolume::TeleportAndHealPlayer(APawn* PlayerPawn, const FTransform& SpawnTransform)
{
	const FVector Loc = SpawnTransform.GetLocation();
	const FRotator Rot = SpawnTransform.GetRotation().Rotator();

	const bool bTeleported = PlayerPawn->TeleportTo(Loc, Rot, /*bIsATest*/ false, /*bNoCheck*/ true);
	if (!bTeleported)
		UE_LOG(LogTemp, Warning, TEXT("ATeleportVolume [%s]: player TeleportTo failed at %s — check spawn marker placement"), *GetName(), *Loc.ToString());

	if (bFacePlayerToMarker)
	{
		AController* Controller = PlayerPawn->GetController();
		if (IsValid(Controller))
			Controller->SetControlRotation(FRotator(0.f, Rot.Yaw, 0.f));
	}

	IExtractionPlayerInterface* PlayerIface = Cast<IExtractionPlayerInterface>(PlayerPawn);
	if (!PlayerIface) return;

	if (PlayerIface->GetIsDBNO())
		PlayerIface->ExitDBNO();

	UHealthComponent* Health = PlayerIface->GetHealthComponent();
	if (!IsValid(Health)) return;

	if (Health->IsDead())
		Health->Revive(1.f);
	else
		Health->Heal(Health->GetMaxHealth());
}

void ATeleportVolume::TeleportAndHealCompanion(const FTransform& SpawnTransform)
{
	ACompanionCharacter* Companion = Cast<ACompanionCharacter>(
		UGameplayStatics::GetActorOfClass(GetWorld(), ACompanionCharacter::StaticClass()));

	if (!IsValid(Companion))
	{
		UE_LOG(LogTemp, Warning, TEXT("ATeleportVolume: no companion found — skipping companion teleport"));
		return;
	}

	ACompanionAIController* CompanionController = Cast<ACompanionAIController>(Companion->GetController());
	if (IsValid(CompanionController))
		CompanionController->TeleportToLocation(SpawnTransform.GetLocation(), SpawnTransform.GetRotation().Rotator());

	UHealthComponent* Health = Companion->FindComponentByClass<UHealthComponent>();
	if (!IsValid(Health)) return;

	if (Health->IsDead())
		Health->Revive(1.f);
	else
		Health->Heal(Health->GetMaxHealth());
}

FTransform ATeleportVolume::GetPlayerSpawnTransform() const
{
	if (IsValid(PlayerSpawnOverride))
		return PlayerSpawnOverride->GetActorTransform();
	if (IsValid(PlayerSpawnPoint))
		return PlayerSpawnPoint->GetComponentTransform();
	return GetActorTransform();
}

FTransform ATeleportVolume::GetCompanionSpawnTransform() const
{
	if (IsValid(CompanionSpawnOverride))
		return CompanionSpawnOverride->GetActorTransform();
	if (IsValid(CompanionSpawnPoint))
		return CompanionSpawnPoint->GetComponentTransform();
	return GetActorTransform();
}
