// Capstone Project Helluna

#include "AnimInstance/AnimNotify_AttackCollisionEnd.h"
#include "Character/HellunaEnemyCharacter.h"

void UAnimNotify_AttackCollisionEnd::Notify(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	// Null 체크
	if (!MeshComp || !MeshComp->GetOwner())
	{
		return;
	}

	// EnemyCharacter 캐스팅
	AHellunaEnemyCharacter* EnemyCharacter = Cast<AHellunaEnemyCharacter>(MeshComp->GetOwner());
	if (EnemyCharacter)
	{
		EnemyCharacter->StopAttackTrace();
		
		UE_LOG(LogTemp, Log, TEXT("AnimNotify_AttackCollisionEnd: %s stopped attack trace"), 
			*EnemyCharacter->GetName());
	}
}
