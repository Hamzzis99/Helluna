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
class UAbilityTask_WaitDelay;
struct FBranchingPointNotifyPayload;

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


	// ✅ 몽타주 1회 재생 (타겟 검증 → 스냅 → 즉시 회전 → 몽타주 + 데미지 지연 셋업)
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
	AActor* ResolveFarmingTarget(const FGameplayAbilityActorInfo* ActorInfo);
	bool IsTargetWithinFarmingRange(const FGameplayAbilityActorInfo* ActorInfo, AActor* Target) const;

	// ✅ 로컬 체감: 즉시 Yaw만 회전 (Pawn + Controller)
	void FaceToTarget_InstantLocalOnly(const FGameplayAbilityActorInfo* ActorInfo, const FVector& TargetLocation) const;

	/**
	 * [레거시] 몽타주 재생 시작 후 데미지가 적용되는 시점 (0.0 ~ 1.0).
	 * DamageImpactTime이 0보다 크면 이 값은 무시된다 (절대 시간 우선).
	 * 절대 시간 쪽으로 통일 후 제거 예정.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Farming|Damage",
		meta = (DisplayName = "[레거시] 데미지 적용 시점 (비율)", ClampMin = "0.0", ClampMax = "1.0"))
	float DamageTimingRatio = 0.26f;

	/**
	 * [DmgSyncV2] 몽타주 재생 시작 후 데미지가 적용되는 절대 시간 (초).
	 * 0 이하로 두면 위의 DamageTimingRatio * 몽타주길이 폴백.
	 * 채굴 공격 몽타주(약 2초)에서 실제 타격 프레임(≈0.30s)에 맞춤.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Farming|Damage",
		meta = (DisplayName = "데미지 적용 시점 (초)", ClampMin = "0.0"))
	float DamageImpactTime = 0.30f;

	/**
	 * [DmgSyncV2] 타격 프레임을 몽타주 AnimNotify로 받고 싶을 때 사용.
	 * 몽타주에 UAnimNotify_PlayMontageNotify를 추가하고 같은 NotifyName을 지정하면
	 * 해당 Notify 시점에 즉시 데미지가 들어간다 (절대 시간보다 우선).
	 * 비워두면 절대 시간(DamageImpactTime) 또는 비율 폴백만 사용.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Farming|Damage",
		meta = (DisplayName = "타격 AnimNotify 이름"))
	FName HitNotifyName = FName(TEXT("FarmHit"));

	/** 데미지 지연 적용용 타이머 (레거시 — 현재 사용 안 함, 호환성을 위해 유지) */
	FTimerHandle DamageTimerHandle;

	/** [DmgSyncV1] 몽타주와 동일 GA 타임라인에서 동작하는 데미지 지연 Task */
	UPROPERTY()
	TObjectPtr<UAbilityTask_WaitDelay> DamageDelayTask = nullptr;

	/** 타이머 콜백: 실제 데미지 적용 */
	UFUNCTION()
	void ApplyFarmingDamage();

	/** [DmgSyncV2] AnimNotify 수신 콜백 — HitNotifyName과 일치하면 즉시 데미지 */
	UFUNCTION()
	void OnMontageNotifyBegin(FName NotifyName, const FBranchingPointNotifyPayload& Payload);

	/** 이번 몽타주 사이클 내에서 데미지가 이미 들어갔는지 (Notify/Timer 중복 방지) */
	bool bDamageFiredThisSwing = false;

	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> FarmingTask = nullptr;

	TWeakObjectPtr<AActor> CachedFarmingTarget;
};
