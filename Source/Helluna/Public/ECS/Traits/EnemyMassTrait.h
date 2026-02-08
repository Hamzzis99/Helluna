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
		meta = (DisplayName = "Enemy Class (적 블루프린트)", AllowAbstract = "false"))
	TSubclassOf<AHellunaEnemyCharacter> EnemyClass;

	// =================================================================
	// 거리 설정
	// =================================================================

	/** Entity->Actor 전환 거리. 플레이어가 이 거리 안에 들어오면 Actor로 전환 */
	UPROPERTY(EditAnywhere, Category = "Enemy Spawn Config|거리 설정",
		meta = (DisplayName = "Spawn Threshold (전환 거리 cm)",
			ToolTip = "플레이어가 이 거리(cm) 안에 들어오면 Mass Entity를 Actor로 전환합니다. DespawnThreshold보다 작아야 합니다.",
			ClampMin = "100.0", ClampMax = "50000.0", UIMin = "500.0", UIMax = "20000.0"))
	float SpawnThreshold = 5000.f;

	/** Actor->Entity 복귀 거리. 플레이어가 이 거리 밖으로 나가면 Actor를 Entity로 복귀 */
	UPROPERTY(EditAnywhere, Category = "Enemy Spawn Config|거리 설정",
		meta = (DisplayName = "Despawn Threshold (복귀 거리 cm)",
			ToolTip = "플레이어가 이 거리(cm) 밖으로 나가면 Actor를 Mass Entity로 복귀시킵니다. SpawnThreshold보다 커야 합니다 (히스테리시스).",
			ClampMin = "200.0", ClampMax = "100000.0", UIMin = "1000.0", UIMax = "30000.0"))
	float DespawnThreshold = 6000.f;

	// =================================================================
	// Actor 제한
	// =================================================================

	/** 동시 최대 Actor 수 (Soft Cap). 초과 시 먼 Actor부터 Entity로 복귀 */
	UPROPERTY(EditAnywhere, Category = "Enemy Spawn Config|Actor 제한",
		meta = (DisplayName = "Max Concurrent Actors (최대 동시 Actor)",
			ToolTip = "동시에 존재할 수 있는 최대 Actor 수입니다. 초과하면 가장 먼 Actor부터 Entity로 복귀합니다.",
			ClampMin = "1", ClampMax = "500", UIMin = "10", UIMax = "200"))
	int32 MaxConcurrentActors = 50;

	// =================================================================
	// Tick 최적화
	// =================================================================

	/** 근거리 기준. 이 거리 이내의 Actor는 NearTickInterval로 동작 */
	UPROPERTY(EditAnywhere, Category = "Enemy Spawn Config|Tick 최적화",
		meta = (DisplayName = "Near Distance (근거리 기준 cm)",
			ToolTip = "이 거리(cm) 이내의 Actor는 매 틱(또는 NearTickInterval) 실행됩니다.",
			ClampMin = "100.0", ClampMax = "20000.0", UIMin = "500.0", UIMax = "10000.0"))
	float NearDistance = 2000.f;

	/** 중거리 기준. Near~Mid 구간은 MidTickInterval, Mid~Despawn은 FarTickInterval */
	UPROPERTY(EditAnywhere, Category = "Enemy Spawn Config|Tick 최적화",
		meta = (DisplayName = "Mid Distance (중거리 기준 cm)",
			ToolTip = "Near~Mid 구간은 MidTickInterval, Mid~Despawn 구간은 FarTickInterval로 동작합니다.",
			ClampMin = "200.0", ClampMax = "40000.0", UIMin = "1000.0", UIMax = "15000.0"))
	float MidDistance = 4000.f;

	/** 근거리 Tick 간격 (초). 0 = 매 틱 (최대 프레임레이트) */
	UPROPERTY(EditAnywhere, Category = "Enemy Spawn Config|Tick 최적화",
		meta = (DisplayName = "Near Tick Interval (근거리 틱 간격 초)",
			ToolTip = "근거리 Actor의 Tick 간격(초). 0이면 매 프레임 실행됩니다.",
			ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "0.5"))
	float NearTickInterval = 0.f;

	/** 중거리 Tick 간격 (초). ~12Hz 권장 */
	UPROPERTY(EditAnywhere, Category = "Enemy Spawn Config|Tick 최적화",
		meta = (DisplayName = "Mid Tick Interval (중거리 틱 간격 초)",
			ToolTip = "중거리 Actor의 Tick 간격(초). 0.08 = 약 12Hz.",
			ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "0.5"))
	float MidTickInterval = 0.08f;

	/** 원거리 Tick 간격 (초). ~4Hz 권장. Mid~Despawn 구간에 적용 */
	UPROPERTY(EditAnywhere, Category = "Enemy Spawn Config|Tick 최적화",
		meta = (DisplayName = "Far Tick Interval (원거리 틱 간격 초)",
			ToolTip = "원거리 Actor의 Tick 간격(초). 0.25 = 약 4Hz.",
			ClampMin = "0.0", ClampMax = "5.0", UIMin = "0.0", UIMax = "1.0"))
	float FarTickInterval = 0.25f;
};
