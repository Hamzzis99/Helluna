// Capstone Project Helluna - Gun Parry Phase 2

#include "AnimInstance/AnimNotify_ParryAOE.h"

void UAnimNotify_ParryAOE::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	// TODO [Phase 2] 샷건 광역 Stagger 구현
	// 1. MeshComp->GetOwner() 위치 기준으로 AOERadius 범위 탐색
	// 2. Enemy.Type.Humanoid 태그를 가진 적 필터링
	// 3. 해당 적들의 ASC에 Enemy.State.Staggered 태그 부여 (GE 또는 LooseTag)
}
