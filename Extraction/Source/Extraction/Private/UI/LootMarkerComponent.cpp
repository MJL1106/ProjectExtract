// ULootMarkerComponent implementation — availability polling and visibility gating.

#include "UI/LootMarkerComponent.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UI/LootMarkerWidget.h"
#include "World/Lootable.h"
#include "World/WorldInteractable.h"

DEFINE_LOG_CATEGORY_STATIC(LogLootMarker, Log, All);

namespace
{
	/** Exit-side hysteresis on NearDistance -- mirrors the screen markers' ReentryHysteresis so a
	 *  player idling/strafing at the boundary doesn't flap OnProximityChanged every poll. */
	constexpr float NearExitHysteresis = 50.f;
}

ULootMarkerComponent::ULootMarkerComponent()
{
	SetWidgetSpace(EWidgetSpace::Screen);
	// Size to the widget rather than a fixed quad: the marker is a small glyph at range but expands
	// to a key prompt plus label up close, and a hardcoded DrawSize would clip the expanded state.
	// Matches AObjectiveMarkerDisplay's widget component. Pivot keeps it centred on the actor either way.
	SetDrawAtDesiredSize(true);
	SetPivot(FVector2D(0.5f, 0.5f));
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	SetTwoSided(false);
	SetVisibility(false);
	SetUsingAbsoluteScale(true);
}

void ULootMarkerComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!GetWidgetClass())
	{
		UE_LOG(LogLootMarker, Warning, TEXT("ULootMarkerComponent on %s: WidgetClass is null -- "
			"marker will be invisible. Assign it in the BP subclass defaults."), *GetNameSafe(GetOwner()));
	}

	// Compute relative Z so the world-space offset equals MarkerWorldZOffset regardless of
	// the attach parent's scale. Preserves any designer-authored X/Y nudge.
	const FVector Rel = GetRelativeLocation();
	if (const USceneComponent* ParentComponent = GetAttachParent())
	{
		constexpr float MinParentScale = 0.01f;
		const float RawScaleZ = ParentComponent->GetComponentScale().Z;
		const float SignZ = (RawScaleZ >= 0.f) ? 1.f : -1.f;
		const float SafeScaleZ = SignZ * FMath::Max(FMath::Abs(RawScaleZ), MinParentScale);
		SetRelativeLocation(FVector(Rel.X, Rel.Y, MarkerWorldZOffset / SafeScaleZ));
	}
	else
	{
		SetRelativeLocation(FVector(Rel.X, Rel.Y, MarkerWorldZOffset));
	}

	// Poll availability on a looping timer rather than every tick.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			AvailabilityTimerHandle,
			this,
			&ULootMarkerComponent::RefreshAvailability,
			AvailabilityPollInterval,
			/*bLoop=*/ true);
	}

	// Resolve once immediately so the first frame is correct.
	RefreshAvailability();
}

void ULootMarkerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AvailabilityTimerHandle);
	}

	AvailabilityQuery.Unbind();

	Super::EndPlay(EndPlayReason);
}

void ULootMarkerComponent::SetMarkerAvailable(bool bAvailable)
{
	bMarkerAvailable = bAvailable;
	RefreshAvailability();
}

void ULootMarkerComponent::RefreshMarkerNow()
{
	RefreshAvailability();
}

void ULootMarkerComponent::SetAvailabilityQuery(const FLootMarkerAvailabilityQuery& InQuery)
{
	AvailabilityQuery = InQuery;
	RefreshAvailability();
}

void ULootMarkerComponent::ClearAvailabilityQuery()
{
	AvailabilityQuery.Unbind();
}

void ULootMarkerComponent::RefreshAvailability()
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner)) return;

	float DistanceCm = 0.f;
	const bool bAvailable = ResolveAvailability(Owner, DistanceCm);
	const bool bWasVisible = IsVisible();

	// Reset parent's stale occlusion state on hidden-to-visible so the first visible
	// tick traces immediately and does not pop or draw through walls.
	if (bAvailable && !bWasVisible)
	{
		ResetOcclusionState();
	}

	SetVisibility(bAvailable);
	if (!bAvailable) return;

	// Widget can resolve late (CreateWidget hasn't run yet) -- bail this tick, the poll retries.
	UUserWidget* RawWidget = GetUserWidgetObject();
	if (!IsValid(RawWidget)) return;

	// Named MarkerWidget, not Widget -- UWidgetComponent already has a 'Widget' member and C4458 is
	// promoted to an error in this project.
	ULootMarkerWidget* MarkerWidget = Cast<ULootMarkerWidget>(RawWidget);
	if (!IsValid(MarkerWidget))
	{
		if (!bLoggedWidgetCastFailure)
		{
			UE_LOG(LogLootMarker, Warning, TEXT("ULootMarkerComponent on %s: widget class '%s' is not a "
				"ULootMarkerWidget -- kind/label/proximity pushes are silently dropped. Reparent the WBP."),
				*GetNameSafe(Owner), *GetNameSafe(GetWidgetClass()));
			bLoggedWidgetCastFailure = true;
		}
		return;
	}

	const FText Label = ResolveMarkerLabel(Owner);
	if (LastInfoWidget.Get() != MarkerWidget || !LastPushedLabel.EqualTo(Label))
	{
		MarkerWidget->SetMarkerInfo(MarkerKind, Label);
		LastInfoWidget = MarkerWidget;
		LastPushedLabel = Label;
	}

	// Hysteresis: enter near at NearDistance, exit only past NearDistance + NearExitHysteresis.
	const float ExitDistanceCm = NearDistance + NearExitHysteresis;
	const bool bNewIsNear = bIsNearLatched ? (DistanceCm <= ExitDistanceCm) : (DistanceCm <= NearDistance);
	bIsNearLatched = bNewIsNear;

	MarkerWidget->SetProximity(DistanceCm / 100.f, bNewIsNear);
}

bool ULootMarkerComponent::ResolveAvailability(AActor* Owner, float& OutDistanceCm) const
{
	// Distance gate first — cheap rejection before interface queries.
	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!IsValid(PlayerPawn))
	{
		return false;
	}

	const float DistSq = FVector::DistSquared(GetComponentLocation(), PlayerPawn->GetActorLocation());
	if (DistSq > FMath::Square(MaxVisibleDistance))
	{
		return false;
	}
	OutDistanceCm = FMath::Sqrt(DistSq);

	// Owner-supplied query, AND'd into every branch below. Unbound is the default and the common
	// case: nothing bound means resolution is exactly the interface/manual logic that follows.
	if (AvailabilityQuery.IsBound() && !AvailabilityQuery.Execute()) return false;

	// Interface-based availability, AND'd with the manual toggle.
	if (Owner->GetClass()->ImplementsInterface(ULootable::StaticClass()))
	{
		return bMarkerAvailable && ILootable::Execute_CanLoot(Owner);
	}

	if (Owner->GetClass()->ImplementsInterface(UWorldInteractable::StaticClass()))
	{
		return bMarkerAvailable && IWorldInteractable::Execute_CanWorldInteract(Owner, PlayerPawn);
	}

	return bMarkerAvailable;
}

FText ULootMarkerComponent::ResolveMarkerLabel(AActor* Owner) const
{
	if (!MarkerLabelOverride.IsEmpty()) return MarkerLabelOverride;

	// Degrades quietly: AWeaponCase implements neither interface and ALootPickup implements only
	// ILootable (no label surface there), so both rely entirely on MarkerLabelOverride above.
	if (!IsValid(Owner) || !Owner->GetClass()->ImplementsInterface(UWorldInteractable::StaticClass()))
		return FText::GetEmpty();

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	return IWorldInteractable::Execute_GetWorldInteractionPrompt(Owner, PlayerPawn);
}
