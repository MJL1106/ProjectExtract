// AnimNotifyState_MagazineSwap

#include "AnimNotifyState_MagazineSwap.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnemyCharacter.h"
#include "WeaponBase.h"

void UAnimNotifyState_MagazineSwap::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	if (!IsValid(MeshComp)) return;

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(MeshComp->GetOwner());
	if (!IsValid(Enemy)) return;

	AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
	if (!IsValid(Weapon)) return;

	Weapon->DetachMagazineToHand(MeshComp, HandSocketName);
}

void UAnimNotifyState_MagazineSwap::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	if (!IsValid(MeshComp)) return;

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(MeshComp->GetOwner());
	if (!IsValid(Enemy)) return;

	AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
	if (!IsValid(Weapon)) return;

	Weapon->ReattachMagazine();
}
