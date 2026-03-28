// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/HellunaHeroGameplayAbility.h"
#include "HeroGameplayAbility_Farming.generated.h"

/**
 * FindResourceComponent가 이미 FocusedActor(파밍 대상)를 정해주므로
 * GA에서는 "포커스 대상 가져오기 + 서버에서 데미지 적용"만 수행한다.
 */
class AActor;
class UAbilityTask_PlayMontageAndWait;

UCLASS()
class HELLUNA_API UHeroGameplayAbility_Farming : public UHellunaHeroGameplayAbility
{
	GENERATED_BODY()

public:
	UHeroGameplayAbility_Farming();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Farming|Snap", meta = (DisplayName = "파밍시 고정될 거리"))
	float FarmingSnapDistance = 130.f;

protected:
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;


	// ✅ 몽타주 1회 재생 (타겟 검증 → 스냅 → 회전 → 데미지 → 몽타주)
	void PlayFarmingMontage();

	UFUNCTION()
	void OnFarmingFinished();

	UFUNCTION()
	void OnFarmingInterrupted();

	// ✅ 파밍 대상과 플레이어 사이의 거리 고정
	bool SnapHeroToFarmingDistance(const FGameplayAbilityActorInfo* ActorInfo) const;



private:

	// ✅ 로컬이면 FocusedActor, 서버면 ServerFarmingTarget을 가져온다
	AActor* GetFarmingTarget(const FGameplayAbilityActorInfo* ActorInfo) const;

	// ✅ 로컬 체감: 즉시 Yaw만 회전
	void FaceToTarget_InstantLocalOnly(const FGameplayAbilityActorInfo* ActorInfo, const FVector& TargetLocation) const;

	UPROPERTY(EditDefaultsOnly, Category = "Farming|Look")  // 크로스헤어 X 위치 기준 (0.0 ~ 1.0)
	float CrosshairXNormalized = 0.57f;

	/**
	 * 몽타주 재생 시작 후 데미지가 적용되는 시점 (0.0 ~ 1.0).
	 * 0.26 = 몽타주 26% 지점에서 데미지 적용.
	 * 나중에 AnimNotify로 교체 가능.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Farming|Damage",
		meta = (DisplayName = "데미지 적용 시점 (비율)", ClampMin = "0.0", ClampMax = "1.0"))
	float DamageTimingRatio = 0.26f;

	/** 데미지 지연 적용용 타이머 */
	FTimerHandle DamageTimerHandle;

	/** 타이머 콜백: 실제 데미지 적용 */
	void ApplyFarmingDamage();

	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> FarmingTask = nullptr;
};
	