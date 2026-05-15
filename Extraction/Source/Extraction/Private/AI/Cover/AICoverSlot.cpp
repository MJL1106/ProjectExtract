#include "AICoverSlot.h"
#include "CoverRegistrySubsystem.h"
#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"

DEFINE_LOG_CATEGORY(LogCoverSlot);

AAICoverSlot::AAICoverSlot()
{
	PrimaryActorTick.bCanEverTick = false;

	ForwardArrow = CreateDefaultSubobject<UArrowComponent>(TEXT("ForwardArrow"));
	SetRootComponent(ForwardArrow);

#if WITH_EDITORONLY_DATA
	DebugBox = CreateDefaultSubobject<UBoxComponent>(TEXT("DebugBox"));
	DebugBox->SetupAttachment(RootComponent);
	DebugBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DebugBox->SetBoxExtent(FVector(30.f, 30.f, 90.f));
#endif
}

void AAICoverSlot::BeginPlay()
{
	Super::BeginPlay();

	if (UWorld* W = GetWorld())
		if (UCoverRegistrySubsystem* Registry = W->GetSubsystem<UCoverRegistrySubsystem>())
			Registry->RegisterSlot(this);

	UpdateDebugViz();
}

void AAICoverSlot::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* W = GetWorld())
		if (UCoverRegistrySubsystem* Registry = W->GetSubsystem<UCoverRegistrySubsystem>())
			Registry->UnregisterSlot(this);

	Super::EndPlay(EndPlayReason);
}

bool AAICoverSlot::IsClaimed() const
{
	return ClaimedBy.IsValid();
}

bool AAICoverSlot::IsClaimedBy(AActor* Claimer) const
{
	return Claimer != nullptr && ClaimedBy.Get() == Claimer;
}

bool AAICoverSlot::TryClaim(AActor* Claimer)
{
	if (!IsValid(Claimer))
		return false;

	if (ClaimedBy.IsValid() && ClaimedBy.Get() != Claimer)
		return false;

	ClaimedBy = Claimer;
	return true;
}

void AAICoverSlot::Release(AActor* Claimer)
{
	if (!ClaimedBy.IsValid() || ClaimedBy.Get() != Claimer)
		return;

	ClaimedBy = nullptr;
}

bool AAICoverSlot::IsTargetInFireArc(const FVector& TargetLoc) const
{
	const FVector ToTarget = (TargetLoc - GetActorLocation()).GetSafeNormal2D();
	const FVector Forward = GetActorForwardVector().GetSafeNormal2D();
	const float DotProduct = FVector::DotProduct(Forward, ToTarget);
	const float HalfArcCos = FMath::Cos(FMath::DegreesToRadians(FireArcDegrees * 0.5f));
	return DotProduct >= HalfArcCos;
}

bool AAICoverSlot::CanStandFireFrom() const
{
	return Height == ECoverHeight::Stand;
}

FVector AAICoverSlot::GetStandPosition() const
{
	return GetActorLocation();
}

void AAICoverSlot::UpdateDebugViz()
{
#if WITH_EDITORONLY_DATA
	if (!IsValid(DebugBox))
		return;

	const FLinearColor SlotColor = (Height == ECoverHeight::Stand)
		? FLinearColor(0.f, 1.f, 0.f, 0.4f)
		: FLinearColor(1.f, 1.f, 0.f, 0.4f);

	DebugBox->ShapeColor = SlotColor.ToFColor(true);
#endif
}
