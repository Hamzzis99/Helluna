// Fill out your copyright notice in the Description page of Project Settings.

#include "AnimInstance/HellunaCharacterAnimInstance.h"
#include "Character/HellunaBaseCharacter.h"
#include "Character/HellunaHeroCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "KismetAnimationLibrary.h"
#include "HellunaGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"

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

	// [AimMoveStateV1] 견착 이동 애니메이션 매핑 (키 없으면 Gun 폴백, 그것도 없으면 nullptr → 그래프에서 폴백)
	//   견착 중엔 정면 고정이라 방향 블렌드 불필요 → 단일 시퀀스.
	if (const TObjectPtr<UAnimSequence>* FoundAimAnim = AimLocomotionAnimMap.Find(WeaponAnimType))
	{
		CurrentAimLocomotionAnim = *FoundAimAnim;
	}
	else if (const TObjectPtr<UAnimSequence>* FbAimAnim = AimLocomotionAnimMap.Find(EWeaponAnimType::Gun))
	{
		CurrentAimLocomotionAnim = *FbAimAnim;
	}
	else
	{
		CurrentAimLocomotionAnim = nullptr;
	}

	// 조준(견착) 포즈 매핑 (키가 없으면 Gun 폴백, 그것도 없으면 nullptr)
	//   라이플/권총=Pistol → 권총 ADS 포즈, 그 외 총기(스나이퍼·샷건·런처)=Gun → 라이플 조준 포즈
	if (const TObjectPtr<UAnimSequence>* FoundAim = AimPoseMap.Find(WeaponAnimType))
	{
		CurrentAimPose = *FoundAim;
	}
	else if (const TObjectPtr<UAnimSequence>* FallbackAim = AimPoseMap.Find(EWeaponAnimType::Gun))
	{
		CurrentAimPose = *FallbackAim;
	}
	else
	{
		CurrentAimPose = nullptr;
	}

	// [AimSpineYawV2] 캐릭터(게임스레드)가 계산한 조준 상체 yaw 값을 가져와 AnimGraph(Transform Modify Bone)에 노출.
	if (const AHellunaHeroCharacter* HeroForAim = Cast<AHellunaHeroCharacter>(OwningCharacter))
	{
		AimSpineYaw = HeroForAim->GetCurrentAimSpineYaw();
	}
	else
	{
		AimSpineYaw = 0.f;
	}

	// [AimIdleOverrideV1] 견착 중 감지 (ASC 태그 Player.status.Aim)
	bool bAimingNow = false;
	if (OwningCharacter)
	{
		if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OwningCharacter))
		{
			bAimingNow = ASC->HasMatchingGameplayTag(HellunaGameplayTags::Player_status_Aim);
		}
	}

	// 견착 중이면 정지 Idle 을 ADS 포즈(CurrentAimPose)로 → 정지 견착이 걷기 견착
	//   (블렌드스페이스 속도0 = MF_Pistol_Idle_ADS)과 같은 하체 스탠스가 되어 facing 통일.
	//   비조준 정지는 IdleAnimMap(MM_Idle 등) 그대로 → 총 내린 일반 idle.
	if (bAimingNow && CurrentAimPose)
	{
		CurrentIdleAnim = CurrentAimPose;
	}

	// [AimUpperBodyV1] 이동과 무관하게 조준이면 상체 알파 1 로 보간 → AnimGraph Layered Blend Per Bone 가중치.
	//   이동 견착에서도 상체가 조준 포즈를 유지해 걷기 블렌드스페이스 상체 스웨이(총구 떨림)를 차단.
	const float TargetAimAlpha = (bAimingNow && CurrentAimPose) ? 1.f : 0.f;
	AimUpperBodyAlpha = FMath::FInterpTo(AimUpperBodyAlpha, TargetAimAlpha, DeltaSeconds, FMath::Max(0.f, AimUpperBodyBlendSpeed));

	// [AimMoveStateV1] 견착 / 견착+이동 상태 노출 — 그래프에서 ADS 이동 전용 애니 분기용.
	bAiming = bAimingNow;
	const float AimSpeed2D = OwningCharacter ? OwningCharacter->GetVelocity().Size2D() : 0.f;
	// [AimFireGateV1] 발사 등 'UpperBody' 슬롯 몽타주가 재생 중이면 견착이동을 해제(false) → 전신 견착이동 애니가
	//   상체 발사 몽타주를 덮지 않도록. 발사 중엔 기존 경로(상체=발사 / 하체=DefaultSlot)로 빠지고, 끝나면 복귀.
	//   ※ Local weight 사용: Global 은 견착이동 노드가 뒤에서 상체를 덮으면 0 이 돼 게이트가 안 켜지는 순환 발생.
	//     Local 은 다운스트림과 무관하게 '슬롯에 몽타주가 재생 중인가'만 보므로 발사 감지에 정확.
	// [AimFireGateV2] 슬롯 가중치는 견착이동 분기가 그 슬롯을 우회하면 0 이 돼(순환) → 이동 중 발사 감지 실패.
	//   그래프 평가와 무관한 '몽타주 인스턴스 재생 여부'로 감지. 견착 중 도는 몽타주는 발사/장전 등 상체 액션뿐이라 안전.
	const bool bMontagePlaying = IsAnyMontagePlaying();
	bAimingMoving = bAimingNow && (AimSpeed2D >= FMath::Max(0.f, AimMovingSpeedThreshold)) && !bMontagePlaying;

	// [AimFireGateDiag] 진단용 — 확인되면 제거.
	if (bAimingNow)
	{
		UE_LOG(LogTemp, Warning, TEXT("[AimFireGateDiag] AnyMontage=%d Speed=%.0f AimMoving=%d"),
			bMontagePlaying ? 1 : 0, AimSpeed2D, bAimingMoving ? 1 : 0);
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
