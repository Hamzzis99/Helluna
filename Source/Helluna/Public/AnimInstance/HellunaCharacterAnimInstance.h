// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AnimInstance/HellunaBaseAnimInstance.h"
#include "HellunaCharacterAnimInstance.generated.h"

class AHellunaBaseCharacter;
class UCharacterMovementComponent;
class UAnimSequence;
class UBlendSpace;

/**
 * 무기 카테고리별 애니메이션 분류
 */
UENUM(BlueprintType)
enum class EWeaponAnimType : uint8
{
	Unarmed		UMETA(DisplayName = "비무장"),
	Pickaxe		UMETA(DisplayName = "곡괭이"),
	Gun			UMETA(DisplayName = "총기류"),
	Pistol		UMETA(DisplayName = "권총")
};

/**
 *
 */
UCLASS()
class HELLUNA_API UHellunaCharacterAnimInstance : public UHellunaBaseAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual void NativeThreadSafeUpdateAnimation(float DeltaSeconds) override;

protected:
	UPROPERTY()
	AHellunaBaseCharacter* OwningCharacter;

	UPROPERTY()
	UCharacterMovementComponent* OwningMovementComponent;

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "AnimData|LocomotionData",
		meta = (DisplayName = "지면 속도"))
	float GroundSpeed;

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "AnimData|LocomotionData",
		meta = (DisplayName = "가속 중인지"))
	bool bHasAcceleration;

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "AnimData|LocomotionData",
		meta = (DisplayName = "이동 방향"))
	float LocomotionDirection;

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadWrite, Category = "AnimData|LocomotionData",
		meta = (DisplayName = "전신 애니메이션 재생"))
	bool PlayFullBody;

	// ── 무기별 애니메이션 에셋 ────────────────────────────

	/** 현재 무기 애니메이션 카테고리 */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "AnimData|Weapon",
		meta = (DisplayName = "무기 애니메이션 타입"))
	EWeaponAnimType WeaponAnimType;

	/** 카테고리별 Idle 애니메이션 시퀀스 (에디터에서 지정) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AnimData|Weapon",
		meta = (DisplayName = "카테고리별 Idle 애니메이션"))
	TMap<EWeaponAnimType, TObjectPtr<UAnimSequence>> IdleAnimMap;

	/** 카테고리별 Locomotion 블렌드 스페이스 (에디터에서 지정) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AnimData|Weapon",
		meta = (DisplayName = "카테고리별 Locomotion 블렌드 스페이스"))
	TMap<EWeaponAnimType, TObjectPtr<UBlendSpace>> LocomotionBlendSpaceMap;

	/** AnimBP에서 바로 사용할 현재 Idle 애니메이션 */
	UPROPERTY(BlueprintReadOnly, Category = "AnimData|Weapon",
		meta = (DisplayName = "현재 Idle 애니메이션"))
	TObjectPtr<UAnimSequence> CurrentIdleAnim;

	/** AnimBP에서 바로 사용할 현재 블렌드 스페이스 */
	UPROPERTY(BlueprintReadOnly, Category = "AnimData|Weapon",
		meta = (DisplayName = "현재 Locomotion 블렌드 스페이스"))
	TObjectPtr<UBlendSpace> CurrentLocomotionBlendSpace;

	/** AnimGraph(Worker Thread)에서 안전하게 Idle 애니메이션을 가져오는 함수 */
	UFUNCTION(BlueprintPure, Category = "AnimData|Weapon",
		meta = (BlueprintThreadSafe, DisplayName = "현재 Idle 애니메이션 가져오기"))
	UAnimSequence* GetCurrentIdleAnim() const { return CurrentIdleAnim; }

	/** AnimGraph(Worker Thread)에서 안전하게 블렌드 스페이스를 가져오는 함수 */
	UFUNCTION(BlueprintPure, Category = "AnimData|Weapon",
		meta = (BlueprintThreadSafe, DisplayName = "현재 블렌드 스페이스 가져오기"))
	UBlendSpace* GetCurrentLocomotionBlendSpace() const { return CurrentLocomotionBlendSpace; }

private:
	/** GameplayTag → EWeaponAnimType 변환 (GameThread에서만 호출) */
	EWeaponAnimType ResolveWeaponAnimType() const;

	// [AnimDiagV2] 마리당 throttle. static 전역은 30마리 공유라 실질 로그 못 얻음 → 인스턴스 멤버.
	double LastAnimDiagLogTime = 0.0;

	// [AnimDiagV3] 위치 jump 감지용. 마지막 샘플 ActorLocation 보관.
	FVector LastAnimDiagLoc = FVector::ZeroVector;
};
