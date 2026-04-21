// Capstone Project Helluna

#include "AnimInstance/AnimNotify_AttackCollisionEnd.h"
#include "Character/HellunaEnemyCharacter.h"

void UAnimNotify_AttackCollisionEnd::Notify(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp || !MeshComp->GetOwner()) return;

	AHellunaEnemyCharacter* Enemy = Cast<AHellunaEnemyCharacter>(MeshComp->GetOwner());
	if (!Enemy) return;

	// BoxComponentName None 이면 EnemyCharacter 가 전체 박스 비활성 처리
	Enemy->SetAttackBoxActive(BoxComponentName, /*bEnable=*/false, 0.f, false);
}

#if WITH_EDITOR
FString UAnimNotify_AttackCollisionEnd::GetNotifyName_Implementation() const
{
	if (BoxComponentName.IsNone())
	{
		return TEXT("AttackEnd (?)");
	}
	return FString::Printf(TEXT("AttackEnd [%s]"), *BoxComponentName.ToString());
}
#endif
