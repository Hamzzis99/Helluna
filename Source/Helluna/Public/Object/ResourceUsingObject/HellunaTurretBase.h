// File: Source/Helluna/Public/Object/ResourceUsingObject/HellunaTurretBase.h
#pragma once

#include "CoreMinimal.h"
#include "Object/ResourceUsingObject/HellunaBaseResourceUsingObject.h"
#include "HellunaTurretBase.generated.h"

/**
 * 모든 포탑류(공격/회복 등)의 공통 부모.
 * 적 AI 어그로 스캔이 이 타입 하나로 모든 포탑을 일괄 잡도록 하기 위한 마커 베이스.
 * 기능 추가는 자식 클래스에서.
 */
UCLASS(Abstract)
class HELLUNA_API AHellunaTurretBase : public AHellunaBaseResourceUsingObject
{
	GENERATED_BODY()
};
