// Player-owned component that handles camera-trace pinging and command confirmation.

#include "CompanionCommandComponent.h"
#include "World/Breachable.h"
#include "Enemy/EnemyCharacter.h"
#include "Companion/CompanionCharacter.h"
#include "AI/CompanionAIController.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Engine/EngineTypes.h"

DEFINE_LOG_CATEGORY(LogCompanionCommand);

UCompanionCommandComponent::UCompanionCommandComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UCompanionCommandComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UCompanionCommandComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CachedCompanion.Reset();
	CachedCamera.Reset();
	PendingTarget.Reset();
	Super::EndPlay(EndPlayReason);
}

// ---- Companion lookup ----

ACompanionCharacter* UCompanionCommandComponent::ResolveCompanion()
{
	if (CachedCompanion.IsValid()) return CachedCompanion.Get();

	AActor* Found = UGameplayStatics::GetActorOfClass(GetWorld(), ACompanionCharacter::StaticClass());
	ACompanionCharacter* Companion = Cast<ACompanionCharacter>(Found);
	if (IsValid(Companion)) CachedCompanion = Companion;
	return Companion;
}

ACompanionAIController* UCompanionCommandComponent::GetCompanionController()
{
	ACompanionCharacter* Companion = ResolveCompanion();
	if (!IsValid(Companion)) return nullptr;
	return Cast<ACompanionAIController>(Companion->GetController());
}

// ---- Ping ----

void UCompanionCommandComponent::IssuePing()
{
	UWorld* World = GetWorld();
	if (!World) return;

	ACharacter* Owner = Cast<ACharacter>(GetOwner());
	if (!IsValid(Owner)) return;

	UE_LOG(LogCompanionCommand, Warning, TEXT("[Ping] IssuePing: owner=%s range=%.0f"), *GetNameSafe(Owner), PingTraceRange);

	// Lazily cache the camera component — avoids FindComponentByClass every ping press.
	if (!CachedCamera.IsValid())
		CachedCamera = Owner->FindComponentByClass<UCameraComponent>();
	UCameraComponent* Cam = CachedCamera.Get();

	FVector EyesLoc; FRotator EyesRot;
	Owner->GetActorEyesViewPoint(EyesLoc, EyesRot);
	const FVector TraceStart = Cam ? Cam->GetComponentLocation() : EyesLoc;
	const FVector TraceDir   = Cam ? Cam->GetForwardVector() : EyesRot.Vector();
	const FVector TraceEnd   = TraceStart + TraceDir * PingTraceRange;

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CompanionPing), false, Owner);
	Params.AddIgnoredActor(Owner);
	const bool bHit = World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, Params);
	UE_LOG(LogCompanionCommand, Warning, TEXT("[Ping] trace hit=%d actor=%s class=%s"), bHit,
		*GetNameSafe(Hit.GetActor()), Hit.GetActor() ? *Hit.GetActor()->GetClass()->GetName() : TEXT("None"));

	if (!bHit || !IsValid(Hit.GetActor()))
	{
		UE_LOG(LogCompanionCommand, Warning, TEXT("[Ping] no hit -> clear"));
		ClearPending();
		return;
	}

	AActor* HitActor = Hit.GetActor();

	// Priority 1: Breachable door
	if (HitActor->Implements<UBreachable>() && IBreachable::Execute_CanBreach(HitActor))
	{
		PendingCommand = ECompanionCommand::Breach;
		PendingTarget  = HitActor;
		OnPingChanged.Broadcast(PendingCommand, HitActor);
		UE_LOG(LogCompanionCommand, Warning, TEXT("[Ping] -> BREACH %s (broadcast)"), *GetNameSafe(HitActor));
		return;
	}

	// Priority 2: Takedown-eligible enemy
	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(HitActor);
	if (IsValid(Enemy))
	{
		const bool bElig = Enemy->IsTakedownEligible();
		UE_LOG(LogCompanionCommand, Warning, TEXT("[Ping] hit enemy %s eligible=%d"), *GetNameSafe(Enemy), bElig);
		if (bElig)
		{
			PendingCommand = ECompanionCommand::Takedown;
			PendingTarget  = Enemy;
			OnPingChanged.Broadcast(PendingCommand, Enemy);
			UE_LOG(LogCompanionCommand, Warning, TEXT("[Ping] -> TAKEDOWN %s (broadcast)"), *GetNameSafe(Enemy));
			return;
		}
	}
	else
	{
		UE_LOG(LogCompanionCommand, Warning, TEXT("[Ping] hit %s is neither breachable door nor enemy -> clear"), *GetNameSafe(HitActor));
	}

	ClearPending();
}

// ---- Confirm helpers ----

void UCompanionCommandComponent::ClearPending()
{
	PendingCommand = ECompanionCommand::None;
	PendingTarget.Reset();
	OnPingChanged.Broadcast(ECompanionCommand::None, nullptr);
}

void UCompanionCommandComponent::ConfirmTakedown(ETakedownMethod Method)
{
	UE_LOG(LogCompanionCommand, Warning, TEXT("[Confirm] ConfirmTakedown method=%d pending=%d target=%s"),
		(int32)Method, (int32)PendingCommand, *GetNameSafe(PendingTarget.Get()));
	if (PendingCommand != ECompanionCommand::Takedown) { UE_LOG(LogCompanionCommand, Warning, TEXT("[Confirm] no takedown pending -> ignore")); return; }
	AActor* Target = PendingTarget.Get();
	if (!IsValid(Target)) { UE_LOG(LogCompanionCommand, Warning, TEXT("[Confirm] target invalid -> clear")); ClearPending(); return; }

	ACompanionAIController* Controller = GetCompanionController();
	if (!IsValid(Controller))
	{
		UE_LOG(LogCompanionCommand, Warning, TEXT("[Confirm] companion controller NOT FOUND (no companion in level?)"));
		return;
	}

	UE_LOG(LogCompanionCommand, Warning, TEXT("[Confirm] -> IssueCommand Takedown on %s via %s"), *GetNameSafe(Target), *GetNameSafe(Controller));
	Controller->IssueCommand(ECompanionCommand::Takedown, Method, Target, Target->GetActorLocation());
	ClearPending();
}

void UCompanionCommandComponent::ConfirmTakedownKnife()
{
	ConfirmTakedown(ETakedownMethod::Knife);
}

void UCompanionCommandComponent::ConfirmTakedownShoot()
{
	ConfirmTakedown(ETakedownMethod::Shoot);
}

void UCompanionCommandComponent::ConfirmBreach()
{
	if (PendingCommand != ECompanionCommand::Breach) return;
	AActor* Target = PendingTarget.Get();
	if (!IsValid(Target)) { ClearPending(); return; }

	ACompanionAIController* Controller = GetCompanionController();
	if (!IsValid(Controller))
	{
		UE_LOG(LogCompanionCommand, Warning, TEXT("ConfirmBreach: companion controller not found"));
		return;
	}

	Controller->IssueCommand(ECompanionCommand::Breach, ETakedownMethod::Knife, Target, Target->GetActorLocation());
	ClearPending();
}
