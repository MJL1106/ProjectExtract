#include "AICoverSlot.h"
#include "CoverRegistrySubsystem.h"
#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"

DEFINE_LOG_CATEGORY(LogCoverSlot);

static constexpr float DefaultEndpointInset = 40.f;

AAICoverSlot::AAICoverSlot()
{
	PrimaryActorTick.bCanEverTick = false;

	ForwardArrow = CreateDefaultSubobject<UArrowComponent>(TEXT("ForwardArrow"));
	SetRootComponent(ForwardArrow);

	CoverBoundsBox = CreateDefaultSubobject<UBoxComponent>(TEXT("CoverBoundsBox"));
	CoverBoundsBox->SetupAttachment(RootComponent);
	CoverBoundsBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CoverBoundsBox->SetGenerateOverlapEvents(false);
	CoverBoundsBox->SetBoxExtent(FVector(30.f, 30.f, 90.f));

	LeftEdgeArrow = CreateDefaultSubobject<UArrowComponent>(TEXT("LeftEdgeArrow"));
	LeftEdgeArrow->SetupAttachment(RootComponent);
	LeftEdgeArrow->SetArrowColor(FLinearColor(0.f, 1.f, 1.f, 1.f)); // cyan

	RightEdgeArrow = CreateDefaultSubobject<UArrowComponent>(TEXT("RightEdgeArrow"));
	RightEdgeArrow->SetupAttachment(RootComponent);
	RightEdgeArrow->SetArrowColor(FLinearColor(1.f, 0.f, 1.f, 1.f)); // magenta
}

void AAICoverSlot::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdateDebugViz();

	// Auto-default edge arrows only when the designer hasn't moved them yet.
	// Relative Y maps to the actor's local right axis (same as GetActorRightVector() in world space).
	const float HalfLen = GetCoverLineHalfLength();
	const float InsetOffset = FMath::Max(HalfLen - DefaultEndpointInset, 0.f);

	if (IsValid(LeftEdgeArrow) && LeftEdgeArrow->GetRelativeLocation().IsNearlyZero())
		LeftEdgeArrow->SetRelativeLocation(FVector(0.f, -InsetOffset, 0.f));

	if (IsValid(RightEdgeArrow) && RightEdgeArrow->GetRelativeLocation().IsNearlyZero())
		RightEdgeArrow->SetRelativeLocation(FVector(0.f, InsetOffset, 0.f));
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

bool AAICoverSlot::CanStandFireOver() const
{
	// Crouch = chest-high wall — companion can stand and fire over the top.
	// Stand = tall wall — must use corner peeks only.
	return Height == ECoverHeight::Crouch;
}

FVector AAICoverSlot::GetStandPosition() const
{
	return GetActorLocation();
}

float AAICoverSlot::GetCoverLineHalfLength() const
{
	if (!IsValid(CoverBoundsBox))
		return 0.f;

	return CoverBoundsBox->GetScaledBoxExtent().Y;
}

void AAICoverSlot::UpdateDebugViz()
{
	if (!IsValid(CoverBoundsBox))
		return;

	const FLinearColor SlotColor = (Height == ECoverHeight::Stand)
		? FLinearColor(0.f, 1.f, 0.f, 0.4f)
		: FLinearColor(1.f, 1.f, 0.f, 0.4f);

	CoverBoundsBox->ShapeColor = SlotColor.ToFColor(true);
}

// --- Endpoint API ---

FVector AAICoverSlot::GetLeftEdge() const
{
	if (!IsValid(LeftEdgeArrow))
		return GetActorLocation();

	return LeftEdgeArrow->GetComponentLocation();
}

FVector AAICoverSlot::GetRightEdge() const
{
	if (!IsValid(RightEdgeArrow))
		return GetActorLocation();

	return RightEdgeArrow->GetComponentLocation();
}

FVector AAICoverSlot::GetLineDirection() const
{
	return (GetRightEdge() - GetLeftEdge()).GetSafeNormal2D();
}

float AAICoverSlot::GetLineLength() const
{
	return (GetRightEdge() - GetLeftEdge()).Size2D();
}

FVector AAICoverSlot::GetForwardDirection() const
{
	const FVector LineDir = GetLineDirection();
	FVector Perp = FVector::CrossProduct(FVector::UpVector, LineDir);

	if (Perp.IsNearlyZero())
	{
		FVector Fwd = GetActorForwardVector();
		Fwd.Z = 0.f;
		return Fwd.GetSafeNormal();
	}

	const float DotWithActorForward = FVector::DotProduct(Perp, GetActorForwardVector());
	if (FMath::Abs(DotWithActorForward) < 0.01f)
	{
		UE_LOG(LogCoverSlot, Warning, TEXT("%s: GetForwardDirection 90deg-ambiguous — actor forward is perpendicular to endpoint line; sign-disambiguation is unstable. Rotate the slot actor toward the enemy half-space."), *GetName());
	}
	if (DotWithActorForward < 0.f)
		Perp = -Perp;

	return Perp.GetSafeNormal();
}

FVector AAICoverSlot::GetLocationAtAlpha(float Alpha) const
{
	const float ClampedAlpha = FMath::Clamp(Alpha, 0.f, 1.f);
	return FMath::Lerp(GetLeftEdge(), GetRightEdge(), ClampedAlpha);
}

float AAICoverSlot::GetAlphaFromLocation(const FVector& WorldLoc) const
{
	FVector Dir = GetRightEdge() - GetLeftEdge();
	Dir.Z = 0.f;

	if (Dir.IsNearlyZero())
		return 0.5f;

	FVector ToLoc = WorldLoc - GetLeftEdge();
	ToLoc.Z = 0.f;

	const float Alpha = FVector::DotProduct(ToLoc, Dir) / Dir.SizeSquared();
	return FMath::Clamp(Alpha, 0.f, 1.f);
}

bool AAICoverSlot::IsAlphaAtPeekableCorner(float Alpha, float Epsilon) const
{
	return (Alpha <= Epsilon && bIsPeekableCornerStart) || (Alpha >= 1.f - Epsilon && bIsPeekableCornerEnd);
}
