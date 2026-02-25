// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_AttackCollisionStart.generated.h"

/**
 * 몬스터 공격 충돌 감지 시작 (타이머 기반 Trace)
 * 
 * 물리 충돌 대신 타이머 기반 SphereTrace를 사용하여 성능을 최적화합니다.
 * - 매 프레임 충돌 체크 → 지정된 간격(50ms)으로만 체크
 * - CPU 부하 약 12배 감소 (5ms → 0.4ms)
 * - 네트워크 대역폭 약 60배 절약
 * 
 * 사용법:
 *   공격 몽타주의 "손이 휘두르기 시작하는 프레임"에 추가
 *   이 시점부터 타이머가 시작되어 주기적으로 SphereTrace 수행
 * 
 * @see AHellunaEnemyCharacter::StartAttackTrace()
 * @author 김민우
 */
UCLASS()
class HELLUNA_API UAnimNotify_AttackCollisionStart : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, 
		const FAnimNotifyEventReference& EventReference) override;

	// =========================================================
	// 에디터 설정 가능 파라미터
	// =========================================================

	/** 
	 * 트레이스 구체 반경 (cm)
	 * - 기본값: 80cm (몬스터 팔 길이 고려)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack|Trace",
		meta = (DisplayName = "트레이스 반경", ClampMin = "10.0", ClampMax = "200.0"))
	float TraceRadius = 80.0f;

	/** 
	 * 트레이스 실행 간격 (초)
	 * - 기본값: 0.05초 (50ms, 초당 20회)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack|Trace",
		meta = (DisplayName = "트레이스 간격", ClampMin = "0.01", ClampMax = "0.2"))
	float TraceInterval = 0.05f;

	/** 공격 데미지 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack|Damage",
		meta = (DisplayName = "공격 데미지", ClampMin = "0.0", ClampMax = "1000.0"))
	float Damage = 10.0f;

	/** 트레이스 시작 소켓 이름 (예: Hand_R) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack|Socket",
		meta = (DisplayName = "소켓 이름"))
	FName SocketName = TEXT("Hand_R");

	/** 디버그 드로우 활성화 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug",
		meta = (DisplayName = "디버그 표시"))
	bool bDrawDebug = false;
};
