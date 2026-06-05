// NavLinkProxy subclass — authored traversal entry point.
// Designer drops one per ledge/vault/climb/drop-down in a level; the smart-link
// callback drives UTraversalComponent::TryStartTraversalFromNavLink on whichever
// agent crosses it.

#pragma once

#include "CoreMinimal.h"
#include "Navigation/NavLinkProxy.h"
#include "TraversalTypes.h"
#include "TraversalNavLink.generated.h"

class UTraversalComponent;
struct FNavigationRelevantData;

UCLASS()
class EXTRACTION_API ATraversalNavLink : public ANavLinkProxy
{
	GENERATED_BODY()

public:
	ATraversalNavLink();

	UPROPERTY(EditAnywhere, Category = "Traversal")
	ETraversalType TraversalType = ETraversalType::Vault;

	UPROPERTY(EditAnywhere, Category = "Traversal",
		meta = (ClampMin = "0.5", ClampMax = "2.0",
			ToolTip = "Playback speed multiplier passed to the traversal component."))
	float PlayRate = 1.f;

	UPROPERTY(EditAnywhere, Category = "Traversal",
		meta = (ToolTip = "If the traversal component refuses (e.g. blocked clearance), teleport the agent to End so pathfinding still completes."))
	bool bFallbackTeleportOnFailure = true;

	// INavRelevantInterface — suppress the simple PointLink from nav data for up-types so only
	// the smart link contributes. Prevents duplicate off-mesh connections in baked AND runtime nav.
	virtual void GetNavigationData(FNavigationRelevantData& Data) const override;

	// INavLinkHostInterface — same suppression for the runtime nav octree query path.
	virtual bool GetNavigationLinksArray(TArray<FNavigationLink>& OutLink, TArray<FNavigationSegmentLink>& OutSegments) const override;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;

	UFUNCTION()
	void HandleSmartLinkReached(AActor* Agent, const FVector& Destination);

private:
	// Returns true for traversal types that use the smart-link montage path (Vault, Climb, Mantle).
	// Returns false for types that rely on native fall / simple off-mesh behaviour (DropDown, Jump, SprintJump).
	bool UsesSmartLinkTraversal() const;
};
