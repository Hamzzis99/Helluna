#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PCGScoreComponent.generated.h"

/**
 * PCG 액터에 부착하여 광석 배치 설정을 관리하는 컴포넌트.
 *
 * 두 축으로 분리된 컨트롤:
 *   - ClusterAmount (뭉침비): 한 뭉침에 몇 개의 광석이 모이는가
 *   - PlacementDensity (촘촘함): 전체 영역에 광석이 얼마나 빼곡히 깔리는가
 *
 * 두 값은 독립적으로 동작 — 둘 중 하나만 올려도 총 광석 수는 증가한다.
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class HELLUNA_API UPCGScoreComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/**
	 * 뭉침비 — 한 뭉침에 들어가는 광석 수를 조절.
	 * 1.0 = 기본(GameMode의 MinClusterSize~MaxClusterSize), 2.0 = 두 배 큰 뭉침.
	 * 값이 높아도 단독 광석은 여전히 존재한다(촘촘함이 결정).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG",
		meta = (DisplayName = "뭉침비", ClampMin = "0.1", ClampMax = "5.0"))
	float ClusterAmount = 1.f;

	/**
	 * 촘촘함 — 전체 광석 배치 밀도를 조절.
	 * 1.0 = 기본. 런타임에 PCG 그래프의 float 유저 파라미터 `PlacementDensity`로 전달되어
	 * 그래프가 생성하는 후보 포인트 풀 자체의 크기를 바꾼다.
	 * (그래프에 해당 파라미터가 노출되어 있어야 하며, 없으면 로그에 전달 실패가 찍힌다)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG",
		meta = (DisplayName = "촘촘함", ClampMin = "0.1", ClampMax = "10.0"))
	float PlacementDensity = 1.f;

	/**
	 * 스폰할 액터 클래스를 직접 지정.
	 * nullptr이면 PCG가 생성한 원래 클래스를 그대로 사용한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG",
		meta = (DisplayName = "스폰 액터 클래스 오버라이드"))
	TSubclassOf<AActor> SpawnActorClassOverride;
};
