// AnimNotify_TakedownKill — drop at the death frame of the player finisher montage.
// When the notify fires, it resolves the owning AExtractionPlayer and calls
// FinishPendingTakedown(), which applies the lethal hit to the frozen victim.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_TakedownKill.generated.h"

UCLASS(meta = (DisplayName = "Takedown Kill"))
class EXTRACTION_API UAnimNotify_TakedownKill : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override { return TEXT("Takedown Kill"); }
};
