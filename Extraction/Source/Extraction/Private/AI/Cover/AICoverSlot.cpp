#include "AICoverSlot.h"
#include "CoverRegistrySubsystem.h"
#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"

DEFINE_LOG_CATEGORY(LogCoverSlot);

static constexpr float SubSlotEpsilon = 0.01f;

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
}

void AAICoverSlot::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdateDebugViz();
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

int32 AAICoverSlot::GetSubSlotCount() const
{
	const float HalfLen = GetCoverLineHalfLength();
	if (HalfLen <= 0.f)
		return 1;

	// ClampMin meta only enforces in-editor; clamp here for runtime safety
	const float Spacing = FMath::Max(SubSlotSpacing, 50.f);
	const int32 Count = 2 + FMath::FloorToInt((2.f * HalfLen - SubSlotEpsilon) / Spacing);
	return FMath::Max(Count, 1);
}

FVector AAICoverSlot::GetSubSlotLocation(int32 Index) const
{
	const int32 Count = GetSubSlotCount();

	if (Index < 0 || Index >= Count)
	{
		UE_LOG(LogCoverSlot, Warning, TEXT("%s: GetSubSlotLocation out-of-range Index=%d Count=%d"), *GetName(), Index, Count);
		return GetActorLocation();
	}

	if (Count == 1)
		return GetActorLocation();

	// Index 0 = left endpoint (-Right), Index Count-1 = right endpoint (+Right)
	// Z is the slot's actor Z — sub-slots are coplanar with the slot; ground-snap requires a trace (Phase 2 if needed)
	const float HalfLen = GetCoverLineHalfLength();
	const FVector Right = GetActorRightVector();

	const float T = (float)Index / (float)(Count - 1);
	const float Offset = FMath::Lerp(-HalfLen, HalfLen, T);
	return GetActorLocation() + Right * Offset;
}

bool AAICoverSlot::IsSubSlotPeekableCorner(int32 Index) const
{
	const int32 Count = GetSubSlotCount();

	if (Index < 0 || Index >= Count)
	{
		UE_LOG(LogCoverSlot, Warning, TEXT("%s: IsSubSlotPeekableCorner out-of-range Index=%d Count=%d"), *GetName(), Index, Count);
		return false;
	}

	if (Count == 1)
		return bIsPeekableCornerStart || bIsPeekableCornerEnd;

	if (Index == 0)
		return bIsPeekableCornerStart;

	if (Index == Count - 1)
		return bIsPeekableCornerEnd;

	return false;
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
