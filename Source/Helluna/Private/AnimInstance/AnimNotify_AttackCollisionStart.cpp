// Capstone Project Helluna

#include "AnimInstance/AnimNotify_AttackCollisionStart.h"
#include "Character/HellunaEnemyCharacter.h"
#include "Helluna.h"

void UAnimNotify_AttackCollisionStart::Notify(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp || !MeshComp->GetOwner()) return;

	AHellunaEnemyCharacter* Enemy = Cast<AHellunaEnemyCharacter>(MeshComp->GetOwner());
	if (!Enemy) return;

	// BoxComponentName 이 None 이면 EnemyCharacter 쪽에서 "모든 박스 활성" 으로 처리됨.
	// 매칭 실패 시 경고는 SetAttackBoxActive 내부에서만.
	Enemy->SetAttackBoxActive(BoxComponentName, /*bEnable=*/true, Damage, bDrawDebug);

#if HELLUNA_DEBUG_ENEMY
	UE_LOG(LogTemp, Log,
		TEXT("AnimNotify_AttackCollisionStart: %s → Box=%s Dmg=%.1f"),
		*Enemy->GetName(), *BoxComponentName.ToString(), Damage);
#endif
}

#if WITH_EDITOR
FString UAnimNotify_AttackCollisionStart::GetNotifyName_Implementation() const
{
	if (BoxComponentName.IsNone())
	{
		return TEXT("AttackStart (?)");
	}
	return FString::Printf(TEXT("AttackStart [%s]"), *BoxComponentName.ToString());
}
#endif
