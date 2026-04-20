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

	// [AnimDiagV3] 루트모션/위치 jump 감지용 확장. MaxWS=0 인데 Vel>0 이면 이상 상태.
	//   - LocDelta: 마지막 샘플 이후 위치 변화 (큰 값 = 순간이동 의심).
	//   - RootMotion: 현재 몬타지가 root motion 을 켜고 있는지.
	//   - HasMontage: 현재 재생 중 몬타지 이름.
	{
		const double Now = FPlatformTime::Seconds();
		if (Now - LastAnimDiagLogTime >= 0.5)
		{
			const FVector CurLoc = OwningCharacter->GetActorLocation();
			const double Dt = LastAnimDiagLogTime > 0.0 ? (Now - LastAnimDiagLogTime) : 0.5;
			const float LocDelta = (float)FVector::Dist2D(CurLoc, LastAnimDiagLoc);
			const float EffectiveSpeed = Dt > 0.0 ? LocDelta / (float)Dt : 0.f;

			LastAnimDiagLogTime = Now;
			LastAnimDiagLoc = CurLoc;

			const FVector PawnVel = OwningCharacter->GetVelocity();
			const FVector CmcVel = OwningMovementComponent ? OwningMovementComponent->Velocity : FVector::ZeroVector;
			const FVector Acc = OwningMovementComponent ? OwningMovementComponent->GetCurrentAcceleration() : FVector::ZeroVector;

			FName MontageName = NAME_None;
			bool bHasRootMotion = false;
			if (UAnimMontage* Mont = GetCurrentActiveMontage())
			{
				MontageName = Mont->GetFName();
				bHasRootMotion = Mont->HasRootMotion();
			}

			UE_LOG(LogTemp, Warning,
				TEXT("[AnimDiagV3] %s | MaxWS=%.1f Locked=%d | PawnVel2D=%.1f CMCVel2D=%.1f Acc2D=%.1f | Loc=%s LocDelta=%.1f EffSpd=%.1f | MoveMode=%d | Montage=%s RootMo=%d"),
				*OwningCharacter->GetName(),
				OwningMovementComponent ? OwningMovementComponent->MaxWalkSpeed : -1.f,
				bMovementLocked ? 1 : 0,
				PawnVel.Size2D(),
				CmcVel.Size2D(),
				Acc.Size2D(),
				*CurLoc.ToCompactString(),
				LocDelta, EffectiveSpeed,
				OwningMovementComponent ? (int32)OwningMovementComponent->MovementMode.GetValue() : -1,
				*MontageName.ToString(),
				bHasRootMotion ? 1 : 0);
		}
	}

	// ★ PlayFullBody 판단:
	// - 히어로: HeroCharacter->PlayFullBody 직접 참조 (GA_Farming 등에서 직접 설정)
	// - 적: [FullBodyLockV1] 공격 락(MaxWS≤0) 중에는 true → Locomotion Idle 하체가
	//        상체 공격 몬타지와 블렌드되어 "Idle 서서 치기"처럼 보이는 증상 제거.
	//        언락 상태는 false 유지 — 이동 애니가 정상적으로 하체에서 재생됨.
	if (const AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(OwningCharacter))
	{
		PlayFullBody = Hero->PlayFullBody;
	}
	else
	{
		PlayFullBody = bMovementLocked;
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
