// File: Source/Helluna/Public/Enemy/Guardian/HellunaDamageType_PhysicsImpact.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/DamageType.h"
#include "HellunaDamageType_PhysicsImpact.generated.h"

/**
 * 가디언 공격에만 사용되는 데미지 태그.
 * HellunaHealthComponent 가 이 타입을 감지하면 HP>0 일 때 히어로에게 물리 래그돌 스턴을 유발.
 */
UCLASS()
class HELLUNA_API UHellunaDamageType_PhysicsImpact : public UDamageType
{
	GENERATED_BODY()
};
