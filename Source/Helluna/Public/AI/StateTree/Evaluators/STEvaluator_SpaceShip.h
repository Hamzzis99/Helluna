/**
 * STEvaluator_SpaceShip.h
 */

#pragma once

#include "CoreMinimal.h"
#include "StateTreeEvaluatorBase.h"
#include "AI/StateTree/HellunaStateTreeTypes.h"
#include "STEvaluator_SpaceShip.generated.h"

class AAIController;
class USpaceShipAttackSlotManager;

USTRUCT()
struct FSTEvaluator_SpaceShipInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> AIController = nullptr;

	/** 우주선 타겟 데이터 (Task에서 바인딩해서 읽음) */
	UPROPERTY(EditAnywhere, Category = "Output")
	FHellunaAITargetData SpaceShipData;

	/** 우주선 소멸 시 재탐색 쿨다운 타이머 (매 프레임 순회 방지) */
	float RespawnSearchTimer = 0.f;

	/** ShipCombatCollision 컴포넌트 캐시 (#9 최적화: 매 틱 GetComponents 방지) */
	TArray<TWeakObjectPtr<UPrimitiveComponent>> CachedShipCollisionPrims;
	bool bShipCollisionPrimsCached = false;
};

USTRUCT(meta = (DisplayName = "Helluna: SpaceShip Target", Category = "Helluna|AI"))
struct HELLUNA_API FSTEvaluator_SpaceShip : public FStateTreeEvaluatorCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTEvaluator_SpaceShipInstanceData;

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual void TreeStart(FStateTreeExecutionContext& Context) const override;
	virtual void Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;

public:
	/** 우주선 액터를 찾을 때 사용하는 태그 */
	UPROPERTY(EditAnywhere, Category = "Config",
		meta = (DisplayName = "우주선 태그"))
	FName SpaceShipTag = FName("SpaceShip");
};
