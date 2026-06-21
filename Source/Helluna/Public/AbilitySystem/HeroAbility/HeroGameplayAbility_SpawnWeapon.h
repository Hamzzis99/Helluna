// Fill out your copyright notice in the Description page of Project Settings.

#pragma once


#include "CoreMinimal.h"
#include "AbilitySystem/HellunaHeroGameplayAbility.h"
#include "HeroGameplayAbility_SpawnWeapon.generated.h"


class AHellunaHeroWeapon;
class UAbilityTask_PlayMontageAndWait;
/**
 * 
 */
UCLASS()
class HELLUNA_API UHeroGameplayAbility_SpawnWeapon : public UHellunaHeroGameplayAbility
{
	GENERATED_BODY()

protected:
	UHeroGameplayAbility_SpawnWeapon();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	UFUNCTION()
	void OnEquipFinished();

	UFUNCTION()
	void OnEquipInterrupted();

	// [EquipWatchdogV1 2026-06-22] 발도 몽타주 콜백(완료/중단/취소/blend-out)이 모두 유실되어
	//   EndAbility가 안 불릴 때, 잠금(SetEquipping)+차단(BlockAbilitiesWithTag: Shoot/Reload/SpawnWeapon)이
	//   영구 stuck 되지 않도록 강제 종료하는 안전망.
	UFUNCTION()
	void OnEquipWatchdog();

	// [EquipWatchdogV1 2026-06-22] 워치독 타임아웃 = (발도 몽타주 길이 + 이 여유 시간[초]).
	//   정상적으로 긴 발도 애니메이션을 조기에 끊지 않도록 넉넉히 둔다(에디터 튜닝 가능).
	UPROPERTY(EditDefaultsOnly, Category = "Helluna|Weapon")
	float EquipWatchdogExtraSeconds = 2.0f;


	// 스폰할 무기(에디터에서 지정)
	UPROPERTY(EditDefaultsOnly, Category = "Helluna|Weapon")
	TSubclassOf<AHellunaHeroWeapon> WeaponClass;

	FName AttachSocketName = TEXT("WeaponSocket");

private:

	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> EquipTask = nullptr;

	// [Fix:reentry-guard 2026-05-02] 몽타주 콜백 이중 호출 차단 (GunParry 동일 패턴)
	bool bEquipEndCalled = false;

	// [EquipWatchdogV1 2026-06-22] 장착 잠금/차단 stuck 방지용 워치독 타이머 핸들
	FTimerHandle EquipWatchdogTimerHandle;
};
