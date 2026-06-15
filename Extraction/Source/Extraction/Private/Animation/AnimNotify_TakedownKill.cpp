// AnimNotify_TakedownKill

#include "AnimNotify_TakedownKill.h"
#include "Components/SkeletalMeshComponent.h"
#include "ExtractionPlayer.h"

void UAnimNotify_TakedownKill::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!IsValid(MeshComp)) return;

	AExtractionPlayer* Player = Cast<AExtractionPlayer>(MeshComp->GetOwner());
	if (!IsValid(Player)) return;

	Player->FinishPendingTakedown();
}
