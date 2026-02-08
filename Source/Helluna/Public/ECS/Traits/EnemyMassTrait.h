/**
 * EnemyMassTrait.h
 *
 * 하이브리드 ECS 시스템의 Trait 정의.
 * MassEntityConfig 에셋의 Traits 배열에 추가하여 에디터 Details 패널에서 설정한다.
 *
 * 모든 설정값이 UPROPERTY로 노출되어 코드 재컴파일 없이 런타임 튜닝 가능.
 * BuildTemplate에서 FEnemyDataFragment에 값을 복사하여 Processor가 읽는다.
 */

// File: Source/Helluna/Public/ECS/Traits/EnemyMassTrait.h

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTraitBase.h"
#include "EnemyMassTrait.generated.h"

class AHellunaEnemyCharacter;
struct FMassEntityTemplateBuildContext;

UCLASS(DisplayName = "Enemy Mass Trait (ECS 적 스폰)")
class HELLUNA_API UEnemyMassTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

public:
	virtual void BuildTemplate(
		FMassEntityTemplateBuildContext& BuildContext,
		const UWorld& World) const override;

protected:
	// =================================================================
	// 스폰 설정
	// =================================================================

	/** 스폰할 적 블루프린트 클래스 */
	UPROPERTY(EditAnywhere, Category = "Enemy Spawn Config",
		meta = (DisplayName = "Enemy Class (스폰할 적 블루프린트 클래스)",
			ToolTip = "Entity가 Actor로 전환될 때 스폰할 적 캐릭터 블루프린트입니다.\n예: BP_MeleeEnemy, BP_RangedEnemy 등.\nAHellunaEnemyCharacter를 상속한 블루프린트만 선택 가능합니다.",
			AllowAbstract = "false"))
	TSubclassOf<AHellunaEnemyCharacter> EnemyClass;

	// =================================================================
	// 거리 설정
	// =================================================================

	/** Entity->Actor 전환 거리 */
	UPROPERTY(EditAnywhere, Category = "Enemy Spawn Config|거리 설정",
		meta = (DisplayName = "Spawn Threshold (Entity→Actor 전환 거리, cm)",
			ToolTip = "플레이어와 Entity 사이 거리가 이 값(cm) 이내로 들어오면 Actor로 전환합니다.\n\n[주의] 반드시 Despawn Threshold보다 작아야 합니다.\n같거나 크면 전환/복귀가 매 틱 반복되는 핑퐁 현상이 발생합니다.\n\n[설정 팁] 무기 최대 사거리보다 크게 설정하세요.\n50m 밖의 적은 Actor가 없어 히트 판정이 불가합니다.\n\n예: 5000 = 50m, 8000 = 80m",
			ClampMin = "100.0", ClampMax = "50000.0", UIMin = "500.0", UIMax = "20000.0"))
	float SpawnThreshold = 5000.f;

	/** Actor->Entity 복귀 거리 */
	UPROPERTY(EditAnywhere, Category = "Enemy Spawn Config|거리 설정",
		meta = (DisplayName = "Despawn Threshold (Actor→Entity 복귀 거리, cm)",
			ToolTip = "플레이어와 Actor 사이 거리가 이 값(cm) 밖으로 나가면 Actor를 파괴하고 Entity로 되돌립니다.\nHP와 위치는 자동 보존되며, 다시 가까이 가면 이전 상태 그대로 재스폰됩니다.\n\n[주의] 반드시 Spawn Threshold보다 커야 합니다 (히스테리시스).\n차이가 클수록 전환이 덜 빈번합니다. 최소 1000cm(10m) 차이를 권장합니다.\n\n예: 6000 = 60m → Spawn 50m / Despawn 60m = 10m 버퍼",
			ClampMin = "200.0", ClampMax = "100000.0", UIMin = "1000.0", UIMax = "30000.0"))
	float DespawnThreshold = 6000.f;

	// =================================================================
	// Actor 제한
	// =================================================================

	/** 동시 최대 Actor 수 (Soft Cap) */
	UPROPERTY(EditAnywhere, Category = "Enemy Spawn Config|Actor 제한",
		meta = (DisplayName = "Max Concurrent Actors (동시 존재 가능한 최대 Actor 수)",
			ToolTip = "월드에 동시에 존재할 수 있는 적 Actor의 최대 수입니다.\n초과하면 플레이어에게서 가장 먼 Actor부터 Entity로 복귀시킵니다 (Soft Cap).\n\n[작동 방식]\n- 50마리 Actor가 있고 앞의 적 1마리가 죽으면 → 슬롯 1개 빔\n- 대기 중인 Entity 중 가장 가까운 1마리가 자동으로 Actor 전환\n- 디펜스 게임의 '다음 투입' 흐름이 자동으로 만들어짐\n\n[성능 가이드]\n- 30~50: 일반적인 디펜스 모드 권장\n- 100+: 고사양 PC 또는 보스전 등 특수 상황\n- 낮을수록 CPU 부하 감소, 높을수록 전투 밀도 증가",
			ClampMin = "1", ClampMax = "500", UIMin = "10", UIMax = "200"))
	int32 MaxConcurrentActors = 50;

	/** Actor Pool 크기 (Phase 2 최적화) */
	UPROPERTY(EditAnywhere, Category = "Enemy Spawn Config|Actor 제한",
		meta = (DisplayName = "Pool Size (사전 생성할 Actor Pool 크기)",
			ToolTip = "게임 시작 시 사전 생성할 Actor의 수입니다.\nMaxConcurrentActors + 버퍼로 설정하세요.\n\n[작동 방식]\n- 게임 시작 시 PoolSize개의 Actor를 Hidden 상태로 사전 생성\n- Entity→Actor 전환 시 SpawnActor 대신 Pool에서 꺼내 Activate\n- Actor→Entity 복귀 시 Destroy 대신 Deactivate 후 Pool에 반납\n- SpawnActor/Destroy 비용이 거의 0으로 감소\n\n[크기 가이드]\n- MaxConcurrentActors + 10이 기본값 (버퍼 = Soft Cap 발동 전 초과분 흡수)\n- 너무 크면 초기 로딩 시간 증가 + 메모리 낭비\n- 너무 작으면 Pool 소진 시 새 Actor를 런타임에 생성해야 함\n\n예: 60 = MaxActors(50) + 버퍼(10)",
			ClampMin = "1", ClampMax = "500", UIMin = "10", UIMax = "300"))
	int32 PoolSize = 60;

	// =================================================================
	// Tick 최적화 (거리별 AI 업데이트 빈도 조절)
	// =================================================================

	/** 근거리 구간 경계 */
	UPROPERTY(EditAnywhere, Category = "Enemy Spawn Config|Tick 최적화",
		meta = (DisplayName = "Near Distance (근거리 구간 경계, cm)",
			ToolTip = "이 거리(cm) 이내의 Actor는 '근거리'로 분류되어 가장 빈번하게 업데이트됩니다.\n전투 중인 적이 여기에 해당하므로 반응성이 중요합니다.\n\n[구간 구조]\n0 ~ Near: 근거리 (NearTickInterval 적용)\nNear ~ Mid: 중거리 (MidTickInterval 적용)\nMid ~ Despawn: 원거리 (FarTickInterval 적용)\n\n예: 2000 = 20m",
			ClampMin = "100.0", ClampMax = "20000.0", UIMin = "500.0", UIMax = "10000.0"))
	float NearDistance = 2000.f;

	/** 중거리 구간 경계 */
	UPROPERTY(EditAnywhere, Category = "Enemy Spawn Config|Tick 최적화",
		meta = (DisplayName = "Mid Distance (중거리 구간 경계, cm)",
			ToolTip = "Near ~ Mid 사이의 Actor는 '중거리'로, Mid ~ Despawn 사이는 '원거리'로 분류됩니다.\n반드시 Near Distance보다 크고 Spawn Threshold보다 작아야 합니다.\n\n예: 4000 = 40m → 20~40m 구간이 중거리, 40~50m 구간이 원거리",
			ClampMin = "200.0", ClampMax = "40000.0", UIMin = "1000.0", UIMax = "15000.0"))
	float MidDistance = 4000.f;

	/** 근거리 Tick 간격 */
	UPROPERTY(EditAnywhere, Category = "Enemy Spawn Config|Tick 최적화",
		meta = (DisplayName = "Near Tick Interval (근거리 AI 업데이트 간격, 초)",
			ToolTip = "근거리 구간 Actor의 Tick 간격(초)입니다.\n0이면 매 프레임 실행 (60FPS = 초당 60회).\n전투 중 적이므로 0 또는 매우 작은 값을 권장합니다.\n\n예: 0 = 매 프레임, 0.016 = ~60Hz, 0.033 = ~30Hz",
			ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "0.5"))
	float NearTickInterval = 0.f;

	/** 중거리 Tick 간격 */
	UPROPERTY(EditAnywhere, Category = "Enemy Spawn Config|Tick 최적화",
		meta = (DisplayName = "Mid Tick Interval (중거리 AI 업데이트 간격, 초)",
			ToolTip = "중거리 구간 Actor의 Tick 간격(초)입니다.\n접근 중이지만 아직 전투 거리가 아닌 적에 해당합니다.\n약간 느려도 체감 차이가 거의 없습니다.\n\n예: 0.08 = ~12Hz (권장), 0.05 = ~20Hz, 0.1 = ~10Hz",
			ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "0.5"))
	float MidTickInterval = 0.08f;

	/** 원거리 Tick 간격 */
	UPROPERTY(EditAnywhere, Category = "Enemy Spawn Config|Tick 최적화",
		meta = (DisplayName = "Far Tick Interval (원거리 AI 업데이트 간격, 초)",
			ToolTip = "원거리 구간 (Mid ~ Despawn) Actor의 Tick 간격(초)입니다.\n플레이어에게서 멀리 있어 행동이 잘 안 보이는 적입니다.\n느리게 업데이트해도 체감 차이가 없으며 CPU를 크게 절약합니다.\n\n예: 0.25 = ~4Hz (권장), 0.5 = ~2Hz, 0.1 = ~10Hz",
			ClampMin = "0.0", ClampMax = "5.0", UIMin = "0.0", UIMax = "1.0"))
	float FarTickInterval = 0.25f;
};
