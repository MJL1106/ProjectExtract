// ULootMarkerWidget implementation.

#include "UI/LootMarkerWidget.h"

void ULootMarkerWidget::SetMarkerInfo(ELootMarkerKind InKind, const FText& InLabel)
{
	const bool bChanged = MarkerKind != InKind || !MarkerLabel.EqualTo(InLabel);

	MarkerKind = InKind;
	MarkerLabel = InLabel;

	if (bChanged) OnMarkerInfoUpdated();
}

void ULootMarkerWidget::SetProximity(float InDistanceMeters, bool bInIsNear)
{
	DistanceMeters = InDistanceMeters;

	const bool bEdgeCrossed = bIsNear != bInIsNear;
	bIsNear = bInIsNear;

	if (bEdgeCrossed) OnProximityChanged(bIsNear);
}
