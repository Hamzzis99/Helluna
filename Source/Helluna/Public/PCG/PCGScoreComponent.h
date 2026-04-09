#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PCGScoreComponent.generated.h"

/**
 * PCG 액터에 부착하여 광석 배치 설정을 관리하는 컴포넌트.
 * - 점수 가중치: 높을수록 해당 영역에 광석이 더 많이 배치된다.
 * - 스폰 액터 클래스: 지정하면 PCG가 생성한 광석 대신 이 클래스로 스폰한다.
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class HELLUNA_API UPCGScoreComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 광석 배치 점수 가중치. 기본 1.0, 높을수록 해당 영역에 광석이 더 많이 배치된다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG",
		meta = (DisplayName = "점수 가중치", ClampMin = "0.01", ClampMax = "10.0"))
	float ScoreWeight = 1.f;

	/**
	 * 스폰할 액터 클래스를 직접 지정.
	 * nullptr이면 PCG가 생성한 원래 클래스를 그대로 사용한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG",
		meta = (DisplayName = "스폰 액터 클래스 오버라이드"))
	TSubclassOf<AActor> SpawnActorClassOverride;
};
