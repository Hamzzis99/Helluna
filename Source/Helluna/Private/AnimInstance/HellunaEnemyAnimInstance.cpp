// Fill out your copyright notice in the Description page of Project Settings.

#include "AnimInstance/HellunaEnemyAnimInstance.h"
#include "Character/HellunaBaseCharacter.h"
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
}
