// Fill out your copyright notice in the Description page of Project Settings.

#include "AnimInstance/HellunaCharacterAnimInstance.h"
#include "Character/HellunaBaseCharacter.h"
#include "Character/HellunaHeroCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "KismetAnimationLibrary.h"
#include "HellunaGameplayTags.h"

void UHellunaCharacterAnimInstance::NativeInitializeAnimation()
{
	OwningCharacter = Cast<AHellunaBaseCharacter>(TryGetPawnOwner());

	if (OwningCharacter)
	{
		OwningMovementComponent = OwningCharacter->GetCharacterMovement();
	}
}

void UHellunaCharacterAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (!OwningCharacter)
	{
		return;
	}

	// GameThread에서 WeaponTag를 읽어 카테고리 결정
	WeaponAnimType = ResolveWeaponAnimType();

	// Idle 애니메이션 매핑 (키가 없으면 Unarmed 폴백, 그것도 없으면 nullptr)
	if (const TObjectPtr<UAnimSequence>* FoundIdle = IdleAnimMap.Find(WeaponAnimType))
	{
		CurrentIdleAnim = *FoundIdle;
	}
	else if (const TObjectPtr<UAnimSequence>* FallbackIdle = IdleAnimMap.Find(EWeaponAnimType::Unarmed))
	{
		CurrentIdleAnim = *FallbackIdle;
	}
	else
	{
		CurrentIdleAnim = nullptr;
	}

	// 블렌드 스페이스 매핑 (키가 없으면 Unarmed 폴백, 그것도 없으면 nullptr)
	if (const TObjectPtr<UBlendSpace>* Found = LocomotionBlendSpaceMap.Find(WeaponAnimType))
	{
		CurrentLocomotionBlendSpace = *Found;
	}
	else if (const TObjectPtr<UBlendSpace>* FallbackBS = LocomotionBlendSpaceMap.Find(EWeaponAnimType::Unarmed))
	{
		CurrentLocomotionBlendSpace = *FallbackBS;
	}
	else
	{
		CurrentLocomotionBlendSpace = nullptr;
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
	float RawSpeed = bMovementLocked ? 0.f : OwningCharacter->GetVelocity().Size2D();

	// 슬로우 보정: MoveSpeedMultiplier가 적용되면 실제 속도가 줄어드는데,
	// 블렌드 스페이스에는 "의도한 속도"를 넘겨야 달리기 애니메이션이 (느리게) 재생됨
	if (const AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(OwningCharacter))
	{
		const float SpeedMul = Hero->GetMoveSpeedMultiplier();
		if (SpeedMul > 0.f && SpeedMul < 1.f)
		{
			RawSpeed /= SpeedMul;
		}
	}
	GroundSpeed = RawSpeed;
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
		PlayFullBody = false;
	}

	LocomotionDirection = UKismetAnimationLibrary::CalculateDirection(OwningCharacter->GetVelocity(), OwningCharacter->GetActorRotation());
}

EWeaponAnimType UHellunaCharacterAnimInstance::ResolveWeaponAnimType() const
{
	const AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(OwningCharacter);
	if (!Hero)
	{
		return EWeaponAnimType::Unarmed;
	}

	const FGameplayTag& Tag = Hero->CurrentWeaponTag;

	if (!Tag.IsValid())
	{
		return EWeaponAnimType::Unarmed;
	}

	if (Tag.MatchesTag(HellunaGameplayTags::Player_Weapon_Farming))
	{
		return EWeaponAnimType::Pickaxe;
	}

	// 권총은 Gun의 하위 태그이므로 먼저 체크
	if (Tag.MatchesTagExact(HellunaGameplayTags::Player_Weapon_Gun_Pistol))
	{
		return EWeaponAnimType::Pistol;
	}

	if (Tag.MatchesTag(HellunaGameplayTags::Player_Weapon_Gun))
	{
		return EWeaponAnimType::Gun;
	}

	return EWeaponAnimType::Unarmed;
}
