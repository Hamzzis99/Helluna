/**
 * AnimNotify_EnemyFireProjectile.cpp
 *
 * @author 김민우
 */

#include "Animation/AnimNotify_EnemyFireProjectile.h"
#include "Character/HellunaEnemyCharacter.h"
#include "Components/SkeletalMeshComponent.h"

void UAnimNotify_EnemyFireProjectile::Notify(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp) return;

	AActor* Owner = MeshComp->GetOwner();
	if (!Owner) return;

	AHellunaEnemyCharacter* Enemy = Cast<AHellunaEnemyCharacter>(Owner);
	if (!Enemy) return;

	// 서버/클라 모두에서 노티는 호출되지만 내부적으로 HasAuthority 검사 후 스폰은 서버만 수행.
	Enemy->FireActiveRangedProjectile();
}

FString UAnimNotify_EnemyFireProjectile::GetNotifyName_Implementation() const
{
	return TEXT("FireProjectile");
}
