// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_AttackCollisionEnd.generated.h"

/**
 * 몬스터 공격 충돌 감지 종료
 * 
 * StartAttackTrace()로 시작된 타이머를 정지시킵니다.
 * 일반적으로 공격 애니메이션의 마지막 프레임에 배치합니다.
 * 
 * 사용법:
 *   공격 몽타주의 "손이 휘두르기 끝나는 프레임"에 추가
 *   이 시점부터 타이머가 정지되어 타격 판정 종료
 * 
 * @see AHellunaEnemyCharacter::StopAttackTrace()
 * @author 김민우
 */
UCLASS()
class HELLUNA_API UAnimNotify_AttackCollisionEnd : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, 
		const FAnimNotifyEventReference& EventReference) override;
};
