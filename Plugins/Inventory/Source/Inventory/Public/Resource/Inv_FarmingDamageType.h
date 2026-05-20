// 채집 도구(곡괭이) 전용 데미지 타입.
// Inv_ResourceComponent 가 이 타입의 데미지만 채집으로 인정한다.
// → 총알/폭발 등 다른 데미지로는 광석이 캐지지 않는다 (총에 맞아도 캐지는 버그 방지).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/DamageType.h"
#include "Inv_FarmingDamageType.generated.h"

UCLASS()
class INVENTORY_API UInv_FarmingDamageType : public UDamageType
{
	GENERATED_BODY()
};
