// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BossPatternZoneBase.generated.h"

class AHellunaEnemyCharacter;

/**
 * ABossPatternZoneBase
 *
 * 보스 패턴 GA가 스폰하는 Zone Actor의 공통 베이스 클래스.
 * GA는 이 Actor를 스폰하고 몽타주를 재생하며,
 * 패턴의 실제 로직(VFX, 슬로우, Orb 등)은 이 Actor가 담당한다.
 *
 * 파생 클래스에서 ActivateZone()을 오버라이드하여 패턴 로직을 구현한다.
 * 패턴이 끝나면 OnPatternFinished 델리게이트를 통해 GA에 알린다.
 */
UCLASS(Abstract)
class HELLUNA_API ABossPatternZoneBase : public AActor
{
	GENERATED_BODY()

public:
	ABossPatternZoneBase();

	// =========================================================
	// 델리게이트
	// =========================================================

	/** 패턴이 종료(성공/실패 무관)되면 GA에 알리는 델리게이트 */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPatternFinished, bool, bWasBroken);

	UPROPERTY(BlueprintAssignable)
	FOnPatternFinished OnPatternFinished;

	// =========================================================
	// 인터페이스
	// =========================================================

	/** 소유 Enemy를 설정한다. GA에서 스폰 직후 호출. */
	void SetOwnerEnemy(AHellunaEnemyCharacter* InEnemy);

	/** GA에서 패턴 지속시간을 전달한다. ActivateZone() 호출 전에 세팅. */
	void SetPatternDuration(float InDuration);

	/** 패턴 시작 — 파생 클래스에서 구현 */
	UFUNCTION(BlueprintCallable, Category = "BossPattern")
	virtual void ActivateZone();

	/** 패턴 강제 종료 (GA 취소 시) — 파생 클래스에서 정리 */
	UFUNCTION(BlueprintCallable, Category = "BossPattern")
	virtual void DeactivateZone();

protected:
	/** 소유 Enemy 캐릭터 참조 */
	UPROPERTY(Transient)
	TObjectPtr<AHellunaEnemyCharacter> OwnerEnemy = nullptr;

	/** GA에서 전달받은 패턴 지속시간 (초) */
	float PatternDuration = 3.0f;

	/** 패턴 종료를 GA에 알린다 (파생 클래스에서 호출) */
	void NotifyPatternFinished(bool bWasBroken);
};
