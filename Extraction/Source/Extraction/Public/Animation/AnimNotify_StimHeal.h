// AnimNotify_StimHeal -- place on the injection montage at the needle-hit frame.
// Resolves the owning actor's UConsumableInventoryComponent and applies the pending
// heal immediately.  The component's bStimHealPending guard makes this idempotent
// with FinishStimUse at window end: whichever fires first wins, the other no-ops.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_StimHeal.generated.h"

UCLASS(meta = (DisplayName = "Stim Heal"))
class EXTRACTION_API UAnimNotify_StimHeal : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override { return TEXT("Stim Heal"); }
};
