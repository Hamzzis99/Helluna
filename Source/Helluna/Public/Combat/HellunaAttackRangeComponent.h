// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "Components/BoxComponent.h"
#include "HellunaAttackRangeComponent.generated.h"

/**
 * UHellunaAttackRangeComponent
 *
 * 몬스터 근접 공격 범위를 표현하는 전용 Box 컴포넌트.
 *
 * ■ 사용 방법
 *   BP_Enemy_* 에 이 컴포넌트를 추가 → CharacterMesh 의 공격 소켓(Hand_R 등)에 Attach.
 *   AnimNotify_AttackCollisionStart 의 BoxComponentName 을 이 컴포넌트 이름과 동일하게 설정.
 *
 * ■ 기본 동작
 *   - 공격 안 할 때: NoCollision (broad-phase 트리 미등록)
 *   - AnimNotify Start: QueryOnly + ECC_Pawn Overlap 활성
 *   - AnimNotify End: NoCollision 복귀
 *   - 중복 히트 방지: AHellunaEnemyCharacter::HitActorsThisAttack Set
 *
 * ■ 최적화
 *   - 기본 Collision 꺼져 있어 비활성 시 비용 0
 *   - Tick 없음
 *   - 서버에서만 BeginOverlap 처리
 *
 * @author 김민우
 */
UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent,
	DisplayName = "헬루나 공격 범위 컴포넌트"))
class HELLUNA_API UHellunaAttackRangeComponent : public UBoxComponent
{
	GENERATED_BODY()

public:
	UHellunaAttackRangeComponent();
};
