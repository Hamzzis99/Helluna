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

protected:
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	UFUNCTION()
	void OnFarmingFinished();

	UFUNCTION()
	void OnFarmingInterrupted();

private:

	// ✅ 로컬이면 FocusedActor, 서버면 ServerFarmingTarget을 가져온다
	AActor* GetFarmingTarget(const FGameplayAbilityActorInfo* ActorInfo) const;

	// ✅ 로컬 체감: 즉시 Yaw만 회전
	void FaceToTarget_InstantLocalOnly(const FGameplayAbilityActorInfo* ActorInfo, const FVector& TargetLocation) const;

	UPROPERTY(EditDefaultsOnly, Category = "Farming|Look")  // 크로스헤어 X 위치 기준 (0.0 ~ 1.0) 
	float CrosshairXNormalized = 0.57f;

	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> FarmingTask = nullptr;
};
	