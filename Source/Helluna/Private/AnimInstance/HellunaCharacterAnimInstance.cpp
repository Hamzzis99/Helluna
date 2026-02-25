// Fill out your copyright notice in the Description page of Project Settings.


#include "AnimInstance/HellunaCharacterAnimInstance.h"
#include "Character/HellunaBaseCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "KismetAnimationLibrary.h"

void UHellunaCharacterAnimInstance::NativeInitializeAnimation()
{
	OwningCharacter = Cast<AHellunaBaseCharacter>(TryGetPawnOwner());

	if (OwningCharacter)
	{
		OwningMovementComponent = OwningCharacter->GetCharacterMovement();
	}
}

void UHellunaCharacterAnimInstance::NativeThreadSafeUpdateAnimation(float DeltaSeconds)
{
	if (!OwningCharacter || !OwningMovementComponent)
	{
		return;
	}

	// ★ 이동 잠금 상태 감지: MaxWalkSpeed가 0이면 LockMovementAndFaceTarget이 걸린 상태
	const bool bMovementLocked = (OwningMovementComponent->MaxWalkSpeed <= 0.f);
	GroundSpeed = bMovementLocked ? 0.f : OwningCharacter->GetVelocity().Size2D();
	bHasAcceleration = bMovementLocked ? false : OwningMovementComponent->GetCurrentAcceleration().SizeSquared2D() > 0.f;

	// ★ 공격 중 여부: State.Enemy.Attacking 태그로 판단
	// PlayFullBody = true 이면 애님 그래프의 Base Locomotion Layer를 완전히 억제해
	// 몽타주 시작/종료 시 Idle 포즈가 한 프레임 보이는 스냅 현상을 방지한다.
	PlayFullBody = DoesOwnerHaveTag(FGameplayTag::RequestGameplayTag(FName("State.Enemy.Attacking")));

	LocomotionDirection = UKismetAnimationLibrary::CalculateDirection(OwningCharacter->GetVelocity(), OwningCharacter->GetActorRotation());
}

