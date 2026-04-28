// BT service — periodically probes for traversal opportunities ahead of the companion.

#include "BTService_TraversalProbe.h"
#include "AIController.h"
#include "CompanionCharacter.h"
#include "TraversalComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/CharacterMovementComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogTraversalProbe, Log, All);

UBTService_TraversalProbe::UBTService_TraversalProbe()
{
	NodeName = TEXT("Traversal Probe");
	Interval = 0.2f;
	RandomDeviation = 0.05f;
}

void UBTService_TraversalProbe::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	AAIController* AIC = OwnerComp.GetAIOwner();
	ACompanionCharacter* Companion = Cast<ACompanionCharacter>(AIC ? AIC->GetPawn() : nullptr);
	if (!IsValid(Companion)) return;

	UTraversalComponent* TC = Companion->GetTraversalComponent();
	if (!IsValid(TC) || TC->IsInTraversal()) return;

	UCharacterMovementComponent* MoveComp = Companion->GetCharacterMovement();
	if (!IsValid(MoveComp) || MoveComp->IsFalling()) return;

#if ENABLE_DRAW_DEBUG
	if (bDrawDebug)
	{
		const FVector Origin = Companion->GetActorLocation();
		const FVector Forward = Companion->GetActorForwardVector();
		DrawDebugSphere(Companion->GetWorld(), Origin, 40.f, 16, FColor::Magenta, false, 0.25f, 0, 2.0f);
		DrawDebugDirectionalArrow(Companion->GetWorld(), Origin, Origin + Forward * 150.f, 25.f, FColor::Magenta, false, 0.25f, 0, 2.0f);
		DrawDebugString(Companion->GetWorld(), Origin + FVector(0, 0, 110), TEXT("PROBE"), nullptr, FColor::Magenta, 0.25f, false);
	}
#endif

	const float SpeedSq2D = MoveComp->Velocity.SizeSquared2D();
	const float MinSpeedSq = MinSpeed2DToProbe * MinSpeed2DToProbe;
	if (SpeedSq2D < MinSpeedSq) return;

	if (bRequireBlocked)
	{
		const bool bWantsToMove = MoveComp->GetCurrentAcceleration().SizeSquared() > 0.f;
		const float StuckThresholdSq = StuckVelocityThreshold * StuckVelocityThreshold;
		if (!bWantsToMove || SpeedSq2D > StuckThresholdSq) return;
	}

	if (TC->TryStartTraversal(Companion->IsSprinting()))
	{
		UE_LOG(LogTraversalProbe, Verbose, TEXT("%s: traversal started (type=%d)"),
			*Companion->GetName(), static_cast<uint8>(TC->GetActiveType()));
	}
}

FString UBTService_TraversalProbe::GetStaticDescription() const
{
	return TEXT("Probes for traversal opportunities (vault/climb/mantle) while moving");
}
