// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/HellunaGameplayAbility.h"
#include "HellunaEnemyGameplayAbility.generated.h"

class AHellunaEnemyCharacter;
class UEnemyCombatComponent;
class UAnimMontage;
class USoundBase;
class USoundAttenuation;
/**
 * 
 */
UCLASS()
class HELLUNA_API UHellunaEnemyGameplayAbility : public UHellunaGameplayAbility
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Helluna|Ability")
	AHellunaEnemyCharacter* GetEnemyCharacterFromActorInfo();

	UFUNCTION(BlueprintPure, Category = "Helluna|Ability")
	UEnemyCombatComponent* GetEnemyCombatComponentFromActorInfo();

	// ═══════════════════════════════════════════════════════════
	// 건패링 윈도우 (자식 GA에서 활용)
	// ═══════════════════════════════════════════════════════════

	/** 이 공격이 패링 윈도우를 여는지 (근거리/원거리 공격 GA에서 개별 설정) */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Parry",
		meta = (DisplayName = "패링 윈도우 열기",
			ToolTip = "true이면 이 공격 시작 시 일정 시간 동안 건패링 대상이 됩니다."))
	bool bOpensParryWindow = false;

	/** 패링 윈도우 지속 시간 (초) */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Parry",
		meta = (DisplayName = "패링 윈도우 시간(초)", ClampMin = "0.1", ClampMax = "100.0",
			EditCondition = "bOpensParryWindow"))
	float ParryWindowDuration = 0.4f;

	// ═══════════════════════════════════════════════════════════
	// [AttackAssetsV1] GA 소유 몬타지 / 사운드 (VFX는 각 GA 하위에서 관리)
	//   — 같은 몬스터가 여러 Attack GA를 가질 때 각 GA가 자기 데이터 소유
	//   — Enemy->AttackMontage 는 하위 호환용 폴백으로만 사용
	// ═══════════════════════════════════════════════════════════

	/** 이 GA가 재생할 공격 몬타지. 비워두면 Enemy->AttackMontage 로 폴백. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Assets",
		meta = (DisplayName = "공격 몬타지",
			ToolTip = "이 GA가 재생할 애니메이션 몬타지. 비우면 캐릭터에 설정된 AttackMontage 사용."))
	TObjectPtr<UAnimMontage> AttackMontage = nullptr;

	/** 공격 몬타지 시작 시 재생할 사운드. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Assets",
		meta = (DisplayName = "공격 사운드"))
	TObjectPtr<USoundBase> AttackSound = nullptr;

	/** 공격 사운드 볼륨 배율. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Assets",
		meta = (DisplayName = "사운드 볼륨 배율", ClampMin = "0.0", ClampMax = "5.0"))
	float AttackSoundVolumeMultiplier = 1.f;

	/** 타격 판정 성공 시 재생할 사운드 (히트 포인트에서 스폰). */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Assets",
		meta = (DisplayName = "타격 사운드",
			ToolTip = "이 GA가 발동한 공격이 플레이어/우주선에 적중했을 때 재생할 사운드.\nAttackTrace Hit.Location 또는 Projectile Impact 위치에서 Multicast 로 재생됩니다."))
	TObjectPtr<USoundBase> HitSound = nullptr;

	/** 타격 사운드 볼륨 배율. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Assets",
		meta = (DisplayName = "타격 사운드 볼륨 배율", ClampMin = "0.0", ClampMax = "5.0"))
	float HitSoundVolumeMultiplier = 1.f;

	/** 타격 사운드 감쇠. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Assets",
		meta = (DisplayName = "타격 사운드 감쇠"))
	TObjectPtr<USoundAttenuation> HitSoundAttenuation = nullptr;

public:
	/** 실제 재생할 몬타지: GA의 AttackMontage 우선, 없으면 Enemy->AttackMontage. */
	UFUNCTION(BlueprintPure, Category = "Helluna|Ability")
	UAnimMontage* GetEffectiveAttackMontage() const;

	/** GA에 설정된 사운드를 캐릭터에서 Multicast 로 재생. */
	UFUNCTION(BlueprintCallable, Category = "Helluna|Ability")
	void PlayAttackSound();

	/** GA에 설정된 타격 사운드를 캐릭터에 캐싱 — 히트 감지 시 Multicast 로 재생됨. */
	UFUNCTION(BlueprintCallable, Category = "Helluna|Ability")
	void CacheHitSound();

	/**
	 * StateTree Task 가 발동 전에 호출하는 타겟 주입 진입점.
	 * 파생 GA (Attack / RangedAttack / DashAttack) 가 각자 CurrentTarget 필드를 업데이트.
	 * 베이스 기본 구현은 no-op — 타겟이 필요 없는 GA(패턴/버프 등)는 오버라이드 불필요.
	 */
	UFUNCTION(BlueprintCallable, Category = "Helluna|Ability")
	virtual void SetCurrentTarget(AActor* InTarget) {}

protected:
	//~ Begin UGameplayAbility Interface
	/** [HitSoundV1] Super 호출 시 자동으로 HitSound 를 캐릭터에 캐싱. */
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
	//~ End UGameplayAbility Interface

	/** 패링 윈도우 열기 — bOpensParryWindow + bCanBeParried 체크 후 태그 부여 + 타이머 */
	void TryOpenParryWindow();

	/** 패링 윈도우 닫기 — 태그 제거 + 타이머 정리 */
	void CloseParryWindow();

private:
	FTimerHandle ParryWindowTimerHandle;

	TWeakObjectPtr<AHellunaEnemyCharacter> CachedHellunaEnemyCharacter;

};
