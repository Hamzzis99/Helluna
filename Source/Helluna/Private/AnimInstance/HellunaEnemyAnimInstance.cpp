// Fill out your copyright notice in the Description page of Project Settings.

#include "AnimInstance/HellunaEnemyAnimInstance.h"
#include "Character/HellunaBaseCharacter.h"
#include "Character/HellunaEnemyCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"

void UHellunaEnemyAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
}

void UHellunaEnemyAnimInstance::NativeThreadSafeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeThreadSafeUpdateAnimation(DeltaSeconds);

	// 부모에서 캐싱한 MovementComponent를 사용해 공중 여부 판정.
	// IsFalling()은 낙하/점프 모두 true — 지면 이탈 시 공중 애니메이션으로 전환.
	if (OwningMovementComponent)
	{
		bIsInAir = OwningMovementComponent->IsFalling();
	}
	else
	{
		bIsInAir = false;
	}

	// [ForceRunAnimV1] 추격(ChaseTarget) 중이면 GroundSpeed 에 달리기 바닥값을 깐다.
	//   우주선 근처 경로 재탐색이 반복되면 실제 Velocity 가 0 으로 떨어져 블렌드스페이스가
	//   idle 을 재생 → "idle 포즈로 미끄러지며 다가오는" 현상. bForceRunAnim 은 서버→클라
	//   복제라 모든 머신 AnimBP(이 워커 스레드 업데이트)가 동일하게 본다. Max 라서 실제 속도가
	//   더 빠르면 그대로 두고, 군집/벽에 막혀 0 이어도 달리기 비주얼 유지. 몽타주 비용 0.
	//   공중(낙하/점프) 중에는 적용 안 함 — 공중 애님 우선.
	if (!bIsInAir)
	{
		if (const AHellunaEnemyCharacter* Enemy = Cast<AHellunaEnemyCharacter>(OwningCharacter))
		{
			if (Enemy->bForceRunAnim && Enemy->ForceRunAnimSpeed > 0.f)
			{
				GroundSpeed = FMath::Max(GroundSpeed, Enemy->ForceRunAnimSpeed);
				bHasAcceleration = true;
			}
		}
	}
}
