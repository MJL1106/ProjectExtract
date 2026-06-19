// AnimNotifyState_MagazineSwap — place on an enemy reload montage to drive the magazine detach/reattach visual.
//
// NotifyBegin: detaches the rifle's magazine component and reparents it to the left hand bone,
//              so it rides the hand IK during the mag-pull phase.
// NotifyEnd:   reparents the magazine back to its original well socket in the visual rifle actor.
//              Fires on interruption and blend-out too, so the mag always comes home.
//
// Set HandSocketName to the left-hand grip socket on the Quantum skeleton (typically "hand_l").

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimNotifyState_MagazineSwap.generated.h"

UCLASS(meta = (DisplayName = "Magazine Swap"))
class EXTRACTION_API UAnimNotifyState_MagazineSwap : public UAnimNotifyState
{
	GENERATED_BODY()

public:

	/**
	 * Socket on the character's skeletal mesh that the magazine attaches to during the swap.
	 * Set to the left-hand grip socket on the Quantum skeleton — typically "hand_l".
	 * Tuning (position/rotation relative to the hand) is done via a socket offset in-editor, not in C++.
	 */
	UPROPERTY(EditAnywhere, Category = "Magazine")
	FName HandSocketName = NAME_None;

	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		float TotalDuration, const FAnimNotifyEventReference& EventReference) override;

	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override { return TEXT("Magazine Swap"); }
};
