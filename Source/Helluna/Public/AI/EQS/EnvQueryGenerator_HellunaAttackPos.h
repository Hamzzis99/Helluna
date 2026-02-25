/**
 * EnvQueryGenerator_HellunaAttackPos.h
 *
 * EQS Generator: 타겟 주변 원형으로 공격 위치 후보 점을 생성하고
 * 내부에서 필터/스코어까지 처리한다.
 * → 에디터에서 별도 Test를 추가할 필요 없음.
 *
 * 처리 순서:
 *   1. 타겟 중심 원형 후보 점 생성
 *   2. NavMesh 위에 있는 점만 통과 (Filter)
 *   3. 반경 PawnOverlapRadius 안에 다른 Pawn이 없는 점만 통과 (Filter)
 *   4. bPreferNearest 설정에 따라 Querier와 가깝거나 먼 순으로 정렬 (Score)
 */

#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryGenerator.h"
#include "EnvQueryGenerator_HellunaAttackPos.generated.h"

UCLASS(meta = (DisplayName = "Helluna: Attack Position Around Target"))
class HELLUNA_API UEnvQueryGenerator_HellunaAttackPos : public UEnvQueryGenerator
{
	GENERATED_BODY()

public:
	UEnvQueryGenerator_HellunaAttackPos();

	// ── Generator ─────────────────────────────────────────────

	/** 타겟으로부터 최소 거리 (cm) */
	UPROPERTY(EditDefaultsOnly, Category = "위치 생성",
		meta = (DisplayName = "최소 반경 (cm)",
			ToolTip = "타겟으로부터 가장 가까운 후보 위치까지의 거리입니다.",
			ClampMin = "10.0"))
	float MinRadius = 100.f;

	/** 타겟으로부터 최대 거리 (cm) */
	UPROPERTY(EditDefaultsOnly, Category = "위치 생성",
		meta = (DisplayName = "최대 반경 (cm)",
			ToolTip = "타겟으로부터 가장 먼 후보 위치까지의 거리입니다.\n공격 사거리보다 작게 설정하세요.",
			ClampMin = "10.0"))
	float MaxRadius = 140.f;

	/** 원 둘레를 몇 도 간격으로 나눌지. 작을수록 후보 점이 많아짐 */
	UPROPERTY(EditDefaultsOnly, Category = "위치 생성",
		meta = (DisplayName = "각도 간격 (도)",
			ToolTip = "후보 위치를 몇 도 간격으로 배치할지 설정합니다.\n45도 = 8방향, 90도 = 4방향. 값이 작을수록 후보가 많아집니다.",
			ClampMin = "5.0", ClampMax = "180.0"))
	float AngleStep = 45.f;

	/** 점 생성 기준 Context */
	UPROPERTY(EditDefaultsOnly, Category = "위치 생성",
		meta = (DisplayName = "기준 Context",
			ToolTip = "후보 위치의 중심이 될 대상을 지정하는 Context입니다.\nEnvQueryContext_HellunaTarget을 사용하세요."))
	TSubclassOf<UEnvQueryContext> TargetContext;

	// ── Filter ────────────────────────────────────────────────

	/**
	 * NavMesh 탐색 허용 오차 (cm).
	 * 생성된 점 주변 이 범위 안에 NavMesh가 있어야 통과.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "필터",
		meta = (DisplayName = "NavMesh 탐색 범위 (cm)",
			ToolTip = "후보 위치 주변 이 범위 안에 걸을 수 있는 지면(NavMesh)이 없으면 제외합니다.\n값이 작을수록 엄격하게 필터링됩니다.",
			ClampMin = "1.0"))
	float NavMeshExtent = 50.f;

	/**
	 * 다른 Pawn 겹침 체크 반경 (cm).
	 * 이 반경 안에 다른 Pawn이 있으면 해당 후보 점 제외.
	 * 0 이하이면 겹침 체크 생략.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "필터",
		meta = (DisplayName = "몬스터 겹침 방지 반경 (cm)",
			ToolTip = "이 반경 안에 다른 몬스터가 이미 있으면 해당 위치를 제외합니다.\n0으로 설정하면 겹침 체크를 생략합니다.",
			ClampMin = "0.0"))
	float PawnOverlapRadius = 40.f;

	// ── Score ─────────────────────────────────────────────────

	/**
	 * 거리 점수 방식.
	 * true  = 가까운 지점 우선 (근접 몬스터용)
	 * false = 먼 지점 우선   (원거리 몬스터용)
	 */
	UPROPERTY(EditDefaultsOnly, Category = "점수",
		meta = (DisplayName = "가까운 위치 우선",
			ToolTip = "체크: 자신과 가까운 위치를 우선 선택합니다. (근접 몬스터 권장)\n해제: 자신과 먼 위치를 우선 선택합니다. (원거리 몬스터 권장)"))
	bool bPreferNearest = true;

	virtual void GenerateItems(FEnvQueryInstance& QueryInstance) const override;
	virtual FText GetDescriptionTitle() const override;
	virtual FText GetDescriptionDetails() const override;
};
