/**
 * EnemyEntityMovementProcessor.h
 *
 * Entity 상태의 적을 우주선(GoalLocation)을 향해 매 틱 이동시키는 Processor.
 *
 * ■ 역할
 *   - GoalLocation 캐싱 (우주선 위치 저장)
 *   - 매 틱 위치 업데이트 (직선 이동)
 *   - Entity 간 충돌 회피 (Separation)
 *
 * ■ 실행 조건
 *   - bHasSpawnedActor = false (Entity 상태)
 *   - bDead = false (살아있음)
 *   - bGoalLocationCached = true (우주선 위치 캐싱 완료)
 *
 * ■ 실행 환경
 *   - Server + Standalone (클라이언트는 시각화만 담당)
 * 
 * ■ AI 로직
 *   - Entity 상태: AI 없음, 단순 이동만
 *   - Actor 전환 후: Actor 기반 StateTree가 AI 담당
 * 
 * @author 김민우
 */

#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "EnemyEntityMovementProcessor.generated.h"

UCLASS()
class HELLUNA_API UEnemyEntityMovementProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UEnemyEntityMovementProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;       // 이동 처리용
	FMassEntityQuery GoalCacheQuery;    // GoalLocation 캐싱용 (별도 쿼리)
};
