// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/HeroAbility/HeroGameplayAbility_Farming.h"
#include "HeroGameplayAbility_OreMine.generated.h"

/**
 * G키 광석 채집 GA.
 * - 인벤토리에서 곡괭이 슬롯을 분리한 뒤 사용. (BP 측 InputAction "G"에 바인딩)
 * - Farming GA를 상속하여 채집 로직(몽타주/데미지/홀딩 반복)을 그대로 재사용한다.
 * - 추가로 ActivateAbility에서 손 무기를 곡괭이로 임시 교체,
 *   EndAbility에서 원래 무기로 복원하는 책임만 진다.
 *
 * 흐름:
 *   1. (로컬) 포커스된 광석 검증 → Hero->Server_SwapToPickaxeTemp() RPC
 *   2. PickaxeSwapDelay 만큼 대기 (서버 스폰/리플리케이션 보정)
 *   3. Super::ActivateAbility() — 기존 Farming 흐름 시작 (몽타주/데미지/홀드 반복)
 *   4. EndAbility → Hero->Server_RestorePrePickaxeWeapon() RPC
 */
UCLASS()
class HELLUNA_API UHeroGameplayAbility_OreMine : public UHeroGameplayAbility_Farming
{
	GENERATED_BODY()

public:
	UHeroGameplayAbility_OreMine();

protected:
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

private:
	/** 곡괭이 스폰/리플리케이션 보정 대기 시간 (초). 0이면 즉시 채집 시도. */
	UPROPERTY(EditDefaultsOnly, Category = "OreMine|Timing",
		meta = (DisplayName = "곡괭이 교체 대기", ClampMin = "0.0", ClampMax = "1.0"))
	float PickaxeSwapDelay = 0.15f;

	/** 곡괭이 장착 시 멀티캐스트할 애니 (다른 클라용, 비워두면 애니 생략) */
	UPROPERTY(EditDefaultsOnly, Category = "OreMine|FX",
		meta = (DisplayName = "곡괭이 장착 멀티캐스트 몽타주"))
	TObjectPtr<UAnimMontage> PickaxeEquipMontage = nullptr;

	/** 원래 무기 복원 시 멀티캐스트할 애니 (다른 클라용, 비워두면 생략) */
	UPROPERTY(EditDefaultsOnly, Category = "OreMine|FX",
		meta = (DisplayName = "원무기 복원 멀티캐스트 몽타주"))
	TObjectPtr<UAnimMontage> RestoreEquipMontage = nullptr;

	/** Activate에서 캐싱된 핸들/액터정보 — 지연 시작 시 Super 호출용 */
	FGameplayAbilitySpecHandle PendingHandle;
	FGameplayAbilityActivationInfo PendingActivationInfo;

	FTimerHandle SwapDelayTimer;

	/** 곡괭이 교체 대기 후 호출 — Super::ActivateAbility로 채집 시퀀스 진입 */
	void StartFarmingAfterSwap();
};
