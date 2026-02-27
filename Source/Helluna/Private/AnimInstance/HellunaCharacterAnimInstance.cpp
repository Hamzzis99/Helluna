// Fill out your copyright notice in the Description page of Project Settings.

#include "AnimInstance/HellunaCharacterAnimInstance.h"
#include "Character/HellunaBaseCharacter.h"
#include "Character/HellunaHeroCharacter.h"
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

	// ★ 이동 잠금 상태 감지
	const bool bMovementLocked = (OwningMovementComponent->MaxWalkSpeed <= 0.f);
	GroundSpeed = bMovementLocked ? 0.f : OwningCharacter->GetVelocity().Size2D();
	bHasAcceleration = bMovementLocked ? false : OwningMovementComponent->GetCurrentAcceleration().SizeSquared2D() > 0.f;

	// ★ PlayFullBody 판단:
	// - 히어로: HeroCharacter->PlayFullBody 직접 참조 (GA_Farming 등에서 직접 설정)
	// - 적: DefaultSlot 몽타주가 전신을 덮어쓰므로 항상 false
	if (const AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(OwningCharacter))
	{
		PlayFullBody = Hero->PlayFullBody;
	}
	else
	{
		//PlayFullBody = DoesOwnerHaveTag(FGameplayTag::RequestGameplayTag(FName("State.Enemy.Attacking")));
		PlayFullBody = false;
	}

	LocomotionDirection = UKismetAnimationLibrary::CalculateDirection(OwningCharacter->GetVelocity(), OwningCharacter->GetActorRotation());
}
