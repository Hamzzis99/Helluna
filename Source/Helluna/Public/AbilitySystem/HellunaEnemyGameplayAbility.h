// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/HellunaGameplayAbility.h"
#include "HellunaEnemyGameplayAbility.generated.h"

class AHellunaEnemyCharacter;
class UEnemyCombatComponent;
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

protected:
	/** 패링 윈도우 열기 — bOpensParryWindow + bCanBeParried 체크 후 태그 부여 + 타이머 */
	void TryOpenParryWindow();

	/** 패링 윈도우 닫기 — 태그 제거 + 타이머 정리 */
	void CloseParryWindow();

private:
	FTimerHandle ParryWindowTimerHandle;

	TWeakObjectPtr<AHellunaEnemyCharacter> CachedHellunaEnemyCharacter;

};
